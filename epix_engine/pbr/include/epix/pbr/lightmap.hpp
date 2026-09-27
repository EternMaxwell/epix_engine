#pragma once

#ifndef EPIX_CXX_MODULE
#include <bit>
#include <cassert>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/image/image.hpp>
#include <epix/render/fallback_image.hpp>
#include <epix/render/sync_world.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <set>
#include <utility>
#include <vector>
#endif

namespace epix::pbr {

/** @brief How many lightmaps one slab holds when binding arrays are in use
 *  (Bevy `LIGHTMAPS_PER_SLAB`, `bevy_pbr/src/lightmap/mod.rs:74`).
 *
 *  Without binding arrays only a single lightmap can be bound at a time, which is
 *  why the lightmapped mesh layout has two shapes (see `mesh_bindings.hpp`). */
EPIX_EXPORT inline constexpr std::size_t LIGHTMAPS_PER_SLAB = 4;

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

/** @brief The index of the slab (binding array) in which a lightmap is located
 *  (Bevy `LightmapSlabIndex`, `bevy_pbr/src/lightmap/mod.rs:174-177`).
 *
 *  Bevy wraps a `NonMaxU32` - a `u32` that is never `u32::MAX` - so a slab index
 *  can never collide with a sentinel. The invariant is documented rather than
 *  enforced here, since the port has no non-max integer type. It sits beside the
 *  slot index because that is where Bevy declares it; `mesh_instance.hpp`'s
 *  instance data uses it. */
EPIX_EXPORT struct LightmapSlabIndex {
    std::uint32_t index                              = 0;
    auto operator<=>(const LightmapSlabIndex&) const = default;
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
    std::uint16_t index                              = 0;
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
EPIX_EXPORT EPIX_API glm::uvec2 pack_lightmap_uv_rect(const std::optional<::epix::image::Rect>& maybe_rect);

/** @brief One visible lightmapped mesh instance's lightmap data in the render
 *  world (Bevy `RenderLightmap`, `bevy_pbr/src/lightmap/mod.rs:110-134`).
 *
 *  Bevy declares this `pub(crate)`; a struct's members are public here, as
 *  elsewhere in the port. */
EPIX_EXPORT struct RenderLightmap {
    ::epix::image::Rect uv_rect{};
    LightmapSlabIndex slab_index{};
    LightmapSlotIndex slot_index{};
    bool bicubic_sampling = false;
};

/** @brief A slot of a lightmap slab (Bevy's `AllocatedLightmap`,
 *  `bevy_pbr/src/lightmap/mod.rs:168-172`): the GPU image, and the asset it was
 *  allocated for when it has been allocated but not yet loaded. */
EPIX_EXPORT struct AllocatedLightmap {
    render::texture::GpuImage gpu_image;
    std::optional<assets::AssetId<::epix::image::Image>> asset_id;
};

/** @brief A binding array containing lightmaps (Bevy `LightmapSlab`,
 *  `bevy_pbr/src/lightmap/mod.rs:159-166,396-455`). It holds a single binding
 *  when bindless lightmaps are unavailable. */
EPIX_EXPORT struct EPIX_API LightmapSlab {
    std::vector<AllocatedLightmap> lightmaps;
    std::uint32_t free_slots_bitmask = 0;

    /** @brief A slab of fallback images, one slot per lightmap it can hold
     *  (Bevy `LightmapSlab::new`, `lightmap/mod.rs:397-413`). */
    static LightmapSlab make(const render::texture::FallbackImage& fallback_images, bool bindless_supported);

    bool is_full() const noexcept { return free_slots_bitmask == 0u; }

    /** @brief Allocate the first free slot for an image (Bevy
     *  `LightmapSlab::allocate`, `lightmap/mod.rs:419-428`). */
    LightmapSlotIndex allocate(assets::AssetId<::epix::image::Image> image_id);

    /** @brief Fill a slot once its image has been uploaded (Bevy
     *  `LightmapSlab::insert`, `lightmap/mod.rs:430-435`). */
    void insert(LightmapSlotIndex index, render::texture::GpuImage gpu_image) {
        lightmaps[index.index] = AllocatedLightmap{.gpu_image = std::move(gpu_image), .asset_id = std::nullopt};
    }

    /** @brief Free a slot back to the fallback image (Bevy
     *  `LightmapSlab::remove`, `lightmap/mod.rs:437-446`). */
    void remove(const render::texture::FallbackImage& fallback_images, LightmapSlotIndex index) {
        lightmaps[index.index] = AllocatedLightmap{.gpu_image = fallback_images.d2, .asset_id = std::nullopt};
        free_slots_bitmask |= 1u << index.index;
    }

    /** @brief Every slot's texture view and sampler, in slot order: what the
     *  lightmapped layout's binding array binds when binding arrays are usable
     *  (Bevy `LightmapSlab::build_binding_arrays`,
     *  `lightmap/mod.rs:452-467`). */
    std::pair<std::vector<const wgpu::TextureView*>, std::vector<const wgpu::Sampler*>> build_binding_arrays() const;

    /** @brief The first slot's texture view and sampler, which is what the
     *  layout without binding arrays binds (Bevy
     *  `LightmapSlab::bindings_for_first_lightmap`,
     *  `lightmap/mod.rs:469-474`). */
    std::pair<const wgpu::TextureView*, const wgpu::Sampler*> bindings_for_first_lightmap() const {
        return {&lightmaps[0].gpu_image.texture_view, &lightmaps[0].gpu_image.sampler};
    }
};

/** @brief All lightmaps in the render world (Bevy `RenderLightmaps`,
 *  `bevy_pbr/src/lightmap/mod.rs:136-157,351-394`).
 *
 *  Cleared and repopulated each frame by the extraction system, which is not
 *  ported yet; the bookkeeping an adapter needs is. Bevy's `FixedBitSet` has no
 *  counterpart in the port, so the free slabs are an ordered set - its first
 *  element is Bevy's `FixedBitSet::minimum`. */
EPIX_EXPORT struct EPIX_API RenderLightmaps {
    render::sync_world::MainEntityHashMap<RenderLightmap> render_lightmaps;
    std::vector<LightmapSlab> slabs;
    /** Bevy's `free_slabs`, private there. */
    std::set<std::size_t> free_slabs;
    /** Bevy's `pending_lightmaps`, private there: slots allocated for an image
     *  whose upload has not been seen yet. */
    std::set<std::pair<LightmapSlabIndex, LightmapSlotIndex>> pending_lightmaps;
    bool bindless_supported = false;

    /** @brief Create a slab with the given fallback images and return its index
     *  (Bevy `RenderLightmaps::create_slab`, `lightmap/mod.rs:353-361`). */
    LightmapSlabIndex create_slab(const render::texture::FallbackImage& fallback_images);

    /** @brief Allocate a slot for an image, creating a slab when none has room
     *  (Bevy `RenderLightmaps::allocate`, `lightmap/mod.rs:363-380`). */
    std::pair<LightmapSlabIndex, LightmapSlotIndex> allocate(const render::texture::FallbackImage& fallback_images,
                                                             assets::AssetId<::epix::image::Image> image_id);

    /** @brief Free a slot, and the slab with it when it becomes free (Bevy
     *  `RenderLightmaps::remove`, `lightmap/mod.rs:382-393`). */
    void remove(const render::texture::FallbackImage& fallback_images, LightmapSlabIndex slab_index,
                LightmapSlotIndex slot_index);
};

}  // namespace epix::pbr
