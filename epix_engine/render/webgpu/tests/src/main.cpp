#include <epix/app.h>
#include <epix/input.h>
#include <epix/wgpu.h>
#include <epix/window.h>
// glfw
#include <GLFW/glfw3.h>

#ifdef __EMSCRIPTEN__
#define GLFW_EXPOSE_NATIVE_EMSCRIPTEN
#ifndef GLFW_PLATFORM_EMSCRIPTEN  // not defined in older versions of emscripten
#define GLFW_PLATFORM_EMSCRIPTEN 0
#endif
#else  // __EMSCRIPTEN__
#ifdef _GLFW_X11
#define GLFW_EXPOSE_NATIVE_X11
#endif
#ifdef _GLFW_WAYLAND
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#ifdef _GLFW_COCOA
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#ifdef _GLFW_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#endif  // __EMSCRIPTEN__

#ifdef GLFW_EXPOSE_NATIVE_COCOA
#include <Foundation/Foundation.h>
#include <QuartzCore/CAMetalLayer.h>
#endif

#ifndef __EMSCRIPTEN__
#include <GLFW/glfw3native.h>
#endif

WGPUSurface glfwCreateWindowWGPUSurface(
    WGPUInstance instance, GLFWwindow* window
) {
#ifndef __EMSCRIPTEN__
    switch (glfwGetPlatform()) {
#else
    // glfwGetPlatform is not available in older versions of emscripten
    switch (GLFW_PLATFORM_EMSCRIPTEN) {
#endif

#ifdef GLFW_EXPOSE_NATIVE_X11
        case GLFW_PLATFORM_X11: {
            Display* x11_display = glfwGetX11Display();
            Window x11_window    = glfwGetX11Window(window);

            WGPUSurfaceSourceXlibWindow fromXlibWindow;
            fromXlibWindow.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
            fromXlibWindow.chain.next  = NULL;
            fromXlibWindow.display     = x11_display;
            fromXlibWindow.window      = x11_window;

            WGPUSurfaceDescriptor surfaceDescriptor;
            surfaceDescriptor.nextInChain  = &fromXlibWindow.chain;
            surfaceDescriptor.label.data   = NULL;
            surfaceDescriptor.label.length = 0;

            return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
        }
#endif  // GLFW_EXPOSE_NATIVE_X11

#ifdef GLFW_EXPOSE_NATIVE_WAYLAND
        case GLFW_PLATFORM_WAYLAND: {
            struct wl_display* wayland_display = glfwGetWaylandDisplay();
            struct wl_surface* wayland_surface = glfwGetWaylandWindow(window);

            WGPUSurfaceSourceWaylandSurface fromWaylandSurface;
            fromWaylandSurface.chain.sType =
                WGPUSType_SurfaceSourceWaylandSurface;
            fromWaylandSurface.chain.next = NULL;
            fromWaylandSurface.display    = wayland_display;
            fromWaylandSurface.surface    = wayland_surface;

            WGPUSurfaceDescriptor surfaceDescriptor;
            surfaceDescriptor.nextInChain  = &fromWaylandSurface.chain;
            surfaceDescriptor.label.data   = NULL;
            surfaceDescriptor.label.length = 0;

            return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
        }
#endif  // GLFW_EXPOSE_NATIVE_WAYLAND

#ifdef GLFW_EXPOSE_NATIVE_COCOA
        case GLFW_PLATFORM_COCOA: {
            id metal_layer      = [CAMetalLayer layer];
            NSWindow* ns_window = glfwGetCocoaWindow(window);
            [ns_window.contentView setWantsLayer:YES];
            [ns_window.contentView setLayer:metal_layer];

            WGPUSurfaceSourceMetalLayer fromMetalLayer;
            fromMetalLayer.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
            fromMetalLayer.chain.next  = NULL;
            fromMetalLayer.layer       = metal_layer;

            WGPUSurfaceDescriptor surfaceDescriptor;
            surfaceDescriptor.nextInChain  = &fromMetalLayer.chain;
            surfaceDescriptor.label.data   = NULL;
            surfaceDescriptor.label.length = 0;

            return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
        }
#endif  // GLFW_EXPOSE_NATIVE_COCOA

#ifdef GLFW_EXPOSE_NATIVE_WIN32
        case GLFW_PLATFORM_WIN32: {
            HWND hwnd           = glfwGetWin32Window(window);
            HINSTANCE hinstance = GetModuleHandle(NULL);

            WGPUSurfaceSourceWindowsHWND fromWindowsHWND;
            fromWindowsHWND.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
            fromWindowsHWND.chain.next  = NULL;
            fromWindowsHWND.hinstance   = hinstance;
            fromWindowsHWND.hwnd        = hwnd;

            WGPUSurfaceDescriptor surfaceDescriptor;
            surfaceDescriptor.nextInChain  = &fromWindowsHWND.chain;
            surfaceDescriptor.label.data   = NULL;
            surfaceDescriptor.label.length = 0;

            return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
        }
#endif  // GLFW_EXPOSE_NATIVE_WIN32

#ifdef GLFW_EXPOSE_NATIVE_EMSCRIPTEN
        case GLFW_PLATFORM_EMSCRIPTEN: {
#ifdef WEBGPU_BACKEND_DAWN
            WGPUSurfaceSourceCanvasHTMLSelector_Emscripten
                fromCanvasHTMLSelector;
            fromCanvasHTMLSelector.chain.sType =
                WGPUSType_SurfaceSourceCanvasHTMLSelector_Emscripten;
#else
            WGPUSurfaceDescriptorFromCanvasHTMLSelector fromCanvasHTMLSelector;
            fromCanvasHTMLSelector.chain.sType =
                WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
#endif
            fromCanvasHTMLSelector.chain.next = NULL;
            fromCanvasHTMLSelector.selector   = "canvas";

            WGPUSurfaceDescriptor surfaceDescriptor;
            surfaceDescriptor.nextInChain  = &fromCanvasHTMLSelector.chain;
            surfaceDescriptor.label.data   = NULL;
            surfaceDescriptor.label.length = 0;

            return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
        }
#endif  // GLFW_EXPOSE_NATIVE_EMSCRIPTEN

        default:
            // Unsupported platform
            return NULL;
    }
}

using namespace epix;

struct Context {
    wgpu::Instance instance;
    wgpu::Surface surface;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::CommandEncoder encoder;
    wgpu::TextureView target_view;

    wgpu::RenderPipeline pipeline;
};

template <typename T>
std::remove_reference_t<T>* addressof(T&& arg) noexcept {
    return reinterpret_cast<std::remove_reference_t<T>*>(
        &const_cast<char&>(reinterpret_cast<const volatile char&>(arg))
    );
}

auto shader = R"(
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
    return vec4f(0.0, 0.0, 0.0, 1.0);
}
)";

void insert_context(epix::Command cmd) { cmd.insert_resource(Context{}); }

void device_lost_callback(
    WGPUDeviceLostReason reason, const char* message, void* userdata
) {
    spdlog::error("Device lost: {}", message);
}
void uncaptured_error_callback(
    WGPUErrorType type, const char* message, void* userdata
) {
    spdlog::error("Error: {}", message);
}

void setup_context(
    epix::ResMut<Context> ctx,
    Query<Get<window::Window>, With<window::PrimaryWindow>> window_query
) {
    if (!window_query) return;
    auto [window] = window_query.single();
    ctx->instance = wgpu::createInstance(WGPUInstanceDescriptor{
        .nextInChain = nullptr,
    });
    ctx->surface =
        glfwCreateWindowWGPUSurface(ctx->instance, window.get_handle());
    ctx->adapter     = ctx->instance.requestAdapter(WGPURequestAdapterOptions{
            .compatibleSurface = ctx->surface,
    });
    auto desc_device = WGPUDeviceDescriptor{
        .label{"Device", WGPU_STRLEN},
        .defaultQueue{
            .label{"Queue", WGPU_STRLEN},
        },
        //    .deviceLostCallbackInfo{
        //        .mode     = wgpu::CallbackMode::AllowProcessEvents,
        //        .callback = device_lost_callback,
        // },
        //    .uncapturedErrorCallbackInfo{
        //        .callback = uncaptured_error_callback,
        // },
    };
    ctx->device = ctx->adapter.requestDevice(desc_device);
    wgpu::SurfaceCapabilities capabilities;
    ctx->surface.getCapabilities(ctx->adapter, &capabilities);
    ctx->surface.configure(WGPUSurfaceConfiguration{
        .device      = ctx->device,
        .format      = capabilities.formats[0],
        .usage       = wgpu::TextureUsage::RenderAttachment,
        .width       = (uint32_t)window.get_size().width,
        .height      = (uint32_t)window.get_size().height,
        .alphaMode   = wgpu::CompositeAlphaMode::Auto,
        .presentMode = wgpu::PresentMode::Immediate,
    });
    ctx->queue = ctx->device.getQueue();

    auto source = WGPUShaderSourceWGSL{
        .chain{
            .next  = nullptr,
            .sType = WGPUSType_ShaderSourceWGSL,
        },
        .code = {shader, WGPU_STRLEN},
    };
    wgpu::ShaderModuleDescriptor desc;
    desc.label         = {"Shader Module", WGPU_STRLEN};
    desc.nextInChain   = &source.chain;
    auto shader_module = ctx->device.createShaderModule(desc);

    ctx->pipeline =
        ctx->device.createRenderPipeline(WGPURenderPipelineDescriptor{
            .label  = "Render Pipeline",
            .layout = nullptr,
            .vertex{
                .module      = shader_module,
                .entryPoint  = {"vs_main", WGPU_STRLEN},
                .bufferCount = 0,
                .buffers     = nullptr,
            },
            .primitive{
                .topology         = wgpu::PrimitiveTopology::TriangleList,
                .stripIndexFormat = wgpu::IndexFormat::Undefined,
                .frontFace        = wgpu::FrontFace::CCW,
                .cullMode         = wgpu::CullMode::None,
            },
            .depthStencil = nullptr,
            .multisample{
                .count                  = 1,
                .mask                   = 0xFFFFFFFF,
                .alphaToCoverageEnabled = false,
            },
            .fragment = addressof(WGPUFragmentState{
                .module      = shader_module,
                .entryPoint  = {"fs_main", WGPU_STRLEN},
                .targetCount = 1,
                .targets     = addressof(WGPUColorTargetState{
                        .format    = capabilities.formats[0],
                        .blend     = addressof(WGPUBlendState{
                                .color{
                                    .operation = wgpu::BlendOperation::Add,
                                    .srcFactor = wgpu::BlendFactor::SrcAlpha,
                                    .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha,
                        },
                                .alpha{
                                    .operation = wgpu::BlendOperation::Add,
                                    .srcFactor = wgpu::BlendFactor::One,
                                    .dstFactor = wgpu::BlendFactor::Zero,
                        },
                    }),
                        .writeMask = wgpu::ColorWriteMask::All,
                }),
            })
        });
    shader_module.release();
}

void begin_frame(epix::ResMut<Context> ctx) {
    ctx->encoder =
        ctx->device.createCommandEncoder(WGPUCommandEncoderDescriptor{
            .label = "Command Encoder",
        });
    wgpu::SurfaceTexture surface_texture;
    ctx->surface.getCurrentTexture(&surface_texture);
    if (surface_texture.status !=
        wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal) {
        spdlog::error("Failed to get current texture");
        throw std::runtime_error("Failed to get current texture");
        return;
    }
    ctx->target_view = wgpu::Texture{surface_texture.texture}.createView(
        WGPUTextureViewDescriptor{
            .label         = "Target View",
            .format        = wgpu::Texture{surface_texture.texture}.getFormat(),
            .dimension     = wgpu::TextureViewDimension::_2D,
            .baseMipLevel  = 0,
            .mipLevelCount = 1,
            .baseArrayLayer  = 0,
            .arrayLayerCount = 1,
            .aspect          = wgpu::TextureAspect::All,
        }
    );
}

void end_frame(epix::ResMut<Context> ctx) {
    ctx->target_view.release();
    ctx->surface.present();
    ctx->encoder.release();
}

void submit_test(epix::ResMut<Context> ctx) {
    wgpu::RenderPassEncoder pass =
        ctx->encoder.beginRenderPass(WGPURenderPassDescriptor{
            .colorAttachmentCount   = 1,
            .colorAttachments       = addressof(WGPURenderPassColorAttachment{
                      .view    = ctx->target_view,
                      .loadOp  = wgpu::LoadOp::Clear,
                      .storeOp = wgpu::StoreOp::Store,
                      .clearValue{
                          .r = 0.1f,
                          .g = 0.1f,
                          .b = 0.1f,
                          .a = 1.0f,
                }
            }),
            .depthStencilAttachment = nullptr,
        });
    pass.setPipeline(ctx->pipeline);
    pass.draw(3, 1, 0, 0);
    pass.end();
    pass.release();
    wgpu::CommandBuffer commands = ctx->encoder.finish();
    ctx->queue.submit(commands);
    commands.release();
    ctx->device.poll(false, nullptr);
}

void cleanup_context(epix::ResMut<Context> ctx) {
    ctx->queue.release();
    ctx->device.release();
    ctx->adapter.release();
    ctx->surface.release();
    ctx->instance.release();
}

struct TestPlugin : epix::Plugin {
    void build(epix::App& app) override {
        app.add_system(Startup, chain(insert_context, setup_context));
        app.add_system(Update, submit_test);
        app.add_system(First, begin_frame);
        app.add_system(Last, end_frame);
        app.add_system(Exit, cleanup_context);
    }
};

int main() {
    epix::App app = epix::App::create2();
    app.add_plugin(epix::window::WindowPlugin{});
    app.get_plugin<epix::window::WindowPlugin>()
        ->primary_desc()
        .set_hints({
            {GLFW_CLIENT_API, GLFW_NO_API},
        })
        .set_size(800, 600)
        .set_vsync(false);
    app.add_plugin(epix::input::InputPlugin{});
    app.add_plugin(TestPlugin{});
    app.run();
}