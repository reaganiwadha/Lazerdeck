# Build & tooling

Everything is FetchContent-based. **No vcpkg, no Qt, no SDL, no system audio
libs.** First configure clones/builds the deps (a few minutes); afterwards it's
incremental.

## Prerequisites

- **Windows:** Visual Studio 2022 (MSVC + CMake), Git. Flutter 3.44+ with the
  Windows desktop toolchain (`flutter config --enable-windows-desktop`).
- **Linux:** GCC/Clang, CMake ≥ 3.20, Git, GTK3 dev (`libgtk-3-dev`), ALSA
  (`libasound2-dev`) for PortAudio, plus Flutter's Linux desktop deps.
- Network access on first configure (FetchContent clones).

## Build & run the app (the normal path)

```
cd app
flutter run -d windows      # or: -d linux
```

This builds the engine (via `add_subdirectory`), bundles the shared library next
to the runner, compiles Dart, and launches.

## Build the engine standalone (CI / smoke test)

```
cmake --preset windows-msvc           # or: cmake --preset default   (Linux)
cmake --build build-windows --config Debug --target lazerdeck_engine
```

Produces `build-windows/engine/Debug/lazerdeck_engine.dll`. Verify exports:

```
dumpbin /exports build-windows/engine/Debug/lazerdeck_engine.dll
```

## Build the optional VST3 plugin

```
cmake --preset windows-msvc -DLAZERDECK_BUILD_VST3=ON
cmake --build build-windows --config Debug --target lazerdeck_vst3
```

## Dependencies (all via FetchContent in `engine/CMakeLists.txt`)

| Dep | Version | How |
|-----|---------|-----|
| RubberBand | v3.3.0 | compiled from `single/RubberBandSingle.cpp` (static) |
| oscpack | master | sources compiled in (platform IP layer) |
| PortAudio | v19.7.0 | static (`PA_BUILD_STATIC=ON`); links `portaudio_static` |
| SoundTouch | 2.3.3 | static, float samples |
| miniaudio / dr_wav | vendored | `engine/third_party/` (header-only) |
| FFT | vendored | `engine/src/SimpleFFT.hpp` (see below) |
| VST3 SDK | v3.7.11_build_10 | only in `plugins/vst3/`, only when `LAZERDECK_BUILD_VST3=ON` |

### Why no FFTW

FFTW's CMake builds unreliably on MSVC (it produced an `fftw3f.dll` missing the
scalar codelet solver tables `fftwf_solvtab_*`, breaking the link). Since the
engine only needs a small real FFT for offline spectral analysis, we vendor a
compact radix-2 FFT in `SimpleFFT.hpp` and dropped FFTW entirely. This keeps the
"one self-contained CMake, painless tooling" promise.

## Gotchas / lessons

- **`BUILD_SHARED_LIBS` must be a CACHE entry.** With
  `CMAKE_POLICY_VERSION_MINIMUM 3.5` (needed for old deps), dependencies run
  under old `CMP0077`, so `option()` ignores plain variables. We set
  `set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)`.
- **PortAudio target name varies**; the CMake picks `portaudio_static` →
  `PortAudio::portaudio` → `portaudio` in that order.
- **SoundTouch headers** are `<BPMDetect.h>` from the FetchContent source tree
  (not vcpkg's `soundtouch/BPMDetect.h`).
- A stale `build-windows/` from the old vcpkg/Qt setup will poison reconfigure —
  delete it if you switch branches.

## Housekeeping

- `pluginterfaces/`, `public.sdk/`, `vstgui4/` at the repo root are leftover,
  unpopulated git submodules from the old in-tree VST3 SDK. They are unused now
  (the SDK comes via FetchContent). They can be `git submodule deinit`'d /
  removed; left in place to avoid destructive submodule surgery in this pass.
