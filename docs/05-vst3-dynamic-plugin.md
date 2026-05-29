# VST3 as a dynamically-loaded plugin

VST3 hosting is **optional** and lives entirely in `plugins/vst3/`
(`lazerdeck_vst3`). The headless engine never links the Steinberg SDK. At
startup it tries to load the plugin library; if absent, all VST features become
silent no-ops and audio passes through.

## The boundary: `engine/include/lazerdeck/vst3_abi.h`

A C vtable struct the plugin fills in:

```c
typedef struct LzrVst3Api {
    uint32_t abi_version;                       // == LZR_VST3_ABI_VERSION
    void* (*create)(const char* path, int sample_rate, int block_size);
    void  (*destroy)(void* instance);
    void  (*process)(void* instance, float* const* inputs,
                                     float* const* outputs, int frames);
    void  (*set_parameter)(void* instance, int param_index, float value);
    void  (*show_editor)(void* instance);
    const char* (*get_path)(void* instance);
} LzrVst3Api;

// The single symbol the engine resolves:
int lzr_vst3_get_api(LzrVst3Api* out);          // returns 1 on success
```

`instance` handles are opaque (`Lazerdeck::VST3Instance*` on the plugin side).

## Engine side: `Vst3Runtime` (`engine/src/Vst3Runtime.{hpp,cpp}`)

- `Vst3Runtime::load()` — `LoadLibrary("lazerdeck_vst3.dll")` /
  `dlopen("liblazerdeck_vst3.so")`, resolves `lzr_vst3_get_api`, validates
  `abi_version`. Called once from `Engine::init`.
- `Vst3Runtime::api()` — returns `const LzrVst3Api*` or `nullptr`.
- `MixerChannel` calls through `Vst3Runtime::api()`; when it's null, `loadVST` /
  `usingVST` / `process` etc. are no-ops. No SDK headers in the engine.

## Plugin side: `plugins/vst3/`

- `VST3Host.{cpp,hpp}` — the SDK-backed hosting (moved verbatim from the old
  engine). Owns module loading, `IComponent`/`IAudioProcessor`/`IEditController`,
  the editor window.
- `vst3_abi_impl.cpp` — exports `lzr_vst3_get_api`, mapping the ABI calls onto
  `VST3Host` / `VST3Instance`. Handles Windows COM init (`CoInitializeEx`) per
  thread inside `create`.
- `CMakeLists.txt` — FetchContent's `vst3sdk` (v3.7.11_build_10) and compiles the
  ~24 SDK sources. Built only when `-DLAZERDECK_BUILD_VST3=ON`.

## Building it (not built for the PoC)

```
cmake --preset windows-msvc -DLAZERDECK_BUILD_VST3=ON
cmake --build build-windows --config Debug --target lazerdeck_vst3
```

Then place `lazerdeck_vst3.dll` next to the host executable (or the engine DLL)
so `Vst3Runtime::load()` finds it. With it present, `Engine::init` logs
"VST support enabled".

## Notes / TODO

- `get_path` currently returns `nullptr`; the engine tracks paths itself
  (`MixerChannel::VstSlot::path`), so it's unused. Implement if a future consumer
  needs it.
- Sample-rate changes don't re-init live instances yet (see `MixerChannel::setSampleRate`).
- The ABI is versioned (`LZR_VST3_ABI_VERSION`); bump it on any struct change so
  an old plugin against a new engine is rejected cleanly.
