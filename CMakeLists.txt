cmake_minimum_required(VERSION 3.15)

project(Lazerdeck
    LANGUAGES C CXX
)

# --------------------------------------------------
# Language standards
# --------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------------
# Options
# --------------------------------------------------
option(USE_ZENITY "Use Zenity for NFD on Linux" ON)

# --------------------------------------------------
# Dependencies
# --------------------------------------------------
if (WIN32)
    # vcpkg / Windows (CONFIG mode only)
    find_package(SDL2 CONFIG REQUIRED)
    find_package(SDL2_ttf CONFIG REQUIRED)
    find_package(portaudio CONFIG REQUIRED)
    find_package(FFTW3 CONFIG REQUIRED)
    find_package(FFTW3f CONFIG REQUIRED)
    find_package(SoundTouch CONFIG REQUIRED)
else()
    # Linux / Unix
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
    pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
    pkg_check_modules(FFTW3 REQUIRED fftw3)
    pkg_check_modules(FFTW3F REQUIRED fftw3f)
endif()

# --------------------------------------------------
# FetchContent (Linux-only SoundTouch + RubberBand)
# --------------------------------------------------
include(FetchContent)

# RubberBand (always FetchContent)
FetchContent_Declare(
    rubberband
    GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
    GIT_TAG v3.3.0
)

# SoundTouch ONLY on non-Windows
if (NOT WIN32)
    FetchContent_Declare(
        soundtouch
        GIT_REPOSITORY https://codeberg.org/soundtouch/soundtouch.git
        GIT_TAG 2.3.3
    )
endif()

FetchContent_MakeAvailable(rubberband)

if (NOT WIN32)
    # Work around SoundTouch old CMake on Linux
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(soundtouch)
endif()

# --------------------------------------------------
# RubberBand (manual target)
# --------------------------------------------------
add_library(rubberband_lib STATIC
    "${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp"
)

target_include_directories(rubberband_lib PUBLIC
    "${rubberband_SOURCE_DIR}"
)

if (WIN32)
    target_compile_definitions(rubberband_lib PRIVATE NOMINMAX)
endif()

# --------------------------------------------------
# Sources
# --------------------------------------------------
set(SRCS
    src/main.cpp
    src/Deck.cpp
    src/Renderer.cpp
    src/Engine.cpp
    src/AudioEngine.cpp
)

if (WIN32 AND NOT MSVC)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/nfd_win.cpp")
        list(APPEND SRCS src/nfd_win.cpp)
    elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/nfd_win.c")
        list(APPEND SRCS src/nfd_win.c)
    endif()
elseif (APPLE)
    # list(APPEND SRCS src/nfd_cocoa.m)
endif()

# --------------------------------------------------
# Executable
# --------------------------------------------------
add_executable(lazerdeck ${SRCS})

target_include_directories(lazerdeck PRIVATE
    src
    include
)

# --------------------------------------------------
# Compile definitions
# --------------------------------------------------
if (WIN32)
    target_compile_definitions(lazerdeck PRIVATE 
        NOMINMAX 
        _USE_MATH_DEFINES
        SOUNDTOUCH_ALLOW_SSE=1
        SOUNDTOUCH_USE_NEON=0
        __SSE__=0
    )
endif()

# --------------------------------------------------
# Link libraries
# --------------------------------------------------
if (WIN32)
    target_link_libraries(lazerdeck PRIVATE
        SDL2::SDL2
        SDL2::SDL2main
        SDL2_ttf::SDL2_ttf
        portaudio
        FFTW3::fftw3
        FFTW3::fftw3f
        SoundTouch::SoundTouch
        rubberband_lib
        shlwapi
        ole32
        comctl32
        uuid
    )
endif()

# --------------------------------------------------
# Resources
# --------------------------------------------------
add_custom_command(TARGET lazerdeck POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:lazerdeck>/resources"
    COMMENT "Copying resources to output directory"
)

# --------------------------------------------------
# Compiler warnings
# --------------------------------------------------
if (MSVC)
    target_compile_options(lazerdeck PRIVATE /permissive)
else()
    target_compile_options(lazerdeck PRIVATE -Wall)
endif()
