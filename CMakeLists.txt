# ============================================================================
# Lazerdeck - top-level aggregator.
#
# This file exists so the engine (and optionally the VST3 plugin) can be built
# standalone for CI / smoke testing, without Flutter:
#
#     cmake --preset windows-msvc
#     cmake --build build-windows
#
# The real "source of truth" for the engine build is engine/CMakeLists.txt,
# which the Flutter desktop runner add_subdirectory()'s directly. The Flutter
# app does NOT use this top-level file.
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(Lazerdeck LANGUAGES C CXX)

option(LAZERDECK_BUILD_VST3 "Build the optional dynamically-loaded VST3 host plugin" OFF)

add_subdirectory(engine)

if (LAZERDECK_BUILD_VST3)
    add_subdirectory(plugins/vst3)
endif()
