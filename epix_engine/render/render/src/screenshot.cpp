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
        CachedPipelineId pipeline_id;
        // Bevy caches both of these in `ScreenshotPreparedState` when the capture
        // is prepared (`screenshot.rs:108-114`, `:356-400`): the transfer buffer is
        // a dedicated `screenshot-transfer-buffer`, not one taken from the readback
        // pool at submit, and the bind group is built once rather than per submit.
        wgpu::Buffer buffer;
        wgpu::BindGroup bind_group;
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

/** @brief Screenshot-to-screen blit resource (Bevy `ScreenshotToScreenPipeline`,
 * screenshot.rs:439-443): the bind-group layout descriptor and the blit shader,
 * inserted once by `init_screenshot_to_screen_pipeline` (:445-460). */
struct ScreenshotToScreenPipeline {
    /** @brief Specialization key: the target's texture format (Bevy
     * `type Key = TextureFormat`, :463). */
    using Key = wgpu::TextureFormat;

    BindGroupLayoutDescriptor bind_group_layout;
    assets::Handle<shader::Shader> shader;

    /** @brief Specialized blit pipeline for `key` (Bevy
     * `SpecializedRenderPipeline::specialize`, screenshot.rs:462-490): the
     * triangle comes from the vertex index, the bind-group layout has the
     * texture alone (the fragment uses `textureLoad`, so there is no sampler)
     * and the primitive culls back faces. */
    RenderPipelineDescriptor specialize(Key key) const {
        VertexState vertex{.shader = shader, .entry_point = std::string("screenshotVert")};
        FragmentState fragment{.shader = shader, .entry_point = std::string("screenshotFrag")};
        fragment.add_target(wgpu::ColorTargetState().setFormat(key).setWriteMask(wgpu::ColorWriteMask::eAll));
        return RenderPipelineDescriptor{
            .label       = "screenshot-to-screen",
            .layouts     = {bind_group_layout},
            .vertex      = std::move(vertex),
            .primitive   = wgpu::PrimitiveState()
                               .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                               .setCullMode(wgpu::CullMode::eBack),
            .multisample = wgpu::MultisampleState().setCount(1).setMask(~0u).setAlphaToCoverageEnabled(false),
            .fragment    = std::move(fragment),
        };
    }
};

constexpr std::string_view kScreenshotBlitPath  = "screenshot/blit.slang";
constexpr std::string_view kScreenshotBlitSlang = R"slang(
struct VOut { float4 pos : SV_Position; };
// Bevy screenshot.wgsl:3-8: one triangle covering the screen, built from the
// vertex index alone, so the pipeline needs no vertex buffer. Slang must name
// the *Vulkan* builtin here: the HLSL `SV_VertexID` reads BaseVertex and makes
// Slang declare `OpCapability DrawParameters`, which naga (wgpu's SPIR-V
// validator) rejects, while `SV_VulkanVertexID` emits `BuiltIn VertexIndex`
// with the core `Shader` capability only.
[shader("vertex")]
VOut screenshotVert(uint vertex_index : SV_VulkanVertexID) {
    VOut o;
    const float x = float((vertex_index & 1u) << 2u);
    const float y = float((vertex_index & 2u) << 1u);
    o.pos = float4(x - 1.0f, y - 1.0f, 0.0f, 1.0f);
    return o;
}
[[vk::binding(0, 0)]] Texture2D<float4> screenshot_texture;
// Bevy screenshot.wgsl:12-16: a texel fetch, not a filtered sample, so the
// bind-group layout carries the texture alone (no sampler).
[shader("fragment")]
float4 screenshotFrag(float4 position : SV_Position) : SV_Target {
    const int2 coords = int2(floor(position.xy));
    return screenshot_texture.Load(int3(coords, 0));
}
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
                                          ResMut<Events<ScreenshotCaptured>> events) {
    while (auto screenshot = completed->try_recv()) {
        commands.entity(screenshot->entity).insert(Captured{});
        events->push(std::move(*screenshot));
    }
}

/** @brief Despawn the request entities whose screenshot has been delivered
 * (Bevy `clear_screenshots`, screenshot.rs:192-196). Bevy schedules it in
 * `First`, after the message update and before `ApplyDeferred` (:410-415). */
static void clear_screenshots(Commands commands, Query<Item<Entity>, Filter<With<Captured>>> captured) {
    for (auto&& [entity] : captured.iter()) commands.entity(entity).despawn();
}

std::function<void(const ScreenshotCaptured&)> save_to_disk(std::filesystem::path path) {
    return [path = std::move(path)](const ScreenshotCaptured& captured) {
        // Bevy saves the RGB conversion, not the captured RGBA image:
        // `try_into_dynamic().to_rgb8().save_with_format(...)`
        // (`bevy_render/src/view/window/screenshot.rs:129-189`).
        if (auto result = image::Image::save_rgb8(path, captured.image); !result) {
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

static void collect_completed_readbacks(ResMut<ScreenshotState> state,
                                        ResMut<readback::GpuReadbacks> readbacks,
                                        ResMut<readback::GpuReadbackBufferPool> buffer_pool);

static std::optional<image::Image> image_from_readback(const ScreenshotState::InFlightCapture& capture,
                                                       std::span<const std::uint8_t> data) {
    // Bevy builds the image straight from the readback buffer, keeping the
    // capture target's own format (`Image::new(extent, D2, result,
    // texture_format, RenderAssetUsages::RENDER_WORLD)`, screenshot.rs:677-687);
    // readers convert it later (`try_into_dynamic`).
    const std::uint32_t bytes_per_pixel = readback::texture_format_pixel_size(capture.format);
    if (bytes_per_pixel == 0) return std::nullopt;
    const std::size_t packed_row  = static_cast<std::size_t>(capture.size.x) * bytes_per_pixel;
    const std::size_t aligned_row = readback::align_byte_size(static_cast<std::uint32_t>(packed_row));
    if (data.size() < aligned_row * capture.size.y) return std::nullopt;
    std::vector<std::byte> pixels(packed_row * capture.size.y);
    for (uint32_t y = 0; y < capture.size.y; ++y) {
        const auto* src = reinterpret_cast<const std::byte*>(data.data() + aligned_row * y);
        std::memcpy(pixels.data() + packed_row * y, src, packed_row);
    }
    auto result = image::Image::create2d(capture.size.x, capture.size.y, capture.format, pixels);
    if (result) result->set_usage(assets::RenderAssetUsages::RENDER_WORLD);
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
        // The capture's transfer buffer is its own (Bevy allocates it per
        // screenshot and drops it with the readback, screenshot.rs:377-382), so only
        // a pooled one goes back.
        if (readback.pooled_buffer) buffer_pool->return_buffer(buffer);
        state->in_flight.erase(pending);
        return true;
    });
}

static void prepare_screenshots(ResMut<ScreenshotState> state,
                                Res<ExtractedWindows> windows,
                                Res<texture::ManualTextureViews> manual_texture_views,
                                Res<wgpu::Device> device,
                                Res<ScreenshotToScreenPipeline> blit_pipeline,
                                ResMut<SpecializedRenderPipelines<ScreenshotToScreenPipeline>> blit_pipelines,
                                Res<PipelineServer> pipeline_server,
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
        std::visit(epix::utils::visitor{
                       [&](const ::epix::camera::ImageRenderTarget& image) {
                           if (!image.texture) return;
                           output_view = image.texture.createView();
                           size        = {image.texture.getWidth(), image.texture.getHeight()};
                       },
                       [&](const ::epix::window::NormalizedWindowRef& window) {
                           if (auto it = windows->windows.find(window.entity()); it != windows->windows.end()) {
                               const auto& extracted = it->second;
                               if (!extracted.swapchain_texture_view || !extracted.swapchain_texture.texture) return;
                               output_view = extracted.swapchain_texture_view;
                               // The size the surface was configured at, which is what the
                               // swapchain texture was created with - Bevy reads the same thing
                               // from the window's surface data (`configuration.width/height`,
                               // screenshot.rs:283-288). The window's own physical size is a
                               // frame ahead or behind that on a resize.
                               size        = {extracted.swapchain_texture.texture.getWidth(),
                                              extracted.swapchain_texture.texture.getHeight()};
                           }
                       },
                       [&](const ::epix::camera::ManualTextureViewHandle& handle) {
                           if (auto it = manual_texture_views->find(handle); it != manual_texture_views->end()) {
                               output_view = it->second.texture_view;
                               size        = it->second.size;
                           }
                       },
                       [&](const ::epix::camera::NoColorTarget&) {},
                   },
                   *target);
        // The target's view format, read the way every other pass reads it. Bevy
        // prefers the target's `texture_view_format` over the texture's own format
        // (`screenshot.rs:308-310`), and this accessor is where the port keeps that
        // preference - for windows the sRGB-suffixed swapchain view format, for
        // manual views their own format. An `ImageRenderTarget` answers with the
        // texture's format, which is **CGR-748**'s accepted divergence: the port's
        // targets carry a direct `wgpu::Texture` and no `Handle<Image>` to look a
        // `GpuImage::texture_view_format` up in.
        wgpu::TextureFormat format = wgpu::TextureFormat::eUndefined;
        if (const auto view_format = ::epix::render::camera::NormalizedRenderTargetExt::get_texture_view_format(
                *target, *windows, *manual_texture_views)) {
            format = *view_format;
        }
        if (!output_view || size.x == 0 || size.y == 0 || readback::texture_format_pixel_size(format) == 0) {
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
        const CachedPipelineId pipeline_id = blit_pipelines->specialize(*pipeline_server, *blit_pipeline, format);
        // Bevy allocates the transfer buffer here, per screenshot and dedicated to
        // it (`screenshot-transfer-buffer`, screenshot.rs:377-382), and caches it
        // together with the bind group the blit needs, so submission only records
        // the copy.
        const std::uint32_t bytes_per_pixel = readback::texture_format_pixel_size(format);
        const wgpu::Extent3D extent(size.x, size.y, 1);
        auto transfer_buffer = device->createBuffer(wgpu::BufferDescriptor()
                                                        .setLabel("screenshot-transfer-buffer")
                                                        .setSize(readback::get_aligned_size(extent, bytes_per_pixel))
                                                        .setUsage(wgpu::BufferUsage::eCopyDst |
                                                                  wgpu::BufferUsage::eMapRead));
        if (!transfer_buffer) continue;
        const auto blit_bind_group = device->createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("screenshot-to-screen-bind-group")
                .setLayout(pipeline_server->get_bind_group_layout(blit_pipeline->bind_group_layout))
                .setEntries(std::array{
                    wgpu::BindGroupEntry().setBinding(0).setTextureView(temporary_view),
                }));
        state->prepared.push_back({readback_entity,
                                   request.entity,
                                   std::move(temporary),
                                   *output_view,
                                   size,
                                   format,
                                   true,
                                   pipeline_id,
                                   std::move(transfer_buffer),
                                   std::move(blit_bind_group)});
    }
    state->pending.clear();
}

static void submit_screenshot_commands(World& world, wgpu::CommandEncoder& encoder) {
    auto& state = world.resource_mut<ScreenshotState>();
    if (state.prepared.empty()) return;
    const auto& pipeline_server = world.resource<PipelineServer>();
    auto& readbacks             = world.resource_mut<readback::GpuReadbacks>();
    for (auto& capture : state.prepared) {
        if (capture.restore_output) {
            if (auto ready = pipeline_server.get_render_pipeline(capture.pipeline_id)) {
                auto pass = encoder.beginRenderPass(
                    wgpu::RenderPassDescriptor()
                        .setLabel("screenshot-to-screen")
                        .setColorAttachments(std::array{wgpu::RenderPassColorAttachment()
                                                            .setView(capture.output_view)
                                                            .setLoadOp(wgpu::LoadOp::eLoad)
                                                            .setStoreOp(wgpu::StoreOp::eStore)
                                                            .setDepthSlice(~0u)}));
                pass.setPipeline(ready->get().pipeline());
                pass.setBindGroup(0, capture.bind_group, std::span<const uint32_t>{});
                pass.draw(3, 1, 0, 0);
                pass.end();
            }
        }
        const std::uint32_t bytes_per_pixel = readback::texture_format_pixel_size(capture.format);
        const wgpu::Extent3D extent(capture.size.x, capture.size.y, 1);
        readbacks.requested.push_back(readback::GpuReadback{
            capture.readback_entity,
            readback::ReadbackSource{readback::ReadbackSource::Texture{
                capture.texture,
                wgpu::TexelCopyBufferLayout()
                    .setBytesPerRow(readback::align_byte_size(capture.size.x * bytes_per_pixel))
                    .setRowsPerImage(capture.size.y),
                extent}},
            capture.buffer,
            std::make_shared<readback::ReadbackChannel>(),
            false,
        });
        state.in_flight.insert_or_assign(
            capture.readback_entity, ScreenshotState::InFlightCapture{capture.entity, capture.size, capture.format});
    }
    state.prepared.clear();
}

/** @brief `RenderStartup` system creating the blit resource (Bevy
 * `init_screenshot_to_screen_pipeline`, screenshot.rs:445-460): a single
 * fragment-visible, non-filterable float texture binding, and the embedded blit
 * shader. */
static void init_screenshot_to_screen_pipeline(Commands commands, Res<assets::AssetServer> server) {
    commands.insert_resource(ScreenshotToScreenPipeline{
        .bind_group_layout =
            BindGroupLayoutDescriptor()
                .set_label("screenshot-to-screen-bgl")
                .set_entries(std::array{
                    wgpu::BindGroupLayoutEntry()
                        .setBinding(0)
                        .setVisibility(wgpu::ShaderStage::eFragment)
                        .setTexture(wgpu::TextureBindingLayout()
                                        .setSampleType(wgpu::TextureSampleType::eUnfilterableFloat)
                                        .setViewDimension(wgpu::TextureViewDimension::e2D)),
                }),
        .shader = server->load<shader::Shader>(std::string("embedded://") + std::string(kScreenshotBlitPath)),
    });
}

void ScreenshotPlugin::attach(epix::app::App& app) {
    app.add_event<ScreenshotCaptured>();
    app.world_mut().insert_resource(ScreenshotRequests{});

    auto [component_sender, component_receiver] = async_channel::unbounded<ScreenshotCaptured>();
    app.world_mut().insert_resource(CapturedScreenshots{std::move(component_receiver)});

    auto& render_app = app.sub_app_mut(Render);
    render_app.world_mut().insert_resource(ScreenshotState{});
    render_app.world_mut().init_resource<SpecializedRenderPipelines<ScreenshotToScreenPipeline>>();
    render_app.world_mut().insert_resource(ScreenshotDeliverySenders{std::move(component_sender)});

    if (auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>()) {
        registry->get().insert_asset_static(kScreenshotBlitPath, shader_bytes(kScreenshotBlitSlang));
    } else
        return;

    render_app.add_systems(RenderStartup,
                           into(init_screenshot_to_screen_pipeline).set_name("screenshot: init to screen pipeline"));

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

    // The request half stages component requests for extraction, which Bevy does
    // inside the extract system itself; delivery is Bevy's `trigger_screenshots`,
    // which runs in `Update` (`bevy_render/src/view/window/screenshot.rs:416`),
    // not before it.
    app.add_systems(PreUpdate, into(request_component_screenshots).set_name("screenshot: component request"));
    app.add_systems(Update, into(trigger_component_screenshots).set_name("screenshot: component delivery"));
    app.add_systems(First, into(clear_screenshots).set_name("screenshot: clear captured requests"));
}
}  // namespace epix::render::screenshot
