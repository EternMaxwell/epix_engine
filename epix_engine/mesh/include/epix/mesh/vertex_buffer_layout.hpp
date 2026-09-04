#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <memory>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::mesh {

/** @brief Unique id of a vertex attribute (Bevy bevy_mesh
 * `MeshVertexAttributeId`, a u64 surrogate).
 *
 * Epix's `MeshAttribute.slot` plays this role for the standard attributes
 * (position=0, color=1, normal=2, uv0=3, uv1=4), whose slot values equal
 * Bevy's default attribute ids. */
struct MeshVertexAttributeId {
    std::uint64_t value = 0;
    bool operator==(const MeshVertexAttributeId&) const noexcept = default;
};

/** @brief Layout metadata of a single vertex attribute (Bevy bevy_mesh uses
 * `wgpu::VertexAttribute`). */
struct VertexAttributeDescriptor {
    std::uint64_t offset = 0;
    wgpu::VertexFormat format = wgpu::VertexFormat::eFloat32;
    std::uint32_t shader_location = 0;

    bool operator==(const VertexAttributeDescriptor&) const noexcept = default;
};

/** @brief A vertex buffer layout: interleaved attribute strides + attributes
 * (Bevy bevy_mesh `VertexBufferLayout`, wgpu-types). */
struct VertexBufferLayout {
    std::uint64_t array_stride = 0;
    wgpu::VertexStepMode step_mode = wgpu::VertexStepMode::eVertex;
    std::vector<VertexAttributeDescriptor> attributes;

    bool operator==(const VertexBufferLayout& other) const noexcept {
        return array_stride == other.array_stride && step_mode == other.step_mode &&
               attributes == other.attributes;
    }
};

/** @brief Interleaved vertex-buffer layout of one mesh (Bevy bevy_mesh
 * `MeshVertexBufferLayout`): the attribute ids in id order plus the concrete
 * wgpu layout (array stride, per-attribute offsets). Interned per-mesh by
 * `MeshVertexBufferLayouts`. */
struct MeshVertexBufferLayout {
    std::vector<MeshVertexAttributeId> attribute_ids;
    VertexBufferLayout layout;

    bool operator==(const MeshVertexBufferLayout&) const noexcept = default;
};

/** @brief Borrowed reference to an interned `MeshVertexBufferLayout` (Bevy
 * `MeshVertexBufferLayoutRef`: an `Arc` wrapper with pointer equality so
 * comparison is O(1)). */
struct MeshVertexBufferLayoutRef {
    std::shared_ptr<const MeshVertexBufferLayout> value;

    bool operator==(const MeshVertexBufferLayoutRef& other) const noexcept {
        return value.get() == other.value.get();
    }
};

/** @brief Render-world store holding a single copy of each mesh vertex buffer
 * layout (Bevy `MeshVertexBufferLayouts`). Layouts are interned on insert so
 * meshes sharing a layout share one reference. */
struct MeshVertexBufferLayouts {
    std::vector<std::shared_ptr<const MeshVertexBufferLayout>> layouts;

    /** @brief Insert a layout, reusing the existing instance when one with the
     * same contents is already stored (Bevy `insert`, structural compare). */
    MeshVertexBufferLayoutRef insert(const MeshVertexBufferLayout& layout) {
        for (const auto& existing : layouts) {
            if (*existing == layout) {
                return MeshVertexBufferLayoutRef{existing};
            }
        }
        layouts.push_back(std::make_shared<MeshVertexBufferLayout>(layout));
        return MeshVertexBufferLayoutRef{layouts.back()};
    }
};

}  // namespace epix::mesh
