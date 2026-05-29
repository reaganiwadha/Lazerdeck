#pragma once

#include "AudioEngine.hpp"
#include "Deck.hpp"
#include "AnalysisDB.hpp"
#include "Mixer.hpp"
#include <atomic>
#include <thread>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>
#include <string>
#include <osc/OscReceivedElements.h>
#include "ThreadSafeQueue.hpp"

class OSCHandler;

// Headless audio engine. Owns the decks, mixer, PortAudio output and the OSC
// listener. Has no windowing/UI dependency: the host (Flutter via the FFI in
// lazerdeck.h, or any other front-end) drives it through commands and reads
// state back. run() is a blocking service loop meant to run on its own thread.
class Engine {
public:
    Engine();
    ~Engine();

    bool init(int numDecks = 2);
    void run();   // blocks until stop()
    void stop();  // breaks the loop safely (callable from another thread)

    void pushCommand(const std::string& cmd);

    void handleOSCCommand(int deckIdx, const std::string& cmd, const osc::ReceivedMessage& m);
    void handleSystemCommand(const std::string& cmd, const osc::ReceivedMessage& m);
    void queueTask(std::function<void()> task);

    int   getNumDecks() const { return (int)decks.size(); }
    int   getSampleRate() const { return sampleRate; }
    Deck* getDeck(int idx) {
        if (idx < 0 || idx >= (int)decks.size()) return nullptr;
        return decks[idx].get();
    }
    Lazerdeck::Mixer* getMixer() { return mixer.get(); }

private:
    void processCommands();   // drains the LazerScript command queue
    void processTasks();
    void updateSync();
    void checkTriggers();
    void executeAction(const TriggerAction& action);
    void shutdown();
    void scanVSTs();          // SDK-free filesystem scan of installed .vst3 bundles

    std::atomic<bool> running;
    AudioEngine audioEngine;
    AnalysisDB analysisDB;
    std::unique_ptr<Lazerdeck::Mixer> mixer;
    std::unique_ptr<OSCHandler> oscHandler;

    std::vector<std::unique_ptr<Deck>> decks;

    int sampleRate;

    std::vector<std::function<void()>> taskQueue;
    std::mutex taskMutex;

    std::vector<std::string> vstPaths;
    std::mutex vstMutex;
    std::thread vstScanThread;

    std::string workingDirectory;
    ThreadSafeQueue<std::string> commandQueue;
};
