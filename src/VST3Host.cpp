#include "VST3Host.hpp"
#include "Logger.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/common/memorystream.h"
#include "base/source/fobject.h"
#include "base/source/fstreamer.h"
#include <iostream>
#include <cmath>

#ifdef WIN32
#include <objbase.h>
#include <windows.h>

struct VSTWindowData {
    Steinberg::IPlugView* view;
};

static LRESULT CALLBACK VSTWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    VSTWindowData* data = (VSTWindowData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (uMsg) {
        case WM_CREATE:
            return 0;

        case WM_SIZE:
            if (data && data->view) {
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);
                Steinberg::ViewRect rect = {0, 0, width, height};
                data->view->onSize(&rect);
            }
            return 0;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            if (data) {
                delete data;
                SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            }
            return 0;

        case WM_PAINT:
            // Logger::info("VSTWindowProc: WM_PAINT");
            break;

        case WM_LBUTTONDOWN:
            Logger::info("VSTWindowProc: WM_LBUTTONDOWN");
            break;

        default:
            break;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static void RegisterVSTWindowClass() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = VSTWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"VST3EditorWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS; // Add CS_DBLCLKS
    RegisterClassW(&wc);
    registered = true;
}
#endif

namespace Lazerdeck {

#ifdef WIN32
// processVSTMessages is removed because SDL handles the message pump for all windows on the thread.
#endif

// Global host application instance
static Steinberg::IPtr<Steinberg::Vst::HostApplication> gHostApp;

class HostParamValueQueue : public Steinberg::Vst::IParamValueQueue, public Steinberg::FObject {
public:
    HostParamValueQueue(Steinberg::Vst::ParamID id) : id(id) {}
    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return (Steinberg::int32)values.size(); }
    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& sampleOffset, Steinberg::Vst::ParamValue& value) override {
        if (index < 0 || index >= (Steinberg::int32)values.size()) return Steinberg::kResultFalse;
        sampleOffset = 0;
        value = values[index];
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset, Steinberg::Vst::ParamValue value, Steinberg::int32& index) override {
        values.push_back(value);
        index = (Steinberg::int32)values.size() - 1;
        return Steinberg::kResultTrue;
    }
    
    // FObject/IUnknown
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, Steinberg::Vst::IParamValueQueue::iid, Steinberg::Vst::IParamValueQueue)
        return Steinberg::FObject::queryInterface(iid, obj);
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return Steinberg::FObject::addRef(); }
    Steinberg::uint32 PLUGIN_API release() override { return Steinberg::FObject::release(); }
    OBJ_METHODS(HostParamValueQueue, Steinberg::FObject)

private:
    Steinberg::Vst::ParamID id;
    std::vector<Steinberg::Vst::ParamValue> values;
};

class HostParameterChanges : public Steinberg::Vst::IParameterChanges, public Steinberg::FObject {
public:
    Steinberg::int32 PLUGIN_API getParameterCount() override { return (Steinberg::int32)queues.size(); }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override {
        if (index < 0 || index >= (Steinberg::int32)queues.size()) return nullptr;
        return queues[index];
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& id, Steinberg::int32& index) override {
        for (size_t i = 0; i < queues.size(); ++i) {
            if (queues[i]->getParameterId() == id) {
                index = (Steinberg::int32)i;
                return queues[i];
            }
        }
        auto* queue = new HostParamValueQueue(id);
        queue->addRef();
        queues.push_back(queue);
        index = (Steinberg::int32)queues.size() - 1;
        return queue;
    }

    void clear() {
        for (auto* q : queues) q->release();
        queues.clear();
    }

    // FObject/IUnknown
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, Steinberg::Vst::IParameterChanges::iid, Steinberg::Vst::IParameterChanges)
        return Steinberg::FObject::queryInterface(iid, obj);
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return Steinberg::FObject::addRef(); }
    Steinberg::uint32 PLUGIN_API release() override { return Steinberg::FObject::release(); }
    OBJ_METHODS(HostParameterChanges, Steinberg::FObject)

private:
    std::vector<HostParamValueQueue*> queues;
};

// Minimal implementation of IComponentHandler
class HostComponentHandler : public Steinberg::Vst::IComponentHandler, public Steinberg::FObject {
public:
    HostComponentHandler(VST3Instance* instance, Steinberg::Vst::IEditController* controller) 
        : instance(instance), controller(controller) {}
    virtual ~HostComponentHandler() {}

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID tag) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue valueNormalized) override {
        if (controller) {
            controller->setParamNormalized(tag, valueNormalized);
            static int editCount = 0;
            if (++editCount % 100 == 0) {
                Logger::info("VST3: Parameter " + std::to_string(tag) + " changed to " + std::to_string(valueNormalized));
            }
        }
        if (instance) {
            instance->pushParameterChange(tag, valueNormalized);
        }
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID tag) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override { return Steinberg::kResultOk; }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, Steinberg::Vst::IComponentHandler::iid, Steinberg::Vst::IComponentHandler)
        return Steinberg::FObject::queryInterface(iid, obj);
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return Steinberg::FObject::addRef(); }
    Steinberg::uint32 PLUGIN_API release() override { return Steinberg::FObject::release(); }

    OBJ_METHODS(HostComponentHandler, Steinberg::FObject)
private:
    VST3Instance* instance;
    Steinberg::Vst::IEditController* controller;
};

// Implementation of IPlugFrame
class PlugFrame : public Steinberg::IPlugFrame, public Steinberg::FObject {
public:
    PlugFrame() = default;
    virtual ~PlugFrame() = default;

#ifdef WIN32
    void setWindowHandle(HWND hwnd) { windowHandle = hwnd; }
#else
    void setWindowHandle(void* hwnd) { windowHandle = hwnd; }
#endif

    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) override {
#ifdef WIN32
        if (!view || !newSize || !windowHandle) return Steinberg::kInvalidArgument;

        int width = newSize->right - newSize->left;
        int height = newSize->bottom - newSize->top;

        RECT winRect = {0, 0, width, height};
        AdjustWindowRect(&winRect, GetWindowLong(windowHandle, GWL_STYLE), FALSE);

        SetWindowPos(windowHandle, NULL, 0, 0, winRect.right - winRect.left, winRect.bottom - winRect.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        return Steinberg::kResultOk;
#else
        return Steinberg::kResultOk;
#endif
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, Steinberg::IPlugFrame::iid, Steinberg::IPlugFrame)
        return Steinberg::FObject::queryInterface(iid, obj);
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return Steinberg::FObject::addRef(); }
    Steinberg::uint32 PLUGIN_API release() override { return Steinberg::FObject::release(); }

    OBJ_METHODS(PlugFrame, Steinberg::FObject)

private:
#ifdef WIN32
    HWND windowHandle = nullptr;
#else
    void* windowHandle = nullptr;
#endif
};

VST3Instance::VST3Instance() {}

VST3Instance::~VST3Instance() {
    release();
}

void VST3Instance::release() {
    if (view) {
        view->setFrame(nullptr);  // Clear frame first
        view->removed();
        view->release();
        view = nullptr;
    }
    if (plugFrame) {
        plugFrame->release();
        plugFrame = nullptr;
    }
#ifdef WIN32
    if (windowHandle) {
        DestroyWindow((HWND)windowHandle);
        windowHandle = nullptr;
    }
#endif
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
    loadedPath = path;

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
        auto* handler = new HostComponentHandler(this, controller);
        if (controller->setComponentHandler(handler) != Steinberg::kResultOk) {
            Logger::warn("VST3: Failed to set component handler");
        } else {
            Logger::info("VST3: Component handler set successfully");
        }
    }

    // Connect Component and Controller
    Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> cpComponent;
    Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> cpController;
    component->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&cpComponent);
    if (controller) {
        controller->queryInterface(Steinberg::Vst::IConnectionPoint::iid, (void**)&cpController);
    }

    if (cpComponent && cpController) {
        cpComponent->connect(cpController);
        cpController->connect(cpComponent);
    }

    // Synchronize controller with component state
    if (controller) {
        Steinberg::MemoryStream stream;
        if (component->getState(&stream) == Steinberg::kResultOk) {
            stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
            controller->setComponentState(&stream);
        }
    }

    // Check bus counts
    int32_t numInBuses = component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
    int32_t numOutBuses = component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
    Logger::info("VST3: Plugin has " + std::to_string(numInBuses) + " input buses and " + std::to_string(numOutBuses) + " output buses");

    // Get bus info for debugging
    if (numInBuses > 0) {
        Steinberg::Vst::BusInfo busInfo;
        component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, busInfo);
        Logger::info("VST3: Input bus channels: " + std::to_string(busInfo.channelCount));
    }
    if (numOutBuses > 0) {
        Steinberg::Vst::BusInfo busInfo;
        component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, busInfo);
        Logger::info("VST3: Output bus channels: " + std::to_string(busInfo.channelCount));
    }

    // Activate buses AFTER controller is initialized
    if (numInBuses > 0) {
        component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, true);
    }
    if (numOutBuses > 0) {
        component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, true);
    }

    // Setup processing
    Steinberg::Vst::ProcessSetup setup {};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;

    if (processor->canProcessSampleSize(setup.symbolicSampleSize) != Steinberg::kResultTrue) {
        Logger::warn("VST3: Plugin does not support 32-bit float samples");
    }

    if (processor->setupProcessing(setup) != Steinberg::kResultOk) {
        Logger::error("VST3: Failed to setup processing: " + path);
        return false;
    }

    // Set component to active state
    if (component->setActive(true) != Steinberg::kResultOk) {
        Logger::warn("VST3: Failed to set component active");
    }

    // Set up bus arrangements for stereo (MUST be done after setActive)
    Steinberg::Vst::SpeakerArrangement arr = Steinberg::Vst::SpeakerArr::kStereo;
    if (numInBuses > 0 && numOutBuses > 0) {
        if (processor->setBusArrangements(&arr, 1, &arr, 1) != Steinberg::kResultOk) {
            Logger::warn("VST3: Failed to set bus arrangements (plugin may not support stereo)");
        } else {
            Logger::info("VST3: Bus arrangements set to stereo");
        }
    }

    processor->setProcessing(true);

    initialized = true;
    Logger::info("VST3: Successfully loaded " + path);
    return true;
}

void VST3Instance::showEditor() {
    if (!controller || !initialized) {
        Logger::warn("VST3: Cannot show editor - not initialized");
        return;
    }

    Logger::info("VST3: Showing editor for " + loadedPath);

#ifdef WIN32
    if (!view) {
        view = controller->createView(Steinberg::Vst::ViewType::kEditor);
        if (view) {
            Logger::info("VST3: Created view for editor");
        } else {
            Logger::error("VST3: Failed to create view");
        }
    }

    if (!view) {
        Logger::warn("VST3: Plugin does not have an editor view");
        return;
    }

    if (view->isPlatformTypeSupported(Steinberg::kPlatformTypeHWND) != Steinberg::kResultTrue) {
        Logger::warn("VST3: Plugin does not support HWND platform type");
        return;
    }

    if (!windowHandle) {
        RegisterVSTWindowClass();

        // Get the size BEFORE creating the window
        Steinberg::ViewRect rect;
        if (view->getSize(&rect) != Steinberg::kResultOk) {
            rect.left = 0; rect.top = 0; rect.right = 800; rect.bottom = 600;
        }

        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        RECT winRect = {0, 0, width, height};
        AdjustWindowRect(&winRect, WS_OVERLAPPEDWINDOW, FALSE);

        // Use wide strings for proper encoding
        std::wstring title = L"VST3 Editor";
        windowHandle = CreateWindowExW(
            0, L"VST3EditorWindow", title.c_str(),
            WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT,
            winRect.right - winRect.left, winRect.bottom - winRect.top,
            NULL, NULL, GetModuleHandle(NULL), NULL
        );

        if (!windowHandle) {
            Logger::error("VST3: Failed to create window");
            return;
        }

        // Create and store the frame (manual reference counting)
        auto frame = new PlugFrame();
        frame->addRef();  // Manually add reference
        plugFrame = frame;
        frame->setWindowHandle((HWND)windowHandle);

        // Set the frame on the view BEFORE attaching
        if (view->setFrame(frame) != Steinberg::kResultOk) {
            Logger::warn("VST3: Failed to set frame for view");
        }

        // Store view data
        VSTWindowData* data = new VSTWindowData{view};
        SetWindowLongPtrW((HWND)windowHandle, GWLP_USERDATA, (LONG_PTR)data);

        // Attach the view
        Steinberg::tresult result = view->attached(windowHandle, Steinberg::kPlatformTypeHWND);
        if (result != Steinberg::kResultOk) {
            Logger::error("VST3: Failed to attach view to window");
            DestroyWindow((HWND)windowHandle);
            windowHandle = nullptr;
            delete data;
            if (plugFrame) {
                plugFrame->release();
                plugFrame = nullptr;
            }
            return;
        }

        // Show the view
        // view->setVisible(true);

        // Notify view of its size
        view->onSize(&rect);
        
        // Force window update
        ShowWindow((HWND)windowHandle, SW_SHOW);
        UpdateWindow((HWND)windowHandle);
    } else {
        ShowWindow((HWND)windowHandle, SW_SHOW);
        SetForegroundWindow((HWND)windowHandle);
        SetFocus((HWND)windowHandle);
    }
#else
    Logger::warn("VST3: GUI showing only implemented on Windows");
#endif
}

void VST3Instance::process(float** inputs, float** outputs, int numFrames) {
    if (!initialized || !processor) return;

    // Check if buses are available
    int32_t numInBuses = component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
    int32_t numOutBuses = component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);
    
    if (numInBuses == 0 || numOutBuses == 0) {
        static bool warnedNoBuses = false;
        if (!warnedNoBuses) {
            Logger::error("VST3: Plugin has no audio buses (in: " + std::to_string(numInBuses) + ", out: " + std::to_string(numOutBuses) + ")");
            warnedNoBuses = true;
        }
        // Copy input to output (bypass)
        for (int i = 0; i < numFrames; ++i) {
            outputs[0][i] = inputs[0][i];
            outputs[1][i] = inputs[1][i];
        }
        return;
    }

    Steinberg::Vst::AudioBusBuffers inBuses[1] {};
    Steinberg::Vst::AudioBusBuffers outBuses[1] {};
    
    inBuses[0].numChannels = 2;
    inBuses[0].silenceFlags = 0;
    inBuses[0].channelBuffers32 = inputs;

    outBuses[0].numChannels = 2;
    outBuses[0].silenceFlags = 0;
    outBuses[0].channelBuffers32 = outputs;

    // Check if input has signal
    static bool signalChecked = false;
    static int inputFramesChecked = 0;
    if (!signalChecked && inputs[0] && inputs[1]) {
        float maxL = 0.0f, maxR = 0.0f;
        for (int i = 0; i < numFrames; ++i) {
            maxL = std::max(maxL, std::abs(inputs[0][i]));
            maxR = std::max(maxR, std::abs(inputs[1][i]));
        }
        if (maxL > 0.001f || maxR > 0.001f) {
            Logger::info("VST3: Input signal detected - L: " + std::to_string(maxL) + ", R: " + std::to_string(maxR));
            signalChecked = true;
        }
        inputFramesChecked += numFrames;
        if (inputFramesChecked > 44100) { // ~1 second at 44.1kHz
            Logger::warn("VST3: No input signal detected after 1 second");
            signalChecked = true;
        }
    }

    HostParameterChanges paramChangesHost;
    paramChangesHost.addRef();
    {
        std::lock_guard<std::mutex> lock(paramMutex);
        for (const auto& pc : paramChanges) {
            Steinberg::int32 index;
            auto* queue = paramChangesHost.addParameterData(pc.id, index);
            if (queue) {
                Steinberg::int32 pointIndex;
                queue->addPoint(0, pc.value, pointIndex);
            }
        }
        paramChanges.clear();
    }

    Steinberg::Vst::ProcessData data {};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = numFrames;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = inBuses;
    data.outputs = outBuses;
    data.inputParameterChanges = &paramChangesHost;

    Steinberg::tresult result = processor->process(data);
    
    paramChangesHost.clear();
    paramChangesHost.release();

    if (result != Steinberg::kResultOk && result != Steinberg::kNotImplemented) {
        static bool warned = false;
        if (!warned) {
            Logger::warn("VST3: process() returned " + std::to_string(result) + " for " + loadedPath);
            warned = true;
        }
    }

    // Check if output has signal
    if (!signalChecked && outputs[0] && outputs[1]) {
        float maxL = 0.0f, maxR = 0.0f;
        for (int i = 0; i < numFrames; ++i) {
            maxL = std::max(maxL, std::abs(outputs[0][i]));
            maxR = std::max(maxR, std::abs(outputs[1][i]));
        }
        if (maxL > 0.001f || maxR > 0.001f) {
            Logger::info("VST3: Output signal detected - L: " + std::to_string(maxL) + ", R: " + std::to_string(maxR));
            signalChecked = true;
        }
    }

    static int processCount = 0;
    if (++processCount % 1000 == 0) {
        Logger::info("VST3: processed " + std::to_string(processCount) + " buffers for " + loadedPath);
    }
}

void VST3Instance::pushParameterChange(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
    std::lock_guard<std::mutex> lock(paramMutex);
    paramChanges.push_back({id, value});
}

void VST3Instance::setParameter(int index, float value) {
    if (!initialized || !controller) return;
    controller->setParamNormalized(index, value);
    pushParameterChange(index, value);
}

VST3Host::VST3Host() {
}

VST3Host::~VST3Host() {
}

std::unique_ptr<VST3Instance> VST3Host::createInstance(const std::string& path, int sampleRate, int blockSize) {
    auto instance = std::make_unique<VST3Instance>();
    if (instance->init(path, sampleRate, blockSize)) {
        return instance;
    }
    return nullptr;
}

} // namespace Lazerdeck
