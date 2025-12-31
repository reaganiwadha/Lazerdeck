#include "VST3Host.hpp"
#include "Logger.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/smartpointer.h"
#include "base/source/fobject.h"
#include <iostream>

#ifdef WIN32
#include <objbase.h>
#endif

namespace Lazerdeck {

// Global host application instance
static Steinberg::IPtr<Steinberg::Vst::HostApplication> gHostApp;

// Minimal implementation of IComponentHandler
class HostComponentHandler : public Steinberg::Vst::IComponentHandler, public Steinberg::FObject {
public:
    HostComponentHandler() {}
    virtual ~HostComponentHandler() {}

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID tag) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue valueNormalized) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID tag) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override { return Steinberg::kResultOk; }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, Steinberg::Vst::IComponentHandler::iid, Steinberg::Vst::IComponentHandler)
        return Steinberg::FObject::queryInterface(iid, obj);
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return Steinberg::FObject::addRef(); }
    Steinberg::uint32 PLUGIN_API release() override { return Steinberg::FObject::release(); }

    OBJ_METHODS(HostComponentHandler, Steinberg::FObject)
};

VST3Instance::VST3Instance() {}

VST3Instance::~VST3Instance() {
    release();
}

void VST3Instance::release() {
    if (processor) {
        processor->setProcessing(false);
        processor->release();
        processor = nullptr;
    }
    if (controller) {
        controller->terminate();
        controller->release();
        controller = nullptr;
    }
    if (component) {
        component->terminate();
        component->release();
        component = nullptr;
    }
    module = nullptr;
    initialized = false;
}

bool VST3Instance::init(const std::string& path, int sr, int bs) {
    release();
    sampleRate = sr;
    blockSize = bs;

    std::string error;
    module = VST3::Hosting::Module::create(path, error);
    if (!module) {
        Logger::error("VST3: Failed to load module: " + path + " - " + error);
        return false;
    }

    auto factory = module->getFactory();
    for (const auto& classInfo : factory.classInfos()) {
        if (classInfo.category() == "Audio Module Class") {
            auto comp = factory.createInstance<Steinberg::Vst::IComponent>(classInfo.ID());
            if (comp) {
                component = comp.take();
                break;
            }
        }
    }

    if (!component) {
        Logger::error("VST3: Failed to create component from: " + path);
        return false;
    }

    // Initialize component
    if (!gHostApp) {
        gHostApp = Steinberg::owned(new Steinberg::Vst::HostApplication());
    }

    if (component->initialize(gHostApp) != Steinberg::kResultOk) {
        Logger::error("VST3: Failed to initialize component: " + path);
        return false;
    }

    // Activate buses
    component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, true);
    component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, true);

    // Query Audio Processor
    if (component->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&processor) != Steinberg::kResultOk) {
        Logger::error("VST3: Component does not support IAudioProcessor: " + path);
        return false;
    }

    // Query Edit Controller
    if (component->queryInterface(Steinberg::Vst::IEditController::iid, (void**)&controller) != Steinberg::kResultOk) {
        Steinberg::TUID controllerCID;
        if (component->getControllerClassId(controllerCID) == Steinberg::kResultOk) {
            auto ctrl = factory.createInstance<Steinberg::Vst::IEditController>(controllerCID);
            if (ctrl) {
                controller = ctrl.take();
            }
        }
    }

    if (controller) {
        controller->initialize(gHostApp);
        controller->setComponentHandler(new HostComponentHandler());
    }

    // Setup processing
    Steinberg::Vst::ProcessSetup setup {};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;

    if (processor->setupProcessing(setup) != Steinberg::kResultOk) {
        Logger::error("VST3: Failed to setup processing: " + path);
        return false;
    }

    // Activate buses
    component->setActive(true);
    processor->setProcessing(true);

    initialized = true;
    Logger::info("VST3: Successfully loaded " + path);
    return true;
}

void VST3Instance::process(float** inputs, float** outputs, int numFrames) {
    if (!initialized || !processor) return;

    Steinberg::Vst::AudioBusBuffers inBuses[1] {};
    Steinberg::Vst::AudioBusBuffers outBuses[1] {};
    
    inBuses[0].numChannels = 2;
    inBuses[0].silenceFlags = 0;
    inBuses[0].channelBuffers32 = inputs;

    outBuses[0].numChannels = 2;
    outBuses[0].silenceFlags = 0;
    outBuses[0].channelBuffers32 = outputs;

    Steinberg::Vst::ProcessData data {};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = numFrames;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = inBuses;
    data.outputs = outBuses;

    processor->process(data);
}

void VST3Instance::setParameter(int index, float value) {
    if (!initialized || !controller) return;
    controller->setParamNormalized(index, value);
    
    // Note: To properly update the processor, we should also pass this change 
    // through the ProcessData queue in the process() call.
}

VST3Host::VST3Host() {
#ifdef WIN32
    CoInitialize(NULL);
#endif
}

VST3Host::~VST3Host() {
#ifdef WIN32
    CoUninitialize();
#endif
}

std::unique_ptr<VST3Instance> VST3Host::createInstance(const std::string& path, int sampleRate, int blockSize) {
    auto instance = std::make_unique<VST3Instance>();
    if (instance->init(path, sampleRate, blockSize)) {
        return instance;
    }
    return nullptr;
}

} // namespace Lazerdeck
