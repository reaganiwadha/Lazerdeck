# ============================================================================
# lazerdeck_vst3 - the OPTIONAL, dynamically-loaded VST3 host plugin.
#
# This is the only place the Steinberg VST3 SDK is compiled. The headless
# engine never links it; at runtime it LoadLibrary/dlopen's the resulting
# shared library and resolves lzr_vst3_get_api (see engine/include/lazerdeck/
# vst3_abi.h). Built only when LAZERDECK_BUILD_VST3=ON.
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(lazerdeck_vst3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)

add_compile_definitions(RELEASE=1)

FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG        v3.7.11_build_10
    UPDATE_DISCONNECTED TRUE)
FetchContent_GetProperties(vst3sdk)
if(NOT vst3sdk_POPULATED)
    FetchContent_Populate(vst3sdk)
endif()

set(VST3_SDK_SRCS
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module.cpp"
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp"
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/parameterchanges.cpp"
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp"
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
    "${vst3sdk_SOURCE_DIR}/base/thread/source/flock.cpp")

if (WIN32)
    list(APPEND VST3_SDK_SRCS "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp")
else()
    list(APPEND VST3_SDK_SRCS "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_linux.cpp")
endif()

add_library(lazerdeck_vst3 SHARED
    src/VST3Host.cpp
    src/vst3_abi_impl.cpp
    ${VST3_SDK_SRCS})

target_include_directories(lazerdeck_vst3 PRIVATE
    src
    "${CMAKE_CURRENT_SOURCE_DIR}/../../engine/include"   # lazerdeck/vst3_abi.h
    "${vst3sdk_SOURCE_DIR}")

target_compile_definitions(lazerdeck_vst3 PRIVATE LAZERDECK_VST3_BUILD)

if (WIN32)
    target_compile_definitions(lazerdeck_vst3 PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(lazerdeck_vst3 PRIVATE ole32 shlwapi comctl32 uuid)
else()
    target_link_libraries(lazerdeck_vst3 PRIVATE dl)
endif()

if (MSVC)
    target_compile_options(lazerdeck_vst3 PRIVATE /permissive- /bigobj)
endif()
