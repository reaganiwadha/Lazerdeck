#pragma once
#include <string>
#include <vector>
#include <memory>
#include <mutex>
// VST3 Headers
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"  // Add this

namespace Lazerdeck {

class VST3Instance {
public:
    VST3Instance();
    ~VST3Instance();
    
    bool init(const std::string& path, int sampleRate, int blockSize);
    void process(float** inputs, float** outputs, int numFrames);
    void setParameter(int index, float value);
    void showEditor();
    
    bool isValid() const { return initialized; }
    std::string getPath() const { return loadedPath; }

private:
    bool initialized = false;
    int sampleRate = 44100;
    int blockSize = 512;
    std::string loadedPath;
    
    VST3::Hosting::Module::Ptr module;
    Steinberg::Vst::IComponent* component = nullptr;
    Steinberg::Vst::IAudioProcessor* processor = nullptr;
    Steinberg::Vst::IEditController* controller = nullptr;
    Steinberg::IPlugView* view = nullptr;
    Steinberg::IPlugFrame* plugFrame = nullptr;  // Change to IPlugFrame*
    void* windowHandle = nullptr;
    
    Steinberg::Vst::AudioBusBuffers inputBuffers;
    
    void release();
};

class VST3Host {
public:
    static VST3Host& getInstance() {
        static VST3Host instance;
        return instance;
    }
    
    std::unique_ptr<VST3Instance> createInstance(const std::string& path, int sampleRate, int blockSize);

private:
    VST3Host();
    ~VST3Host();
};

} // namespace Lazerdeck