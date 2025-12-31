cmake_minimum_required(VERSION 3.10)
project(Lazerdeck)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(USE_ZENITY "Use Zenity for NFD on Linux" ON)

# Find Packages
if(WIN32)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_ttf REQUIRED)
    # On Windows, we often use vcpkg or manual paths for PortAudio and FFTW
    if(NOT PortAudio_FOUND)
        find_package(PortAudio REQUIRED)
    endif()
    if(NOT FFTW3_FOUND)
        find_package(FFTW3 REQUIRED)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
    pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
    pkg_check_modules(FFTW3 REQUIRED fftw3)
    pkg_check_modules(FFTW3F REQUIRED fftw3f)
endif()

# Include FetchContent
include(FetchContent)

# Fetch RubberBand
FetchContent_Declare(
    rubberband
    GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
    GIT_TAG v3.3.0
)

# Fetch SoundTouch
FetchContent_Declare(
    soundtouch
    GIT_REPOSITORY https://codeberg.org/soundtouch/soundtouch.git
    GIT_TAG 2.3.3
)

FetchContent_MakeAvailable(rubberband soundtouch)

# RubberBand (manually define since it lacks CMake)
add_library(rubberband_lib STATIC "${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp")
target_include_directories(rubberband_lib PUBLIC "${rubberband_SOURCE_DIR}")

# Include Directories for other deps
include_directories(
    src
    include
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
    ${FFTW3_INCLUDE_DIRS}
    ${FFTW3F_INCLUDE_DIRS}
)

# Source Files
set(SRCS
    src/main.cpp
    src/Deck.cpp
    src/Renderer.cpp
    src/Engine.cpp
    src/AudioEngine.cpp
    src/nfd_common.c
)

if(WIN32)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/nfd_win.cpp")
        list(APPEND SRCS src/nfd_win.cpp)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/nfd_win.c")
        list(APPEND SRCS src/nfd_win.c)
    endif()
elseif(APPLE)
    # list(APPEND SRCS src/nfd_cocoa.m)
else()
    if(USE_ZENITY)
        list(APPEND SRCS src/nfd_zenity.c)
    else()
        list(APPEND SRCS src/nfd_gtk.c)
    endif()
endif()

add_executable(lazerdeck ${SRCS})

# Definitions
target_compile_definitions(lazerdeck PRIVATE SOUNDTOUCH_DISABLE_X86_OPTIMIZATIONS)

# Libraries
if(WIN32)
    target_link_libraries(lazerdeck
        ${SDL2_LIBRARIES}
        ${SDL2_TTF_LIBRARIES}
        ${PORTAUDIO_LIBRARIES}
        ${FFTW3_LIBRARIES}
        ${FFTW3F_LIBRARIES}
        rubberband_lib
        SoundTouch
        shlwapi
        ole32
        comctl32
        uuid
    )
else()
    target_link_libraries(lazerdeck
        ${SDL2_LIBRARIES}
        ${SDL2_TTF_LIBRARIES}
        ${PORTAUDIO_LIBRARIES}
        ${FFTW3_LIBRARIES}
        ${FFTW3F_LIBRARIES}
        rubberband_lib
        SoundTouch
        dl
        pthread
        m
    )
endif()

# For MSVC specific flags
if(MSVC)
    target_compile_options(lazerdeck PRIVATE /W4)
else()
    target_compile_options(lazerdeck PRIVATE -Wall)
endif()
