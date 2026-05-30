#include "Engine.hpp"
#include <iostream>
#include "Logger.hpp"
#include "OSCHandler.hpp"
#include "ControlServer.hpp"
#include "Vst3Runtime.hpp"
#include <filesystem>
#include <thread>
#include <chrono>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace {
// Formats a (1-based) beat for a marker label: integer when whole, else 2 dp.
std::string formatBeat(double beat) {
    long long r = std::llround(beat);
    if (std::abs(beat - (double)r) < 1e-6) return std::to_string(r);
    std::ostringstream os; os.precision(2); os << std::fixed << beat; return os.str();
}
// Parses a `$dN` token to a 0-based deck index, or -1 if it isn't one.
int parseDeckRef(const std::string& tok) {
    if (tok.rfind("$d", 0) != 0) return -1;
    try { return std::stoi(tok.substr(2)) - 1; } catch (...) { return -1; }
}
} // namespace

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
    syncWanted.assign(numDecks, false);
    masterDeck.store(0);

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

    // JSON control plane: external REPL clients POST /action here (see Control.hpp).
    // Try the default port at startup; a busy port is non-fatal — the host can
    // start it on another port from the settings UI.
    controlServer = std::make_unique<ControlServer>(this, 8203);
    controlServer->start(controlServer->port());

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
        processCommands();     // LazerScript commands pushed via pushCommand()
        processTasks();        // tasks queued from other threads (e.g. OSC)
        checkTriggers();       // beat-synced cue triggers (typed actions)
        checkScriptTriggers(); // general `when hit B do (cmd)` triggers
        applyAutomation();     // drive automated params from their envelopes
        updateMaster();        // elect master + reconcile sync intent
        updateSync();          // deck tempo/phase sync

        std::this_thread::sleep_for(milliseconds(5));
    }

    shutdown();
}

// ---------------------------------------------------------------------------
// Structured verb helpers. Each performs the immediate effect of one verb on the
// engine thread; both the legacy text parser (processCommands) and the JSON
// dispatcher (dispatchAction) route through them. Decks are 0-based; beats are
// 1-based (beat 1 = grid origin), matching the UI grid / script convention.
// ---------------------------------------------------------------------------
bool Engine::frameForBeat(int deck, double scriptBeat, uint64_t& outFrame) {
    Deck* d = getDeck(deck);
    if (!d) return false;
    float bpm = d->getBPM();
    if (!(bpm > 0.0f)) return false;
    double framesPerBeat = (double)sampleRate * 60.0 / (double)bpm;
    outFrame = (uint64_t)std::max(0.0, (scriptBeat - 1.0) * framesPerBeat +
                                       (double)d->getBeatOffset());
    return true;
}

void Engine::actLoad(int deck, const std::string& path) {
    Deck* d = getDeck(deck);
    if (!d || path.empty()) return;
    clearDeckScript(deck);          // drop old lanes/triggers
    d->load(path, &analysisDB);
}

void Engine::actPlay(int deck)  { if (Deck* d = getDeck(deck)) d->play(); }
void Engine::actPause(int deck) { if (Deck* d = getDeck(deck)) d->pause(); }
void Engine::actStop(int deck)  { if (Deck* d = getDeck(deck)) { d->pause(); d->setFrame(0); } }

void Engine::actSeekSeconds(int deck, double seconds) {
    if (Deck* d = getDeck(deck)) d->seek((int64_t)(seconds * sampleRate));
}

void Engine::actSpeed(int deck, double mult) {
    if (Deck* d = getDeck(deck)) d->setSpeed(mult);
}

void Engine::actPlayjump(int deck, double beat) {
    Deck* d = getDeck(deck);
    uint64_t frame;
    if (d && frameForBeat(deck, beat, frame)) { d->setFrame(frame); d->play(); }
}

void Engine::actBpm(int deck, double bpm) {
    Deck* d = getDeck(deck);
    if (d && bpm > 0.0) { d->setBpmManual((float)bpm); d->saveAnalysis(analysisDB); }
}

void Engine::actOffset(int deck, double frames) {
    Deck* d = getDeck(deck);
    if (d) { d->setBeatOffset((float)frames); d->saveAnalysis(analysisDB); }
}

void Engine::actNudgeOffset(int deck, double delta) {
    Deck* d = getDeck(deck);
    if (d) { d->nudgeBeatOffset((float)delta); d->saveAnalysis(analysisDB); }
}

void Engine::actReanalyze(int deck) {
    if (Deck* d = getDeck(deck)) d->reanalyze(analysisDB);
}

void Engine::actMetronome(int deck, bool on) {
    if (Deck* d = getDeck(deck)) d->setMetronome(on);
}

void Engine::actSync(int deck, bool on) {
    if (deck < 0 || deck >= (int)decks.size()) return;
    setSyncWanted(deck, on);
    updateMaster(); // reconcile immediately, don't wait a tick
}

void Engine::actMaster(int deck) {
    if (deck < 0 || deck >= (int)decks.size()) return;
    setMasterDeck(deck);
    updateMaster();
}

void Engine::actAlign(int subj, double subjBeat, int ref, double refBeat) {
    Deck* S = getDeck(subj);
    if (!S || getDeck(ref) == nullptr) return;
    double bpmS = S->getBPM();
    if (!(bpmS > 0.0)) return;
    double rNow = deckBeat(ref);            // reference's current 0-based beat
    if (std::isnan(rNow)) return;
    // Tempo-locked, beat_ref - beat_subj is constant; the 1-based vs 0-based
    // convention cancels in the difference, so subjBeat/refBeat pass through raw.
    double sNew = rNow - (refBeat - subjBeat);   // subject's desired beat now
    double framesPerBeat = (double)sampleRate * 60.0 / bpmS;
    double frame = sNew * framesPerBeat + (double)S->getBeatOffset();
    S->setFrame((uint64_t)std::max(0.0, frame));
    Logger::info("align: D" + std::to_string(subj + 1) + " beat " + formatBeat(subjBeat) +
                 " -> D" + std::to_string(ref + 1) + " beat " + formatBeat(refBeat));
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

                    // Pull out a parenthesised group `( ... )` for the beat-
                    // keyframing grammar (automation envelopes, trigger actions).
                    // The prefix tokens are still read from `iss` as usual.
                    std::string group;
                    bool hasGroup = false;
                    {
                        size_t lp = cmd.find('(');
                        if (lp != std::string::npos) {
                            int depth = 0; size_t i = lp;
                            for (; i < cmd.size(); ++i) {
                                if (cmd[i] == '(') depth++;
                                else if (cmd[i] == ')') { if (--depth == 0) break; }
                            }
                            if (depth == 0 && i > lp) {
                                group = cmd.substr(lp + 1, i - lp - 1);
                                size_t a = group.find_first_not_of(" \t");
                                size_t b = group.find_last_not_of(" \t");
                                group = (a == std::string::npos) ? "" : group.substr(a, b - a + 1);
                                hasGroup = true;
                            }
                        }
                    }

                    // $dT play                    play now
                    // $dT play when $dS <beat>     play when deck S hits <beat>
                    if (action == "play") {
                        std::string kw;
                        if (iss >> kw && kw == "when") {
                            std::string srcTok; double srcBeat;
                            int src;
                            if (iss >> srcTok >> srcBeat &&
                                (src = parseDeckRef(srcTok)) >= 0 &&
                                src < (int)decks.size()) {
                                ScriptTrigger t;
                                t.deck = src;
                                t.beat = srcBeat - 1.0; // 1-based script → 0-based
                                t.command = "$d" + std::to_string(deckIdx + 1) + " play";
                                t.kind = MARKER_PLAY;
                                t.targetDeck = deckIdx;
                                t.label = "\xE2\x96\xB6 D" + std::to_string(deckIdx + 1); // ▶ Dn
                                addScriptTrigger(std::move(t));
                                Logger::info("play-when: D" + std::to_string(deckIdx + 1) +
                                             " plays when D" + std::to_string(src + 1) +
                                             " hits beat " + std::to_string(srcBeat));
                            }
                        } else {
                            actPlay(deckIdx);
                        }
                    }
                    else if (action == "pause") actPause(deckIdx);
                    else if (action == "stop") actStop(deckIdx);
                    else if (action == "load") {
                        std::string remaining;
                        std::getline(iss, remaining);
                        size_t first = remaining.find_first_not_of(" \t\"");
                        if (std::string::npos != first) {
                            size_t last = remaining.find_last_not_of(" \t\"");
                            actLoad(deckIdx, remaining.substr(first, (last - first + 1)));
                        }
                    }
                    else if (action == "seek") {
                        float seconds;
                        if (iss >> seconds) actSeekSeconds(deckIdx, seconds);
                    }
                    else if (action == "speed") {
                        double speed;
                        if (iss >> speed) actSpeed(deckIdx, speed);
                    }
                    else if (action == "speed_up") {
                        decks[deckIdx]->increaseSpeed();
                    }
                    else if (action == "speed_down") {
                        decks[deckIdx]->decreaseSpeed();
                    }
                    else if (action == "speed_reset") {
                        actSpeed(deckIdx, 1.0);
                    }
                    // Automatable params (raw engine units, 0..1; 0.5 = unity).
                    // Three forms:
                    //   $dN <param> <value>            immediate (clears the lane)
                    //   $dN <param> clear              remove the automation lane
                    //   $dN <param> (onBeat b v b v …) set the automation lane
                    else if (action == "low"  || action == "eq_low"  ||
                             action == "mid"  || action == "eq_mid"  ||
                             action == "high" || action == "eq_high" ||
                             action == "volume" || action == "vol") {
                        const std::string canon =
                            (action == "low"  || action == "eq_low")  ? "eq_low"  :
                            (action == "mid"  || action == "eq_mid")  ? "eq_mid"  :
                            (action == "high" || action == "eq_high") ? "eq_high" : "volume";

                        if (hasGroup && group.rfind("onBeat", 0) == 0) {
                            std::istringstream gs(group);
                            std::string kw; gs >> kw; // "onBeat"
                            ParamLane lane; lane.deck = deckIdx; lane.param = canon;
                            double b; float v;
                            // Script beats are 1-based (matching the UI grid:
                            // beat 1 = grid origin); the runtime works in the
                            // 0-based domain of deckBeat(), so shift here.
                            while (gs >> b >> v) lane.keys.push_back({b - 1.0, v});
                            if (!lane.keys.empty()) {
                                std::sort(lane.keys.begin(), lane.keys.end(),
                                          [](const Keyframe& x, const Keyframe& y) { return x.beat < y.beat; });
                                std::lock_guard<std::mutex> lk(lanesMutex);
                                lanes[std::to_string(deckIdx) + ":" + canon] = std::move(lane);
                                Logger::info("Automation lane set: deck " + std::to_string(deckIdx) + " " + canon);
                            }
                        } else {
                            std::string tok;
                            iss >> tok;
                            if (tok == "clear") {
                                clearLane(deckIdx, canon);
                                Logger::info("Automation lane cleared: deck " + std::to_string(deckIdx) + " " + canon);
                            } else {
                                try {
                                    float v = std::stof(tok);
                                    clearLane(deckIdx, canon); // manual overrides automation
                                    setDeckParam(deckIdx, canon, v);
                                } catch (...) {}
                            }
                        }
                    }
                    // $dT playjump <tbeat>                  jump to <tbeat> & play now
                    // $dT playjump <tbeat> when $dS <beat>  …when deck S hits <beat>
                    else if (action == "playjump") {
                        double tbeat;
                        if (iss >> tbeat) {
                            std::string kw;
                            if (iss >> kw && kw == "when") {
                                std::string srcTok; double srcBeat;
                                int src;
                                if (iss >> srcTok >> srcBeat &&
                                    (src = parseDeckRef(srcTok)) >= 0 &&
                                    src < (int)decks.size()) {
                                    ScriptTrigger t;
                                    t.deck = src;
                                    t.beat = srcBeat - 1.0;
                                    std::ostringstream cs;
                                    cs << "$d" << (deckIdx + 1) << " playjump " << tbeat;
                                    t.command = cs.str(); // re-parsed (1-based) when it fires
                                    t.kind = MARKER_JUMP;
                                    t.targetDeck = deckIdx;
                                    t.targetBeat = tbeat - 1.0;
                                    t.label = "D" + std::to_string(deckIdx + 1) +
                                              "\xE2\x86\x92" + formatBeat(tbeat); // Dn→beat
                                    addScriptTrigger(std::move(t));
                                    Logger::info("playjump-when: D" + std::to_string(deckIdx + 1) +
                                                 " jumps to beat " + formatBeat(tbeat) +
                                                 " when D" + std::to_string(src + 1) +
                                                 " hits beat " + std::to_string(srcBeat));
                                }
                            } else {
                                actPlayjump(deckIdx, tbeat);
                            }
                        }
                    }
                    // $dN when hit <beat> do ( <command> ) — general beat trigger.
                    else if (action == "when") {
                        std::string sub, doKw; double beat;
                        if ((iss >> sub >> beat >> doKw) && sub == "hit" &&
                            doKw == "do" && hasGroup && !group.empty()) {
                            ScriptTrigger t;
                            t.deck = deckIdx;
                            t.beat = beat - 1.0; // 1-based script → 0-based runtime
                            t.command = group;
                            t.kind = MARKER_GENERIC;
                            t.targetDeck = parseDeckRef(group); // -1 unless it leads with $dN
                            t.label = group;                    // show the command itself
                            addScriptTrigger(std::move(t));
                            Logger::info("ScriptTrigger added: deck " + std::to_string(deckIdx) +
                                         " hit " + std::to_string(beat) + " -> " + group);
                        }
                    }
                    else if (action == "bpm") {
                        float value;
                        if (iss >> value && value > 0.0f) actBpm(deckIdx, value);
                    }
                    else if (action == "offset") {
                        float frames;
                        if (iss >> frames) actOffset(deckIdx, frames);
                    }
                    else if (action == "nudge_offset") {
                        float delta;
                        if (iss >> delta) actNudgeOffset(deckIdx, delta);
                    }
                    else if (action == "reanalyze") {
                        actReanalyze(deckIdx);
                    }
                    else if (action == "metronome") {
                        int on;
                        if (iss >> on) actMetronome(deckIdx, on != 0);
                    }
                    else if (action == "metronome_tick") {
                        // One-shot click for the Tap Tempo wizard.
                        decks[deckIdx]->requestMetronomeTick();
                    }
                    else if (action == "sync") {
                        // Now a per-deck sync *intent* toggle; the engine elects
                        // the master and points followers at it (see
                        // updateMaster). Any source >= 1 means "on", 0 = off; the
                        // specific source index is ignored (always the master).
                        int sourceIdx;
                        if (iss >> sourceIdx) actSync(deckIdx, sourceIdx != 0);
                    }
                    else if (action == "master") {
                        actMaster(deckIdx);
                    }
                    else if (action == "loop") {
                        float startBeat, endBeat;
                        if (iss >> startBeat >> endBeat) {
                            float bpm = decks[deckIdx]->getBPM();
                            if (bpm > 0) {
                                float offset = decks[deckIdx]->getBeatOffset();
                                double framesPerBeat = (double)sampleRate * 60.0 / (double)bpm;
                                // Beats are 1-based (beat 1 = grid origin).
                                uint64_t startFrame = (uint64_t)std::max(0.0, (startBeat - 1.0) * framesPerBeat + offset);
                                uint64_t endFrame = (uint64_t)std::max(0.0, (endBeat - 1.0) * framesPerBeat + offset);
                                decks[deckIdx]->setLoopRange(startFrame, endFrame);
                            }
                        }
                    }
                    else if (action == "loop_in") {
                        decks[deckIdx]->setLoopStart();
                    }
                    else if (action == "loop_out") {
                        decks[deckIdx]->setLoopEnd();
                    }
                    else if (action == "loop_exit") {
                        decks[deckIdx]->exitLoop();
                    }
                    else if (action == "loop_clear") {
                        decks[deckIdx]->clearLoop();
                    }
                    else if (action == "reloop") {
                        uint64_t start = decks[deckIdx]->getLoopStart();
                        uint64_t end = decks[deckIdx]->getLoopEnd();
                        
                        // Use recall points if current loop points are empty
                        if (start == 0) {
                            start = decks[deckIdx]->getRecallStart();
                            end = decks[deckIdx]->getRecallEnd();
                        }

                        if (start > 0) {
                            decks[deckIdx]->setFrame(start);
                            if (end > start) {
                                decks[deckIdx]->setLoopRange(start, end);
                            }
                            decks[deckIdx]->play();
                        }
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

int Engine::getAudioDeviceCount() {
    return (int)audioEngine.refreshDevices().size();
}

bool Engine::getAudioDevice(int listIndex, AudioDeviceInfo& out) {
    return audioEngine.getCachedDevice(listIndex, out);
}

void Engine::setAudioDevice(int deviceIndex) {
    // Run the reopen on the engine thread so it's serialized with the rest of
    // engine state and never races the audio callback.
    queueTask([this, deviceIndex]() {
        if (audioEngine.reopen(deviceIndex)) {
            sampleRate = audioEngine.getActualSampleRate();
            if (mixer) mixer->setSampleRate(sampleRate);
            for (auto& d : decks) d->updateSampleRate(sampleRate);
            Logger::info("Audio device switched; engine now @ " +
                         std::to_string(sampleRate) + " Hz");
        } else {
            Logger::error("Audio device switch failed; keeping previous output");
        }
    });
}

void Engine::setSampleRate(int rate) {
    // Serialized on the engine thread, like setAudioDevice — never races the
    // audio callback or command processing.
    queueTask([this, rate]() {
        if (rate <= 0 || rate == sampleRate) return;

        // Capture each deck's resume state before we retarget anything.
        // currentInputTime is source-track seconds (sample-rate invariant).
        struct Resume { std::string file; double sec; bool playing; };
        std::vector<Resume> resume(decks.size());
        for (size_t i = 0; i < decks.size(); ++i) {
            resume[i] = { decks[i]->getCurrentFilepath(),
                          decks[i]->getCurrentInputTime(),
                          decks[i]->isPlaying() };
        }

        const int oldRate = sampleRate;
        const bool ok = audioEngine.reopen(audioEngine.getCurrentDevice(), rate);
        const int actual = ok ? audioEngine.getActualSampleRate() : rate;
        if (actual == oldRate) {
            Logger::info("Sample rate unchanged at " + std::to_string(actual) + " Hz");
            return;
        }

        sampleRate = actual;
        if (mixer) mixer->setSampleRate(actual);
        for (auto& d : decks) d->updateSampleRate(actual);

        // Re-decode loaded tracks at the new rate, resuming where they were.
        for (size_t i = 0; i < decks.size(); ++i) {
            if (resume[i].file.empty()) continue;
            decks[i]->load(resume[i].file, &analysisDB, resume[i].sec, resume[i].playing);
        }
        Logger::info("Engine sample rate set to " + std::to_string(actual) + " Hz");
    });
}

void Engine::queueTask(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(taskMutex);
    taskQueue.push_back(task);
}

void Engine::pushCommand(const std::string& cmd) {
    commandQueue.push(cmd);
}

namespace {
// Parses a JSON deck reference ("d1", "D2", or a bare "1") to a 0-based index,
// or -1 if it isn't one. The wire form is 1-based, like the script convention.
int parseDeckName(const std::string& s) {
    if (s.empty()) return -1;
    size_t i = (s[0] == 'd' || s[0] == 'D') ? 1 : 0;
    try { return std::stoi(s.substr(i)) - 1; } catch (...) { return -1; }
}
} // namespace

bool Engine::startControlServer(int port) {
    return controlServer ? controlServer->start(port) : false;
}

void Engine::stopControlServer() {
    if (controlServer) controlServer->stop();
}

bool Engine::isControlServerRunning() const {
    return controlServer && controlServer->isRunning();
}

int Engine::getControlServerPort() const {
    return controlServer ? controlServer->port() : 0;
}

void Engine::submitActions(const ControlRequest& req) {
    // Parse happens on the HTTP thread; mutation must happen on the engine thread.
    for (const ControlAction& a : req.actions) {
        queueTask([this, a]() { dispatchAction(a); });
    }
}

void Engine::dispatchAction(const ControlAction& a) {
    const int deck = parseDeckName(a.deck);

    // align is cross-deck and doesn't schedule on a single deck's beat.
    if (a.action == "align") {
        const int ref = a.reference ? parseDeckName(*a.reference) : -1;
        if (deck < 0 || ref < 0) return;
        actAlign(deck, a.subjectBeat.value_or(1.0), ref, a.referenceBeat.value_or(1.0));
        return;
    }

    if (deck < 0 || deck >= (int)decks.size()) return;

    // The immediate effect of this action, captured so it can run now or later.
    std::function<void()> effect;
    const std::string& v = a.action;
    if      (v == "play")         effect = [this, deck]{ actPlay(deck); };
    else if (v == "pause")        effect = [this, deck]{ actPause(deck); };
    else if (v == "stop")         effect = [this, deck]{ actStop(deck); };
    else if (v == "load")         { if (a.path) { std::string p = *a.path; effect = [this, deck, p]{ actLoad(deck, p); }; } }
    else if (v == "seek")         { double s = a.value.value_or(0.0); effect = [this, deck, s]{ actSeekSeconds(deck, s); }; }
    else if (v == "speed")        { double m = a.speed.value_or(1.0); effect = [this, deck, m]{ actSpeed(deck, m); }; }
    else if (v == "speed_reset")  effect = [this, deck]{ actSpeed(deck, 1.0); };
    else if (v == "playjump")     { double b = a.beat.value_or(1.0); effect = [this, deck, b]{ actPlayjump(deck, b); }; }
    else if (v == "bpm")          { double b = a.value.value_or(0.0); effect = [this, deck, b]{ actBpm(deck, b); }; }
    else if (v == "offset")       { double f = a.value.value_or(0.0); effect = [this, deck, f]{ actOffset(deck, f); }; }
    else if (v == "nudge_offset") { double d = a.value.value_or(0.0); effect = [this, deck, d]{ actNudgeOffset(deck, d); }; }
    else if (v == "reanalyze")    effect = [this, deck]{ actReanalyze(deck); };
    else if (v == "metronome")    { bool on = a.on.value_or(true); effect = [this, deck, on]{ actMetronome(deck, on); }; }
    else if (v == "sync")         { bool on = a.on.value_or(true); effect = [this, deck, on]{ actSync(deck, on); }; }
    else if (v == "master")       effect = [this, deck]{ actMaster(deck); };
    else if (v == "eq_low" || v == "eq_mid" || v == "eq_high" || v == "volume") {
        std::string canon = v; double val = a.value.value_or(0.5);
        effect = [this, deck, canon, val]{ clearLane(deck, canon); setDeckParam(deck, canon, (float)val); };
    }

    if (!effect) {
        Logger::error("dispatchAction: unknown/incomplete action '" + v + "'");
        return;
    }

    if (a.onBeat) {
        // Defer until `deck` reaches the given (1-based) beat. Reuses the same
        // one-shot trigger machinery as the legacy `when hit … do` grammar so the
        // waveform overlay (snapshotMarkers) draws it too.
        ScriptTrigger t;
        t.deck = deck;
        t.beat = *a.onBeat - 1.0;   // 1-based wire -> 0-based runtime
        t.fn = std::move(effect);
        t.kind = MARKER_GENERIC;
        t.targetDeck = deck;
        t.label = v;
        addScriptTrigger(std::move(t));
    } else {
        effect();
    }
}

void Engine::stop() {
    running = false;
}

void Engine::shutdown() {
    if (vstScanThread.joinable()) vstScanThread.join();
    if (controlServer) controlServer->stop();
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

// ---------------------------------------------------------------------------
// Beat keyframing runtime
// ---------------------------------------------------------------------------

// Piecewise-linear envelope with flat-held endpoints (Ableton clip-envelope).
static float evalEnvelope(const std::vector<Keyframe>& keys, double beat) {
    if (keys.empty()) return 0.0f;
    if (beat <= keys.front().beat) return keys.front().value;
    if (beat >= keys.back().beat)  return keys.back().value;
    for (size_t i = 1; i < keys.size(); ++i) {
        if (beat <= keys[i].beat) {
            const Keyframe& a = keys[i - 1];
            const Keyframe& b = keys[i];
            double span = b.beat - a.beat;
            double t = span > 0.0 ? (beat - a.beat) / span : 0.0;
            return (float)(a.value + t * ((double)b.value - (double)a.value));
        }
    }
    return keys.back().value;
}

bool Engine::setDeckParam(int deck, const std::string& canon, float value) {
    if (!mixer) return false;
    auto* ch = mixer->getChannel(deck);
    if (!ch) return false;
    if (canon == "eq_low")       ch->setEqLow(value);
    else if (canon == "eq_mid")  ch->setEqMid(value);
    else if (canon == "eq_high") ch->setEqHigh(value);
    else if (canon == "volume")  ch->setVolume(value);
    else return false;
    return true;
}

void Engine::clearLane(int deck, const std::string& canon) {
    std::lock_guard<std::mutex> lk(lanesMutex);
    lanes.erase(std::to_string(deck) + ":" + canon);
}

void Engine::clearDeckScript(int deck) {
    {
        std::lock_guard<std::mutex> lk(lanesMutex);
        for (auto it = lanes.begin(); it != lanes.end(); ) {
            if (it->second.deck == deck) it = lanes.erase(it);
            else ++it;
        }
    }
    {
        std::lock_guard<std::mutex> lk(triggersMutex);
        scriptTriggers.erase(
            std::remove_if(scriptTriggers.begin(), scriptTriggers.end(),
                [deck](const ScriptTrigger& t) { return t.deck == deck; }),
            scriptTriggers.end());
    }
}

void Engine::addScriptTrigger(ScriptTrigger t) {
    std::lock_guard<std::mutex> lk(triggersMutex);
    scriptTriggers.push_back(std::move(t));
}

double Engine::deckBeat(int deck) const {
    if (deck < 0 || deck >= (int)decks.size()) return std::nan("");
    Deck* d = decks[deck].get();
    float bpm = d->getBPM();
    if (bpm <= 0.0f) return std::nan("");
    double framesPerBeat = (double)sampleRate * 60.0 / (double)bpm;
    double offset = (double)d->getBeatOffset();
    return ((double)d->getCurrentFrame() - offset) / framesPerBeat;
}

void Engine::applyAutomation() {
    // Drive each automated param from its envelope at the deck's current beat.
    // Applied whether or not the deck is playing (paused = value frozen at the
    // playhead). Manual clears the lane, so a present lane always owns the param.
    for (auto& kv : lanes) {
        ParamLane& lane = kv.second;
        if (lane.keys.empty()) continue;
        double beat = deckBeat(lane.deck);
        if (std::isnan(beat)) continue;
        setDeckParam(lane.deck, lane.param, evalEnvelope(lane.keys, beat));
    }
}

int Engine::snapshotLanes(int deck, uint32_t maxLanes, uint32_t maxKeys,
                          int32_t* paramIds, uint32_t* keyCounts,
                          double* beats, float* values) {
    std::lock_guard<std::mutex> lk(lanesMutex);
    uint32_t laneN = 0;
    uint32_t keyN = 0;
    for (auto& kv : lanes) {
        const ParamLane& lane = kv.second;
        if (lane.deck != deck) continue;
        if (laneN >= maxLanes) break;
        const int pid = lane.param == "eq_low"  ? 0 :
                        lane.param == "eq_mid"  ? 1 :
                        lane.param == "eq_high" ? 2 : 3;
        uint32_t written = 0;
        for (const Keyframe& k : lane.keys) {
            if (keyN >= maxKeys) break;
            if (beats)  beats[keyN]  = k.beat;
            if (values) values[keyN] = k.value;
            ++keyN;
            ++written;
        }
        if (paramIds)  paramIds[laneN]  = pid;
        if (keyCounts) keyCounts[laneN] = written;
        ++laneN;
    }
    return (int)laneN;
}

int Engine::snapshotMarkers(int deck, uint32_t maxMarkers,
                            int32_t* kinds, double* beats, int32_t* targetDecks,
                            double* targetBeats, char* labels) {
    std::lock_guard<std::mutex> lk(triggersMutex);
    uint32_t n = 0;
    for (const ScriptTrigger& t : scriptTriggers) {
        if (t.deck != deck) continue;
        if (n >= maxMarkers) break;
        if (kinds)       kinds[n]       = t.kind;
        if (beats)       beats[n]       = t.beat;
        if (targetDecks) targetDecks[n] = t.targetDeck;
        if (targetBeats) targetBeats[n] = t.targetBeat;
        if (labels) {
            char* dst = labels + (size_t)n * kMarkerLabelLen;
            std::strncpy(dst, t.label.c_str(), kMarkerLabelLen - 1);
            dst[kMarkerLabelLen - 1] = '\0';
        }
        ++n;
    }
    return (int)n;
}

void Engine::checkScriptTriggers() {
    for (auto& t : scriptTriggers) {
        if (t.deck < 0 || t.deck >= (int)decks.size()) continue;
        if (!decks[t.deck]->isPlaying()) continue;
        double beat = deckBeat(t.deck);
        if (std::isnan(beat)) continue;

        if (!t.fired && beat >= t.beat && beat < t.beat + 0.5) {
            t.fired = true;
            Logger::info("ScriptTrigger fired on deck " + std::to_string(t.deck) +
                         " @ beat " + std::to_string(t.beat) + ": " + t.command);
            if (t.fn) t.fn();           // structured effect (JSON path)
            else pushCommand(t.command); // legacy: re-enters the parser next tick
        } else if (t.fired && beat < t.beat - 1.0) {
            t.fired = false; // scrubbed back before the trigger; re-arm
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
