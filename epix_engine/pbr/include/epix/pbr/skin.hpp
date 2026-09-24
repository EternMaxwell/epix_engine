#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <webgpu/webgpu.hpp>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief The total number of joints a skinned mesh can have (Bevy `MAX_JOINTS`,
 *  `bevy_pbr/src/render/skin.rs:31`).
 *
 *  The rest of `skin.rs` - `SkinUniforms`, `SkinIndex`, `prepare_skins`,
 *  `extract_skins` - lands with the skinning port; this is what the mesh bind
 *  group layouts need from it. */
EPIX_EXPORT inline constexpr std::size_t MAX_JOINTS = 256;

/** @brief Whether skinning has to use uniform buffers with dynamic offsets
 *  (Bevy `skins_use_uniform_buffers`, `render/skin.rs:194`).
 *
 *  Storage buffers are the fast path; a device without them falls back to
 *  uniforms, which is why the mesh layouts' joint entry and the bind group's
 *  buffer size both consult this. Bevy memoises the answer in a `OnceLock`, so
 *  the first device decides for the process; the function-local static below has
 *  the same property. */
EPIX_EXPORT inline bool skins_use_uniform_buffers(const wgpu::Limits& limits) {
    static const bool use_uniform_buffers = limits.maxStorageBuffersPerShaderStage == 0;
    return use_uniform_buffers;
}

}  // namespace epix::pbr
