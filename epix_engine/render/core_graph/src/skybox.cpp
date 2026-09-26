#include <epix/core_graph/skybox.hpp>

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <epix/core_graph/core3d.hpp>
#include <epix/core_graph/prepass.hpp>
#include <epix/render/view.hpp>
#endif

using namespace epix::ecs;

namespace epix::core_graph {
namespace {
constexpr std::string_view kSkyboxShaderPath  = "core_pipeline/skybox.slang";
constexpr std::string_view kSkyboxShaderSlang = R"slang(
// Bevy's `skybox.wgsl`. `coords_to_viewport_uv` is Bevy's
// `bevy_pbr::utils::coords_to_viewport_uv` inlined, because Epix has no PBR
// utility library to import it from.
import epix.view;

struct SkyboxUniforms {
    // A scalar is 4-byte aligned in a constant buffer, so the matrix lands at
    // offset 16 and the struct is 80 bytes, which is the layout the C++ side
    // reproduces with its three explicit padding floats. A `float3` here would
    // be 16-byte aligned and push the matrix - and the struct - further.
    float brightness;
    float4x4 transform;
};

// WGSL's `texture_cube<f32>` samples to a `vec4<f32>`; the HLSL/Slang texture
// element type is the *sampled* type, so `TextureCube<float>` would sample to a
// scalar (the red channel alone) and broadcasting it would grey every skybox out.
[[vk::binding(0, 0)]] TextureCube<float4> skybox_texture;
[[vk::binding(1, 0)]] SamplerState skybox_sampler;
[[vk::binding(2, 0)]] ConstantBuffer<epix::view::View> view;
[[vk::binding(3, 0)]] ConstantBuffer<SkyboxUniforms> uniforms;

float3 coords_to_ray_direction(float2 position, float4 viewport) {
    // Using world positions of the fragment and camera to calculate a ray
    // direction breaks down at large translations; the view space ray direction
    // is just the fragment position, taken on the near plane so an infinite
    // reverse projection cannot produce an infinite world position.
    const float2 viewport_uv = (position - viewport.xy) / viewport.zw;
    const float4 view_position_homogeneous = mul(
        view.view_from_clip, float4(viewport_uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 1.0f, 1.0f));

    float3 view_ray_direction = view_position_homogeneous.xyz / view_position_homogeneous.w;
    view_ray_direction        = mul(view.world_from_view, float4(view_ray_direction, 0.0f)).xyz;

    // A direction, so the translation part of each matrix is ignored; the
    // skybox transform rotates the environment.
    return normalize(mul(uniforms.transform, float4(view_ray_direction, 0.0f)).xyz);
}

// The vertices are generated from the index: (0,0), (1,0), (0,1) scaled by four
// and shifted, which covers the visible clip-space region with one triangle.
[shader("vertex")]
float4 skybox_vertex(uint vertex_index : SV_VulkanVertexID) : SV_Position {
    const float2 clip_position =
        float2(float(vertex_index & 1u), float((vertex_index >> 1u) & 1u)) * 4.0f - 1.0f;
    return float4(clip_position, 0.0f, 1.0f);
}

[shader("fragment")]
float4 skybox_fragment(float4 position : SV_Position) : SV_Target {
    const float3 ray_direction = coords_to_ray_direction(position.xy, view.viewport);

    // Cube maps are left-handed, so the z coordinate is negated.
    const float4 sampled = skybox_texture.Sample(skybox_sampler, ray_direction * float3(1.0f, 1.0f, -1.0f));
    return float4(sampled.rgb * uniforms.brightness, sampled.a);
}
)slang";
}  // namespace

bool SkyboxPipelineKey::operator==(const SkyboxPipelineKey&) const noexcept = default;

SkyboxPipeline SkyboxPipeline::create(assets::Handle<shader::Shader> shader) {
    // Bevy `SkyboxPipeline::new` (`skybox/mod.rs:156-175`): a cube texture, a
    // filtering sampler, the view uniform with a dynamic offset, and the skybox
    // uniforms - all on the fragment stage except the view uniform, which the
    // vertex stage reads too. The entries are built by hand rather than with the
    // `binding_types` helpers because the view uniform is the only entry whose
    // visibility differs from the rest.
    const auto uniform = [](std::uint64_t min_binding_size) {
        return wgpu::BufferBindingLayout()
            .setType(wgpu::BufferBindingType::eUniform)
            .setHasDynamicOffset(true)
            .setMinBindingSize(min_binding_size);
    };
    const std::vector<wgpu::BindGroupLayoutEntry> entries{
        wgpu::BindGroupLayoutEntry()
            .setBinding(0)
            .setVisibility(wgpu::ShaderStage::eFragment)
            .setTexture(wgpu::TextureBindingLayout()
                            .setSampleType(wgpu::TextureSampleType::eFloat)
                            .setViewDimension(wgpu::TextureViewDimension::eCube)
                            .setMultisampled(false)),
        wgpu::BindGroupLayoutEntry()
            .setBinding(1)
            .setVisibility(wgpu::ShaderStage::eFragment)
            .setSampler(wgpu::SamplerBindingLayout().setType(wgpu::SamplerBindingType::eFiltering)),
        wgpu::BindGroupLayoutEntry()
            .setBinding(2)
            .setVisibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)
            .setBuffer(uniform(0)),
        wgpu::BindGroupLayoutEntry()
            .setBinding(3)
            .setVisibility(wgpu::ShaderStage::eFragment)
            .setBuffer(uniform(sizeof(SkyboxUniforms))),
    };

    return SkyboxPipeline{
        .bind_group_layout =
            render::BindGroupLayoutDescriptor().set_label("skybox_bind_group_layout").set_entries(entries),
        .shader = std::move(shader),
    };
}

render::RenderPipelineDescriptor SkyboxPipeline::specialize(Key key) const {
    // Bevy `SpecializedRenderPipeline for SkyboxPipeline` (`:189-238`).
    render::FragmentState fragment{.shader = shader, .entry_point = std::string("skybox_fragment")};
    wgpu::ColorTargetState target;
    // Bevy leaves the blend state unset here: `None` is not `REPLACE`, and is
    // potentially faster.
    target
        .setFormat(key.hdr ? render::view::ViewTarget::TEXTURE_FORMAT_HDR : image::bevy_default<wgpu::TextureFormat>())
        .setWriteMask(wgpu::ColorWriteMask::eAll);
    fragment.add_target(std::move(target));

    const auto stencil_ignore = wgpu::StencilFaceState()
                                    .setCompare(wgpu::CompareFunction::eAlways)
                                    .setFailOp(wgpu::StencilOperation::eKeep)
                                    .setDepthFailOp(wgpu::StencilOperation::eKeep)
                                    .setPassOp(wgpu::StencilOperation::eKeep);
    auto depth_stencil        = wgpu::DepthStencilState()
                                    .setFormat(key.depth_format)
                                    .setDepthWriteEnabled(wgpu::OptionalBool::eFalse)
                                    .setDepthCompare(wgpu::CompareFunction::eGreaterEqual)
                                    .setStencilFront(stencil_ignore)
                                    .setStencilBack(stencil_ignore)
                                    .setStencilReadMask(0)
                                    .setStencilWriteMask(0)
                                    .setDepthBias(0)
                                    .setDepthBiasSlopeScale(0.0f)
                                    .setDepthBiasClamp(0.0f);

    return render::RenderPipelineDescriptor{
        .label         = "skybox_pipeline",
        .layouts       = {bind_group_layout},
        .vertex        = render::VertexState{.shader = shader, .entry_point = std::string("skybox_vertex")},
        .primitive     = wgpu::PrimitiveState()
                             .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                             .setFrontFace(wgpu::FrontFace::eCCW)
                             .setCullMode(wgpu::CullMode::eNone)
                             .setUnclippedDepth(false),
        .depth_stencil = std::move(depth_stencil),
        .multisample   = wgpu::MultisampleState().setCount(key.samples).setMask(~0u).setAlphaToCoverageEnabled(false),
        .fragment      = std::move(fragment),
    };
}

void prepare_skybox_uniforms(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> device,
    ecs::Res<wgpu::Queue> queue,
    ecs::ResMut<render::ComponentUniforms<SkyboxUniforms>> skybox_uniforms,
    ecs::Query<ecs::Item<ecs::Entity, const Skybox&, const render::camera::ExtractedCamera&>> views) {
    // Bevy's extraction derives `brightness * exposure` and the inverse-rotation
    // matrix and lets `UniformComponentPlugin` upload them (`skybox/mod.rs:109-148`);
    // the exposure comes from the extracted camera here because the render world
    // does not carry the main world's `Exposure` component.
    auto& uniforms = skybox_uniforms->uniforms_mut();
    uniforms.clear();
    for (auto&& [entity, skybox, camera] : views.iter()) {
        const SkyboxUniforms value{
            .brightness = skybox.brightness * camera.exposure,
            .transform  = glm::mat4_cast(glm::inverse(skybox.rotation)),
        };
        const std::size_t offset = uniforms.push(value);
        commands.entity(entity).insert(render::DynamicUniformIndex<SkyboxUniforms>{static_cast<std::uint32_t>(offset)});
    }
    uniforms.write_buffer(device.get(), queue.get());
}

void prepare_skybox_pipelines(
    ecs::Commands commands,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::ResMut<render::SpecializedRenderPipelines<SkyboxPipeline>> pipelines,
    ecs::Res<SkyboxPipeline> pipeline,
    ecs::Query<ecs::Item<ecs::Entity, const render::view::ExtractedView&, const render::view::Msaa&>, ecs::With<Skybox>>
        views) {
    // Bevy `prepare_skybox_pipelines` (`skybox/mod.rs:243-265`).
    for (auto&& [entity, view, msaa] : views.iter()) {
        const auto pipeline_id =
            pipelines->specialize(*pipeline_server, *pipeline,
                                  SkyboxPipelineKey{.hdr          = view.hdr,
                                                    .samples      = render::view::samples(msaa),
                                                    .depth_format = core_3d::CORE_3D_DEPTH_FORMAT});
        commands.entity(entity).insert(SkyboxPipelineId{pipeline_id});
    }
}

void prepare_skybox_bind_groups(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> device,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::Res<SkyboxPipeline> pipeline,
    ecs::Res<render::view::ViewUniforms> view_uniforms,
    ecs::Res<render::ComponentUniforms<SkyboxUniforms>> skybox_uniforms,
    ecs::Res<render::RenderAssets<render::texture::GpuImage>> images,
    ecs::Query<ecs::Item<ecs::Entity, const Skybox&, const render::DynamicUniformIndex<SkyboxUniforms>&>> views) {
    // Bevy `prepare_skybox_bind_groups` (`skybox/mod.rs:270-302`): the cube view,
    // its sampler, the dynamic view uniform and the dynamic skybox uniforms.
    const auto view_binding = view_uniforms->uniforms.binding();
    if (!view_binding) return;
    const auto skybox_binding = skybox_uniforms->uniforms().binding();
    if (!skybox_binding) return;

    const auto layout = pipeline_server->get_bind_group_layout(pipeline->bind_group_layout);
    for (auto&& [entity, skybox, uniform_index] : views.iter()) {
        const auto image = images->get(skybox.image);
        if (!image) continue;

        const auto entries = render::render_resource::BindGroupEntries<>::sequential(
            render::render_resource::BindGroupEntries<>::texture_binding(image->texture_view),
            render::render_resource::BindGroupEntries<>::sampler_binding(image->sampler),
            render::render_resource::BindGroupEntries<>::buffer_binding(view_binding->get(), 0,
                                                                        sizeof(render::view::ViewUniform)),
            render::render_resource::BindGroupEntries<>::buffer_binding(skybox_binding->get(), 0,
                                                                        sizeof(SkyboxUniforms)));
        commands.entity(entity).insert(SkyboxBindGroup{
            .bind_group     = device->createBindGroup(wgpu::BindGroupDescriptor()
                                                          .setLabel("skybox_bind_group")
                                                          .setLayout(layout)
                                                          .setEntries(entries.entries())),
            .uniform_offset = uniform_index.index(),
        });
    }
}

// Bevy `skybox_prepass.wgsl`: the skybox's motion vectors, so TAA and motion
// blur treat it correctly. The fullscreen vertex shader supplies the uv.
constexpr std::string_view kSkyboxPrepassShaderPath  = "core_pipeline/skybox_prepass.slang";
constexpr std::string_view kSkyboxPrepassShaderSlang = R"slang(
import epix.view;

// Bevy spells this struct `PreviousViewUniforms` in the shader, matching its
// Rust type; the C++ side calls the data `PreviousViewData`.
struct PreviousViewUniforms {
    float4x4 view_from_world;
    float4x4 clip_from_world;
    float4x4 clip_from_view;
    float4x4 world_from_clip;
    float4x4 view_from_clip;
};

[[vk::binding(0, 0)]] ConstantBuffer<epix::view::View> view;
[[vk::binding(1, 0)]] ConstantBuffer<PreviousViewUniforms> previous_view;

[shader("fragment")]
float4 skybox_prepass_fragment([[vk::location(0)]] float2 uv) : SV_Target1 {
    // Bevy `bevy_pbr::view_transformations::uv_to_ndc`.
    const float2 clip_position = uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    // z = 0 is the far plane in the reversed infinite projection, and the
    // velocity is measured in clip space, so only the direction matters.
    const float4 world_position = mul(view.world_from_clip, float4(clip_position, 0.0f, 1.0f));
    const float2 previous_clip_position = mul(previous_view.clip_from_world, world_position).xy;
    // Copied from the mesh motion vectors, including the y flip.
    const float2 velocity = (clip_position - previous_clip_position) * float2(0.5f, -0.5f);

    return float4(velocity.x, velocity.y, 0.0f, 1.0f);
}
)slang";

bool SkyboxPrepassPipelineKey::operator==(const SkyboxPrepassPipelineKey& other) const noexcept {
    return samples == other.samples && normal_prepass == other.normal_prepass;
}

SkyboxPrepassPipeline SkyboxPrepassPipeline::create(FullscreenShader fullscreen_shader,
                                                    assets::Handle<shader::Shader> fragment_shader) {
    // Bevy `init_skybox_prepass_pipeline` (`skybox/prepass.rs:62-81`): the view
    // uniform and the previous view uniform, both dynamic, both read by the
    // fragment stage only.
    const auto uniform = [](std::uint64_t min_binding_size) {
        return wgpu::BufferBindingLayout()
            .setType(wgpu::BufferBindingType::eUniform)
            .setHasDynamicOffset(true)
            .setMinBindingSize(min_binding_size);
    };
    const std::vector<wgpu::BindGroupLayoutEntry> entries{
        wgpu::BindGroupLayoutEntry()
            .setBinding(0)
            .setVisibility(wgpu::ShaderStage::eFragment)
            .setBuffer(uniform(sizeof(render::view::ViewUniform))),
        wgpu::BindGroupLayoutEntry()
            .setBinding(1)
            .setVisibility(wgpu::ShaderStage::eFragment)
            .setBuffer(uniform(sizeof(PreviousViewData))),
    };

    return SkyboxPrepassPipeline{
        .bind_group_layout =
            render::BindGroupLayoutDescriptor().set_label("skybox_prepass_bind_group_layout").set_entries(entries),
        .fullscreen_shader = std::move(fullscreen_shader),
        .fragment_shader   = std::move(fragment_shader),
    };
}

render::RenderPipelineDescriptor SkyboxPrepassPipeline::specialize(Key key) const {
    // Bevy `SpecializedRenderPipeline for SkyboxPrepassPipeline` (`:83-111`).
    auto fragment = render::FragmentState{.shader      = fragment_shader,
                                          .entry_point = std::string("skybox_prepass_fragment"),
                                          .targets     = prepass_pipeline_targets(key.normal_prepass, true, false)};

    const auto stencil_ignore = wgpu::StencilFaceState()
                                    .setCompare(wgpu::CompareFunction::eAlways)
                                    .setFailOp(wgpu::StencilOperation::eKeep)
                                    .setDepthFailOp(wgpu::StencilOperation::eKeep)
                                    .setPassOp(wgpu::StencilOperation::eKeep);

    return render::RenderPipelineDescriptor{
        .label         = "skybox_prepass_pipeline",
        .layouts       = {bind_group_layout},
        .vertex        = fullscreen_shader.to_vertex_state(),
        .primitive     = wgpu::PrimitiveState()
                             .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                             .setFrontFace(wgpu::FrontFace::eCCW)
                             .setCullMode(wgpu::CullMode::eNone)
                             .setUnclippedDepth(false),
        .depth_stencil = wgpu::DepthStencilState()
                             .setFormat(core_3d::CORE_3D_DEPTH_FORMAT)
                             .setDepthWriteEnabled(wgpu::OptionalBool::eFalse)
                             .setDepthCompare(wgpu::CompareFunction::eGreaterEqual)
                             .setStencilFront(stencil_ignore)
                             .setStencilBack(stencil_ignore)
                             .setStencilReadMask(0)
                             .setStencilWriteMask(0)
                             .setDepthBias(0)
                             .setDepthBiasSlopeScale(0.0f)
                             .setDepthBiasClamp(0.0f),
        .multisample   = wgpu::MultisampleState().setCount(key.samples).setMask(~0u).setAlphaToCoverageEnabled(false),
        .fragment      = std::move(fragment),
    };
}

void prepare_skybox_prepass_pipelines(ecs::Commands commands,
                                      ecs::Res<render::PipelineServer> pipeline_server,
                                      ecs::ResMut<render::SpecializedRenderPipelines<SkyboxPrepassPipeline>> pipelines,
                                      ecs::Res<SkyboxPrepassPipeline> pipeline,
                                      ecs::Query<ecs::Item<ecs::Entity,
                                                           ecs::Has<core_3d::NormalPrepass>,
                                                           ecs::Has<core_3d::MotionVectorPrepass>,
                                                           const render::view::Msaa&>,
                                                 ecs::With<Skybox>> views) {
    // Bevy `prepare_skybox_prepass_pipelines` (`skybox/prepass.rs:114-133`).
    for (auto&& [entity, normal_prepass, motion_vector_prepass, msaa] : views.iter()) {
        if (!motion_vector_prepass) continue;
        const auto key =
            SkyboxPrepassPipelineKey{.samples = render::view::samples(msaa), .normal_prepass = normal_prepass};
        commands.entity(entity).insert(
            RenderSkyboxPrepassPipeline{pipelines->specialize(*pipeline_server, *pipeline, key)});
    }
}

void prepare_skybox_prepass_bind_groups(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> device,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::Res<SkyboxPrepassPipeline> pipeline,
    ecs::Res<render::view::ViewUniforms> view_uniforms,
    ecs::Res<PreviousViewUniforms> previous_view_uniforms,
    ecs::Query<ecs::Item<ecs::Entity, ecs::Has<core_3d::MotionVectorPrepass>>, ecs::With<Skybox>> views) {
    // Bevy `prepare_skybox_prepass_bind_groups` (`skybox/prepass.rs:138-163`).
    const auto view_binding     = view_uniforms->uniforms.binding();
    const auto previous_binding = previous_view_uniforms->uniforms.binding();
    if (!view_binding || !previous_binding) return;

    const auto layout = pipeline_server->get_bind_group_layout(pipeline->bind_group_layout);

    const auto entries = render::render_resource::BindGroupEntries<>::sequential(
        render::render_resource::BindGroupEntries<>::buffer_binding(view_binding->get(), 0,
                                                                    sizeof(render::view::ViewUniform)),
        render::render_resource::BindGroupEntries<>::buffer_binding(previous_binding->get(), 0,
                                                                    sizeof(PreviousViewData)));

    for (auto&& [entity, motion_vector_prepass] : views.iter()) {
        if (!motion_vector_prepass) continue;
        commands.entity(entity).insert(SkyboxPrepassBindGroup{
            .bind_group = device->createBindGroup(wgpu::BindGroupDescriptor()
                                                      .setLabel("skybox_prepass_bind_group")
                                                      .setLayout(layout)
                                                      .setEntries(entries.entries())),
        });
    }
}

void SkyboxPlugin::attach(app::App& app) const {
    // Bevy `SkyboxPlugin::build` (`skybox/mod.rs:44-75`): the shaders are
    // embedded, the specialization cache is initialized, and the pipeline
    // itself is built in RenderStartup.
    std::optional<assets::Handle<shader::Shader>> shader;
    std::optional<assets::Handle<shader::Shader>> prepass_shader;
    if (auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>();
        auto server   = app.world_mut().get_resource<assets::AssetServer>()) {
        registry->get().insert_asset_static(kSkyboxShaderPath, std::as_bytes(std::span(kSkyboxShaderSlang)));
        shader = server->get().load<shader::Shader>("embedded://core_pipeline/skybox.slang");
        registry->get().insert_asset_static(kSkyboxPrepassShaderPath,
                                            std::as_bytes(std::span(kSkyboxPrepassShaderSlang)));
        prepass_shader = server->get().load<shader::Shader>("embedded://core_pipeline/skybox_prepass.slang");
    } else {
        spdlog::warn("[core_graph.skybox] EmbeddedAssetRegistry or AssetServer not available; skybox disabled.");
    }

    app.add_plugins(render::ExtractComponentPlugin<Skybox>{});

    auto sub_app = app.get_sub_app_mut(render::Render);
    if (!sub_app || !shader) return;

    sub_app->get().world_mut().insert_resource(render::ComponentUniforms<SkyboxUniforms>{});
    sub_app->get().world_mut().init_resource<render::SpecializedRenderPipelines<SkyboxPipeline>>();
    sub_app->get().add_systems(render::RenderStartup, into([shader = std::move(*shader)](Commands commands) {
                                                          // Bevy `init_skybox_pipeline` (`:177-180`).
                                                          commands.insert_resource(SkyboxPipeline::create(shader));
                                                      }).set_name("init skybox pipeline"));
    sub_app->get().add_systems(
        render::Render,
        into(prepare_skybox_pipelines).in_set(render::RenderSystems::Prepare).set_name("prepare skybox pipelines"));
    sub_app->get().add_systems(render::Render, into(prepare_skybox_uniforms)
                                                   .in_set(render::RenderSystems::PrepareResources)
                                                   .set_name("prepare skybox uniforms"));
    sub_app->get().add_systems(render::Render, into(prepare_skybox_bind_groups)
                                                   .in_set(render::RenderSystems::PrepareBindGroups)
                                                   .set_name("prepare skybox bind groups"));

    // The motion-vector variant (Bevy `skybox/prepass.rs`): its own shader and
    // pipeline, and the two prepare systems that fill it for views carrying both
    // a `Skybox` and a `MotionVectorPrepass`.
    if (prepass_shader) {
        sub_app->get().world_mut().init_resource<render::SpecializedRenderPipelines<SkyboxPrepassPipeline>>();
        sub_app->get().add_systems(
            render::RenderStartup,
            into([shader = std::move(*prepass_shader)](Commands commands, Res<FullscreenShader> fullscreen_shader,
                                                       Res<wgpu::Device> device) {
                // `PreviousViewUniforms` is a resource in Bevy; Epix's dynamic
                // uniform buffer needs the device's alignment, which a
                // default-constructed one does not have.
                wgpu::Limits limits{};
                device->getLimits(&limits);
                commands.insert_resource(PreviousViewUniforms{
                    .uniforms = render::render_resource::DynamicUniformBuffer<PreviousViewData>::new_with_alignment(
                        limits.minUniformBufferOffsetAlignment)});
                commands.insert_resource(SkyboxPrepassPipeline::create(fullscreen_shader.get(), std::move(shader)));
            }).set_name("init skybox prepass pipeline"));
        sub_app->get().add_systems(render::Render, into(prepare_skybox_prepass_pipelines)
                                                       .in_set(render::RenderSystems::Prepare)
                                                       .set_name("prepare skybox prepass pipelines"));
        sub_app->get().add_systems(render::Render, into(prepare_skybox_prepass_bind_groups)
                                                       .in_set(render::RenderSystems::PrepareBindGroups)
                                                       .set_name("prepare skybox prepass bind groups"));
    }
}

}  // namespace epix::core_graph

namespace std {
std::size_t hash<epix::core_graph::SkyboxPipelineKey>::operator()(
    const epix::core_graph::SkyboxPipelineKey& key) const noexcept {
    std::size_t h = std::hash<bool>{}(key.hdr);
    h ^= std::hash<std::uint32_t>{}(key.samples) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(key.depth_format)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}
std::size_t hash<epix::core_graph::SkyboxPrepassPipelineKey>::operator()(
    const epix::core_graph::SkyboxPrepassPipelineKey& key) const noexcept {
    std::size_t h = std::hash<std::uint32_t>{}(key.samples);
    h ^= std::hash<bool>{}(key.normal_prepass) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}
}  // namespace std
