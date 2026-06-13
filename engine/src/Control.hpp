#pragma once

#include <string>
#include <vector>
#include <optional>

// ============================================================================
// JSON control-plane wire schema.
//
// This is the structured replacement for the legacy line-based LazerScript text
// (see Engine::processCommands). External REPL-style clients (Python/Bun/Haskell)
// compile one HTTP `POST /action` body per evaluated line; ControlServer parses
// it with Glaze into a ControlRequest and hands it to Engine::submitActions().
//
// Member names ARE the JSON keys (Glaze pure reflection). Keep them in sync with
// the documented schema. Only `action` is mandatory; every other field is
// optional and interpreted per-verb (see Engine::dispatchAction).
// ============================================================================

// One verb targeting (usually) one deck. A flat list rather than the deck-keyed
// sketch `{"d1":[…]}` because cross-deck verbs (align) don't belong under a
// single deck key — each action names its own deck.
struct ControlAction {
    std::string action;                 // load|eject|play|pause|stop|seek|speed|
                                        // speed_reset|playjump|bpm|offset|
                                        // nudge_offset|reanalyze|metronome|sync|
                                        // master|eq_low|eq_mid|eq_high|volume|align
    std::string deck;                   // "d1".. (subject deck for single-deck ops)
    std::optional<double> onBeat;       // schedule on this deck's beat (1-based)

    // Generic args (presence depends on `action`).
    std::optional<std::string> path;    // load
    std::optional<double> beat;         // playjump (1-based)
    std::optional<double> value;        // seek(seconds)/eq_*/volume/bpm/offset/nudge_offset
    std::optional<double> speed;        // speed (multiplier; 1.0 = unity)
    std::optional<bool>   on;           // sync/metronome toggle

    // align: jump `deck` so its `subjectBeat` coincides in time with
    // `reference`'s `referenceBeat`.
    std::optional<std::string> reference;      // "d2"
    std::optional<double>      subjectBeat;    // 1-based beat on subject (`deck`)
    std::optional<double>      referenceBeat;  // 1-based beat on `reference`
};

struct ControlRequest {
    std::vector<ControlAction> actions;
};

// Synchronous reply: reports parse/validation outcome only. Action execution is
// async (marshalled onto the engine thread), so runtime effects aren't awaited.
struct ControlResponse {
    bool ok = true;
    std::string error;
};

struct DeckState {
    std::string deck;                   // "d1", "d2"...
    double bpm = 0.0;
    double beatOffset = 0.0;
    double speed = 1.0;
    bool isPlaying = false;
    bool isLoading = false;
    bool isAnalyzing = false;
    bool loopActive = false;
    uint64_t currentFrame = 0;
    uint64_t loopStart = 0;
    uint64_t loopEnd = 0;
    uint64_t recallStart = 0;
    uint64_t recallEnd = 0;
    bool syncActive = false;
    int32_t syncSource = -1;
    int32_t sampleRate = 44100;
    bool metronomeEnabled = false;
    double eqLow = 0.5;
    double eqMid = 0.5;
    double eqHigh = 0.5;
    double volume = 1.0;
    std::string filepath;
};

struct EngineStateResponse {
    bool ok = true;
    std::vector<DeckState> decks;
};

