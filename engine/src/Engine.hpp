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
#include <cstdint>
#include <string>
#include <map>
#include <osc/OscReceivedElements.h>
#include "ThreadSafeQueue.hpp"
#include "Control.hpp"

class OSCHandler;
class ControlServer;

// --- Beat keyframing (LazerScript automation) ---
// One automation keyframe: a value at an absolute beat (from the grid origin).
struct Keyframe { double beat; float value; };

// An automation lane: at most one per (deck, canonical param). Keys are kept
// sorted by beat; evaluated piecewise-linear with the endpoints flat-held
// (Ableton clip-envelope style).
struct ParamLane {
    int deck;
    std::string param;            // canonical: eq_low|eq_mid|eq_high|volume
    std::vector<Keyframe> keys;
};

// Kind of timeline marker, for the host overlay's styling/labelling. Append
// new kinds at the end (the host maps these by index) — don't renumber.
enum MarkerKind {
    MARKER_GENERIC = 0,  // `when hit … do (cmd)` — opaque command
    MARKER_PLAY    = 1,  // `play when …`         — start another deck
    MARKER_JUMP    = 2,  // `playjump … when …`   — jump another deck to a beat
};

// Max bytes (incl. NUL) of a marker's display label in the FFI snapshot.
static constexpr int kMarkerLabelLen = 48;

// A general beat trigger: when `deck` reaches `beat`, the engine runs `command`
// (any LazerScript line). One-shot, re-armed when the playhead scrubs back.
//
// The trailing fields are display metadata so the host can draw the trigger as
// a marker on the waveform (see snapshotMarkers); they don't affect firing.
struct ScriptTrigger {
    int deck;             // source deck whose playhead beat fires this
    double beat;          // 0-based source beat (grid origin = 0)
    std::string command;  // LazerScript line run when it fires (legacy text path)
    std::function<void()> fn; // structured effect (JSON path); preferred over command
    bool fired = false;
    int kind = MARKER_GENERIC;
    int targetDeck = -1;  // deck the action affects (0-based); -1 = none/self
    double targetBeat = -1.0; // jump destination (0-based); < 0 = not applicable
    std::string label;    // short human-readable marker label
};

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

    // Structured JSON control plane (see Control.hpp / ControlServer). Thread-safe:
    // each action is marshalled onto the engine thread (taskQueue) and run by
    // dispatchAction. Called from the ControlServer's HTTP thread.
    void submitActions(const ControlRequest& req);

    // --- HTTP/JSON control server (host-controllable; see ControlServer) ---
    // The engine tries to bind the default port at startup; a busy port is
    // non-fatal (the server stays stopped). The host can start/stop it and pick a
    // different port at runtime via these (all safe from the FFI thread).
    bool startControlServer(int port); // true iff the bind succeeded
    void stopControlServer();
    bool isControlServerRunning() const;
    int  getControlServerPort() const; // last requested/bound port

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

    // --- Beat-sync master election ---
    // The "master" is the tempo reference every synced deck follows. It is
    // elected automatically (see updateMaster): it always tracks a *playing*
    // deck, so pausing/unloading the master promotes it to the deck still
    // playing. Per-deck sync *intent* (setSyncWanted) is preserved across these
    // role swaps; the master itself never follows. setMasterDeck is a manual
    // override that holds only while that deck keeps playing.
    int  getMasterDeck() const { return masterDeck.load(); }
    void setMasterDeck(int deck);
    void setSyncWanted(int deck, bool wanted);

    // --- Timeline overlay introspection (for the host's waveform overlay) ---
    // The deck timeline carries two families of annotations, both polled per
    // frame and both safe to read from the host (UI) thread while the engine
    // runs (each guarded by its own mutex):
    //   * Automation lanes  — continuous, per-param keyframe envelopes.
    //   * Markers            — discrete beat events (script triggers).

    // Snapshot of a deck's automation lanes. Writes up to maxLanes lane headers
    // and up to maxKeys keyframes total (concatenated in lane order); returns
    // the number of lanes written. paramId: 0=eq_low 1=eq_mid 2=eq_high
    // 3=volume. keyCounts[i] is how many of beats[]/values[] belong to lane i.
    int snapshotLanes(int deck, uint32_t maxLanes, uint32_t maxKeys,
                      int32_t* paramIds, uint32_t* keyCounts,
                      double* beats, float* values);

    // Snapshot of a deck's markers (the script triggers fired by its playhead).
    // Writes up to maxMarkers entries into the parallel out-arrays; `labels` is
    // a flat buffer of maxMarkers * kMarkerLabelLen bytes (one NUL-terminated
    // UTF-8 label per slot). Returns the number of markers written. Any output
    // array may be null. See MarkerKind for `kinds`.
    int snapshotMarkers(int deck, uint32_t maxMarkers,
                        int32_t* kinds, double* beats, int32_t* targetDecks,
                        double* targetBeats, char* labels);

    // --- Audio device management (for the host's settings UI) ---
    AudioEngine& audio() { return audioEngine; }
    int  getAudioDeviceCount();                         // refreshes the cache
    bool getAudioDevice(int listIndex, AudioDeviceInfo& out);
    // Queues a device switch onto the engine thread; reopens the output stream
    // on `deviceIndex` without tearing down decks/mixer.
    void setAudioDevice(int deviceIndex);
    // Queues a sample-rate change onto the engine thread: reopens the output at
    // `rate` on the current device, retargets decks/mixer, and re-decodes loaded
    // tracks at the new rate (resuming position + play state; loops/cues reset).
    void setSampleRate(int rate);

private:
    void processCommands();   // drains the LazerScript command queue
    void processTasks();
    void updateSync();
    // Elects the master deck (promoting to a playing deck when the current
    // master stops) and reconciles each deck's effective sync state from its
    // intent (syncWanted) + the master. Called every service tick.
    void updateMaster();
    void checkTriggers();
    void executeAction(const TriggerAction& action);

    // --- Beat keyframing runtime ---
    // Routes a canonical param (eq_low|eq_mid|eq_high|volume) on a deck to its
    // mixer setter. Shared by immediate commands and applyAutomation().
    bool setDeckParam(int deck, const std::string& canon, float value);
    // Removes a param's automation lane (returns it to manual control).
    void clearLane(int deck, const std::string& canon);
    // Wipes all lanes and script triggers for a deck (on track reload).
    void clearDeckScript(int deck);
    // Registers a script trigger (thread-safe; see triggersMutex).
    void addScriptTrigger(ScriptTrigger t);
    // Per-tick: drive automated params from their envelopes.
    void applyAutomation();
    // Per-tick: fire general beat triggers (one-shot, re-armed on scrub-back).
    void checkScriptTriggers();
    // Current absolute beat for a deck, or NaN if no usable tempo.
    double deckBeat(int deck) const;

    // --- Structured verb helpers (engine thread) ---
    // One method per verb, shared by the legacy text parser (processCommands)
    // and the JSON dispatcher (dispatchAction). Decks are 0-based here; beats are
    // 1-based (matching the UI grid / script convention).
    void actLoad(int deck, const std::string& path);
    void actPlay(int deck);
    void actPause(int deck);
    void actStop(int deck);
    void actSeekSeconds(int deck, double seconds);
    void actSpeed(int deck, double mult);
    void actPlayjump(int deck, double beat);
    void actBpm(int deck, double bpm);
    void actOffset(int deck, double frames);
    void actNudgeOffset(int deck, double delta);
    void actReanalyze(int deck);
    void actMetronome(int deck, bool on);
    void actSync(int deck, bool on);
    void actMaster(int deck);
    // Jumps `subj` so its (1-based) `subjBeat` coincides in time with `ref`'s
    // (1-based) `refBeat`. One-shot phase jump; assumes the decks are tempo-locked
    // (see actSync) to stay aligned afterwards.
    void actAlign(int subj, double subjBeat, int ref, double refBeat);

    // 1-based script beat -> absolute frame on a deck's grid. false if no tempo.
    bool frameForBeat(int deck, double scriptBeat, uint64_t& outFrame);

    // Runs one parsed JSON action on the engine thread: resolves the deck, then
    // either schedules it (action.onBeat set -> ScriptTrigger) or runs it now.
    void dispatchAction(const ControlAction& a);

    void shutdown();
    void scanVSTs();          // SDK-free filesystem scan of installed .vst3 bundles

    std::atomic<bool> running;
    AudioEngine audioEngine;
    AnalysisDB analysisDB;
    std::unique_ptr<Lazerdeck::Mixer> mixer;
    std::unique_ptr<OSCHandler> oscHandler;
    std::unique_ptr<ControlServer> controlServer;

    std::vector<std::unique_ptr<Deck>> decks;

    int sampleRate;

    // Beat-sync master election state (engine thread only, except masterDeck
    // which the host reads via getMasterDeck()). syncWanted[i] is deck i's sync
    // intent; the elected master never follows regardless of its own intent.
    std::atomic<int> masterDeck{0};
    std::vector<bool> syncWanted;

    std::vector<std::function<void()>> taskQueue;
    std::mutex taskMutex;

    std::vector<std::string> vstPaths;
    std::mutex vstMutex;
    std::thread vstScanThread;

    std::string workingDirectory;
    ThreadSafeQueue<std::string> commandQueue;

    // Automation lanes keyed by "<deck>:<canonParam>"; one lane per param.
    // Mutated only on the engine thread; guarded by `lanesMutex` so the host's
    // snapshotLanes() can read them safely from the UI thread.
    std::map<std::string, ParamLane> lanes;
    mutable std::mutex lanesMutex;
    // General beat triggers (`$dN when hit B do (cmd)`, `play when …`). Mutated
    // only on the engine thread; `triggersMutex` guards structural changes
    // (add/clear) against the host's snapshotMarkers().
    std::vector<ScriptTrigger> scriptTriggers;
    mutable std::mutex triggersMutex;
};
