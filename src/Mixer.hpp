#pragma once
#include <vector>
#include <memory>
#include <mutex>
#ifdef ENABLE_VST3
#include "VST3Host.hpp"
#endif

namespace Lazerdeck {

class MixerChannel {
public:
    MixerChannel(int sampleRate);
    
    // Processes input, adds to output (accumulation)
    // input and output are stereo interleaved float buffers
    void process(const float* input, float* output, int frames);

#ifdef ENABLE_VST3
    // VST Management
    void loadVST(const std::string& path);
    void usingVST(int index, const std::string& path);
    void showVST(int index);
    void setVSTParameter(int vstIdx, int paramIdx, float value);
    void clearVSTs();
    
    void beginDefinition();
    void markVSTDefined(int index);
    void endDefinition();
#endif

    void setSampleRate(int sr);

    // Volume/Pan (Future)
    void setVolume(float v) { volume = v; }
    float getVolume() const { return volume; }

private:
    int sampleRate;
#ifdef ENABLE_VST3
    std::vector<std::unique_ptr<VST3Instance>> vstEffects;
    std::mutex vstMutex;
    // Definition
    bool isDefining = false;
    std::vector<int> definedVSTIndices;
#endif
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
