#pragma once
#include <fftw3.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <mutex>
#include "audio.hpp"

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
    
    // FFTW data
    float* fftInput = nullptr;
    fftwf_complex* fftOutput = nullptr;
    fftwf_plan plan = nullptr;
    
    // Frequency band indices
    int lowBandEnd = 0;
    int midBandEnd = 0;
    int highBandEnd = 0;
    
    FFTEnergy() = default;
    
    ~FFTEnergy() {
        cleanup();
    }
    
    void cleanup() {
        if (plan) {
            fftwf_destroy_plan(plan);
            plan = nullptr;
        }
        if (fftInput) {
            fftwf_free(fftInput);
            fftInput = nullptr;
        }
        if (fftOutput) {
            fftwf_free(fftOutput);
            fftOutput = nullptr;
        }
    }
    
    void initialize(const AudioBuffer* audioBuffer) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        // Don't fully cleanup if just resizing/updating, but here we assume new file
        cleanup();
        
        buffer = audioBuffer;
        
        // Allocate FFTW buffers
        fftInput = (float*)fftwf_malloc(sizeof(float) * fftSize);
        fftOutput = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * (fftSize / 2 + 1));
        
        // Create FFTW plan (planner is not thread-safe)
        {
            static std::mutex plannerMutex;
            std::lock_guard<std::mutex> plannerLock(plannerMutex);
            plan = fftwf_plan_dft_r2c_1d(fftSize, fftInput, fftOutput, FFTW_ESTIMATE);
        }
        
        // Calculate frequency band indices
        float freqPerBin = (float)buffer->sampleRate / fftSize;
        lowBandEnd = (int)(250.0f / freqPerBin);
        midBandEnd = (int)(2000.0f / freqPerBin);
        highBandEnd = (int)(20000.0f / freqPerBin);
        
        // Clamp
        highBandEnd = (std::min)(highBandEnd, fftSize / 2);

        energyCache.clear();
    }

    // Analyze a specific range of frames and append to cache
    // Assumes sequential appending
    void analyzeChunk(uint64_t frameStart, uint64_t frameCount) {
        if (!buffer || !plan) return;

        // Align to hopSize
        uint64_t startWindow = frameStart / hopSize;
        uint64_t numWindows = (frameCount) / hopSize; 
        
        // Compute locally first to avoid holding lock during FFT
        std::vector<FrequencyBands> chunkBands;
        chunkBands.reserve(numWindows);

        std::vector<float> window(fftSize);
        for (int i = 0; i < fftSize; i++) {
            window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (fftSize - 1)));
        }

        for (uint64_t w = 0; w < numWindows; ++w) {
            uint64_t currentWindowIdx = startWindow + w;
            uint64_t windowStartFrame = currentWindowIdx * hopSize;
            
            // Check if we have enough data for this window
            if (windowStartFrame + fftSize > buffer->frameCount) break;
            // Also check if we are within the chunk we just loaded (plus some overlap from previous)
            // Ideally we just process whatever new windows are fully available.

            for (int i = 0; i < fftSize; i++) {
                uint64_t f = windowStartFrame + i;
                if (f < buffer->frameCount) {
                    fftInput[i] = buffer->sample(f, 0) * window[i];
                } else {
                    fftInput[i] = 0.0f;
                }
            }
            
            fftwf_execute(plan);
            
            FrequencyBands bands;
            
             // Low band (bass)
            for (int bin = 1; bin < lowBandEnd; bin++) {
                float real = fftOutput[bin][0];
                float imag = fftOutput[bin][1];
                bands.low += sqrtf(real * real + imag * imag);
            }
            bands.low /= (lowBandEnd - 1);
            
            // Mid band
            for (int bin = lowBandEnd; bin < midBandEnd; bin++) {
                float real = fftOutput[bin][0];
                float imag = fftOutput[bin][1];
                bands.mid += sqrtf(real * real + imag * imag);
            }
            bands.mid /= (midBandEnd - lowBandEnd);
            
            // High band
            for (int bin = midBandEnd; bin < highBandEnd; bin++) {
                float real = fftOutput[bin][0];
                float imag = fftOutput[bin][1];
                bands.high += sqrtf(real * real + imag * imag);
            }
            bands.high /= (highBandEnd - midBandEnd);
            
            // Normalize energies (rough normalization)
            float maxEnergy = (std::max)({bands.low, bands.mid, bands.high});
            if (maxEnergy > 0.0f) {
                bands.low /= maxEnergy;
                bands.mid /= maxEnergy;
                bands.high /= maxEnergy;
            }
            
            chunkBands.push_back(bands);
        }

        // Now update cache with lock
        std::lock_guard<std::mutex> lock(cacheMutex);
        // Ensure we have space
        if (energyCache.size() < startWindow + chunkBands.size()) {
            energyCache.resize(startWindow + chunkBands.size());
        }
        
        for (size_t i = 0; i < chunkBands.size(); ++i) {
            energyCache[startWindow + i] = chunkBands[i];
        }
    }
    
    // Kept for compatibility but effectively re-runs everything
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
             // Return last known or silence
             if (energyCache.size() > 0) return energyCache.back();
             return FrequencyBands{0,0,0};
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
