#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/common.hpp>
#endif

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

}  // namespace epix::pbr
