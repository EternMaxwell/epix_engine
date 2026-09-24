#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstdint>
#include <epix/core_graph/core3d.hpp>
#include <epix/render/bind_group_layout_entries.hpp>
#include <optional>
#include <webgpu/webgpu.hpp>
#include <epix/common.hpp>
#endif

#include <epix/pbr/mesh_pipeline_key.hpp>

namespace epix::pbr {

/** @brief The four prepass texture entries the mesh view layout adds, one per
 *  prepass, absent when the key does not ask for it (Bevy
 *  `prepass::get_bind_group_layout_entries`, `prepass/prepass_bindings.rs:13-57`).
 *
 *  The order is Bevy's: depth, normals, motion vectors, deferred, and the binding
 *  numbers the caller gives them are 20 to 23. Depth is a depth texture; normals
 *  and motion vectors are unfilterable float textures, multisampled exactly when the
 *  key is; the deferred G-buffer is a `Uint` texture with no multisampled form,
 *  because a deferred pass cannot be multisampled. */
EPIX_EXPORT inline std::array<std::optional<render::render_resource::BindGroupLayoutEntryBuilder>, 4>
prepass_bind_group_layout_entries(const MeshPipelineViewLayoutKey& layout_key) {
    using render::render_resource::binding_types::texture_2d;
    using render::render_resource::binding_types::texture_2d_multisampled;
    using render::render_resource::binding_types::texture_depth_2d;
    using render::render_resource::binding_types::texture_depth_2d_multisampled;

    std::array<std::optional<render::render_resource::BindGroupLayoutEntryBuilder>, 4> entries{};
    const bool multisampled = layout_key.contains(MeshPipelineViewLayoutKey::MULTISAMPLED);

    if (layout_key.contains(MeshPipelineViewLayoutKey::DEPTH_PREPASS)) {
        entries[0] = multisampled ? texture_depth_2d_multisampled() : texture_depth_2d();
    }
    if (layout_key.contains(MeshPipelineViewLayoutKey::NORMAL_PREPASS)) {
        entries[1] = multisampled ? texture_2d_multisampled(wgpu::TextureSampleType::eUnfilterableFloat)
                                  : texture_2d(wgpu::TextureSampleType::eUnfilterableFloat);
    }
    if (layout_key.contains(MeshPipelineViewLayoutKey::MOTION_VECTOR_PREPASS)) {
        entries[2] = multisampled ? texture_2d_multisampled(wgpu::TextureSampleType::eUnfilterableFloat)
                                  : texture_2d(wgpu::TextureSampleType::eUnfilterableFloat);
    }
    if (layout_key.contains(MeshPipelineViewLayoutKey::DEFERRED_PREPASS)) {
        entries[3] = texture_2d(wgpu::TextureSampleType::eUint);
    }
    return entries;
}

/** @brief The four prepass texture views the bind group binds, absent when the view
 *  has no such prepass (Bevy `prepass::get_bindings`,
 *  `prepass/prepass_bindings.rs:59-75`).
 *
 *  Three of the four reuse the attachment's cached default view. Depth is the
 *  exception: Bevy makes a view restricted to the depth aspect, because the depth
 *  attachment's own view also carries the stencil and cannot be sampled. */
EPIX_EXPORT inline std::array<std::optional<wgpu::TextureView>, 4> prepass_bindings(
    std::optional<std::reference_wrapper<const ::epix::core_graph::core_3d::ViewPrepassTextures>> prepass_textures) {
    std::array<std::optional<wgpu::TextureView>, 4> views{};
    if (!prepass_textures) return views;

    const auto& textures = prepass_textures->get();
    if (textures.depth) {
        wgpu::TextureViewDescriptor descriptor;
        descriptor.setLabel("prepass_depth").setAspect(wgpu::TextureAspect::eDepthOnly);
        views[0] = textures.depth->texture.texture.createView(descriptor);
    }
    views[1] = textures.normal_view();
    views[2] = textures.motion_vectors_view();
    views[3] = textures.deferred_view();
    return views;
}

}  // namespace epix::pbr
