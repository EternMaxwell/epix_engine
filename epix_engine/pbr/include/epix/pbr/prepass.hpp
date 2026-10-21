#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/app.hpp>
#include <epix/common.hpp>
#include <epix/core_graph/prepass.hpp>
#include <epix/ecs.hpp>
#include <epix/render/bind_group_layout.hpp>
#include <epix/render/globals.hpp>
#include <epix/render/pipeline_server.hpp>
#include <epix/render/view.hpp>
#include <epix/render/visibility_range.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/pbr/mesh_bindings.hpp>
#include <epix/pbr/mesh_pipeline_resource.hpp>
#include <epix/pbr/light_meta.hpp>

namespace epix::pbr {

/** @brief The world transform an entity had last frame (Bevy
 *  `PreviousGlobalTransform`, `bevy_pbr/src/prepass/mod.rs:209`).
 *
 *  Bevy wraps an `Affine3A` and keeps it current with
 *  `update_previous_global_transforms`, so motion vectors and the motion-vector
 *  prepass can be derived without re-reading the main world; Epix's convention for
 *  transforms is the project's `glm::mat4`, taken from `GlobalTransform::matrix`.
 *
 *  The mesh extraction only asks whether the component is present - it sets
 *  `HAS_PREVIOUS_TRANSFORM` from that - while the prepass and the mesh uniforms
 *  read the matrix itself. */
EPIX_EXPORT struct PreviousGlobalTransform {
    glm::mat4 matrix{1.0f};
};

/** @brief Snapshot the last frame's camera matrices at the start of PreUpdate. */
EPIX_EXPORT EPIX_API void update_previous_view_data(
    ecs::Commands commands,
    ecs::Query<ecs::Item<ecs::Entity, const camera::Camera&, const transform::GlobalTransform&>,
               ecs::Or<ecs::With<camera::Camera3d>, ecs::With<ShadowView>>> views);

/** @brief Retain mesh history only when a 3D or shadow camera is active, without
 *  marking an unchanged previous transform modified. */
EPIX_EXPORT EPIX_API void update_mesh_previous_global_transforms(
    ecs::Commands commands,
    ecs::Query<const camera::Camera&, ecs::Or<ecs::With<camera::Camera3d>, ecs::With<ShadowView>>> views,
    ecs::Query<ecs::Item<ecs::Entity, const transform::GlobalTransform&>,
               ecs::Filter<ecs::With<mesh::Mesh3d>, ecs::Without<PreviousGlobalTransform>>> new_meshes,
    ecs::Query<ecs::Item<const transform::GlobalTransform&, ecs::Mut<PreviousGlobalTransform>>,
               ecs::With<mesh::Mesh3d>> meshes);

/** @brief Copy active camera history, remove inactive history and leave an
 *  active camera with no main-world history alone. */
EPIX_EXPORT EPIX_API void extract_camera_previous_view_data(
    ecs::Commands commands,
    app::Extract<ecs::Query<ecs::Item<const render::sync_world::RenderEntity&, const camera::Camera&,
                                    ecs::Opt<const core_graph::PreviousViewData&>>,
                            ecs::With<camera::Camera3d>>> cameras);

/** @brief Upload camera/shadow history in PrepareResources, falling back to
 *  the current extracted matrices when there is no saved history. */
EPIX_EXPORT EPIX_API void prepare_previous_view_uniforms(
    ecs::Commands commands, ecs::Res<wgpu::Device> device, ecs::Res<wgpu::Queue> queue,
    ecs::ResMut<core_graph::PreviousViewUniforms> previous_view_uniforms,
    ecs::Query<ecs::Item<ecs::Entity, const render::view::ExtractedView&,
                         ecs::Opt<const core_graph::PreviousViewData&>>,
               ecs::Or<ecs::With<camera::Camera3d>, ecs::With<ShadowView>>> views);

/** @brief The prepass pipeline's view layouts and the device facts the prepass
 *  needs (Bevy `PrepassPipeline`, `prepass/mod.rs:239-256`).
 *
 *  Bevy's `default_prepass_shader` and the `material_pipeline` it clones are not
 *  here: both are read only by `PrepassPipelineSpecializer`, which the port has
 *  not written, and the shader it loads has no port counterpart either.
 *
 *  Bevy reads the device and the adapter to fill the two facts below; Epix takes
 *  them from `MeshPipeline`, which computed the same values from the same device,
 *  which is why `init_prepass_pipeline` needs no adapter. */
EPIX_EXPORT struct EPIX_API PrepassPipeline {
    /** @brief The view layout for a view that writes motion vectors. */
    render::BindGroupLayoutDescriptor view_layout_motion_vectors;
    /** @brief The view layout for a view that writes none. */
    render::BindGroupLayoutDescriptor view_layout_no_motion_vectors;
    MeshLayouts mesh_layouts;
    render::BindGroupLayoutDescriptor empty_layout;

    /** @brief Whether skins will use uniform buffers, because storage buffers are
     *  unavailable on this platform. */
    bool skins_use_uniform_buffers = false;
    bool depth_clip_control_supported = false;
    /** @brief Whether binding arrays (a.k.a. bindless textures) are usable on the
     *  current render device. */
    bool binding_arrays_are_usable = false;
};

/** @brief The prepass pass's view bind groups: one for a view that writes motion
 *  vectors, one for a view that does not, and the empty group slot 1 binds (Bevy
 *  `PrepassViewBindGroup`, `prepass/mod.rs:685-690`).
 *
 *  The two real groups exist only once the buffers they bind do, which is what
 *  `prepare_prepass_view_bind_group` waits for. */
EPIX_EXPORT struct PrepassViewBindGroup {
    std::optional<wgpu::BindGroup> motion_vectors;
    std::optional<wgpu::BindGroup> no_motion_vectors;
    wgpu::BindGroup empty_bind_group;
};

/** @brief Insert the `PrepassPipeline` resource once the mesh pipeline exists
 *  (Bevy `init_prepass_pipeline`, `prepass/mod.rs:258-331`).
 *
 *  Bevy also takes the adapter and the `AssetServer` - for the binding facts and
 *  the embedded prepass shader. */
EPIX_EXPORT EPIX_API void init_prepass_pipeline(ecs::Commands commands, ecs::Res<wgpu::Device> render_device,
                                                ecs::Res<MeshPipeline> mesh_pipeline);

/** @brief Create the empty bind group slot 1 binds, which needs only the layout
 *  (Bevy `init_prepass_view_bind_group`, `prepass/mod.rs:692-708`). */
EPIX_EXPORT EPIX_API void init_prepass_view_bind_group(ecs::Commands commands, ecs::Res<wgpu::Device> render_device,
                                                       ecs::Res<render::PipelineServer> pipeline_server,
                                                       ecs::Res<PrepassPipeline> pipeline);

/** @brief Build the two view bind groups from the buffers they bind (Bevy
 *  `prepare_prepass_view_bind_group`, `prepass/mod.rs:710-760`). */
EPIX_EXPORT EPIX_API void prepare_prepass_view_bind_group(
    ecs::Res<wgpu::Device> render_device, ecs::Res<render::PipelineServer> pipeline_server,
    ecs::Res<PrepassPipeline> prepass_pipeline, ecs::Res<render::view::ViewUniforms> view_uniforms,
    ecs::Res<render::GlobalsBuffer> globals_buffer,
    ecs::Res<core_graph::PreviousViewUniforms> previous_view_uniforms,
    ecs::Res<render::view::RenderVisibilityRanges> visibility_ranges,
    ecs::ResMut<PrepassViewBindGroup> prepass_view_bind_group);

/** @brief Bevy `PrepassPipelinePlugin` (`prepass/mod.rs:69-99`), which
 *  the shared `MaterialsPlugin` installs (Bevy `material.rs:283`).
 *
 *  What it installs here is the pipeline resource and its two view bind groups.
 *  Bevy's other two pieces - the
 *  `prepass_io.wgsl` shader library and
 *  `SpecializedMeshPipelines<PrepassPipelineSpecializer>` - arrive with the
 *  prepass specializer, which the port has not written (PBR-19). */
EPIX_EXPORT struct EPIX_API PrepassPipelinePlugin {
    void attach(epix::app::App& app);
};

/** @brief Shared prepass history systems and draw registrations, installed once
 *  per app by MaterialsPlugin. Specialization/queueing remains PBR-19 work. */
EPIX_EXPORT struct EPIX_API PrepassPlugin {
    void attach(epix::app::App& app);
};

}  // namespace epix::pbr
