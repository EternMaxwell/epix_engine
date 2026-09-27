#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/mesh/morph.hpp>
#include <epix/render/bind_group_layout.hpp>
#include <epix/render/bind_group_layout_entries.hpp>
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

/** @brief The individual entries the mesh bind group layouts are built from
 *  (Bevy `render/mesh_bindings.rs:25-78`).
 *
 *  Each returns a deferred entry: the binding number and the collection's
 *  default visibility are applied when it is placed in a layout, except where the
 *  entry sets its own visibility, which is what Bevy's `.visibility(..)` does. */
namespace mesh_layout_entry {

/** @brief The mesh model uniform (Bevy `layout_entry::model`). */
inline render::render_resource::BindGroupLayoutEntryBuilder model(const wgpu::Limits& limits) {
    return render::render_resource::BindGroupLayoutEntryBuilder{
        wgpu::BindGroupLayoutEntry().setBuffer(
            render::render_resource::GpuArrayBuffer<MeshUniform>::binding_layout(limits))}
        .visibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment);
}

/** @brief The joint matrices, as a storage buffer where the device has them and
 *  as a uniform buffer otherwise (Bevy `layout_entry::skinning`). */
inline render::render_resource::BindGroupLayoutEntryBuilder skinning(const wgpu::Limits& limits) {
    using render::render_resource::binding_types::storage_buffer_read_only;
    using render::render_resource::binding_types::uniform_buffer;
    const auto size = static_cast<std::uint64_t>(JOINT_BUFFER_SIZE);
    return skins_use_uniform_buffers(limits) ? uniform_buffer(true, size) : storage_buffer_read_only(false, size);
}

/** @brief The morph weights (Bevy `layout_entry::weights`). */
inline render::render_resource::BindGroupLayoutEntryBuilder weights() {
    return render::render_resource::binding_types::uniform_buffer(true, MORPH_BUFFER_SIZE);
}

/** @brief The morph target textures (Bevy `layout_entry::targets`). */
inline render::render_resource::BindGroupLayoutEntryBuilder targets() {
    return render::render_resource::binding_types::texture_3d(wgpu::TextureSampleType::eUnfilterableFloat);
}

/** @brief One view's lightmap texture (Bevy
 *  `layout_entry::lightmaps_texture_view`). */
inline render::render_resource::BindGroupLayoutEntryBuilder lightmaps_texture_view() {
    return render::render_resource::binding_types::texture_2d(wgpu::TextureSampleType::eFloat)
        .visibility(wgpu::ShaderStage::eFragment);
}

/** @brief The lightmap sampler (Bevy `layout_entry::lightmaps_sampler`). */
inline render::render_resource::BindGroupLayoutEntryBuilder lightmaps_sampler() {
    return render::render_resource::binding_types::sampler(wgpu::SamplerBindingType::eFiltering)
        .visibility(wgpu::ShaderStage::eFragment);
}

/** @brief The slab's lightmap textures as a binding array (Bevy
 *  `layout_entry::lightmaps_texture_view_array`).
 *
 *  Bevy writes `texture_2d(..).visibility(..).count(N)`: the array-ness is the
 *  entry's binding-array size, not a `2d-array` view dimension. */
inline render::render_resource::BindGroupLayoutEntryBuilder lightmaps_texture_view_array() {
    return render::render_resource::binding_types::texture_2d(wgpu::TextureSampleType::eFloat)
        .visibility(wgpu::ShaderStage::eFragment)
        .count(static_cast<std::uint32_t>(LIGHTMAPS_PER_SLAB));
}

/** @brief The slab's lightmap samplers as a binding array (Bevy
 *  `layout_entry::lightmaps_sampler_array`). */
inline render::render_resource::BindGroupLayoutEntryBuilder lightmaps_sampler_array() {
    return render::render_resource::binding_types::sampler(wgpu::SamplerBindingType::eFiltering)
        .visibility(wgpu::ShaderStage::eFragment)
        .count(static_cast<std::uint32_t>(LIGHTMAPS_PER_SLAB));
}

}  // namespace mesh_layout_entry

/** @brief Every bind group layout the default mesh shader can be specialized
 *  with (Bevy `MeshLayouts`, `render/mesh_bindings.rs:165-198`).
 *
 *  Eight layouts, because the model uniform is always group 1 binding 0 and the
 *  optional parts - joints, morph weights, morph target textures, lightmaps - are
 *  added at fixed binding numbers, so a shader can name them without knowing which
 *  mesh it is drawing. The two motion-vector layouts add bindings 6 and 7 for the
 *  previous frame's joints and weights. */
EPIX_EXPORT struct MeshLayouts {
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

inline MeshLayouts MeshLayouts::make(const wgpu::Limits& limits, bool binding_arrays_are_usable) {
    return MeshLayouts{
        .model_only             = model_only_layout(limits),
        .lightmapped            = lightmapped_layout(limits, binding_arrays_are_usable),
        .skinned                = skinned_layout(limits),
        .skinned_motion         = skinned_motion_layout(limits),
        .morphed                = morphed_layout(limits),
        .morphed_motion         = morphed_motion_layout(limits),
        .morphed_skinned        = morphed_skinned_layout(limits),
        .morphed_skinned_motion = morphed_skinned_motion_layout(limits),
    };
}

inline render::BindGroupLayoutDescriptor MeshLayouts::model_only_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    return render::BindGroupLayoutDescriptor::create(
        "mesh_layout", BindGroupLayoutEntries<>::single(wgpu::ShaderStage::eNone, mesh_layout_entry::model(limits)));
}

inline render::BindGroupLayoutDescriptor MeshLayouts::skinned_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    return render::BindGroupLayoutDescriptor::create(
        "skinned_mesh_layout", BindGroupLayoutEntries<>::with_indices(
                                   wgpu::ShaderStage::eVertex, std::pair{0u, mesh_layout_entry::model(limits)},
                                   // The current frame's joint matrix buffer.
                                   std::pair{1u, mesh_layout_entry::skinning(limits)})
                                   .entries());
}

inline render::BindGroupLayoutDescriptor MeshLayouts::skinned_motion_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    return render::BindGroupLayoutDescriptor::create(
        "skinned_motion_mesh_layout", BindGroupLayoutEntries<>::with_indices(
                                          wgpu::ShaderStage::eVertex, std::pair{0u, mesh_layout_entry::model(limits)},
                                          // The current frame's joint matrix buffer.
                                          std::pair{1u, mesh_layout_entry::skinning(limits)},
                                          // The previous frame's joint matrix buffer.
                                          std::pair{6u, mesh_layout_entry::skinning(limits)})
                                          .entries());
}

inline render::BindGroupLayoutDescriptor MeshLayouts::morphed_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    return render::BindGroupLayoutDescriptor::create(
        "morphed_mesh_layout", BindGroupLayoutEntries<>::with_indices(wgpu::ShaderStage::eVertex,
                                                                      std::pair{0u, mesh_layout_entry::model(limits)},
                                                                      // The current frame's morph weight buffer.
                                                                      std::pair{2u, mesh_layout_entry::weights()},
                                                                      std::pair{3u, mesh_layout_entry::targets()})
                                   .entries());
}

inline render::BindGroupLayoutDescriptor MeshLayouts::morphed_motion_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    // Bevy labels this one `morphed_mesh_layout` as well, the same string as
    // `morphed_layout`; kept as is.
    return render::BindGroupLayoutDescriptor::create(
        "morphed_mesh_layout", BindGroupLayoutEntries<>::with_indices(wgpu::ShaderStage::eVertex,
                                                                      std::pair{0u, mesh_layout_entry::model(limits)},
                                                                      // The current frame's morph weight buffer.
                                                                      std::pair{2u, mesh_layout_entry::weights()},
                                                                      std::pair{3u, mesh_layout_entry::targets()},
                                                                      // The previous frame's morph weight buffer.
                                                                      std::pair{7u, mesh_layout_entry::weights()})
                                   .entries());
}

inline render::BindGroupLayoutDescriptor MeshLayouts::morphed_skinned_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    return render::BindGroupLayoutDescriptor::create(
        "morphed_skinned_mesh_layout",
        BindGroupLayoutEntries<>::with_indices(
            wgpu::ShaderStage::eVertex, std::pair{0u, mesh_layout_entry::model(limits)},
            // The current frame's joint matrix buffer.
            std::pair{1u, mesh_layout_entry::skinning(limits)},
            // The current frame's morph weight buffer.
            std::pair{2u, mesh_layout_entry::weights()}, std::pair{3u, mesh_layout_entry::targets()})
            .entries());
}

inline render::BindGroupLayoutDescriptor MeshLayouts::morphed_skinned_motion_layout(const wgpu::Limits& limits) {
    using render::render_resource::BindGroupLayoutEntries;
    return render::BindGroupLayoutDescriptor::create(
        "morphed_skinned_motion_mesh_layout",
        BindGroupLayoutEntries<>::with_indices(
            wgpu::ShaderStage::eVertex, std::pair{0u, mesh_layout_entry::model(limits)},
            // The current frame's joint matrix buffer.
            std::pair{1u, mesh_layout_entry::skinning(limits)},
            // The current frame's morph weight buffer.
            std::pair{2u, mesh_layout_entry::weights()}, std::pair{3u, mesh_layout_entry::targets()},
            // The previous frame's joint matrix buffer.
            std::pair{6u, mesh_layout_entry::skinning(limits)},
            // The previous frame's morph weight buffer.
            std::pair{7u, mesh_layout_entry::weights()})
            .entries());
}

inline render::BindGroupLayoutDescriptor MeshLayouts::lightmapped_layout(const wgpu::Limits& limits,
                                                                         bool binding_arrays_are_usable) {
    using render::render_resource::BindGroupLayoutEntries;
    if (binding_arrays_are_usable) {
        return render::BindGroupLayoutDescriptor::create(
            "lightmapped_mesh_layout", BindGroupLayoutEntries<>::with_indices(
                                           wgpu::ShaderStage::eVertex, std::pair{0u, mesh_layout_entry::model(limits)},
                                           std::pair{4u, mesh_layout_entry::lightmaps_texture_view_array()},
                                           std::pair{5u, mesh_layout_entry::lightmaps_sampler_array()})
                                           .entries());
    }
    return render::BindGroupLayoutDescriptor::create(
        "lightmapped_mesh_layout", BindGroupLayoutEntries<>::with_indices(
                                       wgpu::ShaderStage::eVertex, std::pair{0u, mesh_layout_entry::model(limits)},
                                       std::pair{4u, mesh_layout_entry::lightmaps_texture_view()},
                                       std::pair{5u, mesh_layout_entry::lightmaps_sampler()})
                                       .entries());
}

/** @brief The entries the mesh bind groups are created from (Bevy's
 *  `mod bind_group_entry`, `render/mesh_bindings.rs:86-160`).
 *
 *  Each one names the binding it fills; what varies between them is the
 *  resource and, for the joint buffer, the size the layout declares: Bevy binds
 *  a fixed slice when skins are uniforms and the whole buffer when they are
 *  storage. Bevy's `BindingResource` is not a type here - the port builds
 *  `wgpu::BindGroupEntry` directly, as its material bind groups already do - so
 *  the functions below take the resource they bind rather than a variant. */
namespace bind_group_entry {

/** @brief A buffer entry covering the whole buffer when `size` is empty (Bevy
 *  `bind_group_entry::entry`). */
inline wgpu::BindGroupEntry entry(std::uint32_t binding, std::optional<std::uint64_t> size,
                                  const wgpu::Buffer& buffer) {
    return wgpu::BindGroupEntry()
        .setBinding(binding)
        .setBuffer(buffer)
        .setOffset(0)
        .setSize(size ? *size : std::numeric_limits<std::uint64_t>::max());
}

/** @brief The mesh model buffer (Bevy `bind_group_entry::model`, which takes a
 *  `BindingResource` because the bindless path binds a texture view array to the
 *  same binding; the two paths are separate overloads here). */
inline wgpu::BindGroupEntry model(std::uint32_t binding, const wgpu::Buffer& buffer) {
    return entry(binding, std::nullopt, buffer);
}

/** @brief The joint matrices, bound as a fixed-size slice when they are uniforms
 *  (Bevy `bind_group_entry::skinning`). Bevy reads the device's limits here; the
 *  port takes the fact itself, as its other capability-dependent helpers do. */
inline wgpu::BindGroupEntry skinning(bool skins_use_uniform_buffers, std::uint32_t binding,
                                     const wgpu::Buffer& buffer) {
    const std::optional<std::uint64_t> size =
        skins_use_uniform_buffers ? std::optional<std::uint64_t>(JOINT_BUFFER_SIZE) : std::nullopt;
    return entry(binding, size, buffer);
}

/** @brief The morph weights, whose layout declares a fixed size (Bevy
 *  `bind_group_entry::weights`). */
inline wgpu::BindGroupEntry weights(std::uint32_t binding, const wgpu::Buffer& buffer) {
    return entry(binding, MORPH_BUFFER_SIZE, buffer);
}

/** @brief The morph target texture (Bevy `bind_group_entry::targets`). */
inline wgpu::BindGroupEntry targets(std::uint32_t binding, const wgpu::TextureView& texture) {
    return wgpu::BindGroupEntry().setBinding(binding).setTextureView(texture);
}

/** @brief A lightmap texture view (Bevy
 *  `bind_group_entry::lightmaps_texture_view`). */
inline wgpu::BindGroupEntry lightmaps_texture_view(std::uint32_t binding, const wgpu::TextureView& texture) {
    return wgpu::BindGroupEntry().setBinding(binding).setTextureView(texture);
}

/** @brief The lightmap sampler (Bevy `bind_group_entry::lightmaps_sampler`). */
inline wgpu::BindGroupEntry lightmaps_sampler(std::uint32_t binding, const wgpu::Sampler& sampler) {
    return wgpu::BindGroupEntry().setBinding(binding).setSampler(sampler);
}

}  // namespace bind_group_entry

}  // namespace epix::pbr
