#include "Mixer.hpp"
#include "Logger.hpp"
#include <algorithm>

namespace Lazerdeck {

MixerChannel::MixerChannel(int sr) : sampleRate(sr) {
    procPtrs.resize(2);
}

void MixerChannel::setSampleRate(int sr) {
    sampleRate = sr;
    // Notify VSTs? VST3 usually requires re-initialization or setupProcessing call for SR change.
    // For now, we assume simple SR updates might need VST reload or we just update the stored SR for new VSTs.
    // TODO: Propagate SR change to existing VSTs properly if supported.
}

void MixerChannel::process(const float* input, float* output, int frames) {
    if (frames <= 0) return;

    // Resize scratch buffers if needed
    if (procBuffer[0].size() < (size_t)frames) {
        procBuffer[0].resize(frames);
        procBuffer[1].resize(frames);
    }

    // De-interleave input to scratch
    // Apply volume here? Or at the end? Let's apply at the end.
    for (int i = 0; i < frames; ++i) {
        procBuffer[0][i] = input[i * 2 + 0];
        procBuffer[1][i] = input[i * 2 + 1];
    }

    procPtrs[0] = procBuffer[0].data();
    procPtrs[1] = procBuffer[1].data();

    // Process VSTs
    {
        std::lock_guard<std::mutex> lock(vstMutex);
        for (auto& vst : vstEffects) {
            if (vst) {
                vst->process(procPtrs.data(), procPtrs.data(), frames);
            }
        }
    }

    // Interleave and accumulate to output
    // Output is assumed to be zeroed or contain other signal? 
    // The requirement is "add to output" (mixing).
    
    for (int i = 0; i < frames; ++i) {
        output[i * 2 + 0] += procBuffer[0][i] * volume;
        output[i * 2 + 1] += procBuffer[1][i] * volume;
    }
}

void MixerChannel::loadVST(const std::string& path) {
    auto instance = VST3Host::getInstance().createInstance(path, sampleRate, 2048);
    if (instance) {
        std::lock_guard<std::mutex> lock(vstMutex);
        vstEffects.push_back(std::move(instance));
    }
}

void MixerChannel::usingVST(int index, const std::string& path) {
    if (index < 0 || index > 32) return;

    std::lock_guard<std::mutex> lock(vstMutex);
    
    if (index >= (int)vstEffects.size()) {
        vstEffects.resize(index + 1);
    }
    
    if (vstEffects[index] && vstEffects[index]->getPath() == path) {
        return; // Already loaded
    }
    
    // Create new instance
    auto instance = VST3Host::getInstance().createInstance(path, sampleRate, 2048);
    if (instance) {
        vstEffects[index] = std::move(instance);
        Logger::info("MixerChannel: Loaded VST at index " + std::to_string(index) + ": " + path);
    }
}

void MixerChannel::showVST(int index) {
    std::lock_guard<std::mutex> lock(vstMutex);
    if (index >= 0 && index < (int)vstEffects.size() && vstEffects[index]) {
        vstEffects[index]->showEditor();
    }
}

void MixerChannel::setVSTParameter(int vstIdx, int paramIdx, float value) {
    std::lock_guard<std::mutex> lock(vstMutex);
    if (vstIdx >= 0 && vstIdx < (int)vstEffects.size() && vstEffects[vstIdx]) {
        vstEffects[vstIdx]->setParameter(paramIdx, value);
    }
}

void MixerChannel::clearVSTs() {
    std::lock_guard<std::mutex> lock(vstMutex);
    vstEffects.clear();
}

Mixer::Mixer(int numChannels, int sr) : sampleRate(sr) {
    for (int i = 0; i < numChannels; ++i) {
        channels.push_back(std::make_unique<MixerChannel>(sampleRate));
    }
}

MixerChannel* Mixer::getChannel(int index) {
    if (index >= 0 && index < (int)channels.size()) {
        return channels[index].get();
    }
    return nullptr;
}

void Mixer::setSampleRate(int sr) {
    sampleRate = sr;
    for (auto& ch : channels) {
        ch->setSampleRate(sr);
    }
}

} // namespace Lazerdeck
