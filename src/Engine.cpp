#include "Engine.hpp"
#include <iostream>
#include "Logger.hpp"
#include "OSCHandler.hpp"
#include <filesystem>
#include <thread>
#include "ScriptEditor.hpp"
#include <QApplication>

#ifdef _WIN32
#ifdef ENABLE_VST3
#include <objbase.h>
#include <windows.h>
#endif
#endif

Engine::Engine() : running(false), samplesPerPixel(50), activeDeckIndex(0), sampleRate(44100) {}

Engine::~Engine() {
    if (oscHandler) oscHandler->stop();
    audioEngine.stop();
    if (scriptEditor) delete scriptEditor;
#ifdef _WIN32
#ifdef ENABLE_VST3
    CoUninitialize();
#endif
#endif
}

bool Engine::init(int numDecks) {
#ifdef _WIN32
#ifdef ENABLE_VST3
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
#endif
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

#ifdef ENABLE_VST3
    std::thread scanThread([this]() {
        scanVSTs();
    });
    scanThread.detach();
#endif

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

    // Initialize ScriptEditor (Qt)
    scriptEditor = new ScriptEditor();
    QObject::connect(scriptEditor, &ScriptEditor::commandExecuted, [this](const QString &cmd) {
        std::string command = cmd.toStdString();
        Logger::info("Script Command: " + command);
        // TODO: Parse command
    });
    scriptEditor->show();

    return true;
}

void Engine::run() {
    const double targetFrameTimeMs = 1000.0 / monitorRefreshRate;
    const double targetFrameTimeNs = targetFrameTimeMs * 1000000.0;

    while (running) {
        QApplication::processEvents();

        uint64_t frameStartPerf = SDL_GetPerformanceCounter();

        // Process tasks queued from other threads (e.g., OSC)
        processTasks();
        
        // Check for beat triggers
        checkTriggers();
        
        // Update Sync
        updateSync();

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

        if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                renderer.updateSize();
            }
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
            
            // Check if this deck is a master for anyone
            bool isMaster = false;
            for (int j = 0; j < numDecks; ++j) {
                if (decks[j]->isSyncActive() && decks[j]->getSyncSource() == i) {
                    isMaster = true;
                    break;
                }
            }

            std::string name = "Deck " + std::string(1, 'A' + i);
            renderer.renderDeck(decks[i].get(), i * deckHeight, deckHeight, samplesPerPixel, name.c_str(), activeDeckIndex == i, isMaster);
        }
    }
    
    renderer.present();
    renderer.frameUpdate();
}

void Engine::checkTriggers() {
    for (size_t i = 0; i < decks.size(); ++i) {
        Deck* deck = decks[i].get();
        if (!deck->isPlaying()) continue;

        float bpm = deck->getBPM();
        if (bpm <= 0.0f) continue;

        uint64_t currentFrame = deck->getCurrentFrame();
        float offset = deck->getBeatOffset();
        double framesPerBeat = (double)sampleRate * 60.0 / (double)bpm;
        
        // Calculate current beat
        double currentBeat = ((double)currentFrame - offset) / framesPerBeat;

        auto& triggers = deck->getTriggers();
        // Lock not strictly needed if we assume single writer (OSC) vs reader (MainThread) 
        // but for safety we should lock or copy. Since we modify 'fired', we need care.
        // However, this is the main thread, and OSC writes from a task queue which runs on main thread?
        // Actually OSC queues tasks to main thread, so we are safe from race conditions with OSC!
        
        for (auto& t : triggers) {
            // Check if we just crossed the beat
            // Simple logic: if we are within a small window AFTER the beat, and haven't fired
            // Window: 1/16th of a beat?
            if (!t.fired && currentBeat >= t.beat && currentBeat < t.beat + 0.5f) {
                t.fired = true;
                Logger::info("Engine: Firing trigger " + std::to_string(t.id) + " on Deck " + std::to_string(i));
                for (const auto& action : t.actions) {
                    executeAction(action);
                }
            }
            // Reset fired flag if we rewound significantly before the beat
            else if (t.fired && currentBeat < t.beat - 1.0f) {
                t.fired = false;
            }
        }
    }
}

void Engine::executeAction(const TriggerAction& action) {
    if (action.targetDeckIdx < 0 || action.targetDeckIdx >= (int)decks.size()) return;
    Deck* targetDeck = decks[action.targetDeckIdx].get();
    
    if (action.command == "play") {
        targetDeck->play();
    }
    else if (action.command == "pause") {
        targetDeck->pause();
    }
    else if (action.command == "stop") {
        targetDeck->pause();
        targetDeck->setFrame(0);
    }
    else if (action.command == "jumpToBeat") {
        if (!action.args.empty()) {
            float beat = action.args[0];
            float bpm = targetDeck->getBPM();
            if (bpm > 0) {
                float offset = targetDeck->getBeatOffset();
                double framesPerBeat = (double)sampleRate * 60.0 / (double)bpm;
                uint64_t frame = (uint64_t)(beat * framesPerBeat + offset);
                targetDeck->setFrame(frame);
                // Also reset triggers if jumping back
                targetDeck->resetTriggers();
            }
        }
    }
    else if (action.command == "syncBpmTo" || action.command == "syncTo") {
        if (!action.args.empty()) {
            int sourceDeckIdx = (int)action.args[0];
            targetDeck->setSync(true, sourceDeckIdx);
        }
    }
    else if (action.command == "beatSync") {
         // Re-implement logic here or call a shared helper? 
         // For now, implementing basic phase jump here for triggers
         if (!action.args.empty()) {
             int sourceIdx = (int)action.args[0];
             if (sourceIdx >= 0 && sourceIdx < (int)decks.size()) {
                 Deck* master = decks[sourceIdx].get();
                 float masterBpm = master->getBPM();
                 float myBpm = targetDeck->getBPM();
                 if (masterBpm > 0 && myBpm > 0) {
                      float masterSpeed = (float)master->getSpeed();
                      float effectiveMasterBpm = masterBpm * masterSpeed;
                      float requiredSpeed = effectiveMasterBpm / myBpm;
                      targetDeck->setSpeed(requiredSpeed);
                      
                      // Phase alignment logic
                      uint64_t mFrame = master->getCurrentFrame();
                      float mOffset = master->getBeatOffset();
                      double mFramesPerBeat = (double)sampleRate * 60.0 / (double)masterBpm;
                      double mBeatTotal = ((double)mFrame - mOffset) / mFramesPerBeat;
                      double mPhase = mBeatTotal - floor(mBeatTotal);
                      
                      uint64_t myFrame = targetDeck->getCurrentFrame();
                      float myOffset = targetDeck->getBeatOffset();
                      double myFramesPerBeat = (double)sampleRate * 60.0 / (double)myBpm;
                      double myBeatTotal = ((double)myFrame - myOffset) / myFramesPerBeat;
                      double myPhase = myBeatTotal - floor(myBeatTotal);
                      
                      double diff = mPhase - myPhase;
                      if (diff > 0.5) diff -= 1.0;
                      if (diff < -0.5) diff += 1.0;
                      
                      int64_t frameDiff = (int64_t)(diff * myFramesPerBeat);
                       targetDeck->seek(frameDiff);
                  }
              }
          }
     }
 }

#ifdef ENABLE_VST3
void Engine::scanVSTs() {
    Logger::info("Scanning VST3 plugins in C:\\Program Files\\Common Files\\VST3...");

    std::string vstPath = "C:\\Program Files\\Common Files\\VST3";
    std::vector<std::string> foundVSTs;

    try {
        if (std::filesystem::exists(vstPath) && std::filesystem::is_directory(vstPath)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(vstPath)) {
                if (entry.is_regular_file() && entry.path().extension() == ".vst3") {
                    foundVSTs.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        Logger::error("Error scanning VST3 directory: " + std::string(e.what()));
        return;
    }

    std::lock_guard<std::mutex> lock(vstMutex);
    vstPaths = std::move(foundVSTs);

    Logger::info("Found " + std::to_string(vstPaths.size()) + " VST3 plugins");
}
#endif

void Engine::handleSystemCommand(const std::string& cmd, const osc::ReceivedMessage& m) {
    auto it = m.ArgumentsBegin();

    try {
        if (cmd == "printVSTs") {
#ifdef ENABLE_VST3
            std::lock_guard<std::mutex> lock(vstMutex);
            Logger::info("--- Available VST3 Plugins ---");
            for (size_t i = 0; i < vstPaths.size(); ++i) {
                Logger::info("  [" + std::to_string(i) + "] " + vstPaths[i]);
            }
            Logger::info("Total: " + std::to_string(vstPaths.size()) + " plugins");
#else
            Logger::info("VST3 support is disabled in this build.");
#endif
        }
        else if (cmd == "workingDirectory") {
            if (it->IsString()) {
                std::string dir = (it++)->AsString();
                workingDirectory = dir;
                Logger::info("Working directory set to: " + workingDirectory);
            }
        }
    } catch (const std::exception& e) {
        Logger::error("System command error: " + std::string(e.what()));
    }
}

