#pragma once
#include "lazerdeck/vst3_abi.h"

namespace Lazerdeck {

// Loads the optional lazerdeck_vst3 shared library at runtime and exposes its
// C ABI. When the library is missing/incompatible, available() stays false and
// api() returns nullptr; callers must treat that as "VST unsupported".
class Vst3Runtime {
public:
    static bool load();              // idempotent; returns available()
    static void unload();
    static bool available();
    static const LzrVst3Api* api();  // nullptr when unavailable
};

} // namespace Lazerdeck
