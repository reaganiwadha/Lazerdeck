#include "Engine.hpp"
#include "Logger.hpp"
#include <cmath>
#include <algorithm>

// Pioneer-style beat sync. A deck with sync enabled (syncActive) follows a
// "source" deck (syncSource): it continuously matches the source's effective
// tempo and, while both are playing, is nudged into beat-phase alignment.
//
// This is a star topology, not a chain: each follower locks directly to the
// deck named as its source, so there is no accumulated drift. Runs on the
// engine service thread (~5 ms cadence) via Engine::run().
//
// Separate, intentionally-unhandled concerns (no-ops for now): a global MASTER
// election on pause/unload, QUANTIZE of play/cue launches, and MASTER TEMPO
// (key lock) — the existing setSpeed() path resamples, so pitch tracks tempo.

namespace {

// Beat phase in [0,1): fractional beat position at the deck's playhead. The beat
// grid is defined in source frames (bpm + beatOffset), so this is invariant to
// playback rate and equals the audible phase regardless of stretch/resample.
double deckBeatPhase(const Deck* d, int sampleRate) {
    float bpm = d->getBPM();
    if (bpm <= 0.0f) return 0.0;
    double framesPerBeat = (double)sampleRate * 60.0 / (double)bpm;
    double pos = (double)d->getCurrentFrame() - (double)d->getBeatOffset();
    double beats = pos / framesPerBeat;
    return beats - std::floor(beats);
}

// Pick a sync rate (1x, 2x, or 1/2x) so the matched tempo lands closest to the
// follower's own native tempo — keeps half/double-time tracks musically aligned
// without forcing an extreme playback speed.
double chooseSyncRate(double sourceEffBpm, double followerNativeBpm) {
    if (followerNativeBpm <= 0.0 || sourceEffBpm <= 0.0) return 1.0;
    const double candidates[] = {1.0, 2.0, 0.5};
    double best = 1.0;
    double bestDist = 1e9;
    for (double c : candidates) {
        double ratio = (sourceEffBpm * c) / followerNativeBpm; // playback speed needed
        double dist = std::abs(std::log(ratio));               // distance from speed 1.0
        if (dist < bestDist) { bestDist = dist; best = c; }
    }
    return best;
}

constexpr double kHardSnapBeats = 0.25; // > 1/4 beat off → seek to realign
constexpr double kPhaseDeadband = 0.004; // within this, hold pure tempo
constexpr double kPhaseGain     = 0.08;  // P-gain for transparent drift correction
constexpr double kMinSpeed      = 0.25;
constexpr double kMaxSpeed      = 4.0;

} // namespace

void Engine::updateSync() {
    for (size_t i = 0; i < decks.size(); ++i) {
        Deck* follower = decks[i].get();
        if (!follower->isSyncActive()) continue;

        int src = follower->getSyncSource();
        if (src < 0 || src >= (int)decks.size() || src == (int)i) continue;

        Deck* source = decks[src].get();

        float fBpm = follower->getBPM();
        float sBpm = source->getBPM();
        if (fBpm <= 0.0f || sBpm <= 0.0f) continue; // both decks need a beat grid

        // --- Tempo lock: match the source's effective (slider-adjusted) BPM ---
        double sourceEffBpm = (double)sBpm * source->getSpeed();
        double rate = chooseSyncRate(sourceEffBpm, (double)fBpm);
        double targetSpeed = (sourceEffBpm * rate) / (double)fBpm;

        // --- Phase lock: only while both are playing; paused holds tempo only ---
        if (follower->isPlaying() && source->isPlaying()) {
            double diff = deckBeatPhase(source, sampleRate) -
                          deckBeatPhase(follower, sampleRate);
            while (diff > 0.5) diff -= 1.0;
            while (diff < -0.5) diff += 1.0; // nearest-beat error in [-0.5, 0.5]

            if (std::abs(diff) > kHardSnapBeats) {
                // Far off-grid (e.g. sync just engaged): snap into alignment.
                double framesPerBeat = (double)sampleRate * 60.0 / (double)fBpm;
                follower->seek((int64_t)std::llround(diff * framesPerBeat));
            } else if (std::abs(diff) > kPhaseDeadband) {
                // Close: ride the speed slightly so it drifts in transparently.
                targetSpeed *= (1.0 + diff * kPhaseGain);
            }
        }

        targetSpeed = std::clamp(targetSpeed, kMinSpeed, kMaxSpeed);
        follower->setSpeed(targetSpeed);
    }
}
