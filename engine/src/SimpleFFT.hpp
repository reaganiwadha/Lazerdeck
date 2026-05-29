#pragma once
#include <vector>
#include <cmath>
#include <utility>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Minimal, dependency-free FFT. Replaces FFTW (whose CMake build is unreliable
// on MSVC) for the engine's spectral analysis. Not the fastest, but the analysis
// runs off the audio thread and the sizes are small (2048).
namespace lzr {

// In-place iterative radix-2 Cooley-Tukey FFT. `n` (== re.size() == im.size())
// must be a power of two.
inline void fftRadix2(std::vector<float>& re, std::vector<float>& im) {
    const int n = (int)re.size();
    if (n < 2) return;

    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / (double)len;
        const float wRe = (float)std::cos(ang);
        const float wIm = (float)std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float curRe = 1.0f, curIm = 0.0f;
            const int half = len >> 1;
            for (int k = 0; k < half; ++k) {
                const float aRe = re[i + k];
                const float aIm = im[i + k];
                const float bRe = re[i + k + half] * curRe - im[i + k + half] * curIm;
                const float bIm = re[i + k + half] * curIm + im[i + k + half] * curRe;
                re[i + k] = aRe + bRe;
                im[i + k] = aIm + bIm;
                re[i + k + half] = aRe - bRe;
                im[i + k + half] = aIm - bIm;
                const float nRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = nRe;
            }
        }
    }
}

} // namespace lzr
