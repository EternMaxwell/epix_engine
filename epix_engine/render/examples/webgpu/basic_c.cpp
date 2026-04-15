#include <cassert>
#include <iostream>
#include <span>
#include <vector>

#define WGPU_TARGET_MACOS 1
#define WGPU_TARGET_LINUX_X11 2
#define WGPU_TARGET_WINDOWS 3
#define WGPU_TARGET_LINUX_WAYLAND 4
#define WGPU_TARGET_EMSCRIPTEN 5

#if defined(__EMSCRIPTEN__)
#define WGPU_TARGET WGPU_TARGET_EMSCRIPTEN
#elif defined(_WIN32)
#define WGPU_TARGET WGPU_TARGET_WINDOWS
#elif defined(__APPLE__)
#define WGPU_TARGET WGPU_TARGET_MACOS
#elif defined(_GLFW_WAYLAND)
#define WGPU_TARGET WGPU_TARGET_LINUX_WAYLAND
#else
#define WGPU_TARGET WGPU_TARGET_LINUX_X11
#endif

#if WGPU_TARGET == WGPU_TARGET_MACOS
#include <Foundation/Foundation.h>
#include <QuartzCore/CAMetalLayer.h>
#endif

#include <GLFW/glfw3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif  // __EMSCRIPTEN__

#if WGPU_TARGET == WGPU_TARGET_MACOS
#define GLFW_EXPOSE_NATIVE_COCOA
#elif WGPU_TARGET == WGPU_TARGET_LINUX_X11
#define GLFW_EXPOSE_NATIVE_X11
#elif WGPU_TARGET == WGPU_TARGET_LINUX_WAYLAND
#define GLFW_EXPOSE_NATIVE_WAYLAND
#elif WGPU_TARGET == WGPU_TARGET_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#if !defined(__EMSCRIPTEN__)
#include <GLFW/glfw3native.h>
#endif
#include <webgpu/webgpu.h>
#ifdef WEBGPU_BACKEND_WGPU
#include <webgpu/wgpu.h>
#endif

WGPUSurface glfwGetWGPUSurface(WGPUInstance instance, GLFWwindow* window) {
#if WGPU_TARGET == WGPU_TARGET_MACOS
    {
        id metal_layer      = [CAMetalLayer layer];
        NSWindow* ns_window = glfwGetCocoaWindow(window);
        [ns_window.contentView setWantsLayer:YES];
        [ns_window.contentView setLayer:metal_layer];

        WGPUSurfaceSourceMetalLayer fromMetalLayer;
        fromMetalLayer.chain.next  = NULL;
        fromMetalLayer.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        fromMetalLayer.layer       = metal_layer;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromMetalLayer.chain;
        surfaceDescriptor.label       = {NULL, WGPU_STRLEN};

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#elif WGPU_TARGET == WGPU_TARGET_LINUX_X11
    {
        Display* x11_display = glfwGetX11Display();
        Window x11_window    = glfwGetX11Window(window);

        WGPUSurfaceSourceXlibWindow fromXlibWindow;
        fromXlibWindow.chain.next  = NULL;
        fromXlibWindow.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        fromXlibWindow.display     = x11_display;
        fromXlibWindow.window      = x11_window;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromXlibWindow.chain;
        surfaceDescriptor.label       = {NULL, WGPU_STRLEN};

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#elif WGPU_TARGET == WGPU_TARGET_LINUX_WAYLAND
    {
        struct wl_display* wayland_display = glfwGetWaylandDisplay();
        struct wl_surface* wayland_surface = glfwGetWaylandWindow(window);

        WGPUSurfaceSourceWaylandSurface fromWaylandSurface;
        fromWaylandSurface.chain.next  = NULL;
        fromWaylandSurface.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        fromWaylandSurface.display     = wayland_display;
        fromWaylandSurface.surface     = wayland_surface;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromWaylandSurface.chain;
        surfaceDescriptor.label       = {NULL, WGPU_STRLEN};

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#elif WGPU_TARGET == WGPU_TARGET_WINDOWS
    {
        HWND hwnd           = glfwGetWin32Window(window);
        HINSTANCE hinstance = GetModuleHandle(NULL);

        WGPUSurfaceSourceWindowsHWND fromWindowsHWND;
        fromWindowsHWND.chain.next  = NULL;
        fromWindowsHWND.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        fromWindowsHWND.hinstance   = hinstance;
        fromWindowsHWND.hwnd        = hwnd;

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromWindowsHWND.chain;
        surfaceDescriptor.label       = {NULL, WGPU_STRLEN};

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#elif WGPU_TARGET == WGPU_TARGET_EMSCRIPTEN
    {
        WGPUSurfaceSourceCanvasHTMLSelector fromCanvasHTMLSelector;
        fromCanvasHTMLSelector.chain.next  = NULL;
        fromCanvasHTMLSelector.chain.sType = WGPUSType_SurfaceSourceCanvasHTMLSelector;
        fromCanvasHTMLSelector.selector    = "canvas";

        WGPUSurfaceDescriptor surfaceDescriptor;
        surfaceDescriptor.nextInChain = &fromCanvasHTMLSelector.chain;
        surfaceDescriptor.label       = {NULL, WGPU_STRLEN};

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
#else
#error "Unsupported WGPU_TARGET"
#endif
}

WGPUAdapter requestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const* options) {
    // A simple structure holding the local information shared with the
    // onAdapterRequestEnded callback.
    struct UserData {
        WGPUAdapter adapter = nullptr;
        bool requestEnded   = false;
    };
    UserData userData;

    // Callback called by wgpuInstanceRequestAdapter when the request returns
    // This is a C++ lambda function, but could be any function defined in the
    // global scope. It must be non-capturing (the brackets [] are empty) so
    // that it behaves like a regular C function pointer, which is what
    // wgpuInstanceRequestAdapter expects (WebGPU being a C API). The workaround
    // is to convey what we want to capture through the pUserData pointer,
    // provided as the last argument of wgpuInstanceRequestAdapter and received
    // by the callback as its last argument.
    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                                    void* pUserData, void*) {
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if (status == WGPURequestAdapterStatus_Success) {
            userData.adapter = adapter;
        } else {
            std::cout << "Could not get WebGPU adapter: " << message.data << std::endl;
        }
        userData.requestEnded = true;
    };

    // Call to the WebGPU request adapter procedure
    wgpuInstanceRequestAdapter(instance /* equivalent of navigator.gpu */, options,
                               WGPURequestAdapterCallbackInfo{.mode      = WGPUCallbackMode_AllowSpontaneous,
                                                              .callback  = onAdapterRequestEnded,
                                                              .userdata1 = (void*)&userData});

    // We wait until userData.requestEnded gets true
#ifdef __EMSCRIPTEN__
    while (!userData.requestEnded) {
        emscripten_sleep(100);
    }
#endif  // __EMSCRIPTEN__

    assert(userData.requestEnded);

    return userData.adapter;
}

void inspectAdapter(WGPUAdapter adapter) {
#ifndef __EMSCRIPTEN__
    WGPULimits supportedLimits  = {};
    supportedLimits.nextInChain = nullptr;
    bool success                = wgpuAdapterGetLimits(adapter, &supportedLimits);
    if (success) {
        std::cout << "Adapter limits:" << std::endl;
        std::cout << " - maxTextureDimension1D: " << supportedLimits.maxTextureDimension1D << std::endl;
        std::cout << " - maxTextureDimension2D: " << supportedLimits.maxTextureDimension2D << std::endl;
        std::cout << " - maxTextureDimension3D: " << supportedLimits.maxTextureDimension3D << std::endl;
        std::cout << " - maxTextureArrayLayers: " << supportedLimits.maxTextureArrayLayers << std::endl;
    }
#endif  // NOT __EMSCRIPTEN__
    WGPUSupportedFeatures supportedFeatures = {};

    // Call the function a first time with a null return address, just to get
    // the entry count.
    wgpuAdapterGetFeatures(adapter, &supportedFeatures);

    std::cout << "Adapter features:" << std::endl;
    std::cout << std::hex;  // Write integers as hexadecimal to ease comparison with webgpu.h literals
    for (auto f : std::span(supportedFeatures.features, supportedFeatures.featureCount)) {
        std::cout << " - 0x" << f << std::endl;
    }
    wgpuSupportedFeaturesFreeMembers(supportedFeatures);

    std::cout << std::dec;  // Restore decimal numbers
    WGPUAdapterInfo properties = {};
    properties.nextInChain     = nullptr;
    wgpuAdapterGetInfo(adapter, &properties);
    std::cout << "Adapter properties:" << std::endl;
    std::cout << " - vendorID: " << properties.vendorID << std::endl;
    if (properties.vendor.data) {
        std::cout << " - vendorName: " << properties.vendor.data << std::endl;
    }
    if (properties.architecture.data) {
        std::cout << " - architecture: " << properties.architecture.data << std::endl;
    }
    std::cout << " - deviceID: " << properties.deviceID << std::endl;
    if (properties.device.data) {
        std::cout << " - name: " << properties.device.data << std::endl;
    }
    if (properties.description.data) {
        std::cout << " - driverDescription: " << properties.description.data << std::endl;
    }
    std::cout << std::hex;
    std::cout << " - adapterType: 0x" << properties.adapterType << std::endl;
    std::cout << " - backendType: 0x" << properties.backendType << std::endl;
    std::cout << std::dec;  // Restore decimal numbers
}

WGPUDevice requestDeviceSync(WGPUAdapter adapter, WGPUDeviceDescriptor const* descriptor) {
    struct UserData {
        WGPUDevice device = nullptr;
        bool requestEnded = false;
    };
    UserData userData;

    auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                                   void* pUserData, void*) {
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if (status == WGPURequestDeviceStatus_Success) {
            userData.device = device;
        } else {
            std::cout << "Could not get WebGPU device: " << message.data << std::endl;
        }
        userData.requestEnded = true;
    };

    wgpuAdapterRequestDevice(adapter, descriptor,
                             WGPURequestDeviceCallbackInfo{.mode      = WGPUCallbackMode_AllowSpontaneous,
                                                           .callback  = onDeviceRequestEnded,
                                                           .userdata1 = (void*)&userData});

#ifdef __EMSCRIPTEN__
    while (!userData.requestEnded) {
        emscripten_sleep(100);
    }
#endif  // __EMSCRIPTEN__

    assert(userData.requestEnded);

    return userData.device;
}

void inspectDevice(WGPUDevice device) {
    WGPUSupportedFeatures features = {};
    wgpuDeviceGetFeatures(device, &features);

    std::cout << "Device features:" << std::endl;
    std::cout << std::hex;
    for (auto f : std::span(features.features, features.featureCount)) {
        std::cout << " - 0x" << f << std::endl;
    }
    std::cout << std::dec;

    WGPULimits limits  = {};
    limits.nextInChain = nullptr;
    bool success       = wgpuDeviceGetLimits(device, &limits);
    if (success) {
        std::cout << "Device limits:" << std::endl;
        std::cout << " - maxTextureDimension1D: " << limits.maxTextureDimension1D << std::endl;
        std::cout << " - maxTextureDimension2D: " << limits.maxTextureDimension2D << std::endl;
        std::cout << " - maxTextureDimension3D: " << limits.maxTextureDimension3D << std::endl;
        std::cout << " - maxTextureArrayLayers: " << limits.maxTextureArrayLayers << std::endl;
        std::cout << " - maxBindGroups: " << limits.maxBindGroups << std::endl;
        std::cout << " - maxDynamicUniformBuffersPerPipelineLayout: "
                  << limits.maxDynamicUniformBuffersPerPipelineLayout << std::endl;
        std::cout << " - maxDynamicStorageBuffersPerPipelineLayout: "
                  << limits.maxDynamicStorageBuffersPerPipelineLayout << std::endl;
        std::cout << " - maxSampledTexturesPerShaderStage: " << limits.maxSampledTexturesPerShaderStage << std::endl;
        std::cout << " - maxSamplersPerShaderStage: " << limits.maxSamplersPerShaderStage << std::endl;
        std::cout << " - maxStorageBuffersPerShaderStage: " << limits.maxStorageBuffersPerShaderStage << std::endl;
        std::cout << " - maxStorageTexturesPerShaderStage: " << limits.maxStorageTexturesPerShaderStage << std::endl;
        std::cout << " - maxUniformBuffersPerShaderStage: " << limits.maxUniformBuffersPerShaderStage << std::endl;
        std::cout << " - maxUniformBufferBindingSize: " << limits.maxUniformBufferBindingSize << std::endl;
        std::cout << " - maxStorageBufferBindingSize: " << limits.maxStorageBufferBindingSize << std::endl;
        std::cout << " - minUniformBufferOffsetAlignment: " << limits.minUniformBufferOffsetAlignment << std::endl;
        std::cout << " - minStorageBufferOffsetAlignment: " << limits.minStorageBufferOffsetAlignment << std::endl;
        std::cout << " - maxVertexBuffers: " << limits.maxVertexBuffers << std::endl;
        std::cout << " - maxVertexAttributes: " << limits.maxVertexAttributes << std::endl;
        std::cout << " - maxVertexBufferArrayStride: " << limits.maxVertexBufferArrayStride << std::endl;
        std::cout << " - maxInterStageShaderVariables: " << limits.maxInterStageShaderVariables << std::endl;
        std::cout << " - maxComputeWorkgroupStorageSize: " << limits.maxComputeWorkgroupStorageSize << std::endl;
        std::cout << " - maxComputeInvocationsPerWorkgroup: " << limits.maxComputeInvocationsPerWorkgroup << std::endl;
        std::cout << " - maxComputeWorkgroupSizeX: " << limits.maxComputeWorkgroupSizeX << std::endl;
        std::cout << " - maxComputeWorkgroupSizeY: " << limits.maxComputeWorkgroupSizeY << std::endl;
        std::cout << " - maxComputeWorkgroupSizeZ: " << limits.maxComputeWorkgroupSizeZ << std::endl;
        std::cout << " - maxComputeWorkgroupsPerDimension: " << limits.maxComputeWorkgroupsPerDimension << std::endl;
    }
}

// We embbed the source of the shader module here
const char* shaderSource = R"(
@vertex
fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> @builtin(position) vec4f {
	var p = vec2f(0.0, 0.0);
	if (in_vertex_index == 0u) {
		p = vec2f(-0.5, -0.5);
	} else if (in_vertex_index == 1u) {
		p = vec2f(0.5, -0.5);
	} else {
		p = vec2f(0.0, 0.5);
	}
	return vec4f(p, 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
	return vec4f(0.0, 0.4, 1.0, 1.0);
}
)";

class Application {
   public:
    // Initialize everything and return true if it went all right
    bool Initialize();

    // Uninitialize everything that was initialized
    void Terminate();

    // Draw a frame and handle events
    void MainLoop();

    // Return true as long as the main loop should keep on running
    bool IsRunning();

   private:
    WGPUSurfaceTexture GetNextSurfaceTexture();

    // Substep of Initialize() that creates the render pipeline
    void InitializePipeline();

   private:
    // We put here all the variables that are shared between init and main loop
    GLFWwindow* window;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUSurface surface;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
    WGPURenderPipeline pipeline;
};

int main() {
    Application app;

    if (!app.Initialize()) {
        return 1;
    }

#ifdef __EMSCRIPTEN__
    // Equivalent of the main loop when using Emscripten:
    auto callback = [](void* arg) {
        Application* pApp = reinterpret_cast<Application*>(arg);
        pApp->MainLoop();  // 4. We can use the application object
    };
    emscripten_set_main_loop_arg(callback, &app, 0, true);
#else   // __EMSCRIPTEN__
    while (app.IsRunning()) {
        app.MainLoop();
    }
#endif  // __EMSCRIPTEN__

    app.Terminate();

    return 0;
}

bool Application::Initialize() {
    // Open window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(640, 480, "Learn WebGPU", nullptr, nullptr);

    WGPUInstance instance = wgpuCreateInstance(nullptr);

    std::cout << "Requesting adapter..." << std::endl;
    surface                               = glfwGetWGPUSurface(instance, window);
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.nextInChain               = nullptr;
    adapterOpts.compatibleSurface         = surface;
    WGPUAdapter adapter                   = requestAdapterSync(instance, &adapterOpts);
    std::cout << "Got adapter: " << adapter << std::endl;

    wgpuInstanceRelease(instance);

    std::cout << "Requesting device..." << std::endl;
    WGPUDeviceDescriptor deviceDesc     = {};
    deviceDesc.nextInChain              = nullptr;
    deviceDesc.label                    = {"My Device", WGPU_STRLEN};
    deviceDesc.requiredFeatureCount     = 0;
    deviceDesc.requiredLimits           = nullptr;
    deviceDesc.defaultQueue.nextInChain = nullptr;
    deviceDesc.defaultQueue.label       = {"The default queue", WGPU_STRLEN};
    deviceDesc.deviceLostCallbackInfo =
        WGPUDeviceLostCallbackInfo{.mode     = WGPUCallbackMode_AllowSpontaneous,
                                   .callback = [](WGPUDevice const*, WGPUDeviceLostReason reason,
                                                  WGPUStringView message, void* /* pUserData */, void*) {
                                       std::cout << "Device lost: reason " << reason;
                                       if (message.data) std::cout << " (" << message.data << ")";
                                       std::cout << std::endl;
                                   }};
    deviceDesc.uncapturedErrorCallbackInfo = WGPUUncapturedErrorCallbackInfo{
        .callback = [](WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void* /* pUserData */, void*) {
            std::cout << "Uncaptured device error: type " << type;
            if (message.data) std::cout << " (" << message.data << ")";
            std::cout << std::endl;
        }};
    device = requestDeviceSync(adapter, &deviceDesc);
    std::cout << "Got device: " << device << std::endl;

    queue = wgpuDeviceGetQueue(device);

    // Configure the surface
    WGPUSurfaceConfiguration config = {};
    config.nextInChain              = nullptr;

    // Configuration of the textures created for the underlying swap chain
    config.width  = 640;
    config.height = 480;
    config.usage  = WGPUTextureUsage_RenderAttachment;
    WGPUSurfaceCapabilities capabilities;
    wgpuSurfaceGetCapabilities(surface, adapter, &capabilities);
    surfaceFormat = capabilities.formats[0];
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);
    config.format = surfaceFormat;

    // And we do not need any particular view format:
    config.viewFormatCount = 0;
    config.viewFormats     = nullptr;
    config.device          = device;
    config.presentMode     = WGPUPresentMode_Fifo;
    config.alphaMode       = WGPUCompositeAlphaMode_Auto;

    wgpuSurfaceConfigure(surface, &config);

    // Release the adapter only after it has been fully utilized
    wgpuAdapterRelease(adapter);

    InitializePipeline();

    return true;
}

void Application::Terminate() {
    // Unconfigure the surface
    wgpuRenderPipelineRelease(pipeline);
    wgpuSurfaceUnconfigure(surface);
    wgpuQueueRelease(queue);
    wgpuSurfaceRelease(surface);
    wgpuDeviceRelease(device);
    glfwDestroyWindow(window);
    glfwTerminate();
}

void Application::MainLoop() {
    glfwPollEvents();

    // Get the next target texture view
    WGPUSurfaceTexture surfaceTexture = GetNextSurfaceTexture();
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        return;
    }
    WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, nullptr);

    // Create a command encoder for the draw call
    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.nextInChain                  = nullptr;
    encoderDesc.label                        = {"My command encoder", WGPU_STRLEN};
    WGPUCommandEncoder encoder               = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    // Create the render pass that clears the screen with our color
    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.nextInChain              = nullptr;

    // The attachment part of the render pass descriptor describes the target texture of the pass
    WGPURenderPassColorAttachment renderPassColorAttachment = {};
    renderPassColorAttachment.view                          = targetView;
    renderPassColorAttachment.resolveTarget                 = nullptr;
    renderPassColorAttachment.loadOp                        = WGPULoadOp_Clear;
    renderPassColorAttachment.storeOp                       = WGPUStoreOp_Store;
    renderPassColorAttachment.clearValue                    = WGPUColor{0.9, 0.1, 0.2, 1.0};
    renderPassColorAttachment.depthSlice                    = WGPU_DEPTH_SLICE_UNDEFINED;

    renderPassDesc.colorAttachmentCount   = 1;
    renderPassDesc.colorAttachments       = &renderPassColorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;
    renderPassDesc.timestampWrites        = nullptr;

    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Select which render pipeline to use
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
    // Draw 1 instance of a 3-vertices shape
    wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);

    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);

    // Encode and submit the render pass
    WGPUCommandBufferDescriptor cmdBufferDescriptor = {};
    cmdBufferDescriptor.nextInChain                 = nullptr;
    cmdBufferDescriptor.label                       = {"Command buffer", WGPU_STRLEN};
    WGPUCommandBuffer command                       = wgpuCommandEncoderFinish(encoder, &cmdBufferDescriptor);
    wgpuCommandEncoderRelease(encoder);

    std::cout << "Submitting command..." << std::endl;
    wgpuQueueSubmit(queue, 1, &command);
    wgpuCommandBufferRelease(command);
    std::cout << "Command submitted." << std::endl;

    // At the enc of the frame
    wgpuTextureViewRelease(targetView);
    wgpuTextureRelease(surfaceTexture.texture);
#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(surface);
#endif

#if defined(WEBGPU_BACKEND_DAWN)
    wgpuDeviceTick(device);
#elif defined(WEBGPU_BACKEND_WGPU)
    wgpuDevicePoll(device, false, nullptr);
#endif
}

bool Application::IsRunning() { return !glfwWindowShouldClose(window); }

WGPUSurfaceTexture Application::GetNextSurfaceTexture() {
    // Get the surface texture
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
    return surfaceTexture;
}

void Application::InitializePipeline() {
    // Load the shader module
    WGPUShaderModuleDescriptor shaderDesc{};

    // We use the extension mechanism to specify the WGSL part of the shader module descriptor
    WGPUShaderSourceWGSL shaderCodeDesc{};
    // Set the chained struct's header
    shaderCodeDesc.chain.next  = nullptr;
    shaderCodeDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    // Connect the chain
    shaderDesc.nextInChain        = &shaderCodeDesc.chain;
    shaderCodeDesc.code           = {shaderSource, WGPU_STRLEN};
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Create the render pipeline
    WGPURenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.nextInChain = nullptr;

    // We do not use any vertex buffer for this first simplistic example
    pipelineDesc.vertex.bufferCount = 0;
    pipelineDesc.vertex.buffers     = nullptr;

    // NB: We define the 'shaderModule' in the second part of this chapter.
    // Here we tell that the programmable vertex shader stage is described
    // by the function called 'vs_main' in that module.
    pipelineDesc.vertex.module        = shaderModule;
    pipelineDesc.vertex.entryPoint    = {"vs_main", WGPU_STRLEN};
    pipelineDesc.vertex.constantCount = 0;
    pipelineDesc.vertex.constants     = nullptr;

    // Each sequence of 3 vertices is considered as a triangle
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;

    // We'll see later how to specify the order in which vertices should be
    // connected. When not specified, vertices are considered sequentially.
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;

    // The face orientation is defined by assuming that when looking
    // from the front of the face, its corner vertices are enumerated
    // in the counter-clockwise (CCW) order.
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;

    // But the face orientation does not matter much because we do not
    // cull (i.e. "hide") the faces pointing away from us (which is often
    // used for optimization).
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    // We tell that the programmable fragment shader stage is described
    // by the function called 'fs_main' in the shader module.
    WGPUFragmentState fragmentState{};
    fragmentState.module        = shaderModule;
    fragmentState.entryPoint    = {"fs_main", WGPU_STRLEN};
    fragmentState.constantCount = 0;
    fragmentState.constants     = nullptr;

    WGPUBlendState blendState{};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_Zero;
    blendState.alpha.dstFactor = WGPUBlendFactor_One;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget{};
    colorTarget.format    = surfaceFormat;
    colorTarget.blend     = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;  // We could write to only some of the color channels.

    // We have only one target because our render pass has only one output color
    // attachment.
    fragmentState.targetCount = 1;
    fragmentState.targets     = &colorTarget;
    pipelineDesc.fragment     = &fragmentState;

    // We do not use stencil/depth testing for now
    pipelineDesc.depthStencil = nullptr;

    // Samples per pixel
    pipelineDesc.multisample.count = 1;

    // Default value for the mask, meaning "all bits on"
    pipelineDesc.multisample.mask = ~0u;

    // Default value as well (irrelevant for count = 1 anyways)
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    pipelineDesc.layout = nullptr;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    // We no longer need to access the shader module
    wgpuShaderModuleRelease(shaderModule);
}