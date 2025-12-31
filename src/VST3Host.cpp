#include "VST3Host.hpp"
#include "Logger.hpp"
#include "public.sdk/source/vst/hosting/module.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/gui/iplugview.h"
#include "base/source/fobject.h"
#include <iostream>

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

// Minimal implementation of IComponentHandler
class HostComponentHandler : public Steinberg::Vst::IComponentHandler, public Steinberg::FObject {
public:
    HostComponentHandler(Steinberg::Vst::IEditController* controller) : controller(controller) {}
    virtual ~HostComponentHandler() {}

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID tag) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue valueNormalized) override {
        if (controller) {
            controller->setParamNormalized(tag, valueNormalized);
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
    Steinberg::Vst::IEditController* controller;
};

// Implementation of IPlugFrame
class PlugFrame : public Steinberg::IPlugFrame, public Steinberg::FObject {
public:
    PlugFrame() = default;
    virtual ~PlugFrame() = default;

    void setWindowHandle(HWND hwnd) { windowHandle = hwnd; }

    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) override {
        if (!view || !newSize || !windowHandle) return Steinberg::kInvalidArgument;

        int width = newSize->right - newSize->left;
        int height = newSize->bottom - newSize->top;

        RECT winRect = {0, 0, width, height};
        AdjustWindowRect(&winRect, GetWindowLong(windowHandle, GWL_STYLE), FALSE);

        SetWindowPos(windowHandle, NULL, 0, 0, winRect.right - winRect.left, winRect.bottom - winRect.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, Steinberg::IPlugFrame::iid, Steinberg::IPlugFrame)
        return Steinberg::FObject::queryInterface(iid, obj);
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return Steinberg::FObject::addRef(); }
    Steinberg::uint32 PLUGIN_API release() override { return Steinberg::FObject::release(); }

    OBJ_METHODS(PlugFrame, Steinberg::FObject)

private:
    HWND windowHandle = nullptr;
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
    plugFrame = nullptr;  
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
        controller->setComponentHandler(new HostComponentHandler(controller));
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

void VST3Instance::showEditor() {
    if (!controller || !initialized) return;

#ifdef WIN32
    if (!view) {
        view = controller->createView(Steinberg::Vst::ViewType::kEditor);
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
