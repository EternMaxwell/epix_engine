#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/render/mesh/offset_allocator.hpp>
#include <epix/render/sync_world.hpp>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
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


/** @brief The total number of joints supported across the scene (Bevy
 *  `MAX_TOTAL_JOINTS`, `render/skin.rs:38`): the capacity the joint buffer is
 *  sized for. */
EPIX_EXPORT inline constexpr std::uint32_t MAX_TOTAL_JOINTS = 1024u * 1024u * 1024u;

/** @brief How many joints one allocation unit holds (Bevy
 *  `JOINTS_PER_ALLOCATION_UNIT`, `render/skin.rs:43`).
 *
 *  Some hardware requires uniform allocations on 256-byte boundaries, so a unit
 *  is four 64-byte matrices. */
EPIX_EXPORT inline constexpr std::uint32_t JOINTS_PER_ALLOCATION_UNIT =
    static_cast<std::uint32_t>(256 / sizeof(glm::mat4));

/** @brief The total number of joints a skinned mesh can have (Bevy `MAX_JOINTS`,
 *  `bevy_pbr/src/render/skin.rs:31`).
 *
 *  The rest of `skin.rs` - `SkinUniforms`, `SkinIndex`, `prepare_skins`,
 *  `extract_skins` - lands with the skinning port; this is what the mesh bind
 *  group layouts need from it. */
EPIX_EXPORT inline constexpr std::size_t MAX_JOINTS_LEGACY = 256;

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

/** @brief The location of the first joint matrix in the skin uniform buffer
 *  (Bevy `SkinByteOffset`, `render/skin.rs:55-76`). */
EPIX_EXPORT struct SkinByteOffset {
    std::uint32_t byte_offset = 0;

    /** @brief The address-space location of the joint matrix at `index` (Bevy
     *  `SkinByteOffset::from_index`, `skin.rs:64-68`). */
    static SkinByteOffset from_index(std::size_t index) {
        return SkinByteOffset{.byte_offset = static_cast<std::uint32_t>(index * sizeof(glm::mat4))};
    }

    /** @brief This offset in elements, each a 4x4 matrix (Bevy
     *  `SkinByteOffset::index`, `skin.rs:73-75`). */
    std::uint32_t index() const { return byte_offset / static_cast<std::uint32_t>(sizeof(glm::mat4)); }
};

/** @brief Allocation information about one skin (Bevy's private
 *  `SkinUniformInfo`, `render/skin.rs:177-190`). A struct's members are public
 *  in the port. */
EPIX_EXPORT struct SkinUniformInfo {
    /** @brief The joints' allocation within the current buffer. */
    render::mesh::offset_allocator::Allocation allocation;
    /** @brief The entities that make up the joints. */
    std::vector<render::sync_world::MainEntity> joints;

    /** @brief The offset in joints within the current staging buffer (Bevy
     *  `SkinUniformInfo::offset`, `skin.rs:185-189`). */
    std::uint32_t offset() const { return allocation.offset * JOINTS_PER_ALLOCATION_UNIT; }
};

/** @brief The GPU buffers holding joint matrices for all skinned meshes (Bevy
 *  `SkinUniforms`, `render/skin.rs:78-117,156-175`).
 *
 *  Double-buffered: each mesh's previous-frame joints are kept alongside the
 *  current frame's for motion vectors, and the buffers are swapped every frame.
 *  Bevy's `FromWorld` is `make`, which takes the device facts as parameters; the
 *  systems that fill it - `prepare_skins`, `extract_skins` and their helpers -
 *  are not ported yet, so this is the allocation bookkeeping and the accessors
 *  the batching adapter and the mesh bind groups read. Bevy's `SmallVec` of the
 *  one-skin-common case is a plain vector here. */
EPIX_EXPORT struct SkinUniforms {
    /** @brief The joint matrices for this frame, on the CPU. */
    std::vector<glm::mat4> current_staging_buffer;
    /** @brief The joint matrices for this frame, on the GPU. */
    wgpu::Buffer current_buffer;
    /** @brief The joint matrices for the previous frame, on the GPU. */
    wgpu::Buffer prev_buffer;
    /** @brief Places joints within `current_buffer`. */
    render::mesh::offset_allocator::Allocator allocator;
    /** @brief What is known about each skin. Bevy's field is private there. */
    render::sync_world::MainEntityHashMap<SkinUniformInfo> skin_uniform_info;
    /** @brief Each joint entity mapped to the skins it belongs to; conceptually
     *  a map of sets, used with change detection to update only what moved.
     *  Bevy's field is private there. */
    render::sync_world::MainEntityHashMap<std::vector<render::sync_world::MainEntity>> joint_to_skins;
    /** @brief The total number of joints in the scene, part of the heuristic
     *  that decides whether fine-grained change detection is worth it. */
    std::size_t total_joints = 0;

    /** @brief Build the joint buffers for a device (Bevy `impl FromWorld for
     *  SkinUniforms`, `render/skin.rs:119-154`), which Bevy also uses to decide
     *  the buffers' usage from `skins_use_uniform_buffers`. */
    static SkinUniforms make(const wgpu::Device& render_device, const wgpu::Limits& limits) {
        const wgpu::BufferUsage usage =
            (::epix::pbr::skins_use_uniform_buffers(limits) ? wgpu::BufferUsage::eUniform : wgpu::BufferUsage::eStorage) |
            wgpu::BufferUsage::eCopyDst;
        const std::uint64_t size = static_cast<std::uint64_t>(MAX_JOINTS) * sizeof(glm::mat4);
        // Aggregate initialization: the allocator has no default constructor, so
        // every member is named.
        return SkinUniforms{
            .current_staging_buffer = {},
            .current_buffer         = render_device.createBuffer(wgpu::BufferDescriptor()
                                                             .setLabel("skin uniform buffer")
                                                             .setSize(size)
                                                             .setUsage(usage)),
            .prev_buffer            = render_device.createBuffer(wgpu::BufferDescriptor()
                                                          .setLabel("skin uniform buffer")
                                                          .setSize(size)
                                                          .setUsage(usage)),
            .allocator              = render::mesh::offset_allocator::Allocator(MAX_TOTAL_JOINTS),
            .skin_uniform_info      = {},
            .joint_to_skins         = {},
            .total_joints           = 0,
        };
    }

    /** @brief The current offset in joints of a skin (Bevy
     *  `SkinUniforms::skin_index`, `skin.rs:158-162`). */
    std::optional<std::uint32_t> skin_index(render::sync_world::MainEntity skin) const {
        const auto it = skin_uniform_info.find(skin);
        if (it == skin_uniform_info.end()) return std::nullopt;
        return it->second.offset();
    }

    /** @brief The current offset in bytes of a skin (Bevy
     *  `SkinUniforms::skin_byte_offset`, `skin.rs:165-169`). */
    std::optional<SkinByteOffset> skin_byte_offset(render::sync_world::MainEntity skin) const {
        const auto index = skin_index(skin);
        if (!index) return std::nullopt;
        return SkinByteOffset::from_index(*index);
    }

    /** @brief Every skin in the scene (Bevy `SkinUniforms::all_skins`,
     *  `skin.rs:172-174`). */
    std::vector<render::sync_world::MainEntity> all_skins() const {
        std::vector<render::sync_world::MainEntity> skins;
        skins.reserve(skin_uniform_info.size());
        for (const auto& [entity, info] : skin_uniform_info) skins.push_back(entity);
        return skins;
    }
};

}  // namespace epix::pbr
