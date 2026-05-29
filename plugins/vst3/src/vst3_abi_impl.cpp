// vst3_abi_impl.cpp - exports the lazerdeck VST3 C ABI (see vst3_abi.h) by
// wrapping the SDK-backed Lazerdeck::VST3Host / VST3Instance. This translation
// unit is the ONLY boundary the headless engine sees; everything Steinberg
// stays inside lazerdeck_vst3.
#include "lazerdeck/vst3_abi.h"
#include "VST3Host.hpp"

#ifdef _WIN32
  #include <objbase.h>
#endif

using namespace Lazerdeck;

namespace {

#ifdef _WIN32
void ensureCom() {
    static thread_local bool inited = false;
    if (!inited) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        inited = true;
    }
}
#endif

void* abi_create(const char* path, int sample_rate, int block_size) {
#ifdef _WIN32
    ensureCom();
#endif
    auto inst = VST3Host::getInstance().createInstance(path ? path : "", sample_rate, block_size);
    return inst ? inst.release() : nullptr;
}

void abi_destroy(void* instance) {
    delete reinterpret_cast<VST3Instance*>(instance);
}

void abi_process(void* instance, float* const* inputs, float* const* outputs, int frames) {
    if (!instance) return;
    reinterpret_cast<VST3Instance*>(instance)->process(
        const_cast<float**>(inputs), const_cast<float**>(outputs), frames);
}

void abi_set_parameter(void* instance, int param_index, float value) {
    if (instance) reinterpret_cast<VST3Instance*>(instance)->setParameter(param_index, value);
}

void abi_show_editor(void* instance) {
    if (instance) reinterpret_cast<VST3Instance*>(instance)->showEditor();
}

const char* abi_get_path(void* /*instance*/) {
    // Engine tracks paths itself; not needed for the current ABI consumer.
    return nullptr;
}

} // namespace

extern "C" int lzr_vst3_get_api(LzrVst3Api* out) {
    if (!out) return 0;
    out->abi_version   = LZR_VST3_ABI_VERSION;
    out->create        = abi_create;
    out->destroy       = abi_destroy;
    out->process       = abi_process;
    out->set_parameter = abi_set_parameter;
    out->show_editor   = abi_show_editor;
    out->get_path      = abi_get_path;
    return 1;
}
