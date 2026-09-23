#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/image/image.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief Applies baked indirect diffuse global illumination from a lightmap
 *  (Bevy `Lightmap`, `bevy_pbr/src/lightmap/mod.rs:88-108`).
 *
 *  When it is on an entity that also carries a `Mesh3d` and a `StandardMaterial`,
 *  and the mesh has a second UV layer, the lightmap is sampled with those UVs.
 *  Bevy derives `Component, Clone, Reflect` but *not* `Default` - the image handle
 *  is what the component is about - so there is no default construction here
 *  either, the same shape `RenderMeshInstanceShared` records for its mesh id. */
EPIX_EXPORT struct Lightmap {
    /** @brief The lightmap texture. */
    assets::Handle<::epix::image::Image> image{assets::AssetId<::epix::image::Image>::invalid()};
    /** @brief The rectangle within the texture the UVs are relative to, from
     *  `(0, 0)` to `(1, 1)`; a lightmap atlas packs several meshes into one
     *  texture this way. */
    ::epix::image::Rect uv_rect{};
    /** @brief Whether to sample this lightmap bicubically: higher quality but
     *  slower, and it can leak, and the texture's sampler must then be linear. */
    bool bicubic_sampling = false;
};

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
