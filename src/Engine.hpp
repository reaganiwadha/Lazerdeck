#pragma once

#include "Renderer.hpp"
#include "AudioEngine.hpp"
#include "Deck.hpp"
#include "AnalysisDB.hpp"
#include "Mixer.hpp"
#ifdef ENABLE_VST3
#include "VST3Host.hpp"
#endif
#include <SDL2/SDL.h>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>
#include <osc/OscReceivedElements.h>
#include "ThreadSafeQueue.hpp"

class OSCHandler;

class Engine {
public:
    Engine();
    ~Engine();

    bool init(int numDecks = 2);
    void run();
    void stop(); // Add stop method to break the loop safely

    void pushCommand(const std::string& cmd);

    void handleOSCCommand(int deckIdx, const std::string& cmd, const osc::ReceivedMessage& m);
    void handleSystemCommand(const std::string& cmd, const osc::ReceivedMessage& m);
    void queueTask(std::function<void()> task);

private:
    void handleEvents();
    void handleGlobalInput(SDL_Event& event, bool shift);
    void handleDeckInput(Deck* activeDeck, SDL_Event& event, bool shift);
    void render();
    void processTasks();
    void updateSync();
    void checkTriggers();
    void executeAction(const TriggerAction& action);
    void shutdown();
#ifdef ENABLE_VST3
    void scanVSTs();
#endif

    bool running;
    Renderer renderer;
    AudioEngine audioEngine;
    AnalysisDB analysisDB;
    std::unique_ptr<Lazerdeck::Mixer> mixer;
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

#ifdef ENABLE_VST3
    std::vector<std::string> vstPaths;
    std::mutex vstMutex;
#endif
    std::string workingDirectory;
    ThreadSafeQueue<std::string> commandQueue;
};
