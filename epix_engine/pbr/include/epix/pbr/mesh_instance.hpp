#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/camera/visibility.hpp>
#include <epix/mesh.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <epix/common.hpp>
#endif
#include <epix/pbr/material_bindings.hpp>

namespace epix::pbr {

/** @brief Flag bits carried by a mesh instance (Bevy `RenderMeshInstanceFlags`,
 *  `bevy_pbr/src/render/mesh.rs:683-699`).
 *
 *  The bits are Bevy's, and their *values* matter rather than only their
 *  distinctness: they travel in `MeshUniform::flags` to the shader, which tests
 *  them by position.
 *
 *  Three of them exist for motion vectors: whether the instance had a transform,
 *  a skin or morph targets last frame. Without them the shader cannot tell "this
 *  instance has not moved" from "this instance had no previous data", and the
 *  difference is a wrong velocity rather than a zero one. */
EPIX_EXPORT struct RenderMeshInstanceFlags {
    enum Bits : std::uint8_t {
        NONE                   = 0,
        /** @brief The mesh casts shadows. */
        SHADOW_CASTER          = 1u << 0,
        /** @brief The mesh may participate in automatic batching. */
        AUTOMATIC_BATCHING     = 1u << 1,
        /** @brief The mesh had a transform last frame. */
        HAS_PREVIOUS_TRANSFORM = 1u << 2,
        /** @brief The mesh had a skin last frame. */
        HAS_PREVIOUS_SKIN      = 1u << 3,
        /** @brief The mesh had morph targets last frame. */
        HAS_PREVIOUS_MORPH     = 1u << 4,
    };

    std::uint8_t bits = NONE;

    constexpr bool contains(Bits flag) const noexcept { return (bits & static_cast<std::uint8_t>(flag)) != 0; }

    constexpr void insert(Bits flag) noexcept { bits |= static_cast<std::uint8_t>(flag); }
    constexpr void remove(Bits flag) noexcept { bits &= static_cast<std::uint8_t>(~static_cast<std::uint8_t>(flag)); }

    /** @brief Whether this instance may be batched automatically (Bevy
     *  `RenderMeshInstanceShared::should_batch`, `render/mesh.rs:894-900`). */
    constexpr bool should_batch() const noexcept { return contains(AUTOMATIC_BATCHING); }

    bool operator==(const RenderMeshInstanceFlags&) const = default;
};

/** @brief The index of a lightmap slab (Bevy `LightmapSlabIndex`,
 *  `bevy_pbr/src/lightmap/mod.rs:177`).
 *
 *  Bevy wraps a `NonMaxU32` - a `u32` that is never `u32::MAX` - so a slab index
 *  can never collide with a sentinel. The invariant is documented rather than
 *  enforced here, since the port has no non-max integer type. */
EPIX_EXPORT struct LightmapSlabIndex {
    std::uint32_t index = 0;
    auto operator<=>(const LightmapSlabIndex&) const = default;
};

/** @brief CPU data the render world keeps for each entity holding a mesh, shared
 *  by both uniform-building paths (Bevy `RenderMeshInstanceShared`,
 *  `bevy_pbr/src/render/mesh.rs:735-755`).
 *
 *  There is no default constructor, and that matches Bevy: the mesh id is what the
 *  record is *about*, and Epix's `AssetId` deletes its own default constructor for
 *  the same reason, offering `invalid()` as an explicit sentinel instead.
 *
 *  `center` is a representative local-space position taken from the mesh's
 *  AABB, used as a spatial proxy for view-dependent sorting rather than a true
 *  centre; `render_layers` is absent when the instance carries no layer component,
 *  which is not the same as belonging to none. */
EPIX_EXPORT struct RenderMeshInstanceShared {
    assets::AssetId<mesh::Mesh> mesh_asset_id;
    MaterialBindingId material_bindings_index{};
    RenderMeshInstanceFlags flags{};
    std::optional<LightmapSlabIndex> lightmap_slab_index;
    std::uint32_t tag = 0;
    std::optional<::epix::camera::RenderLayers> render_layers;
    glm::vec3 center{0.0f};

    /** @brief Whether this instance may be batched automatically (Bevy
     *  `RenderMeshInstanceShared::should_batch`, `render/mesh.rs:894-900`). */
    bool should_batch() const noexcept { return flags.should_batch(); }
};

}  // namespace epix::pbr
