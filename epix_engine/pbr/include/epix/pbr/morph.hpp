#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/camera/visibility.hpp>
#include <epix/common.hpp>
#include <epix/mesh/morph.hpp>
#include <epix/render/mesh/offset_allocator.hpp>
#include <epix/render/render_phase.hpp>
#include <epix/render/render_resource.hpp>
#include <epix/render/sync_world.hpp>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::pbr {

/** @brief The byte offset a morphed mesh's weights start at within
 *  `MorphUniforms` (Bevy `MorphIndex`, `render/morph.rs:15-18`). */
EPIX_EXPORT struct MorphIndex {
    std::uint32_t index = 0;
};

/** @brief Maps each morphed mesh to its offset in `MorphUniforms`, for this
 *  frame and the previous one (Bevy `MorphIndices`, `render/morph.rs:25-34`).
 *
 *  Both frames are kept because motion vectors need the previous weights, the
 *  same reason `SkinUniforms` is double-buffered. */
EPIX_EXPORT struct EPIX_API MorphIndices {
    /** @brief The offset each mesh's weights start at in `current_buffer`. */
    render::sync_world::MainEntityHashMap<MorphIndex> current;
    /** @brief The same for `prev_buffer`, last frame's mapping. */
    render::sync_world::MainEntityHashMap<MorphIndex> prev;
};

/** @brief The morph weights of every morphed mesh, on the GPU (Bevy
 *  `MorphUniforms`, `render/morph.rs:36-57`).
 *
 *  Double-buffered like `SkinUniforms`: every frame the buffers swap and the one
 *  from two frames ago becomes the current one, so the previous frame's weights
 *  are always available for motion vectors. Both are uniform buffers, which is
 *  why `add_to_alignment<f32>` pads to the 256-byte binding alignment. */
EPIX_EXPORT struct EPIX_API MorphUniforms {
    render::render_resource::RawBufferVec<float> current_buffer;
    render::render_resource::RawBufferVec<float> prev_buffer;

    MorphUniforms()
        : current_buffer(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst),
          prev_buffer(wgpu::BufferUsage::eUniform | wgpu::BufferUsage::eCopyDst) {}
};

/** @brief Pad a buffer to the 256-byte alignment WebGPU requires of a uniform
 *  binding (Bevy `add_to_alignment`, `render/morph.rs:83-105`).
 *
 *  The element size must divide or be divided by 256, which for `f32` it does;
 *  a type that satisfied neither would make every offset in the buffer
 *  unreachable, so the check is a compile-time assertion rather than a runtime
 *  panic. */
EPIX_EXPORT EPIX_API void add_to_alignment_f32(render::render_resource::RawBufferVec<float>& buffer);

/** @brief Upload the morph weights for this frame (Bevy `prepare_morphs`,
 *  `render/morph.rs:59-75`).
 *
 *  Nothing is written when there are no weights at all, and `prev_buffer` is
 *  deliberately left alone: it already holds last frame's data on the GPU. */
EPIX_EXPORT EPIX_API void prepare_morphs(ecs::Res<wgpu::Device> render_device, ecs::Res<wgpu::Queue> render_queue,
                                         ecs::ResMut<MorphUniforms> uniform);

/** @brief Collect this frame's morph weights from the visible morphed meshes
 *  (Bevy `extract_morphs`, `render/morph.rs:109-143`).
 *
 *  The two buffers and the two index maps are swapped first, so what was current
 *  becomes the previous frame's record; then the current ones are cleared and
 *  refilled. Each mesh's weights are padded to `MAX_MORPH_WEIGHTS` with zeros -
 *  a fixed-size uniform binding cannot be sized per mesh - aligned, and recorded
 *  at the byte offset they started at. Invisible meshes contribute nothing. */
EPIX_EXPORT EPIX_API void extract_morphs(
    ecs::ResMut<MorphIndices> morph_indices, ecs::ResMut<MorphUniforms> uniform,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const camera::ViewVisibility&, const mesh::MorphWeights&>>>);

/** @brief Tag every morphed mesh as un-batchable (Bevy
 *  `no_automatic_morph_batching`, `render/morph.rs:147-154`).
 *
 *  Morph targets are read through per-mesh bindings, so two morphed meshes cannot
 *  share a batch at all - and unlike the skin case there is no platform on which
 *  the question changes, so there is no device check. */
EPIX_EXPORT EPIX_API void no_automatic_morph_batching(
    ecs::Commands commands,
    ecs::Query<ecs::Item<ecs::Entity>,
               ecs::Filter<ecs::With<mesh::MorphWeights>, ecs::Without<render::phase::NoAutomaticBatching>>> query);

}  // namespace epix::pbr
