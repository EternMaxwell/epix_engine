
#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <epix/core_graph.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <webgpu/webgpu.hpp>
using namespace epix::render;
using namespace epix::core_graph::core_2d;
using namespace epix::ecs;
using namespace epix::app;

namespace {
// Slang blit shaders: sample the view's main texture and write it to the
// output attachment with a fullscreen triangle (Bevy core_pipeline blit).
constexpr std::string_view kBlitVertexPath  = "core2d/blit_vert.slang";
constexpr std::string_view kBlitVertexSlang = R"slang(
struct VOut {
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 uv;
};
// Bevy fullscreen_vertex_shader/fullscreen.wgsl:30-31. wgpu clip space is
// Y-up, so a UV of (0,0) is the TOP-LEFT of the screen: the position mapping
// flips V (uv.y * -2 + 1) so texture row 0 (v = 0) is presented at the top.
[shader("vertex")]
VOut blitVert([[vk::location(0)]] float2 uv : TEXCOORD0) {
    VOut o;
    o.uv  = uv;
    o.pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}
)slang";

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
    if (camera.output_mode.blend_state) return camera.output_mode.blend_state;
    if (camera.sorted_camera_index_for_target.value_or(0) > 0) return alpha_blend_state();
    return std::nullopt;
}
}  // namespace

void Camera2D::register_required_components(epix::ecs::RequiredComponentsRegistrator& registrator) {
    // Bevy gets these through Camera's transitive required components. EPIX's
    // component inserter deliberately does not propagate that relationship,
    // so a bare Camera2D must declare the complete view dependency set.
    registrator.template register_required<camera::Camera>([] { return camera::Camera{}; });
    registrator.template register_required<camera::Projection>([] {
        return camera::Projection{camera::OrthographicProjection::default_2d()};
    });
    registrator.template register_required<transform::Transform>([] { return transform::Transform{}; });
    registrator.template register_required<camera::VisibleEntities>([] { return camera::VisibleEntities{}; });
    registrator.template register_required<camera::RenderLayers>([] { return camera::RenderLayers::layer(0); });
    registrator.template register_required<view::Msaa>([] { return view::Msaa::Sample4; });
    registrator.template register_required<camera::CameraMainTextureUsages>([] { return camera::CameraMainTextureUsages{}; });
    registrator.template register_required<view::Frustum>([] { return view::Frustum{}; });
    registrator.template register_required<render::camera::CameraRenderGraph>([] { return render::camera::CameraRenderGraph{Core2d}; });
}

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
    auto res = g.add_sub_graph(Core2d, std::move(g2d)).transform_error([](auto&& err) {
        spdlog::error("Failed to add Core2D graph: {}", err.to_string());
        return std::move(err);
    });
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
    Res<Core2dBlitHandles> handles,
    ResMut<PipelineServer> pipeline_server,
    ResMut<Core2dBlitPipelines> pipelines) {
    for (auto&& [camera, target] : views.iter()) {
        if (camera.output_mode.type == camera::CameraOutputMode::Type::Skip || !target.out_texture.view) continue;
        const auto output_blend = output_blend_for(camera);
        const auto existing = std::ranges::find_if(pipelines->pipelines, [&](const Core2dBlitPipeline& pipeline) {
            return pipeline.format == target.out_texture.view_format && same_blend_state(pipeline.output_blend, output_blend);
        });
        if (existing != pipelines->pipelines.end()) continue;

        Core2dBlitPipeline built;
        built.layout = device->createBindGroupLayout(
            wgpu::BindGroupLayoutDescriptor()
                .setLabel("Core2dBlitLayout")
                .setEntries(std::array{
                    wgpu::BindGroupLayoutEntry().setBinding(0).setVisibility(wgpu::ShaderStage::eFragment).setSampler(
                        wgpu::SamplerBindingLayout().setType(wgpu::SamplerBindingType::eFiltering)),
                    wgpu::BindGroupLayoutEntry().setBinding(1).setVisibility(wgpu::ShaderStage::eFragment).setTexture(
                        wgpu::TextureBindingLayout().setSampleType(wgpu::TextureSampleType::eFloat).setViewDimension(
                            wgpu::TextureViewDimension::e2D)),
                }));
        built.sampler = device->createSampler(wgpu::SamplerDescriptor()
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
        constexpr float kBlitVerts[6] = {0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 2.0f};
        built.vertex_buffer = device->createBuffer(wgpu::BufferDescriptor()
                                                        .setLabel("Core2dBlitVBO")
                                                        .setSize(sizeof(kBlitVerts))
                                                        .setUsage(wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst));
        device->getQueue().writeBuffer(built.vertex_buffer, 0, kBlitVerts, sizeof(kBlitVerts));

        epix::render::VertexState vs{.shader = handles->vertex_shader, .entry_point = std::string("blitVert")};
        vs.buffers.push_back(wgpu::VertexBufferLayout()
                                 .setArrayStride(sizeof(float) * 2)
                                 .setStepMode(wgpu::VertexStepMode::eVertex)
                                 .setAttributes(std::array{
                                     wgpu::VertexAttribute().setShaderLocation(0).setOffset(0).setFormat(wgpu::VertexFormat::eFloat32x2),
                                 }));
        epix::render::FragmentState fs{.shader = handles->fragment_shader, .entry_point = std::string("blitFrag")};
        wgpu::ColorTargetState color_target;
        color_target.setFormat(target.out_texture.view_format).setWriteMask(wgpu::ColorWriteMask::eAll);
        if (output_blend) color_target.setBlend(*output_blend);
        fs.add_target(color_target);
        built.pipeline_id = pipeline_server->queue_render_pipeline(epix::render::RenderPipelineDescriptor{
            .label       = "core2d-blit",
            .layouts     = {built.layout},
            .vertex      = std::move(vs),
            .primitive   = wgpu::PrimitiveState().setTopology(wgpu::PrimitiveTopology::eTriangleList).setCullMode(wgpu::CullMode::eNone),
            .multisample = wgpu::MultisampleState().setCount(1).setMask(~0u).setAlphaToCoverageEnabled(false),
            .fragment    = std::move(fs),
        });
        built.format       = target.out_texture.view_format;
        built.output_blend = output_blend;
        pipelines->pipelines.push_back(std::move(built));
    }
}

void Core2dBlitNode::run(graph::GraphContext& ctx, graph::RenderContext& render_ctx, const World& world) {
    if (!views) return;
    auto view_opt = views->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(ctx.view_entity());
    if (!view_opt) return;
    auto&& [camera, target] = *view_opt;
    if (!target.out_texture.view || camera.output_mode.type == camera::CameraOutputMode::Type::Skip) return;

    auto pipelines = world.get_resource<Core2dBlitPipelines>();
    if (!pipelines) return;
    const auto output_blend = output_blend_for(camera);
    const auto blit = std::ranges::find_if(pipelines->get().pipelines, [&](const Core2dBlitPipeline& candidate) {
        return candidate.format == target.out_texture.view_format && same_blend_state(candidate.output_blend, output_blend);
    });
    if (blit == pipelines->get().pipelines.end()) return;
    const auto& ps = world.resource<PipelineServer>();
    auto pipeline = ps.get_render_pipeline(blit->pipeline_id);
    if (!pipeline) return;
    auto device = render_ctx.device();

    // get_attachment marks the output as written -> needs_present -> present.
    std::optional<glm::vec4> clear_color;
    switch (camera.output_mode.clear_color.type) {
        case camera::ClearColorConfig::Type::None: break;
        case camera::ClearColorConfig::Type::Custom:
            clear_color = camera.output_mode.clear_color.clear_color;
            break;
        case camera::ClearColorConfig::Type::Default:
            if (auto global = world.get_resource<camera::ClearColor>()) clear_color = global->get().to_vec4();
            break;
    }
    auto bind_group = device.createBindGroup(
        wgpu::BindGroupDescriptor()
            .setLabel("Core2dBlitBG")
            .setLayout(blit->layout)
            .setEntries(std::array{
                wgpu::BindGroupEntry().setBinding(0).setSampler(blit->sampler),
                wgpu::BindGroupEntry().setBinding(1).setTextureView(target.main_texture_view()),
            }));
    auto render_pass = render_ctx.command_encoder().beginRenderPass(
        wgpu::RenderPassDescriptor().setColorAttachments(
            std::array{target.out_texture.get_attachment(clear_color)}));
    // Bevy upscaling node set_scissor_rect: clip the blit to the camera viewport.
    if (camera.viewport) {
        const auto& vp = *camera.viewport;
        render_pass.setScissorRect(vp.pos.x, vp.pos.y, vp.size.x, vp.size.y);
    }
    render_pass.setPipeline(pipeline->get().pipeline());
    render_pass.setVertexBuffer(0, blit->vertex_buffer, 0, sizeof(float) * 6);
    render_pass.setBindGroup(0, bind_group, std::span<const uint32_t>{});
    render_pass.draw(3, 1, 0, 0);
    render_pass.end();
    render_ctx.flush_encoder();
}

void Core2dPlugin::attach(App& app) {
    // Register the embedded blit shaders (Bevy core_pipeline blit). The
    // handles must live in the RENDER world (the blit node reads them there);
    // asset handles are plain ids so they are safe to copy across worlds.
    std::optional<Core2dBlitHandles> blit_handles;
    if (auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>();
        auto server = app.world_mut().get_resource<assets::AssetServer>()) {
        registry->get().insert_asset_static(kBlitVertexPath, shader_bytes(kBlitVertexSlang));
        registry->get().insert_asset_static(kBlitFragmentPath, shader_bytes(kBlitFragmentSlang));
        blit_handles = Core2dBlitHandles{
            .vertex_shader   = server->get().load<shader::Shader>("embedded://core2d/blit_vert.slang"),
            .fragment_shader = server->get().load<shader::Shader>("embedded://core2d/blit_frag.slang"),
        };
    } else {
        spdlog::warn("[core_graph] EmbeddedAssetRegistry or AssetServer not available; 2D output blit disabled.");
    }

    app.get_sub_app_mut(render::Render).and_then([&](App& render_app) {
        render_app.world_mut().insert_resource(phase::DrawFunctions<Transparent2D>{});
        render_app.world_mut().insert_resource(phase::DrawFunctions<Opaque2D>{});
        render_app.world_mut().insert_resource(phase::DrawFunctions<UI2DItem>{});
        render_app.world_mut().init_resource<Core2dBlitPipelines>();
        if (blit_handles) {
            render_app.world_mut().insert_resource(std::move(*blit_handles));
        }
        Core2d.add_to(render_app.resource_mut<graph::RenderGraph>());

        render_app.add_systems(Render, into(phase::sort_phase_items<Transparent2D>, phase::sort_phase_items<UI2DItem>,
                                            phase::sort_phase_items<Opaque2D>)
                                           .in_set(RenderSystems::PhaseSort)
                                           .set_names(std::array{"sort transparent 2d phase", "sort ui 2d phase",
                                                                 "sort opaque 2d phase"}));
        render_app.add_systems(Render, into(queue_core2d_blit_pipelines)
                                           .in_set(RenderSystems::Queue)
                                           .set_name("queue core2d blit pipelines"));
        render_app.add_systems(
            Render, into([](Commands cmd,
                            Query<Item<Entity, const render::camera::ExtractedCamera&>, With<view::ExtractedView>> views) {
                        // insert render phases for each view
                        for (auto&& [entity, camera] : views.iter()) {
                            // only insert for 2d camera render graph
                            if (camera.render_graph == render::camera::CameraRenderGraph(Core2d)) {
                                auto entity_commands = cmd.entity(entity);
                                entity_commands.insert(phase::RenderPhase<Transparent2D>{},
                                                       phase::RenderPhase<Opaque2D>{}, phase::RenderPhase<UI2DItem>{});
                            }
                        }
                    })
                        .in_set(RenderSystems::ManageViews)
                        .set_name("insert 2d render phases"));
        return std::make_optional(std::ref(render_app));
    });
}
