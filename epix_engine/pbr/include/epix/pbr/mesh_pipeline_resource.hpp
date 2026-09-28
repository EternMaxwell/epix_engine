#pragma once

#include <unordered_map>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/render/bind_group_layout.hpp>
#include <epix/render/render_resource.hpp>
#include <epix/shader.hpp>
#include <optional>
#include <utility>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/pbr/mesh_bindings.hpp>
#include <epix/pbr/mesh_pipeline_key.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <epix/pbr/mesh_view_bindings.hpp>
#include <epix/pbr/skin.hpp>

namespace epix::pbr {

/** @brief All the data needed to construct a pipeline for rendering 3D meshes
 *  (Bevy `MeshPipeline`, `bevy_pbr/src/render/mesh.rs:1847-1881`).
 *
 *  Bevy declares this in the same file as the specialization helpers; the port
 *  keeps it apart so `mesh_view_bindings.hpp`'s dependency on this file's
 *  `IRRADIANCE_VOLUMES_ARE_USABLE` stays one-way.
 *
 *  Bevy's `FromWorld` counterpart is `make`: it takes the device facts Bevy
 *  reads off `RenderDevice`/`RenderAdapter` as parameters and the shader handle
 *  Bevy loads with `load_embedded_asset!(world, "mesh.wgsl")` from the caller,
 *  because `mesh.wgsl` is not ported yet (`PBR-262`/`PBR-265`). */
EPIX_EXPORT struct EPIX_API MeshPipeline {
    /** @brief A reference to all the mesh pipeline view layouts. */
    MeshPipelineViewLayouts view_layouts;
    wgpu::BufferBindingType clustered_forward_buffer_binding_type = wgpu::BufferBindingType::eReadOnlyStorage;
    MeshLayouts mesh_layouts;
    /** @brief The shader asset handle. */
    assets::Handle<shader::Shader> shader;
    /** @brief `MeshUniform`s are stored in arrays in buffers. If storage buffers
     *  are available they are used and this is empty, otherwise uniform buffers
     *  are used with batches of this many `MeshUniform`s at dynamic offsets. */
    std::optional<std::uint32_t> per_object_buffer_batch_size;

    /** @brief Whether binding arrays (bindless textures) are usable on the
     *  current render device, which is what decides whether reflection probes
     *  can be used. */
    bool binding_arrays_are_usable = false;
    /** @brief Whether clustered decals are usable on the current render device. */
    bool clustered_decals_are_usable = false;
    /** @brief Whether skins will use uniform buffers, because storage buffers
     *  are unavailable on this platform. */
    bool skins_use_uniform_buffers = false;

    /** @brief Build the pipeline for a device with the given facts (Bevy
     *  `impl FromWorld for MeshPipeline`, `render/mesh.rs:1883-1912`).
     *
     *  `limits` stands in for Bevy's `RenderDevice::limits()`, the two binding
     *  facts for `RenderAdapter`'s capabilities, and the shader handle for the
     *  embedded asset load. `MeshLayouts::new(&render_device, &render_adapter)`
     *  becomes `MeshLayouts::make`, which needs the same two facts. */
    static MeshPipeline make(const wgpu::Limits& limits,
                             wgpu::BufferBindingType clustered_forward_buffer_binding_type,
                             assets::Handle<shader::Shader> shader,
                             MeshPipelineViewLayouts view_layouts,
                             bool binding_arrays_are_usable,
                             bool clustered_decals_are_usable) {
        return MeshPipeline{
            .view_layouts                          = std::move(view_layouts),
            .clustered_forward_buffer_binding_type = clustered_forward_buffer_binding_type,
            .mesh_layouts                          = MeshLayouts::make(limits, binding_arrays_are_usable),
            .shader                                = std::move(shader),
            .per_object_buffer_batch_size = render::render_resource::GpuArrayBuffer<MeshUniform>::batch_size(limits),
            .binding_arrays_are_usable    = binding_arrays_are_usable,
            .clustered_decals_are_usable  = clustered_decals_are_usable,
            // Qualified: the member of the same name shadows the free function.
            .skins_use_uniform_buffers = ::epix::pbr::skins_use_uniform_buffers(limits),
        };
    }

    /** @brief The view layout a key asks for (Bevy `MeshPipeline::get_view_layout`,
     *  `render/mesh.rs:1915-1920`). */
    const MeshPipelineViewLayout& get_view_layout(const MeshPipelineViewLayoutKey& layout_key) const {
        return view_layouts.get_view_layout(layout_key);
    }

    /** @brief Build the render pipeline descriptor for a key and a mesh vertex
     *  buffer layout (Bevy's `SpecializedMeshPipeline for MeshPipeline`,
     *  `render/mesh.rs:2372-2740`).
     *
     *  The shader definitions and vertex attributes come from the helpers this
     *  module already declares; what this adds is the descriptor assembly: the
     *  view layouts, the morph and skinning layout the key selects, the HDR
     *  target format, and the depth, stencil and multisample state. */
    std::expected<render::RenderPipelineDescriptor, render::SpecializedMeshPipelineError> specialize(
        const MeshPipelineKey& key, const mesh::MeshVertexBufferLayoutRef& layout) const;
};


/** @brief The mesh pipeline key of each view, refreshed when it changes (Bevy
 *  `ViewKeyCache`, `render/mesh.rs:116-117`): the 3D counterpart of the 2D
 *  resource of the same name, which the port already has in `sprite_render`. */
EPIX_EXPORT struct ViewKeyCache {
    // Keyed by `RetainedViewEntity`, as Bevy keys it (render/mesh.rs:118): the view
    // a specialisation is recorded for is the render view, not its main-world entity,
    // and batch 183 found the mismatch by trying to insert what the system reads.
    std::unordered_map<render::view::RetainedViewEntity, MeshPipelineKey> keys;
};

/** @brief When each view last needed specialization (Bevy
 *  `ViewSpecializationTicks`, `render/mesh.rs:119-120`). */
EPIX_EXPORT struct ViewSpecializationTicks {
    std::unordered_map<render::view::RetainedViewEntity, ecs::Tick> ticks;
};

}  // namespace epix::pbr
