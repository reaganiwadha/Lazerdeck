# ============================================================================
# Lazerdeck headless engine - self-contained.
#
# Builds the `lazerdeck_engine` SHARED library (the dart:ffi / C ABI surface).
# All native dependencies are pulled via FetchContent: NO vcpkg, NO system
# packages, NO Qt, NO SDL. This file can be add_subdirectory()'d directly from
# the Flutter desktop runner's CMake with zero toolchain injection.
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(lazerdeck_engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Build third-party deps as static so they fold into the engine shared lib.
# Must be a CACHE entry: with CMAKE_POLICY_VERSION_MINIMUM below, deps run with
# old CMP0077 behaviour where option() ignores plain variables.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# Several deps still declare ancient cmake_minimum_required.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Suppress install() rules for the engine and every FetchContent dep below.
# PortAudio/SoundTouch emit install() rules that break when this tree is
# add_subdirectory()'d into the Flutter desktop build (Flutter sets a
# generator-expression CMAKE_INSTALL_PREFIX that third-party install scripts
# can't expand). CMAKE_SKIP_INSTALL_RULES is effectively global, so instead we
# override install() to a no-op *within this subtree*: the macro persists but is
# gated by LZR_SUPPRESS_INSTALL, which is only set in this scope — the parent
# (Flutter) scope leaves it unset and keeps its own install rules.
set(LZR_SUPPRESS_INSTALL TRUE)
macro(install)
    if(NOT LZR_SUPPRESS_INSTALL)
        _install(${ARGV})
    endif()
endmacro()

include(FetchContent)

# ---------------------------------------------------------------------------
# RubberBand (compiled from its amalgamated single source, no extra deps)
# ---------------------------------------------------------------------------
FetchContent_Declare(rubberband
    GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
    GIT_TAG        v3.3.0
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE)
FetchContent_GetProperties(rubberband)
if(NOT rubberband_POPULATED)
    FetchContent_Populate(rubberband)
endif()

add_library(rubberband_lib STATIC "${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp")
target_include_directories(rubberband_lib PUBLIC "${rubberband_SOURCE_DIR}")
if (WIN32)
    target_compile_definitions(rubberband_lib PRIVATE NOMINMAX)
endif()

# ---------------------------------------------------------------------------
# oscpack (compiled from source; pick platform IP layer)
# ---------------------------------------------------------------------------
FetchContent_Declare(oscpack
    GIT_REPOSITORY https://github.com/RossBencina/oscpack.git
    GIT_TAG        master
    UPDATE_DISCONNECTED TRUE)
FetchContent_GetProperties(oscpack)
if(NOT oscpack_POPULATED)
    FetchContent_Populate(oscpack)
endif()

set(OSCPACK_SRCS
    "${oscpack_SOURCE_DIR}/osc/OscTypes.cpp"
    "${oscpack_SOURCE_DIR}/osc/OscReceivedElements.cpp"
    "${oscpack_SOURCE_DIR}/osc/OscPrintReceivedElements.cpp"
    "${oscpack_SOURCE_DIR}/osc/OscOutboundPacketStream.cpp"
    "${oscpack_SOURCE_DIR}/ip/IpEndpointName.cpp")
if (WIN32)
    list(APPEND OSCPACK_SRCS
        "${oscpack_SOURCE_DIR}/ip/win32/NetworkingUtils.cpp"
        "${oscpack_SOURCE_DIR}/ip/win32/UdpSocket.cpp")
else()
    list(APPEND OSCPACK_SRCS
        "${oscpack_SOURCE_DIR}/ip/posix/NetworkingUtils.cpp"
        "${oscpack_SOURCE_DIR}/ip/posix/UdpSocket.cpp")
endif()

# ---------------------------------------------------------------------------
# PortAudio (static)
# ---------------------------------------------------------------------------
set(PA_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
set(PA_BUILD_STATIC   ON  CACHE BOOL "" FORCE)
set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(PA_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(portaudio
    GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
    GIT_TAG        v19.7.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(portaudio)
if (TARGET portaudio_static)
    set(PA_LIB portaudio_static)
elseif (TARGET PortAudio::portaudio)
    set(PA_LIB PortAudio::portaudio)
else()
    set(PA_LIB portaudio)
endif()

# ---------------------------------------------------------------------------
# SoundTouch (static, float samples)
# ---------------------------------------------------------------------------
set(INTEGER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SOUNDSTRETCH    OFF CACHE BOOL "" FORCE)
set(SOUNDTOUCH_DLL  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(soundtouch
    GIT_REPOSITORY https://codeberg.org/soundtouch/soundtouch.git
    GIT_TAG        2.3.3)
FetchContent_MakeAvailable(soundtouch)

# Spectral analysis uses the vendored src/SimpleFFT.hpp (no FFTW dependency).
#
# BPM + beat-phase detection uses vendored BTrack (GPL-3, third_party/btrack)
# built against vendored KissFFT (third_party/kissfft) via -DUSE_KISS_FFT. The
# upstream libsamplerate dependency was replaced with an inline linear resampler
# and the 44100 Hz tempo constants were made sample-rate aware (see BTrack.cpp).

# ---------------------------------------------------------------------------
# The engine library
# ---------------------------------------------------------------------------
set(ENGINE_SRCS
    src/Engine.cpp
    src/Engine_OSC.cpp
    src/Engine_Sync.cpp
    src/Deck.cpp
    src/Deck_Define.cpp
    src/AudioEngine.cpp
    src/Mixer.cpp
    src/OSCHandler.cpp
    src/Vst3Runtime.cpp
    src/lazerdeck_ffi.cpp
    third_party/btrack/BTrack.cpp
    third_party/btrack/OnsetDetectionFunction.cpp
    third_party/kissfft/kiss_fft.c
    ${OSCPACK_SRCS})

add_library(lazerdeck_engine SHARED ${ENGINE_SRCS})

target_include_directories(lazerdeck_engine PRIVATE
    src
    third_party
    third_party/btrack
    third_party/kissfft
    "${oscpack_SOURCE_DIR}"
    "${soundtouch_SOURCE_DIR}/include")
target_include_directories(lazerdeck_engine PUBLIC include)  # lazerdeck/lazerdeck.h

# BTrack/OnsetDetectionFunction select their FFT backend at compile time.
target_compile_definitions(lazerdeck_engine PRIVATE USE_KISS_FFT)

target_link_libraries(lazerdeck_engine PRIVATE
    rubberband_lib
    ${PA_LIB}
    SoundTouch)

target_compile_definitions(lazerdeck_engine PRIVATE LAZERDECK_BUILD_DLL)

if (WIN32)
    target_compile_definitions(lazerdeck_engine PRIVATE
        NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(lazerdeck_engine PRIVATE
        ws2_32 iphlpapi winmm ole32 shlwapi uuid)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(lazerdeck_engine PRIVATE Threads::Threads dl m)
endif()

if (MSVC)
    target_compile_options(lazerdeck_engine PRIVATE /permissive- /bigobj)
else()
    target_compile_options(lazerdeck_engine PRIVATE -Wall)
endif()

# Make the public header path discoverable to dependents (e.g. ffigen consumers).
set(LAZERDECK_ENGINE_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/lazerdeck/lazerdeck.h"
    CACHE FILEPATH "Path to the public Lazerdeck FFI header" FORCE)
