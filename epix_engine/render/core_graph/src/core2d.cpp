
#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <epix/core_graph.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <webgpu/webgpu.hpp>
using namespace epix::render;
using namespace epix::core_graph::core_2d;
using namespace epix::ecs;
using namespace epix::app;

namespace {
void extract_core2d_camera_phases(
    ::epix::ecs::ResMut<::epix::render::phase::ViewSortedRenderPhases<::epix::core_graph::core_2d::Transparent2D>>
        transparent_phases,
    ::epix::ecs::ResMut<::epix::render::phase::ViewSortedRenderPhases<::epix::core_graph::core_2d::Opaque2D>>
        opaque_phases,
    ::epix::ecs::ResMut<::epix::render::phase::ViewSortedRenderPhases<::epix::core_graph::core_2d::UI2DItem>> ui_phases,
    ::epix::app::Extract<::epix::ecs::Query<::epix::ecs::Item<::epix::ecs::Entity,
                                                               const ::epix::camera::Camera&,
                                                               const ::epix::render::camera::CameraRenderGraph&>,
                                            ::epix::ecs::With<::epix::camera::Camera2d>>>
                cameras) {
    std::unordered_set<::epix::render::view::RetainedViewEntity> live_views;
    for (auto&& [entity, camera, graph] : cameras.iter()) {
        if (!camera.is_active ||
            graph != ::epix::render::graph::GraphLabel(::epix::core_graph::core_2d::Core2d))
            continue;
        const auto retained_view = ::epix::render::view::RetainedViewEntity::create(
            ::epix::render::sync_world::MainEntity{entity}, std::nullopt, 0);
        transparent_phases->insert_or_clear(retained_view);
        opaque_phases->insert_or_clear(retained_view);
        ui_phases->insert_or_clear(retained_view);
        live_views.insert(retained_view);
    }
    const auto remove_dead_views = [&live_views](const auto& entry) { return !live_views.contains(entry.first); };
    std::erase_if(*transparent_phases, remove_dead_views);
    std::erase_if(*opaque_phases, remove_dead_views);
    std::erase_if(*ui_phases, remove_dead_views);
}

constexpr std::string_view kBlitFragmentPath  = "core2d/blit_frag.slang";
constexpr std::string_view kBlitFragmentSlang = R"slang(
[[vk::binding(0, 0)]] SamplerState blit_sampler;
[[vk::binding(1, 0)]] Texture2D<float4> main_tex;
struct VIn { [[vk::location(0)]] float2 uv; };
[shader("fragment")]
float4 blitFrag(VIn input) : SV_Target {
    return main_tex.Sample(blit_sampler, input.uv);
}
)slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(source.data()), source.size());
}

/** @brief Standard alpha blending (Bevy BlendState::ALPHA_BLENDING), used when
 * a later camera composites over an earlier one on the same target. */
wgpu::BlendState alpha_blend_state() noexcept {
    return wgpu::BlendState()
        .setColor(wgpu::BlendComponent()
                      .setOperation(wgpu::BlendOperation::eAdd)
                      .setSrcFactor(wgpu::BlendFactor::eSrcAlpha)
                      .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha))
        .setAlpha(wgpu::BlendComponent()
                      .setOperation(wgpu::BlendOperation::eAdd)
                      .setSrcFactor(wgpu::BlendFactor::eOne)
                      .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha));
}

bool same_blend_component(const wgpu::BlendComponent& lhs, const wgpu::BlendComponent& rhs) noexcept {
    return lhs.operation == rhs.operation && lhs.srcFactor == rhs.srcFactor && lhs.dstFactor == rhs.dstFactor;
}

bool same_blend_state(const std::optional<wgpu::BlendState>& lhs, const std::optional<wgpu::BlendState>& rhs) noexcept {
    if (lhs.has_value() != rhs.has_value()) return false;
    return !lhs || (same_blend_component(lhs->color, rhs->color) && same_blend_component(lhs->alpha, rhs->alpha));
}

std::optional<wgpu::BlendState> output_blend_for(const epix::render::camera::ExtractedCamera& camera) {
    const auto* write = std::get_if<epix::camera::CameraOutputMode::Write>(&camera.output_mode);
    if (!write) return std::nullopt;
    if (write->blend_state) return write->blend_state;
    if (camera.sorted_camera_index_for_target > 0) return alpha_blend_state();
    return std::nullopt;
}
}  // namespace

void Core2dGraph::add_to(graph::RenderGraph& g) {
    spdlog::debug("[render.core_graph] Adding Core2D sub-graph to render graph.");
    graph::RenderGraph g2d;
    g2d.add_node(Core2dNodes::StartMainPass, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::MainTransparentPass, Node2D<Transparent2D>{});
    g2d.add_node(Core2dNodes::MainOpaquePass, Node2D<Opaque2D>{});
    g2d.add_node(Core2dNodes::EndMainPass, graph::EmptyNode{});
    g2d.add_node(Core2dNodes::ScreenUIPass, Node2D<UI2DItem>{});
    g2d.add_node(Core2dNodes::BlitToOutput, Core2dBlitNode{});
    g2d.add_node_edges(Core2dNodes::StartMainPass, Core2dNodes::MainOpaquePass, Core2dNodes::MainTransparentPass,
                       Core2dNodes::EndMainPass, Core2dNodes::ScreenUIPass, Core2dNodes::BlitToOutput);
    g.add_sub_graph(Core2d, std::move(g2d));
}

void Core2dBlitNode::update(World& world) {
    if (!views) {
        views = world.try_query<Item<const render::camera::ExtractedCamera&, const view::ViewTarget&>>();
    } else {
        views->update_archetypes(world);
    }
}

void queue_core2d_blit_pipelines(
    Query<Item<const epix::render::camera::ExtractedCamera&, const epix::render::view::ViewTarget&>> views,
    Res<wgpu::Device> device,
    Res<epix::core_graph::FullscreenShader> fullscreen_shader,
    Res<Core2dBlitHandles> handles,
    ResMut<PipelineServer> pipeline_server,
    ResMut<Core2dBlitPipelines> pipelines) {
    for (auto&& [camera, target] : views.iter()) {
        if (std::holds_alternative<::epix::camera::CameraOutputMode::Skip>(camera.output_mode) || !target.out_texture())
            continue;
        const auto output_blend = output_blend_for(camera);
        const auto existing     = std::ranges::find_if(pipelines->pipelines, [&](const Core2dBlitPipeline& pipeline) {
            return pipeline.format == target.out_texture_view_format() &&
                   same_blend_state(pipeline.output_blend, output_blend);
        });
        if (existing != pipelines->pipelines.end()) continue;

        Core2dBlitPipeline built;
        built.layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("Core2dBlitLayout")
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
        built.sampler                 = device->createSampler(wgpu::SamplerDescriptor()
                                                                  .setLabel("Core2dBlitSampler")
                                                                  .setAddressModeU(wgpu::AddressMode::eClampToEdge)
                                                                  .setAddressModeV(wgpu::AddressMode::eClampToEdge)
                                                                  .setAddressModeW(wgpu::AddressMode::eClampToEdge)
                                                                  .setMinFilter(wgpu::FilterMode::eLinear)
                                                                  .setMagFilter(wgpu::FilterMode::eLinear)
                                                                  .setMipmapFilter(wgpu::MipmapFilterMode::eLinear)
                                                                  .setLodMinClamp(0.0f)
                                                                  .setLodMaxClamp(32.0f)
                                                                  .setMaxAnisotropy(1));
        epix::render::VertexState vs = fullscreen_shader->to_vertex_state();
        epix::render::FragmentState fs{.shader = handles->fragment_shader, .entry_point = std::string("blitFrag")};
        wgpu::ColorTargetState color_target;
        color_target.setFormat(target.out_texture_view_format()).setWriteMask(wgpu::ColorWriteMask::eAll);
        if (output_blend) color_target.setBlend(*output_blend);
        fs.add_target(color_target);
        built.pipeline_id  = pipeline_server->queue_render_pipeline(epix::render::RenderPipelineDescriptor{
            .label       = "core2d-blit",
            .layouts     = {built.layout},
            .vertex      = std::move(vs),
            .primitive   = wgpu::PrimitiveState()
                               .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                               .setCullMode(wgpu::CullMode::eNone),
            .multisample = wgpu::MultisampleState().setCount(1).setMask(~0u).setAlphaToCoverageEnabled(false),
            .fragment    = std::move(fs),
        });
        built.format       = target.out_texture_view_format();
        built.output_blend = output_blend;
        pipelines->pipelines.push_back(std::move(built));
    }
}

std::expected<void, graph::NodeRunError> Core2dBlitNode::run(graph::GraphContext& ctx,
                                                             graph::RenderContext& render_ctx,
                                                             const World& world) {
    if (!views) return {};
    auto view_opt =
        views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(ctx.view_entity());
    if (!view_opt) return {};
    auto&& [camera, target] = *view_opt;
    if (!target.out_texture() || std::holds_alternative<::epix::camera::CameraOutputMode::Skip>(camera.output_mode))
        return {};

    auto pipelines = world.get_resource<Core2dBlitPipelines>();
    if (!pipelines) return {};
    const auto output_blend = output_blend_for(camera);
    const auto blit = std::ranges::find_if(pipelines->get().pipelines, [&](const Core2dBlitPipeline& candidate) {
        return candidate.format == target.out_texture_view_format() &&
               same_blend_state(candidate.output_blend, output_blend);
    });
    if (blit == pipelines->get().pipelines.end()) return {};
    const auto& ps = world.resource<PipelineServer>();
    auto pipeline  = ps.get_render_pipeline(blit->pipeline_id);
    if (!pipeline) return {};
    auto device = render_ctx.device();

    // get_attachment marks the output as written -> needs_present -> present.
    std::optional<glm::vec4> clear_color;
    const auto& output = std::get<::epix::camera::CameraOutputMode::Write>(camera.output_mode);
    if (const auto* custom = std::get_if<::epix::camera::ClearColorConfig::Custom>(&output.clear_color)) {
        clear_color = custom->color.to_vec4();
    } else if (!std::holds_alternative<::epix::camera::ClearColorConfig::None>(output.clear_color)) {
        if (auto global = world.get_resource<::epix::camera::ClearColor>()) clear_color = global->get().to_vec4();
    }
    auto bind_group =
        device.createBindGroup(wgpu::BindGroupDescriptor()
                                   .setLabel("Core2dBlitBG")
                                   .setLayout(blit->layout)
                                   .setEntries(std::array{
                                       wgpu::BindGroupEntry().setBinding(0).setSampler(blit->sampler),
                                       wgpu::BindGroupEntry().setBinding(1).setTextureView(target.main_texture_view()),
                                   }));
    auto render_pass = render_ctx.command_encoder().beginRenderPass(
        wgpu::RenderPassDescriptor().setColorAttachments(std::array{target.out_texture_color_attachment(clear_color)}));
    // Bevy upscaling node set_scissor_rect: clip the blit to the camera viewport.
    if (camera.viewport) {
        const auto& vp = *camera.viewport;
        render_pass.setScissorRect(vp.physical_position.x, vp.physical_position.y, vp.physical_size.x,
                                   vp.physical_size.y);
    }
    render_pass.setPipeline(pipeline->get().pipeline());
    render_pass.setBindGroup(0, bind_group, std::span<const uint32_t>{});
    render_pass.draw(3, 1, 0, 0);
    render_pass.end();
    render_ctx.flush_encoder();
    return {};
}

void Core2dPlugin::attach(App& app) {
    // `Camera2d` belongs to the camera module. Core2d only supplies the
    // render-graph requirement, as Bevy's Core2dPlugin does.
    app.world_mut().register_required_components_with<::epix::camera::Camera2d>(
        [] { return render::camera::CameraRenderGraph{Core2d}; });
    // Register the Core2D-specific output fragment shader. Its handle lives
    // in the render world because queued output pipelines consume it there.
    std::optional<Core2dBlitHandles> blit_handles;
    if (auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>();
        auto server   = app.world_mut().get_resource<assets::AssetServer>()) {
        registry->get().insert_asset_static(kBlitFragmentPath, shader_bytes(kBlitFragmentSlang));
        blit_handles = Core2dBlitHandles{
            .fragment_shader = server->get().load<shader::Shader>("embedded://core2d/blit_frag.slang"),
        };
    } else {
        spdlog::warn("[core_graph] EmbeddedAssetRegistry or AssetServer not available; 2D output blit disabled.");
    }

    app.get_sub_app_mut(render::Render).and_then([&](App& render_app) {
        render_app.world_mut().insert_resource(phase::DrawFunctions<Transparent2D>{});
        render_app.world_mut().insert_resource(phase::DrawFunctions<Opaque2D>{});
        render_app.world_mut().insert_resource(phase::DrawFunctions<UI2DItem>{});
        render_app.world_mut().init_resource<phase::ViewSortedRenderPhases<Transparent2D>>();
        render_app.world_mut().init_resource<phase::ViewSortedRenderPhases<Opaque2D>>();
        render_app.world_mut().init_resource<phase::ViewSortedRenderPhases<UI2DItem>>();
        render_app.world_mut().init_resource<Core2dBlitPipelines>();
        if (blit_handles) {
            render_app.world_mut().insert_resource(std::move(*blit_handles));
        }
        Core2d.add_to(render_app.resource_mut<graph::RenderGraph>());

        render_app.add_systems(Render, into(phase::sort_phase_system<Transparent2D>, phase::sort_phase_system<UI2DItem>,
                                            phase::sort_phase_system<Opaque2D>)
                                           .in_set(RenderSystems::PhaseSort)
                                           .set_names(std::array{"sort transparent 2d phase", "sort ui 2d phase",
                                                                 "sort opaque 2d phase"}));
        render_app.add_systems(
            Render,
            into(queue_core2d_blit_pipelines).in_set(RenderSystems::Queue).set_name("queue core2d blit pipelines"));
        render_app.add_systems(ExtractSchedule,
                               into(extract_core2d_camera_phases).set_name("extract core 2d camera phases"));
        return std::make_optional(std::ref(render_app));
    });
}
