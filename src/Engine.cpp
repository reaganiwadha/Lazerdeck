#include "Engine.hpp"
#include <iostream>
#include "Logger.hpp"
#include "OSCHandler.hpp"

#ifdef _WIN32
#include <objbase.h>
#include <windows.h>
#endif

Engine::Engine() : running(false), samplesPerPixel(50), activeDeckIndex(0), sampleRate(44100) {}

Engine::~Engine() {
    if (oscHandler) oscHandler->stop();
    audioEngine.stop();
#ifdef _WIN32
    CoUninitialize();
#endif
}

bool Engine::init(int numDecks) {
#ifdef _WIN32
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
#endif
    if (!renderer.init()) {
        Logger::error("Renderer initialization failed");
        return false;
    }

    // Load resources (non-blocking now)
    Logger::info("Loading resources...");
    
    std::vector<Deck*> deckPtrs;
    for (int i = 0; i < numDecks; ++i) {
        decks.push_back(std::make_unique<Deck>(sampleRate));
        deckPtrs.push_back(decks.back().get());
    }

    mixer = std::make_unique<Lazerdeck::Mixer>(numDecks, sampleRate);

    if (!audioEngine.init(deckPtrs, mixer.get(), sampleRate, 128)) { // 128 frames buffer (~2.9ms) - stable low latency
        Logger::error("AudioEngine initialization failed");
        return false;
    }

    sampleRate = audioEngine.getActualSampleRate();

    if (numDecks > 0) decks[0]->load("resources/znfodastica.wav", &analysisDB);
    if (numDecks > 1) decks[1]->load("resources/glory.mp3", &analysisDB);

    if (!audioEngine.start()) {
        Logger::error("Failed to start AudioEngine");
        return false;
    }

    oscHandler = std::make_unique<OSCHandler>(this, 9000);
    oscHandler->start();

    Logger::info("Lazerdeck Mixer Ready with " + std::to_string(numDecks) + " decks!");
    Logger::info("Controls:");
    Logger::info("  Active Deck:     Space (Play/Pause), Left/Right (Seek), M (Metronome), S (Save Analysis)");
    Logger::info("                   -/+ (Speed Control)");
    Logger::info("  Global:          Up/Down (Select Deck), O (Open File)");
    Logger::info("                   Mouse Wheel (Zoom)");
    Logger::info("  Debug:           1-9 (Toggle Render Passes)");

    running = true;
    
    lastTime = SDL_GetTicks64();
    frameCount = 0;
    fpsTimer = SDL_GetTicks64();
    currentFPS = 0;
    monitorRefreshRate = renderer.getRefreshRate();
    perfFrequency = SDL_GetPerformanceFrequency();
    Logger::info("Monitor refresh rate: " + std::to_string(monitorRefreshRate) + "Hz");
    
    // Set initial title
    std::string backend = renderer.getRendererBackend();
    int actualRate = audioEngine.getActualSampleRate();
    std::string title = "Lazerdeck - " + std::to_string(actualRate) + "Hz, 0 FPS | " + 
                      std::to_string(audioEngine.getBufferSize()) + " frames buffer, " +
                      std::to_string(audioEngine.getBitDepth()) + "-bit | " + backend;
    renderer.setWindowTitle(title);

    return true;
}

void Engine::run() {
    const double targetFrameTimeMs = 1000.0 / monitorRefreshRate;
    const double targetFrameTimeNs = targetFrameTimeMs * 1000000.0;

    while (running) {
        uint64_t frameStartPerf = SDL_GetPerformanceCounter();

        // Process tasks queued from other threads (e.g., OSC)
        processTasks();

        handleEvents();
        render();

        frameCount++;
        uint64_t currentTime = SDL_GetTicks64();
        if (currentTime - fpsTimer >= 1000) {
            currentFPS = (int)frameCount;
            frameCount = 0;
            fpsTimer = currentTime;

            // Update title with full system info
            std::string backend = renderer.getRendererBackend();
            int actualRate = audioEngine.getActualSampleRate();
            std::string title = "Lazerdeck - " + std::to_string(actualRate) + "Hz, " + std::to_string(currentFPS) + " FPS | " + 
                              std::to_string(audioEngine.getBufferSize()) + " frames buffer, " +
                              std::to_string(audioEngine.getBitDepth()) + "-bit | " + backend + 
                              " | Latency: " + std::to_string(audioEngine.getLatencyMs()) + "ms";
            renderer.setWindowTitle(title);
        }

        // High-precision timing for smooth animation
        uint64_t frameEndPerf = SDL_GetPerformanceCounter();
        double frameTimeMs = ((double)(frameEndPerf - frameStartPerf) / perfFrequency) * 1000.0;
        
        if (frameTimeMs < targetFrameTimeMs) {
            double sleepTimeMs = targetFrameTimeMs - frameTimeMs;
            SDL_Delay((uint32_t)(sleepTimeMs));
        }
    }
}

void Engine::queueTask(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(taskMutex);
    taskQueue.push_back(task);
}

void Engine::processTasks() {
    std::vector<std::function<void()>> currentTasks;
    {
        std::lock_guard<std::mutex> lock(taskMutex);
        currentTasks.swap(taskQueue);
    }
    
    for (const auto& task : currentTasks) {
        if (task) task();
    }
}

void Engine::handleEvents() {
#ifdef _WIN32
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            running = false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
#endif

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = false;
        }

        if (event.type == SDL_KEYDOWN) {
            bool shift = event.key.keysym.mod & KMOD_SHIFT;
            handleGlobalInput(event, shift);
            
            // Handle render pass toggles (1-9)
            if (event.key.keysym.sym >= SDLK_1 && event.key.keysym.sym <= SDLK_9) {
                int passIndex = event.key.keysym.sym - SDLK_1;
                renderer.togglePass(passIndex);
            }
            
            if (activeDeckIndex >= 0 && activeDeckIndex < (int)decks.size()) {
                handleDeckInput(decks[activeDeckIndex].get(), event, shift);
            }
        }

        if (event.type == SDL_MOUSEWHEEL) {
            if (event.wheel.y > 0) samplesPerPixel += 5;
            else samplesPerPixel -= 5;
            if (samplesPerPixel < 1) samplesPerPixel = 1;
        }
    }
}

void Engine::handleGlobalInput(SDL_Event& event, bool shift) {
    // Deck Selection / BPM Adjust (Global-ish)
    if (event.key.keysym.sym == SDLK_UP) {
        if (!shift) {
            activeDeckIndex--;
            if (activeDeckIndex < 0) activeDeckIndex = (int)decks.size() - 1;
        }
    }
    else if (event.key.keysym.sym == SDLK_DOWN) {
        if (!shift) {
            activeDeckIndex++;
            if (activeDeckIndex >= (int)decks.size()) activeDeckIndex = 0;
        }
    }
    // Open File
    else if (event.key.keysym.sym == SDLK_o) {
        Logger::info("Open File dialog is currently disabled.");
    }
}

void Engine::handleDeckInput(Deck* activeDeck, SDL_Event& event, bool shift) {
    if (event.key.keysym.sym == SDLK_UP) {
        if (shift) {
            float bpm = activeDeck->getBPM();
            if (bpm > 0) activeDeck->setBPM(bpm + 1.0f);
        }
    }
    else if (event.key.keysym.sym == SDLK_DOWN) {
        if (shift) {
            float bpm = activeDeck->getBPM();
            if (bpm > 0) activeDeck->setBPM(bpm - 1.0f);
        }
    }
    else if (event.key.keysym.sym == SDLK_SPACE) {
        activeDeck->togglePlayback();
    }
    else if (event.key.keysym.sym == SDLK_m) {
        activeDeck->toggleMetronome();
    }
    else if (event.key.keysym.sym == SDLK_s) {
        activeDeck->saveAnalysis(analysisDB);
    }
    else if (event.key.keysym.sym == SDLK_EQUALS || event.key.keysym.sym == SDLK_KP_PLUS) {
            activeDeck->increaseSpeed();
    }
    else if (event.key.keysym.sym == SDLK_MINUS || event.key.keysym.sym == SDLK_KP_MINUS) {
            activeDeck->decreaseSpeed();
    }
    else if (event.key.keysym.sym == SDLK_LEFT) {
        if (shift) {
            float offset = activeDeck->getBeatOffset();
            float sr = (float)sampleRate;
            if (sr > 0) activeDeck->setBeatOffset(offset - (sr * 0.001f));
        } else {
            activeDeck->seek(-(int64_t)sampleRate * 5);
        }
    }
    else if (event.key.keysym.sym == SDLK_RIGHT) {
        if (shift) {
                float offset = activeDeck->getBeatOffset();
                float sr = (float)sampleRate;
                if (sr > 0) activeDeck->setBeatOffset(offset + (sr * 0.001f));
        } else {
            activeDeck->seek((int64_t)sampleRate * 5);
        }
    }
    else if (event.key.keysym.sym == SDLK_LEFTBRACKET) {
        activeDeck->setLoopStart();
    }
    else if (event.key.keysym.sym == SDLK_RIGHTBRACKET) {
        activeDeck->setLoopEnd();
    }
    else if (event.key.keysym.sym == SDLK_BACKSLASH) {
        activeDeck->exitLoop();
    }
}

void Engine::render() {
    renderer.clear();
    
    int numDecks = (int)decks.size();
    if (numDecks > 0) {
        int deckHeight = renderer.getHeight() / numDecks;
        for (int i = 0; i < numDecks; ++i) {
            decks[i]->updateVisualFrame();
            std::string name = "Deck " + std::string(1, 'A' + i);
            renderer.renderDeck(decks[i].get(), i * deckHeight, deckHeight, samplesPerPixel, name.c_str(), activeDeckIndex == i);
        }
    }
    
    renderer.present();
    renderer.frameUpdate();
}
