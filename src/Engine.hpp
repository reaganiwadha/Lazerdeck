#pragma once

#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "Deck.hpp"
#include "AnalysisDB.hpp"
#include <SDL2/SDL.h>
#include <memory>

class Engine {
public:
    Engine();
    ~Engine();

    bool init();
    void run();

private:
    void handleEvents();
    void update();
    void render();
    
    void handleDeckInput(Deck* deck, SDL_Event& event, bool shift);
    void handleGlobalInput(SDL_Event& event, bool shift);

    Renderer renderer;
    AudioEngine audioEngine;
    AnalysisDB analysisDB;
    
    std::unique_ptr<Deck> deckA;
    std::unique_ptr<Deck> deckB;
    
    bool running;
    int samplesPerPixel;
    int activeDeckIndex; // 0 for A, 1 for B
    int sampleRate;
    
    uint64_t lastTime;
    uint64_t frameCount;
    uint64_t fpsTimer;
    int currentFPS;
    int monitorRefreshRate;
};
