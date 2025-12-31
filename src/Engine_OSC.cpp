#include "Engine.hpp"
#include "Deck.hpp"
#include "Logger.hpp"
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
    auto it = m.ArgumentsBegin();

    try {
        if (cmd == "load" || cmd == "using") {
            if (it->IsString()) {
                std::string path = (it++)->AsString();
                if (cmd == "load" || deck->getCurrentFilepath() != path) {
                    deck->load(path, &analysisDB);
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
        } else if (cmd.find("vst/") == 0) {
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
                        this->queueTask([channel, path]() {
                            channel->loadVST(path);
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
    } catch (const std::exception& e) {
        Logger::error("OSC: Error processing command '" + cmd + "': " + std::string(e.what()));
    }
}
