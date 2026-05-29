#pragma once
#include <string>
#include <vector>
#include <variant>

struct TriggerAction {
    int targetDeckIdx; // 0 for A, 1 for B...
    std::string command; // "play", "pause", "jump", "sync"
    std::vector<float> args;
};

struct DeckTrigger {
    int id;
    float beat;
    bool fired;
    std::vector<TriggerAction> actions;
};
