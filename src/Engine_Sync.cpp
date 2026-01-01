#include "Engine.hpp"
#include "Deck.hpp"
#include "Logger.hpp"
#include <cmath>

void Engine::updateSync() {
    for (size_t i = 0; i < decks.size(); ++i) {
        Deck* follower = decks[i].get();
        if (follower->isSyncActive()) {
            int sourceIdx = follower->getSyncSource();
            if (sourceIdx >= 0 && sourceIdx < (int)decks.size() && (size_t)sourceIdx != i) {
                Deck* master = decks[sourceIdx].get();
                
                // Continuous BPM Sync with Phase Correction
                float masterBpm = master->getBPM();
                float masterSpeed = (float)master->getSpeed();
                float myBpm = follower->getBPM();
                
                if (masterBpm > 0 && myBpm > 0 && master->isPlaying() && follower->isPlaying()) {
                    float effectiveMasterBpm = masterBpm * masterSpeed;
                    float baseRequiredSpeed = effectiveMasterBpm / myBpm;
                    
                    // Phase Calculation
                    // Calculate "Total Beats" elapsed from start (or offset)
                    uint64_t mFrame = master->getCurrentFrame();
                    float mOffset = master->getBeatOffset();
                    double mFramesPerBeat = (double)sampleRate * 60.0 / (double)masterBpm;
                    double mBeatTotal = ((double)mFrame - mOffset) / mFramesPerBeat;
                    double mPhase = mBeatTotal - floor(mBeatTotal);
                    
                    uint64_t myFrame = follower->getCurrentFrame();
                    float myOffset = follower->getBeatOffset();
                    double myFramesPerBeat = (double)sampleRate * 60.0 / (double)myBpm;
                    double myBeatTotal = ((double)myFrame - myOffset) / myFramesPerBeat;
                    double myPhase = myBeatTotal - floor(myBeatTotal);
                    
                    // Calculate shortest phase difference (-0.5 to 0.5)
                    double diff = mPhase - myPhase;
                    if (diff > 0.5) diff -= 1.0;
                    if (diff < -0.5) diff += 1.0;
                    
                    // P-Controller for Nudge
                    // Kp determines how aggressively we correct phase error.
                    // Too high = jitter/pitch wobble. Too low = drift.
                    // 0.05 means we try to close 5% of the gap per frame update? 
                    // No, we are setting speed multiplier.
                    // If diff is +0.1 beat, we are behind. We need to speed up.
                    // If we add 0.1 to speed, we catch up fast.
                    const double Kp = 0.5; 
                    
                    double finalSpeed = baseRequiredSpeed + (diff * Kp);
                    
                    // Sanity limits to prevent extreme pitch shifts
                    if (finalSpeed < 0.1) finalSpeed = 0.1;
                    if (finalSpeed > 4.0) finalSpeed = 4.0;
                    
                    // Smooth update: RubberBand handles small variations well if we don't spam it too hard?
                    // Actually, let's update.
                    follower->setSpeed(finalSpeed);
                } else if (masterBpm > 0 && myBpm > 0) {
                     // If paused, just match tempo without phase correction
                    float effectiveMasterBpm = masterBpm * masterSpeed;
                    float requiredSpeed = effectiveMasterBpm / myBpm;
                    follower->setSpeed(requiredSpeed);
                }
            } else {
                // Invalid source, disable sync
                follower->setSync(false);
            }
        }
    }
}
