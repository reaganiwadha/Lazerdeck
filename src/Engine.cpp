#include "Engine.hpp"
#include "nfd.h"
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

    deckA->load("resources/znfodastica.wav", &analysisDB);
    deckB->load("resources/glory.mp3", &analysisDB);

    if (!audioEngine.init(deckA.get(), deckB.get(), sampleRate)) {
        Logger::error("AudioEngine initialization failed");
        return false;
    }

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

    running = true;
    
    lastTime = SDL_GetTicks64();
    frameCount = 0;
    fpsTimer = SDL_GetTicks64();
    currentFPS = 0;
    monitorRefreshRate = renderer.getRefreshRate();
    Logger::info("Monitor refresh rate: " + std::to_string(monitorRefreshRate) + "Hz");

    return true;
}

void Engine::run() {
    const double frameDelay = 1000.0 / monitorRefreshRate;

    while (running) {
        uint64_t frameStart = SDL_GetTicks64();

        handleEvents();
        render();

        frameCount++;
        uint64_t currentTime = SDL_GetTicks64();
        if (currentTime - fpsTimer >= 1000) {
            currentFPS = (int)frameCount;
            frameCount = 0;
            fpsTimer = currentTime;

            // Update title
            std::string title = "Lazerdeck - " + std::to_string(sampleRate) + "Hz, " + std::to_string(currentFPS) + " FPS";
            renderer.setWindowTitle(title);
        }

        uint64_t frameTime = SDL_GetTicks64() - frameStart;
        if (frameTime < frameDelay) {
            SDL_Delay((uint32_t)(frameDelay - frameTime));
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
        nfdchar_t *outPath = NULL;
        nfdresult_t result = NFD_OpenDialog(NULL, NULL, &outPath);
        
        if (result == NFD_OKAY) {
            Logger::info("Loading: " + std::string(outPath));
            if (activeDeckIndex == 0) deckA->load(outPath);
            else deckB->load(outPath);
            free(outPath);
        }
        else if (result == NFD_CANCEL) {
            Logger::info("Open cancelled");
        }
        else {
            Logger::error("Error: " + std::string(NFD_GetError()));
        }
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
    
    // Render Top Deck (A)
    renderer.renderDeck(deckA.get(), 0, renderer.getHeight() / 2, samplesPerPixel, "Deck A", activeDeckIndex == 0);
    
    // Render Bottom Deck (B)
    renderer.renderDeck(deckB.get(), renderer.getHeight() / 2, renderer.getHeight() / 2, samplesPerPixel, "Deck B", activeDeckIndex == 1);
    
    renderer.present();
}
