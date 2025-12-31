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

    bool init(int numDecks = 2);
    void run();

private:
    void handleEvents();
    void handleGlobalInput(SDL_Event& event, bool shift);
    void handleDeckInput(Deck* activeDeck, SDL_Event& event, bool shift);
    void render();

    bool running;
    Renderer renderer;
    AudioEngine audioEngine;
    AnalysisDB analysisDB;
    
    std::vector<std::unique_ptr<Deck>> decks;
    int activeDeckIndex;

    int samplesPerPixel;
    int sampleRate;

    uint64_t lastTime;
    uint64_t frameCount;
    uint64_t fpsTimer;
    int currentFPS;
    int monitorRefreshRate;
    uint64_t perfFrequency;
};
