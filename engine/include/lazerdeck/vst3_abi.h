#pragma once
/*
 * vst3_abi.h - C ABI contract between the headless Lazerdeck engine and the
 * optional, dynamically-loaded VST3 host plugin (lazerdeck_vst3.dll / .so).
 *
 * The engine NEVER links the VST3 SDK. At startup it tries to load the plugin
 * library and resolve a single exported entrypoint:
 *
 *     int lzr_vst3_get_api(LzrVst3Api* out);   // returns 1 on success
 *
 * If the library is absent or the symbol/version does not match, the engine
 * runs with VST support disabled (all mixer VST calls become no-ops).
 *
 * All audio buffers are de-interleaved, per-channel float arrays (stereo).
 * Plugin instances are opaque void* handles owned by the plugin library.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LZR_VST3_ABI_VERSION 1u

typedef struct LzrVst3Api {
    uint32_t abi_version;   /* must equal LZR_VST3_ABI_VERSION */

    /* Create a plugin instance from a .vst3 path. Returns NULL on failure. */
    void* (*create)(const char* path, int sample_rate, int block_size);

    /* Destroy an instance previously returned by create(). */
    void  (*destroy)(void* instance);

    /* Process `frames` samples in place. inputs/outputs are float*[2]. */
    void  (*process)(void* instance, float* const* inputs, float* const* outputs, int frames);

    /* Set a normalized [0,1] parameter value by index. */
    void  (*set_parameter)(void* instance, int param_index, float value);

    /* Open the plugin's editor window (no-op if unsupported). */
    void  (*show_editor)(void* instance);

    /* Path the instance was created from (for de-dup); may return NULL. */
    const char* (*get_path)(void* instance);
} LzrVst3Api;

/* Implemented and exported by lazerdeck_vst3. Fills *out, returns 1 on success. */
#ifdef _WIN32
  #ifdef LAZERDECK_VST3_BUILD
    __declspec(dllexport) int lzr_vst3_get_api(LzrVst3Api* out);
  #endif
#else
  __attribute__((visibility("default"))) int lzr_vst3_get_api(LzrVst3Api* out);
#endif

typedef int (*lzr_vst3_get_api_fn)(LzrVst3Api* out);

#ifdef __cplusplus
}
#endif
