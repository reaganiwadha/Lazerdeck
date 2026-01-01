# Building Lazerdeck

Lazerdeck supports Windows, Linux, and macOS.
It also supports a VST3 plugin host, which can be disabled for platforms that don't need it or for easier building.

## Build Options

- `ENABLE_VST3` (Default: ON): Enable VST3 hosting support. Requires VST3 SDK (fetched automatically).

## Linux

### Dependencies
Install the required development packages:

**Ubuntu / Debian:**
```bash
sudo apt-get install cmake build-essential pkg-config \
    libsdl2-dev libsdl2-ttf-dev libportaudio2 portaudio19-dev \
    libfftw3-dev libsndfile1-dev
```

**Fedora:**
```bash
sudo dnf install cmake gcc-c++ make pkgconf-pkg-config \
    SDL2-devel SDL2_ttf-devel portaudio-devel fftw-devel libsndfile-devel
```

### Build Instructions
```bash
# Build with VST3 support (default)
cmake -B build
cmake --build build

# Build WITHOUT VST3 support (recommended for initial setup or minimal build)
cmake -B build_novst -DENABLE_VST3=OFF
cmake --build build_novst
```

## Windows

### Dependencies
- Visual Studio 2022 (or newer) with C++ Desktop Development workload.
- CMake (bundled with VS or standalone).
- Dependencies (SDL2, etc.) are best managed via `vcpkg`.

### Build Instructions
1. Open the folder in Visual Studio.
2. Let CMake configure the project.
3. Select your target (e.g., `lazerdeck.exe`).
4. Build.

Alternatively, via command line with `vcpkg` toolchain:
```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

## macOS

### Dependencies
Install dependencies using Homebrew:
```bash
brew install sdl2 sdl2_ttf portaudio fftw
```

### Build Instructions
```bash
cmake -B build
cmake --build build
```
