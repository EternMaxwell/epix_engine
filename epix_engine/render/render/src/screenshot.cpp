#include <spdlog/spdlog.h>

#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/render/gpu_readback.hpp>
#include <epix/render/graph.hpp>
#include <epix/render/manual_texture_view.hpp>
#include <epix/render/screenshot.hpp>
#include <epix/render/view.hpp>
#include <epix/task.hpp>
#include <span>
#include <unordered_set>
#include <webgpu/webgpu.hpp>

using namespace epix::render::screenshot;
using namespace epix::ecs;
using namespace epix::app;
using namespace epix::assets;
using namespace epix::image;
using namespace epix::render;
using namespace epix::render::window;
namespace assets = epix::assets;
namespace image  = epix::image;

namespace epix::render::screenshot {

/** @brief Internal render-world state for the screenshot plugin. */
struct ScreenshotState {
    struct PendingCapture {
        ::epix::camera::RenderTarget target;
        std::optional<Entity> entity;
    };
    struct CompletedCapture {
        image::Image image;
        std::optional<Entity> entity;
    };
    struct PreparedCapture {
        Entity readback_entity;
        std::optional<Entity> entity;
        wgpu::Texture texture;
        wgpu::TextureView output_view;
        glm::uvec2 size{};
        wgpu::TextureFormat format = wgpu::TextureFormat::eUndefined;
        bool restore_output        = false;
    };
    struct InFlightCapture {
        std::optional<Entity> entity;
        glm::uvec2 size{};
        wgpu::TextureFormat format = wgpu::TextureFormat::eUndefined;
    };

    std::vector<PendingCapture> pending;
    std::vector<PreparedCapture> prepared;
    std::vector<CompletedCapture> completed;
    std::unordered_map<Entity, InFlightCapture> in_flight;
};

struct ScreenshotBlitHandles {
    assets::Handle<shader::Shader> vertex_shader;
    assets::Handle<shader::Shader> fragment_shader;
};

struct ScreenshotBlitPipeline {
    wgpu::TextureFormat format = wgpu::TextureFormat::eUndefined;
    wgpu::BindGroupLayout layout;
    wgpu::Sampler sampler;
    wgpu::Buffer vertex_buffer;
    CachedPipelineId pipeline_id;
};

struct ScreenshotBlitPipelines {
    std::vector<ScreenshotBlitPipeline> pipelines;
};

constexpr std::string_view kScreenshotVertexPath    = "screenshot/blit_vert.slang";
constexpr std::string_view kScreenshotVertexSlang   = R"slang(
struct VOut { float4 pos : SV_Position; [[vk::location(0)]] float2 uv; };
[shader("vertex")]
VOut screenshotVert([[vk::location(0)]] float2 uv : TEXCOORD0) {
    VOut o;
    o.uv = uv;
    o.pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}
)slang";
constexpr std::string_view kScreenshotFragmentPath  = "screenshot/blit_frag.slang";
constexpr std::string_view kScreenshotFragmentSlang = R"slang(
[[vk::binding(0, 0)]] SamplerState screenshot_sampler;
[[vk::binding(1, 0)]] Texture2D<float4> screenshot_texture;
struct VIn { [[vk::location(0)]] float2 uv; };
[shader("fragment")]
float4 screenshotFrag(VIn input) : SV_Target { return screenshot_texture.Sample(screenshot_sampler, input.uv); }
)slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return {reinterpret_cast<const std::byte*>(source.data()), source.size()};
}

/** @brief Main-world staging area for component requests awaiting extraction. */
struct ScreenshotRequests {
    std::vector<ScreenshotState::PendingCapture> pending;
};

/** @brief Render-to-main completion sender for component captures. */
struct ScreenshotDeliverySenders {
    async_channel::Sender<ScreenshotCaptured> component;
};

/** @brief Retains delivered request entities for exactly one main-world frame. */
struct ScreenshotCleanupState {
    std::vector<Entity> delivered_this_frame;
    std::vector<Entity> pending_cleanup;
};

static void collect_completed_readbacks(ResMut<ScreenshotState> state,
                                        ResMut<readback::GpuReadbacks> readbacks,
                                        ResMut<readback::GpuReadbackBufferPool> buffer_pool);

static void request_component_screenshots(
    Commands commands,
    Query<Item<Entity, const Screenshot&>, Without<Capturing>> screenshots,
    Query<Entity, With<::epix::window::PrimaryWindow, ::epix::window::Window>> primary_window,
    ResMut<ScreenshotRequests> requests) {
    std::unordered_set<::epix::camera::RenderTargetId, ::epix::camera::RenderTargetIdHash> seen_targets;
    const auto primary = primary_window.single();
    for (auto&& [entity, screenshot] : screenshots.iter()) {
        const auto target = screenshot.target.normalize(primary);
        if (!target.has_value()) {
            spdlog::warn(
                "[render.screenshot] Could not normalize component screenshot target; request remains pending");
            continue;
        }
        if (!seen_targets.insert(target->identity()).second) {
            spdlog::warn("[render.screenshot] Duplicate component screenshot target; removing request entity {}",
                         entity.index);
            commands.entity(entity).despawn();
            continue;
        }
        requests->pending.push_back(ScreenshotState::PendingCapture{.target = screenshot.target, .entity = entity});
        commands.entity(entity).insert(Capturing{});
    }
}

static void trigger_component_screenshots(Commands commands,
                                          Res<CapturedScreenshots> completed,
                                          ResMut<ScreenshotCleanupState> cleanup,
                                          ResMut<Events<ScreenshotCaptured>> events) {
    while (auto screenshot = completed->try_recv()) {
        commands.entity(screenshot->entity).insert(Captured{});
        cleanup->delivered_this_frame.push_back(screenshot->entity);
        events->push(std::move(*screenshot));
    }
}

static void clear_captured_screenshots(Commands commands, ResMut<ScreenshotCleanupState> cleanup) {
    for (Entity entity : cleanup->pending_cleanup) commands.entity(entity).despawn();
    cleanup->pending_cleanup = std::move(cleanup->delivered_this_frame);
    cleanup->delivered_this_frame.clear();
}

std::function<void(const ScreenshotCaptured&)> save_to_disk(std::filesystem::path path) {
    return [path = std::move(path)](const ScreenshotCaptured& captured) {
        if (auto result = image::Image::save(path, captured.image); !result) {
            spdlog::warn("[render.screenshot] Failed to save screenshot to '{}'", path.string());
        } else {
            spdlog::info("[render.screenshot] Screenshot saved to '{}'", path.string());
        }
    };
}

/** @brief ExtractSchedule system — delivers completed images and queues component requests. */
static void extract_captures_and_deliver(ResMut<ScreenshotState> state,
                                         Extract<ResMut<ScreenshotRequests>> component_captures,
                                         Res<ScreenshotDeliverySenders> delivery,
                                         ResMut<readback::GpuReadbacks> readbacks,
                                         ResMut<readback::GpuReadbackBufferPool> buffer_pool) {
    collect_completed_readbacks(state, readbacks, buffer_pool);
    for (auto& completed : state->completed) {
        auto& img = completed.image;
        if (completed.entity.has_value()) {
            if (!delivery->component.try_send(
                    ScreenshotCaptured{.entity = *completed.entity, .image = std::move(img)})) {
                spdlog::warn("[render.screenshot] Dropped screenshot because its delivery receiver closed");
            }
        }
    }
    state->completed.clear();

    for (auto& capture : component_captures->pending) state->pending.push_back(std::move(capture));
    component_captures->pending.clear();
}

struct CaptureFormatInfo {
    uint32_t bytes_per_pixel   = 0;
    image::Format image_format = image::Format::Unknown;
    bool bgra_swap             = false;
};

static void collect_completed_readbacks(ResMut<ScreenshotState> state,
                                        ResMut<readback::GpuReadbacks> readbacks,
                                        ResMut<readback::GpuReadbackBufferPool> buffer_pool);

static CaptureFormatInfo capture_format(wgpu::TextureFormat format) {
    switch (format) {
        case wgpu::TextureFormat::eRGBA8Unorm:
        case wgpu::TextureFormat::eRGBA8UnormSrgb:
            return {4, image::Format::RGBA8, false};
        case wgpu::TextureFormat::eBGRA8Unorm:
        case wgpu::TextureFormat::eBGRA8UnormSrgb:
            return {4, image::Format::RGBA8, true};
        case wgpu::TextureFormat::eR8Unorm:
            return {1, image::Format::Grey8, false};
        case wgpu::TextureFormat::eRG8Unorm:
            return {2, image::Format::GreyAlpha8, false};
        case wgpu::TextureFormat::eRGBA16Uint:
        case wgpu::TextureFormat::eRGBA16Sint:
        case wgpu::TextureFormat::eRGBA16Float:
            return {8, image::Format::RGBA16, false};
        case wgpu::TextureFormat::eRGBA32Float:
            return {16, image::Format::RGBA32F, false};
        default:
            return {};
    }
}

static std::optional<image::Image> image_from_readback(const ScreenshotState::InFlightCapture& capture,
                                                       std::span<const std::uint8_t> data) {
    const auto info = capture_format(capture.format);
    if (info.bytes_per_pixel == 0) return std::nullopt;
    const std::size_t packed_row  = static_cast<std::size_t>(capture.size.x) * info.bytes_per_pixel;
    const std::size_t aligned_row = readback::align_byte_size(static_cast<std::uint32_t>(packed_row));
    if (data.size() < aligned_row * capture.size.y) return std::nullopt;
    std::vector<std::byte> pixels(packed_row * capture.size.y);
    for (uint32_t y = 0; y < capture.size.y; ++y) {
        const auto* src = reinterpret_cast<const std::byte*>(data.data() + aligned_row * y);
        auto* dst       = pixels.data() + packed_row * y;
        if (info.bgra_swap) {
            for (uint32_t x = 0; x < capture.size.x; ++x) {
                dst[x * 4]     = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4];
                dst[x * 4 + 3] = src[x * 4 + 3];
            }
        } else {
            std::memcpy(dst, src, packed_row);
        }
    }
    auto result = image::Image::create2d(capture.size.x, capture.size.y, info.image_format, pixels);
    if (result) result->set_usage(image::ImageUsage::Main);
    return result;
}

static void collect_completed_readbacks(ResMut<ScreenshotState> state,
                                        ResMut<readback::GpuReadbacks> readbacks,
                                        ResMut<readback::GpuReadbackBufferPool> buffer_pool) {
    std::erase_if(readbacks->mapped, [&](readback::GpuReadback& readback) {
        auto pending = state->in_flight.find(readback.entity);
        if (pending == state->in_flight.end()) return false;
        auto result = readback.channel->try_recv();
        if (!result) return false;
        auto [entity, buffer, data] = std::move(*result);
        (void)entity;
        if (data) {
            if (auto image = image_from_readback(pending->second, *data)) {
                state->completed.push_back({.image = std::move(*image), .entity = pending->second.entity});
            } else {
                spdlog::warn("[render.screenshot] Readback data did not describe a supported screenshot image");
            }
        }
        buffer_pool->return_buffer(buffer);
        state->in_flight.erase(pending);
        return true;
    });
}

static ScreenshotBlitPipeline& prepare_blit_pipeline(wgpu::TextureFormat format,
                                                     const wgpu::Device& device,
                                                     const ScreenshotBlitHandles& handles,
                                                     PipelineServer& pipeline_server,
                                                     ScreenshotBlitPipelines& pipelines) {
    if (auto it = std::ranges::find(pipelines.pipelines, format, &ScreenshotBlitPipeline::format);
        it != pipelines.pipelines.end())
        return *it;
    ScreenshotBlitPipeline pipeline;
    pipeline.format = format;
    pipeline.layout = device.createBindGroupLayout(
        wgpu::BindGroupLayoutDescriptor()
            .setLabel("ScreenshotBlitLayout")
            .setEntries(std::array{
                wgpu::BindGroupLayoutEntry()
                    .setBinding(0)
                    .setVisibility(wgpu::ShaderStage::eFragment)
                    .setSampler(wgpu::SamplerBindingLayout().setType(wgpu::SamplerBindingType::eFiltering)),
                wgpu::BindGroupLayoutEntry()
                    .setBinding(1)
                    .setVisibility(wgpu::ShaderStage::eFragment)
                    .setTexture(wgpu::TextureBindingLayout()
                                    .setSampleType(wgpu::TextureSampleType::eFloat)
                                    .setViewDimension(wgpu::TextureViewDimension::e2D)),
            }));
    pipeline.sampler           = device.createSampler(wgpu::SamplerDescriptor()
                                                          .setLabel("ScreenshotBlitSampler")
                                                          .setAddressModeU(wgpu::AddressMode::eClampToEdge)
                                                          .setAddressModeV(wgpu::AddressMode::eClampToEdge)
                                                          .setAddressModeW(wgpu::AddressMode::eClampToEdge)
                                                          .setMinFilter(wgpu::FilterMode::eLinear)
                                                          .setMagFilter(wgpu::FilterMode::eLinear)
                                                          .setMaxAnisotropy(1));
    constexpr float vertices[] = {0.f, 0.f, 2.f, 0.f, 0.f, 2.f};
    pipeline.vertex_buffer =
        device.createBuffer(wgpu::BufferDescriptor()
                                .setLabel("ScreenshotBlitVertices")
                                .setSize(sizeof(vertices))
                                .setUsage(wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst));
    device.getQueue().writeBuffer(pipeline.vertex_buffer, 0, vertices, sizeof(vertices));
    VertexState vertex{.shader = handles.vertex_shader, .entry_point = std::string("screenshotVert")};
    vertex.buffers.push_back(
        wgpu::VertexBufferLayout()
            .setArrayStride(sizeof(float) * 2)
            .setStepMode(wgpu::VertexStepMode::eVertex)
            .setAttributes(std::array{
                wgpu::VertexAttribute().setShaderLocation(0).setOffset(0).setFormat(wgpu::VertexFormat::eFloat32x2),
            }));
    FragmentState fragment{.shader = handles.fragment_shader, .entry_point = std::string("screenshotFrag")};
    fragment.add_target(wgpu::ColorTargetState().setFormat(format).setWriteMask(wgpu::ColorWriteMask::eAll));
    pipeline.pipeline_id = pipeline_server.queue_render_pipeline(RenderPipelineDescriptor{
        .label       = "screenshot-to-screen",
        .layouts     = {pipeline.layout},
        .vertex      = std::move(vertex),
        .primitive   = wgpu::PrimitiveState()
                           .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                           .setCullMode(wgpu::CullMode::eNone),
        .multisample = wgpu::MultisampleState().setCount(1).setMask(~0u).setAlphaToCoverageEnabled(false),
        .fragment    = std::move(fragment),
    });
    pipelines.pipelines.push_back(std::move(pipeline));
    return pipelines.pipelines.back();
}

static void prepare_screenshots(ResMut<ScreenshotState> state,
                                Res<ExtractedWindows> windows,
                                Res<texture::ManualTextureViews> manual_texture_views,
                                Res<wgpu::Device> device,
                                Res<ScreenshotBlitHandles> handles,
                                ResMut<PipelineServer> pipeline_server,
                                ResMut<ScreenshotBlitPipelines> pipelines,
                                ResMut<view::ViewTargetAttachments> attachments) {
    state->prepared.clear();
    for (const auto& request : state->pending) {
        const auto target = request.target.normalize(windows->primary);
        if (!target || std::holds_alternative<::epix::camera::NoColorTarget>(*target)) {
            spdlog::warn("[render.screenshot] Could not resolve screenshot render target");
            continue;
        }
        std::optional<wgpu::TextureView> output_view;
        glm::uvec2 size{};
        wgpu::TextureFormat format = wgpu::TextureFormat::eUndefined;
        std::visit(epix::utils::visitor{
                       [&](const ::epix::camera::ImageRenderTarget& image) {
                           if (!image.texture) return;
                           output_view    = image.texture.createView();
                           size           = {image.texture.getWidth(), image.texture.getHeight()};
                           format         = image.texture.getFormat();
                       },
                       [&](const ::epix::window::NormalizedWindowRef& window) {
                           if (auto it = windows->windows.find(window.entity()); it != windows->windows.end()) {
                               const auto& extracted = it->second;
                               if (!extracted.swapchain_texture_view || !extracted.swapchain_texture.texture) return;
                               output_view    = extracted.swapchain_texture_view;
                               size           = {static_cast<uint32_t>(extracted.physical_width),
                                                 static_cast<uint32_t>(extracted.physical_height)};
                               format         = extracted.swapchain_texture_view_format;
                           }
                       },
                       [&](const ::epix::camera::ManualTextureViewHandle& handle) {
                           if (auto it = manual_texture_views->find(handle); it != manual_texture_views->end()) {
                               output_view = it->second.texture_view;
                               size        = it->second.size;
                               format      = it->second.view_format;
                           }
                       },
                       [&](const ::epix::camera::NoColorTarget&) {},
                   },
                   *target);
        if (!output_view || size.x == 0 || size.y == 0 || capture_format(format).bytes_per_pixel == 0) {
            spdlog::warn("[render.screenshot] Could not resolve a readable screenshot target");
            continue;
        }
        if (!request.entity) continue;
        const Entity readback_entity = *request.entity;
        // Bevy Screenshot always redirects the target to a temporary texture:
        // it is safe to COPY_SRC and works for ManualTextureView, whose source
        // texture is intentionally not part of the public camera API.
        auto temporary =
            device->createTexture(wgpu::TextureDescriptor()
                                      .setLabel("screenshot-capture-rendertarget")
                                      .setSize({size.x, size.y, 1})
                                      .setFormat(format)
                                      .setUsage(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eCopySrc |
                                                wgpu::TextureUsage::eTextureBinding)
                                      .setDimension(wgpu::TextureDimension::e2D)
                                      .setMipLevelCount(1)
                                      .setSampleCount(1));
        if (!temporary) continue;
        const auto temporary_view = temporary.createView();
        attachments->attachments.insert_or_assign(target->identity(),
                                                  view::OutputColorAttachment::create(temporary_view, format));
        prepare_blit_pipeline(format, *device, *handles, *pipeline_server, *pipelines);
        state->prepared.push_back(
            {readback_entity, request.entity, std::move(temporary), *output_view, size, format, true});
    }
    state->pending.clear();
}

static void submit_screenshot_commands(World& world, wgpu::CommandEncoder& encoder) {
    auto& state = world.resource_mut<ScreenshotState>();
    if (state.prepared.empty()) return;
    const auto& device          = world.resource<wgpu::Device>();
    const auto& pipelines       = world.resource<ScreenshotBlitPipelines>();
    const auto& pipeline_server = world.resource<PipelineServer>();
    auto& readbacks             = world.resource_mut<readback::GpuReadbacks>();
    auto& pool                  = world.resource_mut<readback::GpuReadbackBufferPool>();
    for (auto& capture : state.prepared) {
        if (capture.restore_output) {
            const auto pipeline =
                std::ranges::find(pipelines.pipelines, capture.format, &ScreenshotBlitPipeline::format);
            if (pipeline != pipelines.pipelines.end())
                if (auto ready = pipeline_server.get_render_pipeline(pipeline->pipeline_id)) {
                    const auto source_view = capture.texture.createView();
                    const auto bind_group  = device.createBindGroup(
                        wgpu::BindGroupDescriptor()
                            .setLabel("ScreenshotBlitBindGroup")
                            .setLayout(pipeline->layout)
                            .setEntries(std::array{
                                wgpu::BindGroupEntry().setBinding(0).setSampler(pipeline->sampler),
                                wgpu::BindGroupEntry().setBinding(1).setTextureView(source_view),
                            }));
                    auto pass = encoder.beginRenderPass(
                        wgpu::RenderPassDescriptor()
                            .setLabel("screenshot-to-screen")
                            .setColorAttachments(std::array{wgpu::RenderPassColorAttachment()
                                                                .setView(capture.output_view)
                                                                .setLoadOp(wgpu::LoadOp::eLoad)
                                                                .setStoreOp(wgpu::StoreOp::eStore)
                                                                .setDepthSlice(~0u)}));
                    pass.setPipeline(ready->get().pipeline());
                    pass.setVertexBuffer(0, pipeline->vertex_buffer, 0, sizeof(float) * 6);
                    pass.setBindGroup(0, bind_group, std::span<const uint32_t>{});
                    pass.draw(3, 1, 0, 0);
                    pass.end();
                }
        }
        const auto info = capture_format(capture.format);
        const wgpu::Extent3D extent(capture.size.x, capture.size.y, 1);
        auto buffer = pool.get(device, readback::get_aligned_size(extent, info.bytes_per_pixel));
        readbacks.requested.push_back(readback::GpuReadback{
            capture.readback_entity,
            readback::ReadbackSource{readback::ReadbackSource::Texture{
                capture.texture,
                wgpu::TexelCopyBufferLayout()
                    .setBytesPerRow(readback::align_byte_size(capture.size.x * info.bytes_per_pixel))
                    .setRowsPerImage(capture.size.y),
                extent}},
            buffer,
            std::make_shared<readback::ReadbackChannel>(),
        });
        state.in_flight.insert_or_assign(
            capture.readback_entity, ScreenshotState::InFlightCapture{capture.entity, capture.size, capture.format});
    }
    state.prepared.clear();
}

void ScreenshotPlugin::attach(epix::app::App& app) {
    app.add_event<ScreenshotCaptured>();
    app.world_mut().insert_resource(ScreenshotRequests{});
    app.world_mut().insert_resource(ScreenshotCleanupState{});

    auto [component_sender, component_receiver] = async_channel::unbounded<ScreenshotCaptured>();
    app.world_mut().insert_resource(CapturedScreenshots{std::move(component_receiver)});

    auto& render_app = app.sub_app_mut(Render);
    render_app.world_mut().insert_resource(ScreenshotState{});
    render_app.world_mut().init_resource<ScreenshotBlitPipelines>();
    render_app.world_mut().insert_resource(ScreenshotDeliverySenders{std::move(component_sender)});

    if (auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>();
        auto server   = app.world_mut().get_resource<assets::AssetServer>()) {
        registry->get().insert_asset_static(kScreenshotVertexPath, shader_bytes(kScreenshotVertexSlang));
        registry->get().insert_asset_static(kScreenshotFragmentPath, shader_bytes(kScreenshotFragmentSlang));
        render_app.world_mut().insert_resource(ScreenshotBlitHandles{
            .vertex_shader   = server->get().load<shader::Shader>("embedded://screenshot/blit_vert.slang"),
            .fragment_shader = server->get().load<shader::Shader>("embedded://screenshot/blit_frag.slang"),
        });
    } else return;

    render_app.add_systems(
        ExtractSchedule,
        into(extract_captures_and_deliver).before(sync_readbacks).set_name("screenshot: extract & deliver"));

    render_app.add_systems(Render, into(prepare_screenshots)
                                       .after(window::prepare_windows)
                                       .after(view::prepare_view_attachments)
                                       .before(view::prepare_view_target)
                                       .in_set(RenderSystems::ManageViews)
                                       .set_name("screenshot: prepare targets"));
    render_app.world_mut().resource_mut<graph::RenderGraphFinalizers>().callbacks.push_back(submit_screenshot_commands);

    app.add_systems(PreUpdate,
                    into(request_component_screenshots, trigger_component_screenshots)
                        .set_names(std::array{"screenshot: component request", "screenshot: component delivery"}));
    app.add_systems(Last, into(clear_captured_screenshots).set_name("screenshot: cleanup captured requests"));
}
}  // namespace epix::render::screenshot
