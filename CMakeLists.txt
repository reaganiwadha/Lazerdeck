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

# oscpack
FetchContent_Declare(
    oscpack
    GIT_REPOSITORY https://github.com/RossBencina/oscpack.git
    GIT_TAG master
)

# VST3 SDK
FetchContent_Declare(
    vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG v3.7.11_build_10
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

# Work around old CMake in dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_GetProperties(oscpack)
if(NOT oscpack_POPULATED)
    FetchContent_Populate(oscpack)
endif()

FetchContent_GetProperties(vst3sdk)
if(NOT vst3sdk_POPULATED)
    FetchContent_Populate(vst3sdk)
endif()

if (NOT WIN32)
    # Work around SoundTouch old CMake on Linux
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
set(OSCPACK_SRCS
    "${oscpack_SOURCE_DIR}/osc/OscTypes.cpp"
    "${oscpack_SOURCE_DIR}/osc/OscReceivedElements.cpp"
    "${oscpack_SOURCE_DIR}/osc/OscPrintReceivedElements.cpp"
    "${oscpack_SOURCE_DIR}/osc/OscOutboundPacketStream.cpp"
    "${oscpack_SOURCE_DIR}/ip/IpEndpointName.cpp"
)

if (WIN32)
    list(APPEND OSCPACK_SRCS
        "${oscpack_SOURCE_DIR}/ip/win32/NetworkingUtils.cpp"
        "${oscpack_SOURCE_DIR}/ip/win32/UdpSocket.cpp"
    )
else()
    list(APPEND OSCPACK_SRCS
        "${oscpack_SOURCE_DIR}/ip/posix/NetworkingUtils.cpp"
        "${oscpack_SOURCE_DIR}/ip/posix/UdpSocket.cpp"
    )
endif()

set(SRCS
    src/main.cpp
    src/Deck.cpp
    src/Renderer.cpp
    src/Engine.cpp
    src/AudioEngine.cpp
    src/OSCHandler.cpp
    src/Engine_OSC.cpp
    src/VST3Host.cpp
    ${OSCPACK_SRCS}
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module.cpp"
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp"
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp"
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/parameterchanges.cpp"
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp"
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/utility/stringconvert.cpp"
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstinitiids.cpp"
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/common/commoniids.cpp"
    "${vst3sdk_SOURCE_DIR}/pluginterfaces/base/funknown.cpp"
    "${vst3sdk_SOURCE_DIR}/pluginterfaces/base/ustring.cpp"
    "${vst3sdk_SOURCE_DIR}/pluginterfaces/base/conststringtable.cpp"
    "${vst3sdk_SOURCE_DIR}/pluginterfaces/base/coreiids.cpp"
    "${vst3sdk_SOURCE_DIR}/base/source/fstring.cpp"
    "${vst3sdk_SOURCE_DIR}/base/source/fobject.cpp"
    "${vst3sdk_SOURCE_DIR}/base/source/fdebug.cpp"
    "${vst3sdk_SOURCE_DIR}/base/source/baseiids.cpp"
    "${vst3sdk_SOURCE_DIR}/base/source/updatehandler.cpp"
    "${vst3sdk_SOURCE_DIR}/base/source/fstreamer.cpp"
    "${vst3sdk_SOURCE_DIR}/base/source/fbuffer.cpp"
    "${vst3sdk_SOURCE_DIR}/base/thread/source/fcondition.cpp"
    "${vst3sdk_SOURCE_DIR}/base/thread/source/flock.cpp"
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
    "${oscpack_SOURCE_DIR}"
    "${vst3sdk_SOURCE_DIR}"
    "${vst3sdk_SOURCE_DIR}/public.sdk/.."
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
        _CRT_SECURE_NO_WARNINGS
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
        ws2_32
        iphlpapi
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
