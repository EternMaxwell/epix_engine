#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <webgpu/webgpu.hpp>
#include <epix/common.hpp>
#endif
#include <epix/core_graph/fullscreen.hpp>

namespace epix::core_graph {

/** @brief Adds a skybox to a 3D camera, based on a cubemap texture (Bevy
 *  `Skybox`, `skybox/mod.rs:86-107`).
 *
 *  The image must produce a cube texture view; as in Bevy that is expressed with
 *  the image's own `texture_view_descriptor` rather than a dedicated image type. */
EPIX_EXPORT struct Skybox {
    /** @brief The cubemap to draw behind the scene. Defaults to the default
     *  image, as Bevy's `Handle::default()` does. */
    assets::Handle<image::Image> image{image::DEFAULT_IMAGE_HANDLE};
    /** @brief Scale factor applied to the image samples, in cd/m^2. */
    float brightness = 0.0f;
    /** @brief View-space rotation applied to the cubemap. */
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

/** @brief The per-view skybox uniforms (Bevy `SkyboxUniforms`).
 *
 *  `brightness` is already multiplied by the view's exposure and `transform` is
 *  the inverse of the component's rotation, as Bevy's extraction does
 *  (`skybox/mod.rs:114-134`). Bevy's `ShaderType` derives the layout for
 *  `{ f32, Mat4 }` with the matrix 16-byte aligned, so the three padding floats
 *  below reproduce what it inserts automatically: brightness at offset 0, the
 *  matrix at offset 16, 80 bytes in total. */
EPIX_EXPORT struct SkyboxUniforms {
    float brightness = 1.0f;
    /** @brief Padding that aligns the matrix to 16 bytes (Bevy's `ShaderType`
     *  inserts it implicitly). */
    std::array<float, 3> padding{};
    glm::mat4 transform{1.0f};
};

}  // namespace epix::core_graph

namespace epix::render::render_resource {
/** @brief `SkyboxUniforms` is laid out to match the shader's constant buffer
 *  (brightness at offset 0, matrix at 16, 80 bytes), so it uses the raw
 *  encoder. */
template <>
struct ShaderTypeInfo<::epix::core_graph::SkyboxUniforms>
    : RawShaderType<::epix::core_graph::SkyboxUniforms> {};
}  // namespace epix::render::render_resource

namespace epix::core_graph {

/** @brief Uniquely identifies a configuration of the skybox shader (Bevy
 *  `SkyboxPipelineKey`, `skybox/mod.rs:182-187`). */
EPIX_EXPORT struct EPIX_API SkyboxPipelineKey {
    /** @brief True when the view renders to a high dynamic range target. */
    bool hdr = false;
    /** @brief The view's MSAA sample count. */
    std::uint32_t samples = 1;
    /** @brief The depth format of the view's depth attachment. */
    wgpu::TextureFormat depth_format = wgpu::TextureFormat::eUndefined;

    [[nodiscard]] bool operator==(const SkyboxPipelineKey&) const noexcept;
};

/** @brief Draws a cubemap behind the scene (Bevy `SkyboxPipeline`).
 *
 *  Its single bind group holds the cube texture, a filtering sampler, the
 *  view uniform (dynamic) and the skybox uniforms. */
EPIX_EXPORT struct EPIX_API SkyboxPipeline {
    using Key = SkyboxPipelineKey;

    /** @brief The bind group layout shared by every specialized pipeline. */
    render::BindGroupLayoutDescriptor bind_group_layout;
    /** @brief A handle that identifies the compiled shader. */
    assets::Handle<shader::Shader> shader;

    /** @brief Creates the pipeline and its layout for `shader` (Bevy
     *  `SkyboxPipeline::new`, `skybox/mod.rs:156-175`). */
    static SkyboxPipeline create(assets::Handle<shader::Shader> shader);

    /** @brief Builds the render descriptor for `key` (Bevy
     *  `SpecializedRenderPipeline for SkyboxPipeline`, `:189-238`): writes no
     *  depth, compares with `GreaterEqual`, and targets the HDR or default
     *  colour format with no blending. */
    [[nodiscard]] render::RenderPipelineDescriptor specialize(Key key) const;
};

/** @brief The specialized skybox pipeline chosen for one view (Bevy
 *  `SkyboxPipelineId`). */
EPIX_EXPORT struct SkyboxPipelineId {
    render::CachedPipelineId pipeline_id;
};

/** @brief A view's skybox bind group and the dynamic offset of its uniform
 *  entry (Bevy `SkyboxBindGroup(pub (BindGroup, u32))`). */
EPIX_EXPORT struct SkyboxBindGroup {
    wgpu::BindGroup bind_group;
    /** @brief The byte offset of this view's `SkyboxUniforms` entry. */
    std::uint32_t uniform_offset = 0;
};

namespace core_3d {
/** @brief The prepass markers the motion-vector skybox keys off. They are
 *  declared rather than included, because `core3d.hpp` includes this header and
 *  that include cannot run the other way (both are core_graph). */
struct NormalPrepass;
struct MotionVectorPrepass;
}  // namespace core_3d

/** @brief The per-view previous-frame data this header's prepass systems bind;
 *  its definition is in `core_graph/prepass.hpp`, which cannot be included here
 *  for the same reason as the markers above. */
struct PreviousViewUniforms;

/** @brief Uniquely identifies a configuration of the skybox's motion-vector
 *  prepass pipeline (Bevy `SkyboxPrepassPipelineKey`,
 *  `skybox/prepass.rs:46-50`). */
EPIX_EXPORT struct EPIX_API SkyboxPrepassPipelineKey {
    /** @brief The view's MSAA sample count. */
    std::uint32_t samples = 1;
    /** @brief True when the view also writes normals, which adds a target. */
    bool normal_prepass = false;

    [[nodiscard]] bool operator==(const SkyboxPrepassPipelineKey&) const noexcept;
};

/** @brief Writes the skybox's motion vectors into the prepass targets, so TAA
 *  and motion blur treat the skybox correctly (Bevy `SkyboxPrepassPipeline`,
 *  `skybox/prepass.rs:33-43`).
 *
 *  Its bind group is the view uniform and the previous view uniform, both
 *  dynamic, and it renders into the prepass colour attachments rather than a
 *  colour target of its own. */
EPIX_EXPORT struct EPIX_API SkyboxPrepassPipeline {
    using Key = SkyboxPrepassPipelineKey;

    /** @brief The bind group layout shared by every specialized pipeline. */
    render::BindGroupLayoutDescriptor bind_group_layout;
    /** @brief The shared fullscreen-triangle vertex shader. */
    FullscreenShader fullscreen_shader;
    /** @brief The motion-vector fragment shader. */
    assets::Handle<shader::Shader> fragment_shader;

    /** @brief Creates the pipeline and its layout (Bevy
     *  `init_skybox_prepass_pipeline`, `:62-81`). */
    static SkyboxPrepassPipeline create(FullscreenShader fullscreen_shader,
                                        assets::Handle<shader::Shader> fragment_shader);

    /** @brief Builds the render descriptor for `key` (Bevy
     *  `SpecializedRenderPipeline for SkyboxPrepassPipeline`, `:83-111`): the
     *  fullscreen vertex stage, the main skybox's depth state without a depth
     *  write, and the prepass targets for the requested prepasses with motion
     *  vectors always written and deferred never. */
    [[nodiscard]] render::RenderPipelineDescriptor specialize(Key key) const;
};

/** @brief The specialized skybox prepass pipeline chosen for one view (Bevy
 *  `RenderSkyboxPrepassPipeline`). */
EPIX_EXPORT struct RenderSkyboxPrepassPipeline {
    render::CachedPipelineId pipeline_id;
};

/** @brief A view's skybox prepass bind group, which the prepass node adds to its
 *  render pass (Bevy `SkyboxPrepassBindGroup`). */
EPIX_EXPORT struct SkyboxPrepassBindGroup {
    wgpu::BindGroup bind_group;
};

/** @brief Specializes the prepass pipeline for every view that has a `Skybox`
 *  and a `MotionVectorPrepass` (Bevy `prepare_skybox_prepass_pipelines`,
 *  `skybox/prepass.rs:113-133`). */
EPIX_EXPORT EPIX_API void prepare_skybox_prepass_pipelines(
    ecs::Commands commands,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::ResMut<render::SpecializedRenderPipelines<SkyboxPrepassPipeline>> pipelines,
    ecs::Res<SkyboxPrepassPipeline> pipeline,
    ecs::Query<ecs::Item<ecs::Entity, ecs::Has<core_3d::NormalPrepass>, ecs::Has<core_3d::MotionVectorPrepass>,
                         const render::view::Msaa&>,
               ecs::With<Skybox>>
        views);

/** @brief Creates each view's prepass bind group from the view uniforms and the
 *  previous view uniforms, so the prepass can compute the camera's motion
 *  (Bevy `prepare_skybox_prepass_bind_groups`, `skybox/prepass.rs:138-163`). */
EPIX_EXPORT EPIX_API void prepare_skybox_prepass_bind_groups(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> device,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::Res<SkyboxPrepassPipeline> pipeline,
    ecs::Res<render::view::ViewUniforms> view_uniforms,
    ecs::Res<PreviousViewUniforms> previous_view_uniforms,
    ecs::Query<ecs::Item<ecs::Entity, ecs::Has<core_3d::MotionVectorPrepass>>, ecs::With<Skybox>> views);

/** @brief Derives each view's `SkyboxUniforms` from its `Skybox` and uploads
 *  them, inserting the view's `DynamicUniformIndex` (Bevy derives the uniforms
 *  during extraction and uploads them through
 *  `UniformComponentPlugin<SkyboxUniforms>`; `ExtractComponent<C>::Out` is a
 *  single component here, so the derivation and the upload share one system).
 *  The brightness is scaled by the view's extracted exposure, as Bevy's
 *  extraction does. */
EPIX_EXPORT EPIX_API void prepare_skybox_uniforms(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> device,
    ecs::Res<wgpu::Queue> queue,
    ecs::ResMut<render::ComponentUniforms<SkyboxUniforms>> skybox_uniforms,
    ecs::Query<ecs::Item<ecs::Entity, const Skybox&, const render::camera::ExtractedCamera&>> views);

/** @brief Specializes the skybox pipeline for every view with a `Skybox` (Bevy
 *  `prepare_skybox_pipelines`, `skybox/mod.rs:243-265`). */
EPIX_EXPORT EPIX_API void prepare_skybox_pipelines(
    ecs::Commands commands,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::ResMut<render::SpecializedRenderPipelines<SkyboxPipeline>> pipelines,
    ecs::Res<SkyboxPipeline> pipeline,
    ecs::Query<ecs::Item<ecs::Entity, const render::view::ExtractedView&, const render::view::Msaa&>,
               ecs::With<Skybox>>
        views);

/** @brief Creates each view's skybox bind group from its cubemap's cube view,
 *  the view uniform and its skybox uniforms (Bevy `prepare_skybox_bind_groups`,
 *  `skybox/mod.rs:270-302`). */
EPIX_EXPORT EPIX_API void prepare_skybox_bind_groups(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> device,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::Res<SkyboxPipeline> pipeline,
    ecs::Res<render::view::ViewUniforms> view_uniforms,
    ecs::Res<render::ComponentUniforms<SkyboxUniforms>> skybox_uniforms,
    ecs::Res<render::RenderAssets<render::texture::GpuImage>> images,
    ecs::Query<ecs::Item<ecs::Entity, const Skybox&, const render::DynamicUniformIndex<SkyboxUniforms>&>> views);

/** @brief Installs the skybox pipeline (Bevy `SkyboxPlugin`, `skybox/mod.rs:44-75`).
 *
 *  The shader is registered and loaded here, the specialization cache is
 *  initialized and the pipeline itself is built in `RenderStartup`, exactly as
 *  Bevy's `init_skybox_pipeline` does. */
EPIX_EXPORT struct EPIX_API SkyboxPlugin {
    void attach(app::App& app) const;
};

}  // namespace epix::core_graph

namespace epix::render {
/** @brief Extracts the skybox marker onto the view (Bevy's extraction also
 *  derives `SkyboxUniforms`; see `prepare_skybox_uniforms`). */
template <>
struct ExtractComponent<core_graph::Skybox> {
    using QueryData   = const core_graph::Skybox&;
    using QueryFilter = epix::ecs::Filter<>;
    using Out         = core_graph::Skybox;
    static std::optional<Out> extract_component(const core_graph::Skybox& item) { return item; }
};
}  // namespace epix::render

template <>
struct EPIX_API std::hash<epix::core_graph::SkyboxPipelineKey> {
    std::size_t operator()(const epix::core_graph::SkyboxPipelineKey& key) const noexcept;
};

template <>
struct EPIX_API std::hash<epix::core_graph::SkyboxPrepassPipelineKey> {
    std::size_t operator()(const epix::core_graph::SkyboxPrepassPipelineKey& key) const noexcept;
};
