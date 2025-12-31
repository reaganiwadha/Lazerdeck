#include "Engine.hpp"
#include <iostream>
#include "Logger.hpp"

Engine::Engine() : running(false), samplesPerPixel(50), activeDeckIndex(0), sampleRate(44100) {}

Engine::~Engine() {
    audioEngine.stop();
}

bool Engine::init() {
    if (!renderer.init()) {
        Logger::error("Renderer initialization failed");
        return false;
    }

    // Load resources (non-blocking now)
    Logger::info("Loading resources...");
    
    deckA = std::make_unique<Deck>(sampleRate);
    deckB = std::make_unique<Deck>(sampleRate);

    if (!audioEngine.init(deckA.get(), deckB.get(), sampleRate, 128)) { // 128 frames buffer (~2.9ms) - stable low latency
        Logger::error("AudioEngine initialization failed");
        return false;
    }

    sampleRate = audioEngine.getActualSampleRate();

    deckA->load("resources/znfodastica.wav", &analysisDB);
    deckB->load("resources/glory.mp3", &analysisDB);

    if (!audioEngine.start()) {
        Logger::error("Failed to start AudioEngine");
        return false;
    }

    Logger::info("Lazerdeck Mixer Ready!");
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

void Engine::handleEvents() {
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
            
            Deck* activeDeck = (activeDeckIndex == 0) ? deckA.get() : deckB.get();
            handleDeckInput(activeDeck, event, shift);
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
            activeDeckIndex = 0;
        }
    }
    else if (event.key.keysym.sym == SDLK_DOWN) {
        if (!shift) {
            activeDeckIndex = 1;
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
}

void Engine::render() {
    renderer.clear();
    
    // Update visual frame positions for smooth animation
    deckA->updateVisualFrame();
    deckB->updateVisualFrame();
    
    // Render Top Deck (A)
    renderer.renderDeck(deckA.get(), 0, renderer.getHeight() / 2, samplesPerPixel, "Deck A", activeDeckIndex == 0);
    
    // Render Bottom Deck (B)
    renderer.renderDeck(deckB.get(), renderer.getHeight() / 2, renderer.getHeight() / 2, samplesPerPixel, "Deck B", activeDeckIndex == 1);
    
    renderer.present();
    renderer.frameUpdate();
}
