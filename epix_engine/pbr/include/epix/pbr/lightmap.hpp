#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/image/image.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief The index of the slot (element within the binding array) in the slab in
 *  which a lightmap is located (Bevy `LightmapSlotIndex`,
 *  `bevy_pbr/src/lightmap/mod.rs:177-183`).
 *
 *  Bevy wraps a `NonMaxU16` - a `u16` that is never `u16::MAX` - so a slot index
 *  cannot collide with the `u16::MAX` sentinel `MeshUniform` uses for "no
 *  lightmap". The invariant is documented rather than enforced, since the port has
 *  no non-max integer type, exactly as `LightmapSlabIndex` records it for the
 *  slab index. */
EPIX_EXPORT struct LightmapSlotIndex {
    std::uint16_t index = 0;
    auto operator<=>(const LightmapSlotIndex&) const = default;
};

/** @brief Four 16-bit unsigned normalized UV values packed into two words (Bevy
 *  `pack_lightmap_uv_rect`, `bevy_pbr/src/lightmap/mod.rs:309-322`).
 *
 *  The rect's four components are scaled by 65535 and rounded, then packed as
 *  `(min.u | min.v << 16, max.u | max.v << 16)` - the layout `MeshUniform`'s
 *  `lightmap_uv_rect` documents. No rect packs to zero, which is how the shader
 *  reads "no lightmap". Bevy takes `bevy_math::Rect`; Epix's counterpart is the
 *  float `Rect` the image module already owns for atlas coordinates. */
EPIX_EXPORT inline glm::uvec2 pack_lightmap_uv_rect(const std::optional<::epix::image::Rect>& maybe_rect) {
    if (!maybe_rect) return glm::uvec2{0u, 0u};
    const glm::uvec4 rect = glm::uvec4(glm::round(
        glm::vec4(maybe_rect->min.x, maybe_rect->min.y, maybe_rect->max.x, maybe_rect->max.y) * 65535.0f));
    return glm::uvec2(rect.x | (rect.y << 16), rect.z | (rect.w << 16));
}

}  // namespace epix::pbr
