#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/mesh/morph.hpp>
#include <epix/render/bind_group_layout.hpp>
#include <epix/render/bind_group_layout_entries.hpp>
#include <epix/render/pipeline_server.hpp>
#include <epix/render/render_resource.hpp>
#include <glm/glm.hpp>
#include <limits>
#include <optional>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/pbr/lightmap.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <epix/pbr/skin.hpp>

namespace epix::pbr {

/** @brief The size of one morph weight, in bytes (Bevy `MORPH_WEIGHT_SIZE`,
 *  `render/mesh_bindings.rs:12`). */
inline constexpr std::size_t MORPH_WEIGHT_SIZE = sizeof(float);

/** @brief The size of the morph weight buffer the layout declares (Bevy
 *  `MORPH_BUFFER_SIZE`, `render/mesh_bindings.rs:19`).
 *
 *  Bevy's value is chosen because it is guaranteed to work everywhere: a larger
 *  one would need a runtime check against the device's limits, which would mean
 *  the size could no longer be a constant. */
EPIX_EXPORT inline constexpr std::size_t MORPH_BUFFER_SIZE = mesh::MAX_MORPH_WEIGHTS * MORPH_WEIGHT_SIZE;

/** @brief The size of one joint matrix, in bytes (Bevy `JOINT_SIZE`,
 *  `render/mesh_bindings.rs:21`). */
inline constexpr std::size_t JOINT_SIZE = sizeof(glm::mat4);

/** @brief The size of the joint buffer the layout declares (Bevy
 *  `JOINT_BUFFER_SIZE`, `render/mesh_bindings.rs:22`). */
EPIX_EXPORT inline constexpr std::size_t JOINT_BUFFER_SIZE = MAX_JOINTS * JOINT_SIZE;


/** @brief Every bind group layout the default mesh shader can be specialized
 *  with (Bevy `MeshLayouts`, `render/mesh_bindings.rs:165-198`).
 *
 *  Eight layouts, because the model uniform is always group 1 binding 0 and the
 *  optional parts - joints, morph weights, morph target textures, lightmaps - are
 *  added at fixed binding numbers, so a shader can name them without knowing which
 *  mesh it is drawing. The two motion-vector layouts add bindings 6 and 7 for the
 *  previous frame's joints and weights. */
EPIX_EXPORT struct EPIX_API MeshLayouts {
    /** @brief The mesh model uniform (transform) and nothing else. */
    render::BindGroupLayoutDescriptor model_only;
    /** @brief Also the lightmap texture and sampler, one or a slab's worth. */
    render::BindGroupLayoutDescriptor lightmapped;
    /** @brief Also the joint matrices. */
    render::BindGroupLayoutDescriptor skinned;
    /** @brief Like `skinned`, plus the previous frame's joint matrices. */
    render::BindGroupLayoutDescriptor skinned_motion;
    /** @brief Also the morph weights and target textures. */
    render::BindGroupLayoutDescriptor morphed;
    /** @brief Like `morphed`, plus the previous frame's morph weights. */
    render::BindGroupLayoutDescriptor morphed_motion;
    /** @brief Both the joints and the morph targets. */
    render::BindGroupLayoutDescriptor morphed_skinned;
    /** @brief Both, plus the previous frame's joints and morph weights. */
    render::BindGroupLayoutDescriptor morphed_skinned_motion;

    /** @brief Build the eight layouts for a device (Bevy `MeshLayouts::new`).
     *
     *  Only the limits and one device capability are needed, not the device
     *  itself: the layouts are descriptors, and the pipeline cache interns the
     *  native layouts from them. `binding_arrays_are_usable` is Bevy's
     *  `binding_arrays_are_usable(render_device, render_adapter)`, which
     *  `MeshPipeline` owns in Bevy; it is a parameter here for the same reason
     *  `specialize_mesh_capability_shader_defs` takes the other capabilities. */
    static MeshLayouts make(const wgpu::Limits& limits, bool binding_arrays_are_usable);

    // ---------- Bind group creation (Bevy `render/mesh_bindings.rs:370-562`) ---
    //
    // One per layout: the model buffer, then whichever of the joint buffers, the
    // morph weight buffers and the morph target texture the layout declares. Bevy
    // reads the device's limits for the joint entries' size; these take that fact
    // as the joint entry does. The methods are named `create_*` because Bevy
    // gives a field and a method the same name (`model_only`) and the fields are
    // public, so C++ cannot have both. The lightmapped layout's method is not
    // here: its bindless branch needs the binding-array entries (`PBR-319`).
    wgpu::BindGroup create_model_only(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                               const wgpu::Buffer& model) const;
    wgpu::BindGroup create_skinned(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                            bool skins_use_uniform_buffers, const wgpu::Buffer& model,
                            const wgpu::Buffer& current_skin) const;
    wgpu::BindGroup create_skinned_motion(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                                   bool skins_use_uniform_buffers, const wgpu::Buffer& model,
                                   const wgpu::Buffer& current_skin, const wgpu::Buffer& prev_skin) const;
    wgpu::BindGroup create_morphed(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                            const wgpu::Buffer& model, const wgpu::Buffer& current_weights,
                            const wgpu::TextureView& targets) const;
    wgpu::BindGroup create_morphed_motion(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                                   const wgpu::Buffer& model, const wgpu::Buffer& current_weights,
                                   const wgpu::TextureView& targets, const wgpu::Buffer& prev_weights) const;
    wgpu::BindGroup create_morphed_skinned(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                                    bool skins_use_uniform_buffers, const wgpu::Buffer& model,
                                    const wgpu::Buffer& current_skin, const wgpu::Buffer& current_weights,
                                    const wgpu::TextureView& targets) const;
    /** @brief The bind group for meshes carrying a lightmap (Bevy
     *  `MeshLayouts::lightmapped`, `render/mesh_bindings.rs:385-416`).
     *
     *  Bevy branches on `bindless_lightmaps`: with binding arrays usable it binds
     *  the slab's whole array, otherwise the first slot's texture view and
     *  sampler. The slab supplies either through `build_binding_arrays` or
     *  `bindings_for_first_lightmap`. */
    wgpu::BindGroup create_lightmapped(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                                       const wgpu::Buffer& model, const LightmapSlab& lightmap_slab,
                                       bool bindless_lightmaps) const;
    wgpu::BindGroup create_morphed_skinned_motion(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                                           bool skins_use_uniform_buffers, const wgpu::Buffer& model,
                                           const wgpu::Buffer& current_skin, const wgpu::Buffer& current_weights,
                                           const wgpu::TextureView& targets, const wgpu::Buffer& prev_skin,
                                           const wgpu::Buffer& prev_weights) const;

   private:
    static render::BindGroupLayoutDescriptor model_only_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor skinned_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor skinned_motion_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor morphed_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor morphed_motion_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor morphed_skinned_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor morphed_skinned_motion_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor lightmapped_layout(const wgpu::Limits& limits,
                                                                bool binding_arrays_are_usable);
};

}  // namespace epix::pbr
