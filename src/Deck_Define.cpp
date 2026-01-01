#include "Deck.hpp"
#include "Mixer.hpp"
#include "Engine.hpp" 
// We need access to Mixer from Deck? No, Deck doesn't own VSTs anymore. MixerChannel does.
// The Definition logic for VSTs needs to happen in MixerChannel or be orchestrated by Engine.
// The prompt implies d('a') manages everything.
// Deck manages Playback/Triggers. MixerChannel manages VSTs.
// We need to forward definition state to MixerChannel.

void Deck::beginDefinition() {
    std::lock_guard<std::mutex> lock(definitionMutex);
    isDefining = true;
    definedTriggerIDs.clear();
    // VST tracking needs to be handled via Engine -> Mixer
}

void Deck::markTriggerDefined(int id) {
    std::lock_guard<std::mutex> lock(definitionMutex);
    if (isDefining) {
        definedTriggerIDs.push_back(id);
    }
}

void Deck::endDefinition() {
    std::lock_guard<std::mutex> defLock(definitionMutex);
    if (!isDefining) return;

    // Prune Triggers
    {
        std::lock_guard<std::mutex> trigLock(triggerMutex);
        auto it = triggers.begin();
        while (it != triggers.end()) {
            bool kept = false;
            for (int id : definedTriggerIDs) {
                if (it->id == id) {
                    kept = true;
                    break;
                }
            }
            if (!kept) {
                it = triggers.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    isDefining = false;
}
