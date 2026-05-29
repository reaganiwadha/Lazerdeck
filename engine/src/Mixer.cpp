#include "Mixer.hpp"
#include "Vst3Runtime.hpp"
#include "Logger.hpp"
#include <algorithm>

namespace Lazerdeck {

MixerChannel::MixerChannel(int sr) : sampleRate(sr) {
    procPtrs.resize(2);
}

MixerChannel::~MixerChannel() {
    clearVSTs();
}

void MixerChannel::setSampleRate(int sr) {
    sampleRate = sr;
    // VST3 sample-rate changes would require re-initializing instances. For now
    // we only store the new rate; new instances pick it up on creation.
}

void MixerChannel::destroySlot(VstSlot& slot) {
    if (slot.handle) {
        if (const LzrVst3Api* vst = Vst3Runtime::api()) vst->destroy(slot.handle);
        slot.handle = nullptr;
    }
    slot.path.clear();
}

void MixerChannel::process(const float* input, float* output, int frames) {
    if (frames <= 0) return;

    // Resize scratch buffers if needed
    if (procBuffer[0].size() < (size_t)frames) {
        procBuffer[0].resize(frames);
        procBuffer[1].resize(frames);
    }

    // De-interleave input to scratch
    for (int i = 0; i < frames; ++i) {
        procBuffer[0][i] = input[i * 2 + 0];
        procBuffer[1][i] = input[i * 2 + 1];
    }

    procPtrs[0] = procBuffer[0].data();
    procPtrs[1] = procBuffer[1].data();

    // Process VSTs (only if the runtime plugin is loaded).
    if (const LzrVst3Api* vst = Vst3Runtime::api()) {
        std::lock_guard<std::mutex> lock(vstMutex);
        for (auto& slot : vstEffects) {
            if (slot.handle) {
                vst->process(slot.handle, procPtrs.data(), procPtrs.data(), frames);
            }
        }
    }

    // Interleave and accumulate to output (mixing).
    for (int i = 0; i < frames; ++i) {
        output[i * 2 + 0] += procBuffer[0][i] * volume;
        output[i * 2 + 1] += procBuffer[1][i] * volume;
    }
}

void MixerChannel::loadVST(const std::string& path) {
    const LzrVst3Api* vst = Vst3Runtime::api();
    if (!vst) return;
    void* inst = vst->create(path.c_str(), sampleRate, 2048);
    if (inst) {
        std::lock_guard<std::mutex> lock(vstMutex);
        vstEffects.push_back(VstSlot{ inst, path });
    }
}

void MixerChannel::usingVST(int index, const std::string& path) {
    if (index < 0 || index > 32) return;
    const LzrVst3Api* vst = Vst3Runtime::api();
    if (!vst) return;

    std::lock_guard<std::mutex> lock(vstMutex);

    if (index >= (int)vstEffects.size()) {
        vstEffects.resize(index + 1);
    }

    if (vstEffects[index].handle && vstEffects[index].path == path) {
        return; // Already loaded
    }

    destroySlot(vstEffects[index]);

    void* inst = vst->create(path.c_str(), sampleRate, 2048);
    if (inst) {
        vstEffects[index] = VstSlot{ inst, path };
        Logger::info("MixerChannel: Loaded VST at index " + std::to_string(index) + ": " + path);
    }
}

void MixerChannel::showVST(int index) {
    const LzrVst3Api* vst = Vst3Runtime::api();
    if (!vst) return;
    std::lock_guard<std::mutex> lock(vstMutex);
    if (index >= 0 && index < (int)vstEffects.size() && vstEffects[index].handle) {
        vst->show_editor(vstEffects[index].handle);
    }
}

void MixerChannel::setVSTParameter(int vstIdx, int paramIdx, float value) {
    const LzrVst3Api* vst = Vst3Runtime::api();
    if (!vst) return;
    std::lock_guard<std::mutex> lock(vstMutex);
    if (vstIdx >= 0 && vstIdx < (int)vstEffects.size() && vstEffects[vstIdx].handle) {
        vst->set_parameter(vstEffects[vstIdx].handle, paramIdx, value);
    }
}

void MixerChannel::clearVSTs() {
    std::lock_guard<std::mutex> lock(vstMutex);
    for (auto& slot : vstEffects) destroySlot(slot);
    vstEffects.clear();
}

void MixerChannel::beginDefinition() {
    std::lock_guard<std::mutex> lock(vstMutex);
    isDefining = true;
    definedVSTIndices.clear();
}

void MixerChannel::markVSTDefined(int index) {
    std::lock_guard<std::mutex> lock(vstMutex);
    if (isDefining) {
        definedVSTIndices.push_back(index);
    }
}

void MixerChannel::endDefinition() {
    std::lock_guard<std::mutex> lock(vstMutex);
    if (!isDefining) return;

    // Trim any VSTs past the last defined index (the client builds the chain
    // sequentially, so defined indices are 0..N).
    int maxIndex = -1;
    for (int idx : definedVSTIndices) {
        if (idx > maxIndex) maxIndex = idx;
    }

    if ((int)vstEffects.size() > maxIndex + 1) {
        for (int i = maxIndex + 1; i < (int)vstEffects.size(); ++i) destroySlot(vstEffects[i]);
        vstEffects.resize(maxIndex + 1);
    }

    isDefining = false;
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
