#pragma once

#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "Deck.hpp"
#include "AnalysisDB.hpp"
#include "VST3Host.hpp"
#include <SDL2/SDL.h>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>
#include <osc/OscReceivedElements.h>

class OSCHandler;

class Engine {
public:
    Engine();
    ~Engine();

    bool init(int numDecks = 2);
    void run();

    void handleOSCCommand(int deckIdx, const std::string& cmd, const osc::ReceivedMessage& m);
    void queueTask(std::function<void()> task);

private:
    void handleEvents();
    void handleGlobalInput(SDL_Event& event, bool shift);
    void handleDeckInput(Deck* activeDeck, SDL_Event& event, bool shift);
    void render();
    void processTasks();

    bool running;
    Renderer renderer;
    AudioEngine audioEngine;
    AnalysisDB analysisDB;
    std::unique_ptr<OSCHandler> oscHandler;
    
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

    std::vector<std::function<void()>> taskQueue;
    std::mutex taskMutex;
};
