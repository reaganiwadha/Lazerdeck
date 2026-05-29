# Building Lazerdeck

> Lazerdeck was re-architected onto a **headless C++ engine + Flutter desktop
> host**. The old SDL2/Qt/vcpkg/FFTW build is gone. Full, current build and
> tooling instructions live in **[docs/07-build-and-tooling.md](docs/07-build-and-tooling.md)**;
> start with **[docs/00-overview.md](docs/00-overview.md)**.

## TL;DR

Run the app (builds the engine automatically, no vcpkg/Qt/SDL):

```bash
cd app
flutter run -d windows      # or: flutter run -d linux
```

Build the engine library standalone (CI / smoke test):

```bash
cmake --preset windows-msvc        # or: cmake --preset default   (Linux)
cmake --build build-windows --config Debug --target lazerdeck_engine
```

Build the optional VST3 host plugin (not needed for normal use):

```bash
cmake --preset windows-msvc -DLAZERDECK_BUILD_VST3=ON
cmake --build build-windows --config Debug --target lazerdeck_vst3
```

## Requirements

- **Windows:** Visual Studio 2022 (MSVC + CMake), Git, Flutter 3.44+ (Windows desktop).
- **Linux:** GCC/Clang, CMake ≥ 3.20, Git, `libgtk-3-dev`, `libasound2-dev`, Flutter (Linux desktop).
- Network on first configure (dependencies are fetched via CMake FetchContent).

All native dependencies (PortAudio, RubberBand, SoundTouch, oscpack) are fetched
and built automatically — no system packages, no vcpkg. See
[docs/07-build-and-tooling.md](docs/07-build-and-tooling.md) for details and the
FFTW/Flutter-install gotchas.
