#include "Engine.hpp"
#include <iostream>
#include "Logger.hpp"
#include "OSCHandler.hpp"
#include "Vst3Runtime.hpp"
#include <filesystem>
#include <thread>
#include <chrono>
#include <sstream>
#include <cmath>

Engine::Engine() : running(false), sampleRate(44100) {}

Engine::~Engine() {
    stop();
    shutdown();
}

bool Engine::init(int numDecks) {
    Logger::info("Initializing Lazerdeck engine (headless)...");

    std::vector<Deck*> deckPtrs;
    for (int i = 0; i < numDecks; ++i) {
        decks.push_back(std::make_unique<Deck>(sampleRate));
        deckPtrs.push_back(decks.back().get());
    }

    mixer = std::make_unique<Lazerdeck::Mixer>(numDecks, sampleRate);

    if (!audioEngine.init(deckPtrs, mixer.get(), sampleRate, 128)) { // 128 frames (~2.9ms)
        Logger::error("AudioEngine initialization failed");
        return false;
    }

    sampleRate = audioEngine.getActualSampleRate();
    mixer->setSampleRate(sampleRate);
    for (auto& d : decks) d->updateSampleRate(sampleRate);

    if (!audioEngine.start()) {
        Logger::error("Failed to start AudioEngine");
        return false;
    }

    oscHandler = std::make_unique<OSCHandler>(this, 9000);
    oscHandler->start();

    // Optional VST3 support: load the separate plugin library if present.
    Lazerdeck::Vst3Runtime::load();
    vstScanThread = std::thread([this]() { scanVSTs(); });

    Logger::info("Lazerdeck engine ready with " + std::to_string(numDecks) +
                 " decks @ " + std::to_string(sampleRate) + "Hz");

    running = true;
    return true;
}

void Engine::run() {
    using namespace std::chrono;
    while (running.load()) {
        processCommands();   // LazerScript commands pushed via pushCommand()
        processTasks();      // tasks queued from other threads (e.g. OSC)
        checkTriggers();     // beat-synced triggers
        updateSync();        // deck tempo/phase sync

        std::this_thread::sleep_for(milliseconds(5));
    }

    shutdown();
}

void Engine::processCommands() {
    std::string cmd;
    while (commandQueue.try_pop(cmd)) {
        std::istringstream iss(cmd);
        std::string target;
        iss >> target;

        if (target == "s") {
            std::string action;
            iss >> action;
            if (action == "restart") {
                Logger::info("Restart requested (not implemented)");
            }
        } else if (target.rfind("$d", 0) == 0) { // Starts with '$d'
            try {
                int deckIdx = std::stoi(target.substr(2)) - 1; // $d1 -> 0
                if (deckIdx >= 0 && deckIdx < (int)decks.size()) {
                    std::string action;
                    iss >> action;
                    if (action == "play") decks[deckIdx]->play();
                    else if (action == "pause") decks[deckIdx]->pause();
                    else if (action == "stop") { decks[deckIdx]->pause(); decks[deckIdx]->setFrame(0); }
                    else if (action == "load") {
                        std::string remaining;
                        std::getline(iss, remaining);
                        size_t first = remaining.find_first_not_of(" \t\"");
                        if (std::string::npos != first) {
                            size_t last = remaining.find_last_not_of(" \t\"");
                            std::string path = remaining.substr(first, (last - first + 1));
                            decks[deckIdx]->load(path, &analysisDB);
                        }
                    }
                    else if (action == "seek") {
                        float seconds;
                        if (iss >> seconds) {
                            decks[deckIdx]->seek((int64_t)(seconds * sampleRate));
                        }
                    }
                    else if (action == "speed") {
                        double speed;
                        if (iss >> speed) {
                            decks[deckIdx]->setSpeed(speed);
                        }
                    }
                    else if (action == "volume") {
                        float vol;
                        if (iss >> vol && mixer) {
                            mixer->getChannel(deckIdx)->setVolume(vol);
                        }
                    }
                    else if (action == "sync") {
                        int sourceIdx;
                        if (iss >> sourceIdx) {
                            // 1-based index from user
                            if (sourceIdx >= 1 && sourceIdx <= (int)decks.size()) {
                                decks[deckIdx]->setSync(true, sourceIdx - 1);
                            } else if (sourceIdx == 0) {
                                decks[deckIdx]->setSync(false);
                            }
                        }
                    }
                    else if (action == "loop") {
                        float startBeat, endBeat;
                        if (iss >> startBeat >> endBeat) {
                            float bpm = decks[deckIdx]->getBPM();
                            if (bpm > 0) {
                                float offset = decks[deckIdx]->getBeatOffset();
                                double framesPerBeat = (double)sampleRate * 60.0 / (double)bpm;
                                uint64_t startFrame = (uint64_t)(startBeat * framesPerBeat + offset);
                                uint64_t endFrame = (uint64_t)(endBeat * framesPerBeat + offset);
                                decks[deckIdx]->setLoopRange(startFrame, endFrame);
                            }
                        }
                    }
                    else if (action == "loop_exit") {
                        decks[deckIdx]->exitLoop();
                    }
                    else if (action == "cue") {
                        int id;
                        if (iss >> id) {
                            float bpm = decks[deckIdx]->getBPM();
                            if (bpm > 0) {
                                float offset = decks[deckIdx]->getBeatOffset();
                                uint64_t current = decks[deckIdx]->getCurrentFrame();
                                double framesPerBeat = (double)sampleRate * 60.0 / (double)bpm;
                                float beat = (float)(((double)current - offset) / framesPerBeat);
                                decks[deckIdx]->addTrigger(id, beat);
                                decks[deckIdx]->addTriggerAction(id, { deckIdx, "play", {} });
                            }
                        }
                    }
                    else if (action == "goto_cue") {
                        int id;
                        if (iss >> id) {
                            auto& triggers = decks[deckIdx]->getTriggers();
                            for (const auto& t : triggers) {
                                if (t.id == id) {
                                    float bpm = decks[deckIdx]->getBPM();
                                    if (bpm > 0) {
                                        float offset = decks[deckIdx]->getBeatOffset();
                                        double framesPerBeat = (double)sampleRate * 60.0 / (double)bpm;
                                        uint64_t frame = (uint64_t)(t.beat * framesPerBeat + offset);
                                        decks[deckIdx]->setFrame(frame);
                                        decks[deckIdx]->resetTriggers();
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            } catch (...) {}
        }
    }
}

void Engine::queueTask(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(taskMutex);
    taskQueue.push_back(task);
}

void Engine::pushCommand(const std::string& cmd) {
    commandQueue.push(cmd);
}

void Engine::stop() {
    running = false;
}

void Engine::shutdown() {
    if (vstScanThread.joinable()) vstScanThread.join();
    if (oscHandler) oscHandler->stop();
    audioEngine.stop();
    if (mixer) {
        for (int i = 0; i < getNumDecks(); ++i) {
            if (auto* ch = mixer->getChannel(i)) ch->clearVSTs();
        }
    }
    Lazerdeck::Vst3Runtime::unload();
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

void Engine::checkTriggers() {
    for (size_t i = 0; i < decks.size(); ++i) {
        Deck* deck = decks[i].get();
        if (!deck->isPlaying()) continue;

        float bpm = deck->getBPM();
        if (bpm <= 0.0f) continue;

        uint64_t currentFrame = deck->getCurrentFrame();
        float offset = deck->getBeatOffset();
        double framesPerBeat = (double)sampleRate * 60.0 / (double)bpm;

        double currentBeat = ((double)currentFrame - offset) / framesPerBeat;

        auto& triggers = deck->getTriggers();
        for (auto& t : triggers) {
            if (!t.fired && currentBeat >= t.beat && currentBeat < t.beat + 0.5f) {
                t.fired = true;
                Logger::info("Engine: Firing trigger " + std::to_string(t.id) + " on Deck " + std::to_string(i));
                for (const auto& action : t.actions) {
                    executeAction(action);
                }
            }
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

void Engine::scanVSTs() {
    // SDK-free: just enumerate installed .vst3 bundles so a front-end can list
    // them. Actual hosting is delegated to the optional lazerdeck_vst3 plugin.
#ifdef _WIN32
    const std::string vstPath = "C:\\Program Files\\Common Files\\VST3";
#else
    const std::string vstPath = "/usr/lib/vst3";
#endif
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

void Engine::handleSystemCommand(const std::string& cmd, const osc::ReceivedMessage& m) {
    auto it = m.ArgumentsBegin();

    try {
        if (cmd == "printVSTs") {
            std::lock_guard<std::mutex> lock(vstMutex);
            Logger::info("--- Available VST3 Plugins ---");
            for (size_t i = 0; i < vstPaths.size(); ++i) {
                Logger::info("  [" + std::to_string(i) + "] " + vstPaths[i]);
            }
            Logger::info("Total: " + std::to_string(vstPaths.size()) + " plugins" +
                         (Lazerdeck::Vst3Runtime::available() ? "" : " (hosting disabled - lazerdeck_vst3 not loaded)"));
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
