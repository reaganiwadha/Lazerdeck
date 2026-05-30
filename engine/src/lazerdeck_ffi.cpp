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
    out->recall_start  = deck->getRecallStart();
    out->recall_end    = deck->getRecallEnd();
    out->sync_active   = deck->isSyncActive() ? 1 : 0;
    out->sync_source   = deck->getSyncSource();
    out->sample_rate   = g_engine->getSampleRate();
    out->metronome_enabled = deck->isMetronomeEnabled() ? 1 : 0;

    // Current channel EQ/volume so the UI knobs reflect script/automation.
    auto* mixer = g_engine->getMixer();
    auto* ch = mixer ? mixer->getChannel(deck_idx) : nullptr;
    out->eq_low  = ch ? ch->getEqLow()  : 0.5f;
    out->eq_mid  = ch ? ch->getEqMid()  : 0.5f;
    out->eq_high = ch ? ch->getEqHigh() : 0.5f;
    out->volume  = ch ? ch->getVolume() : 1.0f;

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

int32_t lazerdeck_get_audio_device_count() {
    if (!g_engine) return 0;
    return g_engine->getAudioDeviceCount();
}

int32_t lazerdeck_get_audio_device(int32_t list_index, LazerAudioDevice* out) {
    if (!g_engine || !out) return 0;
    AudioDeviceInfo info;
    if (!g_engine->getAudioDevice(list_index, info)) return 0;

    out->index               = info.index;
    out->is_default          = info.isDefault ? 1 : 0;
    out->is_current          = (info.index == g_engine->audio().getCurrentDevice()) ? 1 : 0;
    out->max_output_channels = info.maxOutputChannels;
    out->default_sample_rate = (int32_t)info.defaultSampleRate;
    std::strncpy(out->name, info.name.c_str(), 255);     out->name[255] = '\0';
    std::strncpy(out->host_api, info.hostApi.c_str(), 63); out->host_api[63] = '\0';
    return 1;
}

int32_t lazerdeck_get_audio_config(LazerAudioConfig* out) {
    if (!g_engine || !out) return 0;
    AudioEngine& a = g_engine->audio();

    out->device_index  = a.getCurrentDevice();
    out->sample_rate   = a.getActualSampleRate();
    out->buffer_frames = a.getBufferSize();
    out->bit_depth     = a.getBitDepth();
    out->latency_ms    = a.getLatencyMs();

    std::string name = a.getCurrentDeviceName();
    std::string host = a.getCurrentHostApi();
    std::strncpy(out->device_name, name.c_str(), 255); out->device_name[255] = '\0';
    std::strncpy(out->host_api, host.c_str(), 63);     out->host_api[63] = '\0';
    return 1;
}

int32_t lazerdeck_set_audio_device(int32_t device_index) {
    if (!g_engine) return 0;
    g_engine->setAudioDevice(device_index);
    return 1;
}

int32_t lazerdeck_get_wave_info(int32_t deck_idx, uint64_t* out_bin_count,
                                uint32_t* out_bin_frames) {
    if (!g_engine) return 0;
    Deck* deck = g_engine->getDeck(deck_idx);
    if (!deck) return 0;
    if (out_bin_count)  *out_bin_count  = deck->getWaveBinCount();
    if (out_bin_frames) *out_bin_frames = deck->getWaveBinFrames();
    return 1;
}

int32_t lazerdeck_copy_wave_bins(int32_t deck_idx, uint64_t start, uint32_t count,
                                 float* out_minmax, uint32_t* out_rgba) {
    if (!g_engine) return 0;
    Deck* deck = g_engine->getDeck(deck_idx);
    if (!deck) return 0;
    return (int32_t)deck->copyWaveBins(start, count, out_minmax, out_rgba);
}

int32_t lazerdeck_get_lanes(int32_t deck_idx, uint32_t max_lanes, uint32_t max_keys,
                            int32_t* out_param_ids, uint32_t* out_key_counts,
                            double* out_beats, float* out_values) {
    if (!g_engine) return 0;
    return g_engine->snapshotLanes(deck_idx, max_lanes, max_keys,
                                   out_param_ids, out_key_counts,
                                   out_beats, out_values);
}

int32_t lazerdeck_get_markers(int32_t deck_idx, uint32_t max_markers,
                              int32_t* out_kinds, double* out_beats,
                              int32_t* out_target_decks, double* out_target_beats,
                              char* out_labels) {
    if (!g_engine) return 0;
    return g_engine->snapshotMarkers(deck_idx, max_markers, out_kinds, out_beats,
                                     out_target_decks, out_target_beats, out_labels);
}
