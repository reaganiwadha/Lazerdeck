#pragma once
#include <cstdint>
#include <vector>
#include <assert.h>

struct AudioBuffer {
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    uint64_t frameCount = 0;
    std::vector<float> samples;

    AudioBuffer() = default;
    AudioBuffer(unsigned int ch, unsigned int sr, uint64_t frames, std::vector<float>&& data)
        : channels(ch)
        , sampleRate(sr)
        , frameCount(frames)
        , samples(std::move(data))
    {
        assert(samples.size() == frameCount * channels);
    }

    float& sample(uint64_t frame, unsigned int channel) {
        static float dummy = 0.0f;
        if (channel >= channels || frame >= frameCount) {
            dummy = 0.0f;
            return dummy;
        }
        return samples[frame * channels + channel];
    }

    float sample(uint64_t frame, unsigned int channel) const {
        if (channel >= channels || frame >= frameCount) return 0.0f;
        return samples[frame * channels + channel];
    }

    float* frame(uint64_t frame) {
        if (frame >= frameCount) return nullptr;
        return &samples[frame * channels];
    }

    const float* frame(uint64_t frame) const {
        if (frame >= frameCount) return nullptr;
        return &samples[frame * channels];
    }

    uint64_t totalSamples() const {
        return frameCount * channels;
    }

    void resize(uint64_t frames) {
        frameCount = frames;
        samples.resize(frameCount * channels);
    }
};