#include <cassert>
#include <iostream>
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

WGPUSurface glfwGetWGPUSurfaceRaw(WGPUInstance instance, GLFWwindow* window) {
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

import epix.core;
import epix.render;

import webgpu;

wgpu::Surface glfwGetWGPUSurface(const wgpu::Instance& instance, GLFWwindow* window) {
    auto res = glfwGetWGPUSurfaceRaw(instance, window);
    return std::move(*reinterpret_cast<wgpu::Surface*>(&res));
}

using namespace wgpu;

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
    SurfaceTexture GetNextSurfaceTexture();

    // Substep of Initialize() that creates the render pipeline
    void InitializePipeline();

   private:
    // We put here all the variables that are shared between init and main loop
    GLFWwindow* window;
    DeviceDescriptor deviceDescriptor;  // this will keep callbacks alive
    Device device;
    LogCallback logCallback;  // this will keep the callback alive
    Queue queue;
    Surface surface;
    TextureFormat surfaceFormat = TextureFormat::eUndefined;
    RenderPipeline pipeline;
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

    Instance instance = wgpu::createInstance();
    // wgpu::setLogLevel(wgpu::LogLevel::eDebug);
    // logCallback = [](wgpu::LogLevel level, wgpu::StringView message) {
    //     std::cout << "WGPU Log [level " << wgpu::to_string(level) << "]: " << std::string_view(message) << std::endl;
    // };
    // wgpu::setLogCallback(logCallback, nullptr);

    surface = glfwGetWGPUSurface(instance, window);

    std::cout << "Requesting adapter..." << std::endl;
    RequestAdapterOptions adapterOpts = {};
    adapterOpts.powerPreference       = PowerPreference::eHighPerformance;
    adapterOpts.backendType           = wgpu::BackendType::eVulkan;
    adapterOpts.compatibleSurface     = surface;
    Adapter adapter                   = instance.requestAdapter(adapterOpts);
    // std::cout << "Got adapter: " << adapter << std::endl;

    std::cout << "Requesting device..." << std::endl;
    DeviceDescriptor deviceDesc   = {};
    deviceDesc.label              = "My Device";
    deviceDesc.defaultQueue.label = "The default queue";
    deviceDesc.deviceLostCallbackInfo =
        wgpu::DeviceLostCallbackInfo()
            .setMode(wgpu::CallbackMode::eAllowSpontaneous)
            .setCallback([](wgpu::Device const&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
                std::cout << "Device lost: reason " << wgpu::to_string(reason);
                std::cout << " (" << std::string_view(message) << ")";
                std::cout << std::endl;
            });
    deviceDesc.uncapturedErrorCallbackInfo = wgpu::UncapturedErrorCallbackInfo().setCallback(
        [](wgpu::Device const&, ErrorType type, wgpu::StringView message) {
            std::cout << "Uncaptured device error: type " << wgpu::to_string(type);
            std::cout << " (" << std::string_view(message) << ")";
            std::cout << std::endl;
        });

    device = adapter.requestDevice(deviceDesc);

    queue = device.getQueue();

    // Configure the surface
    SurfaceConfiguration config = {};

    // Configuration of the textures created for the underlying swap chain
    config.width  = 640;
    config.height = 480;
    config.usage  = TextureUsage::eRenderAttachment;
    SurfaceCapabilities capabilities;
    surface.getCapabilities(adapter, &capabilities);
    surfaceFormat = capabilities.formats[0];
    config.format = surfaceFormat;

    // And we do not need any particular view format:
    config.device      = device;
    config.presentMode = PresentMode::eMailbox;
    config.alphaMode   = CompositeAlphaMode::eAuto;

    surface.configure(config);

    InitializePipeline();

    return true;
}

void Application::Terminate() {
    pipeline = nullptr;
    surface.unconfigure();
    queue   = nullptr;
    surface = nullptr;
    device  = nullptr;
    glfwDestroyWindow(window);
    glfwTerminate();
}

void Application::MainLoop() {
    glfwPollEvents();
    static size_t frameCount = 0;
    frameCount++;

    // Get the next target texture view
    SurfaceTexture surfaceTexture = GetNextSurfaceTexture();
    if (surfaceTexture.status != SurfaceGetCurrentTextureStatus::eSuccessOptimal &&
        surfaceTexture.status != SurfaceGetCurrentTextureStatus::eSuccessSuboptimal) {
        std::cout << "Could not acquire next surface texture, at frame " << frameCount
                  << ", status: " << wgpu::to_string(surfaceTexture.status) << std::endl;
        return;
    }
    TextureViewDescriptor viewDescriptor;
    viewDescriptor.label           = "Surface texture view";
    viewDescriptor.format          = surfaceTexture.texture.getFormat();
    viewDescriptor.dimension       = TextureViewDimension::e2D;
    viewDescriptor.baseMipLevel    = 0;
    viewDescriptor.mipLevelCount   = 1;
    viewDescriptor.baseArrayLayer  = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.aspect          = TextureAspect::eAll;
    TextureView targetView         = surfaceTexture.texture.createView(viewDescriptor);

    // Create a command encoder for the draw call
    CommandEncoderDescriptor encoderDesc = {};
    encoderDesc.label                    = "My command encoder";
    CommandEncoder encoder               = device.createCommandEncoder(encoderDesc);

    // Create the render pass that clears the screen with our color
    RenderPassDescriptor renderPassDesc = {};

    // The attachment part of the render pass descriptor describes the target texture of the pass
    RenderPassColorAttachment renderPassColorAttachment = {};
    renderPassColorAttachment.view                      = targetView;
    renderPassColorAttachment.resolveTarget             = nullptr;
    renderPassColorAttachment.loadOp                    = LoadOp::eClear;
    renderPassColorAttachment.storeOp                   = StoreOp::eStore;
    renderPassColorAttachment.clearValue                = WGPUColor{0.9, 0.1, 0.2, 1.0};
    renderPassColorAttachment.depthSlice                = WGPU_DEPTH_SLICE_UNDEFINED;

    renderPassDesc.colorAttachments = {renderPassColorAttachment};

    RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);

    // Select which render pipeline to use
    renderPass.setPipeline(pipeline);
    // Draw 1 instance of a 3-vertices shape
    renderPass.draw(3, 1, 0, 0);

    renderPass.end();
    renderPass = nullptr;

    // Finally encode and submit the render pass
    CommandBufferDescriptor cmdBufferDescriptor = {};
    cmdBufferDescriptor.label                   = "Command buffer";
    CommandBuffer command                       = encoder.finish(cmdBufferDescriptor);
    encoder                                     = nullptr;

    // std::cout << "Submitting command..." << std::endl;
    queue.submit(command);
    command = nullptr;
    // std::cout << "Command submitted." << std::endl;

    // At the enc of the frame
    targetView = nullptr;
#ifndef __EMSCRIPTEN__
    surface.present();
#endif

#if defined(WEBGPU_BACKEND_DAWN)
    device.tick();
#elif defined(WEBGPU_BACKEND_WGPU)
    device.poll(false);
#endif
}

bool Application::IsRunning() { return !glfwWindowShouldClose(window); }

SurfaceTexture Application::GetNextSurfaceTexture() {
    // Get the surface texture
    SurfaceTexture surfaceTexture;
    surface.getCurrentTexture(&surfaceTexture);
    return surfaceTexture;
    // if (surfaceTexture.status != SurfaceGetCurrentTextureStatus::eSuccessOptimal &&
    //     surfaceTexture.status != SurfaceGetCurrentTextureStatus::eSuccessSuboptimal) {
    //     std::cout << "Could not acquire next surface texture view, status: " <<
    //     wgpu::to_string(surfaceTexture.status)
    //               << std::endl;
    //     return surfaceTexture;
    // }
    // Texture texture = surfaceTexture.texture;

    // // Create a view for this surface texture
    // TextureViewDescriptor viewDescriptor;
    // viewDescriptor.label           = "Surface texture view";
    // viewDescriptor.format          = texture.getFormat();
    // viewDescriptor.dimension       = TextureViewDimension::e2D;
    // viewDescriptor.baseMipLevel    = 0;
    // viewDescriptor.mipLevelCount   = 1;
    // viewDescriptor.baseArrayLayer  = 0;
    // viewDescriptor.arrayLayerCount = 1;
    // viewDescriptor.aspect          = TextureAspect::eAll;
    // TextureView targetView         = texture.createView(viewDescriptor);

    // return targetView;
}

void Application::InitializePipeline() {
    // Load the shader module
    ShaderModuleDescriptor shaderDesc;

    // We use the extension mechanism to specify the WGSL part of the shader module descriptor
    ShaderSourceWGSL shaderCodeDesc;
    // Set the chained struct's header
    shaderCodeDesc.chain.sType = SType::eShaderSourceWGSL;
    shaderCodeDesc.code        = shaderSource;
    // Connect the chain
    shaderDesc.setNextInChain(std::move(shaderCodeDesc));
    ShaderModule shaderModule = device.createShaderModule(shaderDesc);

    // Create the render pipeline
    RenderPipelineDescriptor pipelineDesc;

    // We do not use any vertex buffer for this first simplistic example

    // NB: We define the 'shaderModule' in the second part of this chapter.
    // Here we tell that the programmable vertex shader stage is described
    // by the function called 'vs_main' in that module.
    pipelineDesc.vertex.module     = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";

    // Each sequence of 3 vertices is considered as a triangle
    pipelineDesc.primitive.topology = PrimitiveTopology::eTriangleList;

    // We'll see later how to specify the order in which vertices should be
    // connected. When not specified, vertices are considered sequentially.
    pipelineDesc.primitive.stripIndexFormat = IndexFormat::eUndefined;

    // The face orientation is defined by assuming that when looking
    // from the front of the face, its corner vertices are enumerated
    // in the counter-clockwise (CCW) order.
    pipelineDesc.primitive.frontFace = FrontFace::eCCW;

    // But the face orientation does not matter much because we do not
    // cull (i.e. "hide") the faces pointing away from us (which is often
    // used for optimization).
    pipelineDesc.primitive.cullMode = CullMode::eNone;

    // We tell that the programmable fragment shader stage is described
    // by the function called 'fs_main' in the shader module.
    FragmentState fragmentState;
    fragmentState.module     = shaderModule;
    fragmentState.entryPoint = "fs_main";

    BlendState blendState;
    blendState.color.srcFactor = BlendFactor::eSrcAlpha;
    blendState.color.dstFactor = BlendFactor::eOneMinusSrcAlpha;
    blendState.color.operation = BlendOperation::eAdd;
    blendState.alpha.srcFactor = BlendFactor::eZero;
    blendState.alpha.dstFactor = BlendFactor::eOne;
    blendState.alpha.operation = BlendOperation::eAdd;

    ColorTargetState colorTarget;
    colorTarget.format = surfaceFormat;
    // colorTarget.blend     = blendState;
    colorTarget.writeMask = ColorWriteMask::eAll;  // We could write to only some of the color channels.

    // We have only one target because our render pass has only one output color
    // attachment.
    fragmentState.targets = {colorTarget};
    pipelineDesc.fragment = fragmentState;

    // We do not use stencil/depth testing for now
    pipelineDesc.depthStencil = std::nullopt;

    // Samples per pixel
    pipelineDesc.multisample.count = 1;

    // Default value for the mask, meaning "all bits on"
    pipelineDesc.multisample.mask = ~0u;

    // Default value as well (irrelevant for count = 1 anyways)
    pipelineDesc.multisample.alphaToCoverageEnabled = false;
    pipelineDesc.layout                             = nullptr;

    pipeline = device.createRenderPipeline(pipelineDesc);
}