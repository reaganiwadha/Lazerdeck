#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <mutex>
#include "audio.hpp"
#include "SimpleFFT.hpp"

struct FrequencyBands {
    float low = 0.0f;    // Bass: 20-250 Hz
    float mid = 0.0f;    // Mids: 250-2000 Hz
    float high = 0.0f;   // Highs: 2000-20000 Hz
};

struct FFTEnergy {
    const AudioBuffer* buffer = nullptr;
    int fftSize = 2048;
    int hopSize = 512;  // Overlap for smoother results

    std::vector<FrequencyBands> energyCache;
    mutable std::mutex cacheMutex;

    // Frequency band indices
    int lowBandEnd = 0;
    int midBandEnd = 0;
    int highBandEnd = 0;

    FFTEnergy() = default;
    ~FFTEnergy() { cleanup(); }

    void cleanup() {
        buffer = nullptr;
    }

    void initialize(const AudioBuffer* audioBuffer) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        buffer = audioBuffer;

        // Calculate frequency band indices
        float freqPerBin = (float)buffer->sampleRate / fftSize;
        lowBandEnd = (int)(250.0f / freqPerBin);
        midBandEnd = (int)(2000.0f / freqPerBin);
        highBandEnd = (int)(20000.0f / freqPerBin);
        highBandEnd = (std::min)(highBandEnd, fftSize / 2);

        energyCache.clear();
    }

    // Analyze a specific range of frames and append to cache.
    // Assumes sequential appending.
    void analyzeChunk(uint64_t frameStart, uint64_t frameCount) {
        if (!buffer) return;

        uint64_t startWindow = frameStart / hopSize;
        uint64_t numWindows = frameCount / hopSize;

        std::vector<FrequencyBands> chunkBands;
        chunkBands.reserve(numWindows);

        // Hann window
        std::vector<float> window(fftSize);
        for (int i = 0; i < fftSize; i++) {
            window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (fftSize - 1)));
        }

        std::vector<float> re(fftSize), im(fftSize);

        for (uint64_t w = 0; w < numWindows; ++w) {
            uint64_t currentWindowIdx = startWindow + w;
            uint64_t windowStartFrame = currentWindowIdx * hopSize;

            if (windowStartFrame + fftSize > buffer->frameCount) break;

            for (int i = 0; i < fftSize; i++) {
                uint64_t f = windowStartFrame + i;
                re[i] = (f < buffer->frameCount) ? buffer->sample(f, 0) * window[i] : 0.0f;
                im[i] = 0.0f;
            }

            lzr::fftRadix2(re, im);

            FrequencyBands bands;

            // Low band (bass)
            for (int bin = 1; bin < lowBandEnd; bin++) {
                bands.low += sqrtf(re[bin] * re[bin] + im[bin] * im[bin]);
            }
            if (lowBandEnd > 1) bands.low /= (lowBandEnd - 1);

            // Mid band
            for (int bin = lowBandEnd; bin < midBandEnd; bin++) {
                bands.mid += sqrtf(re[bin] * re[bin] + im[bin] * im[bin]);
            }
            if (midBandEnd > lowBandEnd) bands.mid /= (midBandEnd - lowBandEnd);

            // High band
            for (int bin = midBandEnd; bin < highBandEnd; bin++) {
                bands.high += sqrtf(re[bin] * re[bin] + im[bin] * im[bin]);
            }
            if (highBandEnd > midBandEnd) bands.high /= (highBandEnd - midBandEnd);

            // Normalize energies (rough normalization)
            float maxEnergy = (std::max)({bands.low, bands.mid, bands.high});
            if (maxEnergy > 0.0f) {
                bands.low /= maxEnergy;
                bands.mid /= maxEnergy;
                bands.high /= maxEnergy;
            }

            chunkBands.push_back(bands);
        }

        std::lock_guard<std::mutex> lock(cacheMutex);
        if (energyCache.size() < startWindow + chunkBands.size()) {
            energyCache.resize(startWindow + chunkBands.size());
        }
        for (size_t i = 0; i < chunkBands.size(); ++i) {
            energyCache[startWindow + i] = chunkBands[i];
        }
    }

    void computeEnergy() {
        if (!buffer) return;
        analyzeChunk(0, buffer->frameCount);
    }

    FrequencyBands getEnergyAtFrame(uint64_t frame) const {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (energyCache.empty()) {
            return FrequencyBands{0.0f, 0.0f, 0.0f};
        }

        uint64_t windowIdx = frame / hopSize;
        if (windowIdx >= energyCache.size()) {
            if (energyCache.size() > 0) return energyCache.back();
            return FrequencyBands{0, 0, 0};
        }
        return energyCache[windowIdx];
    }

    void getColorAtFrame(uint64_t frame, uint8_t& r, uint8_t& g, uint8_t& b) const {
        FrequencyBands bands = getEnergyAtFrame(frame);
        float gamma = 0.5f;
        bands.low = powf(bands.low, gamma);
        bands.mid = powf(bands.mid, gamma);
        bands.high = powf(bands.high, gamma);

        float boost = 2.0f;
        r = (uint8_t)(std::min)(255.0f, bands.low * 255.0f * boost);
        g = (uint8_t)(std::min)(255.0f, bands.mid * 255.0f * boost);
        b = (uint8_t)(std::min)(255.0f, bands.high * 255.0f * boost);

        if (r + g + b < 50) {
            r = g = b = 50;
        }
    }
};
