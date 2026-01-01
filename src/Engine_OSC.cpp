#include "Engine.hpp"
#include "Deck.hpp"
#include "Logger.hpp"
#include "Trigger.hpp"
#include <cmath>

static float getFloat(osc::ReceivedMessage::const_iterator& it) {
    if (it->IsFloat()) return (it++)->AsFloat();
    if (it->IsInt32()) return (float)(it++)->AsInt32();
    if (it->IsDouble()) return (float)(it++)->AsDouble();
    throw std::runtime_error("Argument is not a numeric type");
}

void Engine::handleOSCCommand(int deckIdx, const std::string& cmd, const osc::ReceivedMessage& m) {
    if (deckIdx < 0 || deckIdx >= (int)decks.size()) {
        Logger::error("OSC: Invalid deck index " + std::to_string(deckIdx));
        return;
    }

    Deck* deck = decks[deckIdx].get();
    auto* channel = mixer ? mixer->getChannel(deckIdx) : nullptr;
    auto it = m.ArgumentsBegin();

    try {
        if (cmd == "define/begin") {
            deck->beginDefinition();
#ifdef ENABLE_VST3
            if (channel) channel->beginDefinition();
#endif
        } else if (cmd == "define/end") {
            deck->endDefinition();
#ifdef ENABLE_VST3
            if (channel) channel->endDefinition();
#endif
        } else if (cmd == "load" || cmd == "using") {
            if (it->IsString()) {
                std::string path = (it++)->AsString();
                if (cmd == "load" || deck->getCurrentFilepath() != path) {
                    std::string fullPath = path;
                    if (!workingDirectory.empty()) {
                        if (path.length() >= 2 && path[1] != ':') {
                            fullPath = workingDirectory;
                            if (!workingDirectory.empty() && workingDirectory.back() != '\\' && workingDirectory.back() != '/') {
                                fullPath += "\\";
                            }
                            fullPath += path;
                        }
                    }
                    deck->load(fullPath, &analysisDB);
                }
            }
        } else if (cmd == "play") {
            deck->play();
        } else if (cmd == "pause") {
            deck->pause();
        } else if (cmd == "stop") {
            deck->pause();
            deck->setFrame(0);
        } else if (cmd == "speed") {
            deck->setSpeed(getFloat(it));
        } else if (cmd == "stretchBpm") {
            float targetBpm = getFloat(it);
            float currentBpm = deck->getBPM();
            if (currentBpm > 0.0f) {
                deck->setSpeed(targetBpm / currentBpm);
            } else {
                Logger::warn("OSC: Cannot stretchBpm without BPM analysis");
            }
        } else if (cmd == "loopAB") {
            // ... (rest of the function)
        } else if (cmd == "bpm" || cmd == "setRecognizedBPM") {
            deck->setBPM(getFloat(it));
        } else if (cmd == "offset") {
            deck->setBeatOffset(getFloat(it));
        } else if (cmd == "syncTo") {
             int sourceIdx = (int)getFloat(it);
             deck->setSync(true, sourceIdx);
             Logger::info("Deck " + std::to_string(deckIdx) + " syncing to Deck " + std::to_string(sourceIdx));
        } else if (cmd == "syncOff") {
             deck->setSync(false);
             Logger::info("Deck " + std::to_string(deckIdx) + " sync disabled");
        } else if (cmd == "beatSync") {
             int sourceIdx = (int)getFloat(it);
             if (sourceIdx >= 0 && sourceIdx < (int)decks.size()) {
                 Deck* master = decks[sourceIdx].get();
                 float masterBpm = master->getBPM();
                 float myBpm = deck->getBPM();
                 
                 if (masterBpm > 0 && myBpm > 0) {
                     // 1. Match Tempo First (One-shot)
                     float masterSpeed = (float)master->getSpeed();
                     float effectiveMasterBpm = masterBpm * masterSpeed;
                     float requiredSpeed = effectiveMasterBpm / myBpm;
                     deck->setSpeed(requiredSpeed);
                     
                     // 2. Align Phase
                     // Calculate master's phase (0.0 to 1.0 within a beat)
                     uint64_t mFrame = master->getCurrentFrame();
                     float mOffset = master->getBeatOffset();
                     double mFramesPerBeat = (double)sampleRate * 60.0 / (double)masterBpm; // Base BPM frame size
                     // Wait, effective BPM frames per beat changes with speed, 
                     // but Deck uses Original BPM for grid calculations usually.
                     // The audio frame count doesn't change, just the playback rate.
                     // So we use Original BPM to calculate beat phase in FILE frames.
                     
                     double mBeatTotal = ((double)mFrame - mOffset) / mFramesPerBeat;
                     double mPhase = mBeatTotal - floor(mBeatTotal);
                     
                     // My Phase
                     uint64_t myFrame = deck->getCurrentFrame();
                     float myOffset = deck->getBeatOffset();
                     double myFramesPerBeat = (double)sampleRate * 60.0 / (double)myBpm;
                     
                     double myBeatTotal = ((double)myFrame - myOffset) / myFramesPerBeat;
                     double myPhase = myBeatTotal - floor(myBeatTotal);
                     
                     // Calculate difference in beats
                     double diff = mPhase - myPhase;
                     // Wrap to shortest distance -0.5 to 0.5
                     if (diff > 0.5) diff -= 1.0;
                     if (diff < -0.5) diff += 1.0;
                     
                     // Convert beat difference to frames
                     int64_t frameDiff = (int64_t)(diff * myFramesPerBeat);
                     
                     deck->seek(frameDiff);
                     Logger::info("Deck " + std::to_string(deckIdx) + " beat-synced to Deck " + std::to_string(sourceIdx) + " (Shift: " + std::to_string(frameDiff) + " frames)");
                 }
             }
        } else if (cmd == "trigger/new") {
            int id = (int)getFloat(it);
            float beat = getFloat(it);
            this->queueTask([deck, id, beat]() {
                deck->addTrigger(id, beat);
                deck->markTriggerDefined(id);
            });
        } else if (cmd == "trigger/action") {
            int triggerId = (int)getFloat(it);
            int targetDeckIdx = (int)getFloat(it);
            if (it->IsString()) {
                std::string command = (it++)->AsString();
                std::vector<float> args;
                while (it != m.ArgumentsEnd()) {
                    args.push_back(getFloat(it));
                }
                TriggerAction action{targetDeckIdx, command, args};
                deck->addTriggerAction(triggerId, action);
                Logger::info("OSC: Added action '" + command + "' to trigger " + std::to_string(triggerId) + " on Deck " + std::to_string(deckIdx));
            }
        } 
#ifdef ENABLE_VST3
        else if (cmd.find("vst/") == 0) {
            auto* channel = mixer ? mixer->getChannel(deckIdx) : nullptr;
            if (channel) {
                // Handle vst/<index>/...
                size_t nextSlash = cmd.find('/', 4);
                if (nextSlash != std::string::npos) {
                    int vstIdx = std::stoi(cmd.substr(4, nextSlash - 4));
                    std::string subCmd = cmd.substr(nextSlash + 1);
                    
                    if (subCmd == "show") {
                        this->queueTask([channel, vstIdx]() {
                            channel->showVST(vstIdx);
                        });
                    } else if (subCmd == "using") {
                        if (it->IsString()) {
                            std::string path = (it++)->AsString();
                            this->queueTask([channel, vstIdx, path]() {
                                channel->usingVST(vstIdx, path);
                                channel->markVSTDefined(vstIdx);
                            });
                        }
                    } else if (subCmd == "param") {
                        int paramIdx = (int)getFloat(it);
                        float value = getFloat(it);
                        channel->setVSTParameter(vstIdx, paramIdx, value);
                    }
                } else if (cmd == "vst/load") {
                    if (it->IsString()) {
                        std::string path = (it++)->AsString();
                        // Legacy single VST load, map to index 0
                        this->queueTask([channel, path]() {
                            channel->usingVST(0, path);
                            channel->markVSTDefined(0);
                        });
                    }
                } else if (cmd == "vst/param") {
                    int vstIdx = (int)getFloat(it);
                    int paramIdx = (int)getFloat(it);
                    float value = getFloat(it);
                    channel->setVSTParameter(vstIdx, paramIdx, value);
                } else if (cmd == "vst/clear") {
                    channel->clearVSTs();
                }
            } else {
                 Logger::warn("OSC: No mixer channel for deck " + std::to_string(deckIdx));
            }
        }
#endif
    } catch (const std::exception& e) {
        Logger::error("OSC: Error processing command '" + cmd + "': " + std::string(e.what()));
    }
}
