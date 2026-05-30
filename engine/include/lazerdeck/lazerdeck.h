#pragma once
#include <stdint.h>

/*
 * lazerdeck.h - public C FFI for the headless Lazerdeck engine.
 *
 * Designed to be consumed from Dart (dart:ffi / ffigen) or any C ABI host.
 * All functions are safe to call after lazerdeck_init() returns 1, and the
 * engine runs on its own thread.
 */

#ifdef _WIN32
  #ifdef LAZERDECK_BUILD_DLL
    #define LAZERDECK_API __declspec(dllexport)
  #else
    #define LAZERDECK_API __declspec(dllimport)
  #endif
#else
  #define LAZERDECK_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    bpm;
    float    beat_offset;
    float    speed;
    int32_t  is_playing;
    int32_t  is_loading;
    int32_t  is_analyzing;
    int32_t  loop_active;
    uint64_t current_frame;
    uint64_t loop_start;
    uint64_t loop_end;
    uint64_t recall_start;
    uint64_t recall_end;
    int32_t  sync_active;
    int32_t  sync_source;
    int32_t  sample_rate;   /* engine sample rate; current_frame / sample_rate = seconds */
    int32_t  metronome_enabled;
    float    eq_low;        /* current channel EQ/volume knob positions (0..1), */
    float    eq_mid;        /* so the UI reflects script/automation changes.     */
    float    eq_high;
    float    volume;
    char     filepath[512];
} LazerDeckState;

/* One selectable audio output device. */
typedef struct {
    int32_t index;                /* PortAudio device index (pass to set_audio_device) */
    int32_t is_default;           /* 1 if this is the host API's default output */
    int32_t is_current;           /* 1 if the engine is currently using it */
    int32_t max_output_channels;
    int32_t default_sample_rate;
    char    name[256];
    char    host_api[64];
} LazerAudioDevice;

/* The engine's live audio output configuration. */
typedef struct {
    int32_t device_index;
    int32_t sample_rate;
    int32_t buffer_frames;
    int32_t bit_depth;
    int32_t latency_ms;
    char    device_name[256];
    char    host_api[64];
} LazerAudioConfig;

/* Lifecycle */
LAZERDECK_API int32_t lazerdeck_init(int32_t num_decks);
LAZERDECK_API void    lazerdeck_shutdown();

/* Introspection */
LAZERDECK_API int32_t lazerdeck_get_deck_count();
LAZERDECK_API int32_t lazerdeck_get_sample_rate();
LAZERDECK_API int32_t lazerdeck_get_deck_state(int32_t deck_idx, LazerDeckState* out);

/* Typed transport controls (return 1 on success, 0 on failure). */
LAZERDECK_API int32_t lazerdeck_load_file(int32_t deck_idx, const char* path);
LAZERDECK_API int32_t lazerdeck_play(int32_t deck_idx);
LAZERDECK_API int32_t lazerdeck_pause(int32_t deck_idx);

/* Raw LazerScript command channel (e.g. "$d1 seek 30"). */
LAZERDECK_API void    lazerdeck_push_command(const char* cmd);

/*
 * Audio device configuration.
 *
 * get_audio_device_count() re-enumerates output devices and caches them;
 * get_audio_device(i, ...) then reads cached entry i (call count first). The
 * `index` field of LazerAudioDevice is what set_audio_device() expects.
 *
 * set_audio_device() is asynchronous: it queues a safe stream reopen onto the
 * engine thread (decks/mixer stay loaded) and returns immediately. Poll
 * get_audio_config() to observe the new device/sample rate once applied.
 */
LAZERDECK_API int32_t lazerdeck_get_audio_device_count();
LAZERDECK_API int32_t lazerdeck_get_audio_device(int32_t list_index, LazerAudioDevice* out);
LAZERDECK_API int32_t lazerdeck_get_audio_config(LazerAudioConfig* out);
LAZERDECK_API int32_t lazerdeck_set_audio_device(int32_t device_index);

/*
 * Waveform summary (precomputed peak/color "mips" for UI rendering).
 *
 * The engine builds these once while a track streams in; the host never needs
 * to rescan the raw audio. Typical use: poll get_wave_info each frame; when
 * bin_count changes, copy the bins into a host-owned buffer once and index it.
 */

/* Writes the current bin count and frames-per-bin for `deck_idx`.
 * Either out-param may be null. Returns 1 on success, 0 on failure. */
LAZERDECK_API int32_t lazerdeck_get_wave_info(int32_t deck_idx,
                                              uint64_t* out_bin_count,
                                              uint32_t* out_bin_frames);

/* Copies up to `count` bins starting at `start` into caller buffers.
 * out_minmax receives 4 floats per bin: [mn, mx, rms, transient (0-1)].
 * out_rgba receives one 0xAARRGGBB value per bin.
 * Either buffer may be null. Returns bins copied. */
LAZERDECK_API int32_t lazerdeck_copy_wave_bins(int32_t deck_idx,
                                               uint64_t start, uint32_t count,
                                               float* out_minmax,
                                               uint32_t* out_rgba);

/*
 * Automation lanes (LazerScript `onBeat` keyframe envelopes) for a deck, so the
 * host can draw them on top of the waveform.
 *
 * Writes up to `max_lanes` lane headers and up to `max_keys` keyframes total
 * (each lane's keyframes are concatenated in lane order). Returns the number of
 * lanes written.
 *   out_param_ids[i]  : 0=eq_low 1=eq_mid 2=eq_high 3=volume  (lane i)
 *   out_key_counts[i] : keyframes belonging to lane i
 *   out_beats[]       : keyframe beats (absolute, from the grid origin)
 *   out_values[]      : keyframe values (raw engine units, 0..1; 0.5 = unity)
 * Any out buffer may be null. The snapshot is taken atomically under lock.
 */
LAZERDECK_API int32_t lazerdeck_get_lanes(int32_t deck_idx,
                                          uint32_t max_lanes, uint32_t max_keys,
                                          int32_t* out_param_ids,
                                          uint32_t* out_key_counts,
                                          double* out_beats, float* out_values);

/*
 * Markers (discrete beat events — the script triggers fired by a deck's
 * playhead, e.g. `play when …`), so the host can draw them on the waveform.
 *
 * Writes up to `max_markers` entries into the parallel out-arrays; out_labels
 * is a flat buffer of max_markers * LAZERDECK_MARKER_LABEL_LEN bytes, one
 * NUL-terminated UTF-8 label per slot. Returns the number of markers written.
 * Any out buffer may be null. The snapshot is taken atomically under lock.
 *   out_kinds[i]        : 0=generic 1=play 2=jump  (see MarkerKind)
 *   out_beats[i]        : beat on THIS deck's timeline where the marker fires
 *   out_target_decks[i] : deck the action affects (0-based); -1 if none
 *   out_target_beats[i] : jump destination beat; < 0 if not applicable
 */
#define LAZERDECK_MARKER_LABEL_LEN 48
LAZERDECK_API int32_t lazerdeck_get_markers(int32_t deck_idx, uint32_t max_markers,
                                            int32_t* out_kinds, double* out_beats,
                                            int32_t* out_target_decks,
                                            double* out_target_beats,
                                            char* out_labels);

#ifdef __cplusplus
}
#endif
