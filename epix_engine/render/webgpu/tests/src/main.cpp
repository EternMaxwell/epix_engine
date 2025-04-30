#include <epix/app.h>
#include <epix/input.h>
#include <epix/utils/time.h>
#include <epix/wgpu.h>
#include <epix/window.h>

using namespace epix;

struct Context {
    wgpu::Instance instance;
    wgpu::Surface surface;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::CommandEncoder encoder;
    wgpu::TextureView target_view;
    wgpu::Texture surface_texture;

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

void insert_context(epix::Commands cmd) { cmd.insert_resource(Context{}); }

void setup_context(
    epix::ResMut<Context> ctx,
    Query<Get<window::Window>, Filter<With<window::PrimaryWindow>>> window_query
) {
    if (!window_query) return;
    auto [window] = window_query.single();
    ctx->instance = wgpu::createInstance(WGPUInstanceDescriptor{
        .nextInChain = nullptr,
    });
    ctx->surface =
        epix::webgpu::utils::create_surface(ctx->instance, window.get_handle());
    ctx->adapter     = ctx->instance.requestAdapter(WGPURequestAdapterOptions{
        // .backendType       = wgpu::BackendType::D3D12,
            .compatibleSurface = ctx->surface,
    });
    auto desc_device = WGPUDeviceDescriptor{
        .label{"Device", WGPU_STRLEN},
        .defaultQueue{
            .label{"Queue", WGPU_STRLEN},
        },
        .deviceLostCallbackInfo{
            .mode     = wgpu::CallbackMode::AllowProcessEvents,
            .callback = [](const WGPUDevice* device,
                           WGPUDeviceLostReason reason, WGPUStringView message,
                           void* userdata1, void* userdata2
                        ) { spdlog::error("Device lost: {}", message.data); },
        },
        .uncapturedErrorCallbackInfo{
            .callback =
                [](const WGPUDevice* device, WGPUErrorType type,
                   WGPUStringView message, void* userdata1, void* userdata2) {
                    spdlog::error("Uncaptured error: {}", message.data);
                },
        },
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

void begin_frame(epix::ResMut<Context>& ctx, const epix::app::World&) {
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
    ctx->surface_texture = surface_texture.texture;
    ctx->target_view     = wgpu::Texture{surface_texture.texture}.createView(
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
    ctx->surface.present();
    ctx->target_view.release();
    ctx->surface_texture.release();
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
        app.add_systems(Startup, into(insert_context, setup_context).chain());
        app.add_systems(
            Update,
            into(submit_test /* ,
                  [](epix::Res<epix::AppProfile> profile,
                     Local<std::optional<epix::utils::time::Timer>> timer,
                     epix::Res<epix::app::ScheduleProfiles> profiles) {
                      if (!timer->has_value()) {
                          *timer = epix::utils::time::Timer::repeat(1);
                      }
                      if (!(**timer).tick()) {
                          return;
                      }
                      spdlog::info("FrameTime: {:1.3f}ms", profile->frame_time);
                      spdlog::info("FPS:       {:4.2f}", profile->fps);
                      profiles->for_each([](auto&& id, auto&& profile) {
                          spdlog::info(
                              "Schedule {:<30}: average-> {:2.3f}ms",
                              std::format("{}", id.name()), profile.time_avg
                          );
                      });
                  } */
            )
        );
        app.add_systems(First, into(begin_frame));
        app.add_systems(Last, into(end_frame));
        app.add_systems(Exit, into(cleanup_context));
    }
};

int main() {
    epix::App app = epix::App::create();
    app.add_plugin(epix::window::WindowPlugin{});
    app.get_plugin<epix::window::WindowPlugin>()
        ->primary_desc()
        .set_hints({
            {GLFW_CLIENT_API, GLFW_NO_API},
        })
        .set_size(800, 600)
        .set_vsync(false);
    app.add_plugin(epix::input::InputPlugin{}.enable_output());
    app.add_plugin(TestPlugin{});
    app.run();
}