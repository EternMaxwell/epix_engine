// C15-D4: Bevy's `CopyDeferredLightingIdPlugin` (`deferred/copy_lighting_id.rs`).
//
// The deferred lighting pass cannot read a `u32` material id from a colour
// attachment directly, so before it runs the id is written into the depth
// attachment as `id / 255`, one depth write per pixel. This is the last node in
// the Core3D chain that C13 had left as an `EmptyNode`.

#include <epix/core_graph.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

using namespace epix::app;
using namespace epix::ecs;

namespace epix::core_graph {
namespace {
constexpr std::string_view kCopyLightingIdFragmentPath = "core_pipeline/copy_deferred_lighting_id.slang";

// Bevy `copy_deferred_lighting_id.wgsl`. The value is stored as unorm, so the
// `u8` material id is divided by 255 on the way into `frag_depth`. The fragment
// stage has no colour output at all: the pipeline writes depth only.
constexpr std::string_view kCopyLightingIdFragmentSlang = R"slang(
[[vk::binding(0, 0)]] Texture2D<uint> material_id_texture;
struct FragmentOutput {
    float depth : SV_Depth;
};
[shader("fragment")]
FragmentOutput fs_main(float4 position : SV_Position) {
    FragmentOutput output;
    output.depth = float(material_id_texture.Load(int3(int2(position.xy), 0)).x) / 255.0f;
    return output;
}
)slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(source.data()), source.size());
}
}  // namespace

void epix::core_graph::core_3d::init_copy_deferred_lighting_id_pipeline(
    Commands cmd,
    Res<wgpu::Device> device,
    Res<assets::AssetServer> asset_server,
    Res<FullscreenShader> fullscreen_shader,
    Res<render::PipelineServer> pipeline_server) {
    using render::render_resource::BindGroupLayoutEntries;
    using render::render_resource::binding_types::texture_2d;

    // Bevy's layout is a single fragment-visible `texture_2d<u32>`
    // (`copy_lighting_id.rs:99-107`).
    const auto entries = BindGroupLayoutEntries<1>::sequential(
        wgpu::ShaderStage::eFragment, texture_2d(wgpu::TextureSampleType::eUint));
    const auto layout = pipeline_server->get_bind_group_layout(
        render::BindGroupLayoutDescriptor()
            .set_label("copy_deferred_lighting_id_bind_group_layout")
            .set_entries(entries.entries()));

    const auto shader =
        asset_server->load<shader::Shader>("embedded://core_pipeline/copy_deferred_lighting_id.slang");
    // Bevy has no colour target here; the pass writes depth only, with
    // `depth_write_enabled: true` and `depth_compare: Always`, and tests depth
    // against `DEFERRED_LIGHTING_PASS_ID_DEPTH_FORMAT` (copy_lighting_id.rs:122-133).
    render::FragmentState fragment{.shader = shader, .entry_point = std::string("fs_main")};
    // wgpu-native's C API inlines what Rust wgpu names `StencilState` and
    // `DepthBiasState` into `DepthStencilState`, so Bevy's
    // `StencilState::default()` and `DepthBiasState::default()` are written out
    // field by field. `StencilFaceState::IGNORE` is compare-always with
    // keep/keep/keep.
    const auto stencil_ignore = wgpu::StencilFaceState()
                                    .setCompare(wgpu::CompareFunction::eAlways)
                                    .setFailOp(wgpu::StencilOperation::eKeep)
                                    .setDepthFailOp(wgpu::StencilOperation::eKeep)
                                    .setPassOp(wgpu::StencilOperation::eKeep);
    auto depth_stencil = wgpu::DepthStencilState()
                             .setFormat(DEFERRED_LIGHTING_PASS_ID_DEPTH_FORMAT)
                             .setDepthWriteEnabled(wgpu::OptionalBool::eTrue)
                             .setDepthCompare(wgpu::CompareFunction::eAlways)
                             .setStencilFront(stencil_ignore)
                             .setStencilBack(stencil_ignore)
                             .setStencilReadMask(0)
                             .setStencilWriteMask(0)
                             .setDepthBias(0)
                             .setDepthBiasSlopeScale(0.0f)
                             .setDepthBiasClamp(0.0f);

    render::RenderPipelineDescriptor descriptor{
        .label         = "copy_deferred_lighting_id_pipeline",
        .layouts       = {layout},
        .vertex        = fullscreen_shader->to_vertex_state(),
        .primitive     = wgpu::PrimitiveState()
                             .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                             .setFrontFace(wgpu::FrontFace::eCCW)
                             .setCullMode(wgpu::CullMode::eNone)
                             .setUnclippedDepth(false),
        .depth_stencil = std::move(depth_stencil),
        .multisample   = wgpu::MultisampleState().setCount(1).setMask(~0u).setAlphaToCoverageEnabled(false),
        .fragment      = std::move(fragment),
    };
    cmd.insert_resource(CopyDeferredLightingIdPipeline{
        .layout      = layout,
        .pipeline_id = pipeline_server->queue_render_pipeline(std::move(descriptor)),
    });
}

void epix::core_graph::core_3d::prepare_deferred_lighting_id_textures(
    Commands cmd,
    ResMut<render::render_resource::TextureCache> texture_cache,
    Res<wgpu::Device> device,
    Query<Item<Entity, const render::camera::ExtractedCamera&>, With<DeferredPrepass>> views) {
    // Bevy keys nothing here: one depth texture per view, single-sampled, in
    // `DEFERRED_LIGHTING_PASS_ID_DEPTH_FORMAT`, usable as a render attachment and
    // as a copy source (`copy_lighting_id.rs:160-193`).
    for (auto&& [entity, camera] : views.iter()) {
        const auto physical_target_size = camera.physical_target_size;
        if (!physical_target_size) continue;
        const wgpu::Extent3D size{physical_target_size->x, physical_target_size->y, 1};
        const auto usage =
            static_cast<wgpu::TextureUsage>(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eCopySrc);
        wgpu::TextureDescriptor descriptor;
        descriptor.setLabel("deferred_lighting_id_depth_texture_a")
            .setSize(size)
            .setMipLevelCount(1)
            .setSampleCount(1)
            .setDimension(wgpu::TextureDimension::e2D)
            .setFormat(DEFERRED_LIGHTING_PASS_ID_DEPTH_FORMAT)
            .setUsage(usage);
        const render::render_resource::TextureCacheKey key{
            .format                = DEFERRED_LIGHTING_PASS_ID_DEPTH_FORMAT,
            .dimension             = wgpu::TextureDimension::e2D,
            .width                 = size.width,
            .height                = size.height,
            .depth_or_array_layers = 1,
            .mip_level_count       = 1,
            .sample_count          = 1,
            .usage                 = usage,
            .label                 = "deferred_lighting_id_depth_texture_a",
        };
        cmd.entity(entity).insert(DeferredLightingIdDepthTexture{texture_cache->get(*device, key, descriptor)});
    }
}

std::expected<void, render::graph::NodeRunError> epix::core_graph::core_3d::CopyDeferredLightingIdNode::run(
    render::graph::GraphContext& graph,
    render::graph::RenderContext& render_context,
    typename ecs::QueryData<ViewQuery>::Item view,
    const ecs::World& world) const {
    auto&& [view_target, view_prepass_textures, deferred_lighting_id_depth_texture] = view;
    static_cast<void>(view_target);

    const auto pipeline_server = world.get_resource<render::PipelineServer>();
    const auto pipeline_res    = world.get_resource<CopyDeferredLightingIdPipeline>();
    if (!pipeline_server || !pipeline_res) return {};
    const auto pipeline = pipeline_server->get().get_render_pipeline(pipeline_res->get().pipeline_id);
    // Bevy returns without drawing while the pipeline is still being created.
    if (!pipeline) return {};

    const auto deferred_lighting_pass_id_texture = view_prepass_textures.deferred_lighting_pass_id;
    if (!deferred_lighting_pass_id_texture) return {};

    const auto device = render_context.device();
    const auto bind_group =
        device.createBindGroup(wgpu::BindGroupDescriptor()
                                   .setLabel("copy_deferred_lighting_id_bind_group")
                                   .setLayout(pipeline_res->get().layout)
                                   .setEntries(render::render_resource::BindGroupEntries<>::sequential(
                                       render::render_resource::BindGroupEntries<>::texture_binding(
                                           deferred_lighting_pass_id_texture->texture.default_view))
                                                  .entries()));

    const auto render_pipeline = pipeline->get().pipeline();
    // Bevy clears the depth attachment to 0.0 and stores it; the id texture is a
    // per-pixel material id, so the clear value is the "background" id.
    const auto depth_attachment =
        wgpu::RenderPassDepthStencilAttachment()
            .setView(deferred_lighting_id_depth_texture.texture.default_view)
            .setDepthLoadOp(wgpu::LoadOp::eClear)
            .setDepthClearValue(0.0f)
            .setDepthStoreOp(wgpu::StoreOp::eStore);

    render_context.add_command_buffer_generation_task(
        [render_pipeline, bind_group, depth_attachment](wgpu::Device device) {
            auto encoder = device.createCommandEncoder(
                wgpu::CommandEncoderDescriptor().setLabel("copy_deferred_lighting_id_encoder"));
            {
                auto pass = encoder.beginRenderPass(
                    wgpu::RenderPassDescriptor().setLabel("copy_deferred_lighting_id").setDepthStencilAttachment(
                        depth_attachment));
                pass.setPipeline(render_pipeline);
                pass.setBindGroup(0, bind_group, std::span<const std::uint32_t>{});
                pass.draw(3, 1, 0, 0);
                pass.end();
            }
            return encoder.finish();
        });
    return {};
}

void epix::core_graph::core_3d::CopyDeferredLightingIdPlugin::attach(App& app) {
    auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>();
    auto server   = app.world_mut().get_resource<assets::AssetServer>();
    if (!registry || !server) return;

    registry->get().insert_asset_static(kCopyLightingIdFragmentPath, shader_bytes(kCopyLightingIdFragmentSlang));
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().add_systems(
            render::RenderStartup,
            into(epix::core_graph::core_3d::init_copy_deferred_lighting_id_pipeline)
                .set_name("init copy deferred lighting id pipeline"));
        render_app->get().add_systems(render::Render,
                                      into(epix::core_graph::core_3d::prepare_deferred_lighting_id_textures)
                                          .in_set(render::RenderSystems::PrepareResources)
                                          .set_name("prepare deferred lighting id textures"));
    }
}

}  // namespace epix::core_graph
