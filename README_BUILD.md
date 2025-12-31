# Building Lazerdeck

## Linux
```bash
cmake -B build
cmake --build build
```

## Cross-compiling for MSVC (Windows)
To cross-compile for MSVC from Linux, you need `clang-cl` and a Windows sysroot (headers and libraries).
You can use `xwin` to fetch the necessary Windows SDK and MSVC components.

```bash
# Example using a toolchain file or presets
cmake --preset msvc-cross -DCMAKE_SYSROOT=/path/to/win-sysroot
cmake --build build-msvc
```

## Dependencies
- SDL2
- SDL2_ttf
- PortAudio
- FFTW3
- RubberBand (fetched automatically by CMake)
- SoundTouch (fetched automatically by CMake)
