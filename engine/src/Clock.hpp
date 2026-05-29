#pragma once
#include <cstdint>
#include <chrono>

// Headless replacement for SDL_GetTicks64(): milliseconds since first call.
namespace lzr {
inline uint64_t nowMs() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now() - start).count();
}
}
