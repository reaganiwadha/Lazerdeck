#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <string>

namespace Lazerdeck {

// One effect slot in a channel. `handle` is an opaque instance owned by the
// dynamically-loaded VST3 plugin (see Vst3Runtime / vst3_abi.h).
struct VstSlot {
    void* handle = nullptr;
    std::string path;
};

class MixerChannel {
public:
    MixerChannel(int sampleRate);
    ~MixerChannel();

    // Processes input, adds to output (accumulation).
    // input and output are stereo interleaved float buffers.
    void process(const float* input, float* output, int frames);

    // VST management. All of these are safe no-ops when the VST3 runtime
    // (lazerdeck_vst3) is not available.
    void loadVST(const std::string& path);
    void usingVST(int index, const std::string& path);
    void showVST(int index);
    void setVSTParameter(int vstIdx, int paramIdx, float value);
    void clearVSTs();

    void beginDefinition();
    void markVSTDefined(int index);
    void endDefinition();

    void setSampleRate(int sr);

    void setVolume(float v) { volume = v; }
    float getVolume() const { return volume; }

private:
    void destroySlot(VstSlot& slot); // requires vstMutex held

    int sampleRate;
    std::vector<VstSlot> vstEffects;
    std::mutex vstMutex;
    bool isDefining = false;
    std::vector<int> definedVSTIndices;
    float volume = 1.0f;

    // Scratch buffers for VST processing
    std::vector<float> procBuffer[2]; // De-interleaved
    std::vector<float*> procPtrs;     // Pointers to procBuffer
};

class Mixer {
public:
    Mixer(int numChannels, int sampleRate);

    MixerChannel* getChannel(int index);

    void setSampleRate(int sr);

private:
    std::vector<std::unique_ptr<MixerChannel>> channels;
    int sampleRate;
};

} // namespace Lazerdeck
