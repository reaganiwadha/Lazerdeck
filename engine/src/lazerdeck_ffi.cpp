#include "lazerdeck/lazerdeck.h"
#include "Engine.hpp"
#include "Deck.hpp"
#include <thread>
#include <future>
#include <string>
#include <cstring>

static Engine*      g_engine = nullptr;
static std::thread  g_engineThread;

int32_t lazerdeck_init(int32_t num_decks) {
    if (g_engine) return 0;

    g_engine = new Engine();

    std::promise<bool> initPromise;
    std::future<bool>  initFuture = initPromise.get_future();

    g_engineThread = std::thread([num_decks, &initPromise]() {
        bool ok = g_engine->init(num_decks);
        initPromise.set_value(ok);
        if (ok) g_engine->run();
    });

    bool ok = initFuture.get();
    if (!ok) {
        g_engineThread.join();
        delete g_engine;
        g_engine = nullptr;
        return 0;
    }
    return 1;
}

void lazerdeck_shutdown() {
    if (!g_engine) return;
    g_engine->stop();
    if (g_engineThread.joinable()) g_engineThread.join();
    delete g_engine;
    g_engine = nullptr;
}

int32_t lazerdeck_get_deck_count() {
    if (!g_engine) return 0;
    return g_engine->getNumDecks();
}

int32_t lazerdeck_get_sample_rate() {
    if (!g_engine) return 0;
    return g_engine->getSampleRate();
}

int32_t lazerdeck_get_deck_state(int32_t deck_idx, LazerDeckState* out) {
    if (!g_engine || !out) return 0;
    Deck* deck = g_engine->getDeck(deck_idx);
    if (!deck) return 0;

    out->bpm           = deck->getBPM();
    out->beat_offset   = deck->getBeatOffset();
    out->speed         = (float)deck->getSpeed();
    out->is_playing    = deck->isPlaying()    ? 1 : 0;
    out->is_loading    = deck->isLoading()    ? 1 : 0;
    out->is_analyzing  = deck->isAnalyzing()  ? 1 : 0;
    out->loop_active   = deck->isLoopActive() ? 1 : 0;
    out->current_frame = deck->getCurrentFrame();
    out->loop_start    = deck->getLoopStart();
    out->loop_end      = deck->getLoopEnd();
    out->sync_active   = deck->isSyncActive() ? 1 : 0;
    out->sync_source   = deck->getSyncSource();
    out->sample_rate   = g_engine->getSampleRate();

    std::string fp = deck->getCurrentFilepath();
    std::strncpy(out->filepath, fp.c_str(), 511);
    out->filepath[511] = '\0';

    return 1;
}

int32_t lazerdeck_load_file(int32_t deck_idx, const char* path) {
    if (!g_engine || !path) return 0;
    if (deck_idx < 0 || deck_idx >= g_engine->getNumDecks()) return 0;
    // Route through the command channel so loading reuses the engine's
    // analysis DB and runs on the engine thread. Decks are 1-based in script.
    g_engine->pushCommand("$d" + std::to_string(deck_idx + 1) + " load " + path);
    return 1;
}

int32_t lazerdeck_play(int32_t deck_idx) {
    if (!g_engine) return 0;
    Deck* deck = g_engine->getDeck(deck_idx);
    if (!deck) return 0;
    deck->play();
    return 1;
}

int32_t lazerdeck_pause(int32_t deck_idx) {
    if (!g_engine) return 0;
    Deck* deck = g_engine->getDeck(deck_idx);
    if (!deck) return 0;
    deck->pause();
    return 1;
}

void lazerdeck_push_command(const char* cmd) {
    if (!g_engine || !cmd) return;
    g_engine->pushCommand(std::string(cmd));
}
