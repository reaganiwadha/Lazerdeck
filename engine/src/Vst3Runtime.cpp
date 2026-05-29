#include "Vst3Runtime.hpp"
#include "Logger.hpp"

#ifdef _WIN32
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace Lazerdeck {

namespace {
    void*       g_handle = nullptr;
    LzrVst3Api  g_api{};
    bool        g_loaded = false;

#ifdef _WIN32
    const char* kLibNames[] = { "lazerdeck_vst3.dll", nullptr };
    void* openLib(const char* n)        { return (void*)LoadLibraryA(n); }
    void* sym(void* h, const char* s)   { return (void*)GetProcAddress((HMODULE)h, s); }
    void  closeLib(void* h)             { FreeLibrary((HMODULE)h); }
#else
    const char* kLibNames[] = { "liblazerdeck_vst3.so", "./liblazerdeck_vst3.so", nullptr };
    void* openLib(const char* n)        { return dlopen(n, RTLD_NOW | RTLD_LOCAL); }
    void* sym(void* h, const char* s)   { return dlsym(h, s); }
    void  closeLib(void* h)             { dlclose(h); }
#endif
}

bool Vst3Runtime::load() {
    if (g_loaded) return available();
    g_loaded = true; // attempt only once

    for (int i = 0; kLibNames[i] != nullptr; ++i) {
        g_handle = openLib(kLibNames[i]);
        if (g_handle) break;
    }
    if (!g_handle) {
        Logger::info("VST3: lazerdeck_vst3 not found - VST support disabled.");
        return false;
    }

    auto getApi = reinterpret_cast<lzr_vst3_get_api_fn>(sym(g_handle, "lzr_vst3_get_api"));
    if (!getApi || !getApi(&g_api) || g_api.abi_version != LZR_VST3_ABI_VERSION) {
        Logger::warn("VST3: lazerdeck_vst3 found but ABI mismatch - VST support disabled.");
        closeLib(g_handle);
        g_handle = nullptr;
        g_api = LzrVst3Api{};
        return false;
    }

    Logger::info("VST3: lazerdeck_vst3 loaded - VST support enabled.");
    return true;
}

void Vst3Runtime::unload() {
    if (g_handle) { closeLib(g_handle); g_handle = nullptr; }
    g_api = LzrVst3Api{};
    g_loaded = false;
}

bool Vst3Runtime::available() { return g_handle != nullptr && g_api.create != nullptr; }

const LzrVst3Api* Vst3Runtime::api() { return available() ? &g_api : nullptr; }

} // namespace Lazerdeck
