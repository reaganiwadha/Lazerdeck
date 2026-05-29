# Directory structure

```
lazerdeck/
├── CMakeLists.txt              # thin aggregator (engine [+ plugins/vst3])
├── CMakePresets.json           # windows-msvc / default (Linux); no vcpkg, no Qt
├── docs/                       # this planning material
├── engine/                     # the headless C++ engine
│   ├── CMakeLists.txt          # SINGLE source of truth; FetchContent all deps
│   ├── include/lazerdeck/
│   │   ├── lazerdeck.h         # public C FFI (the dart:ffi contract)
│   │   └── vst3_abi.h          # C ABI for the optional VST3 plugin
│   ├── src/                    # engine implementation (see below)
│   └── third_party/            # miniaudio.h, dl_wav.h (vendored, header-only)
├── plugins/
│   └── vst3/                   # optional, dynamically-loaded VST3 host
│       ├── CMakeLists.txt      # the ONLY place the VST3 SDK is compiled
│       └── src/
│           ├── VST3Host.cpp/.hpp     # SDK-backed hosting
│           └── vst3_abi_impl.cpp     # exports lzr_vst3_get_api
├── app/                        # Flutter desktop host
│   ├── pubspec.yaml            # ffi, file_picker
│   ├── lib/
│   │   ├── main.dart
│   │   ├── ffi/lazerdeck_bindings.dart   # dart:ffi symbol lookups + struct
│   │   ├── ffi/engine.dart               # high-level wrapper
│   │   └── ui/decks_view.dart            # deck cards (timecode, play/pause, open)
│   ├── windows/CMakeLists.txt  # add_subdirectory(engine) + bundle DLL
│   └── linux/CMakeLists.txt    # add_subdirectory(engine) + bundle .so
└── legacy/qt-sdl-ui/           # retired SDL Renderer + Qt ScriptEditor (out of build)
```

## engine/src/ inventory

| File | Role |
|------|------|
| `Engine.{hpp,cpp}` | Orchestrator; headless `run()` service loop; command parsing |
| `Engine_OSC.cpp` | OSC/command handlers (`load`, `play`, `sync`, `vst/*`, …) |
| `Engine_Sync.cpp` | Deck tempo/phase sync |
| `Deck.{hpp,cpp}` | One playback unit: decode, play/pause, seek, stretch, BPM, FFT |
| `Deck_Define.cpp` | Declarative define/end trigger pruning |
| `AudioEngine.{hpp,cpp}` | PortAudio stream + callback + metronome |
| `Mixer.{hpp,cpp}` | Per-channel volume + VST chain via the runtime ABI |
| `OSCHandler.{hpp,cpp}` | UDP :9000 OSC listener |
| `Vst3Runtime.{hpp,cpp}` | dlopen/LoadLibrary of `lazerdeck_vst3`; exposes `LzrVst3Api*` |
| `lazerdeck_ffi.cpp` | Implements `include/lazerdeck/lazerdeck.h` |
| `Clock.hpp` | `lzr::nowMs()` — headless replacement for `SDL_GetTicks64` |
| `SimpleFFT.hpp` | Vendored radix-2 FFT (replaces FFTW) |
| `audio.hpp`, `fft.hpp`, `Trigger.hpp`, `AnalysisDB.hpp`, `ThreadSafeQueue.hpp`, `Logger.hpp` | Support headers |

## Conventions

- Public engine API lives only in `engine/include/lazerdeck/`. Everything in
  `engine/src/` is private implementation.
- New native deps are added via FetchContent in `engine/CMakeLists.txt` only.
- The VST3 SDK must never be referenced outside `plugins/vst3/`.
- Retired code goes to `legacy/` (kept out of every build) rather than deleted.
