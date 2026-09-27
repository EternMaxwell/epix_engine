#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <epix/pbr/lightmap.hpp>
#include <epix/render/fallback_image.hpp>
#include <gtest/gtest.h>

#include <optional>

namespace pbr    = epix::pbr;
namespace image  = epix::image;
namespace render = epix::render;

/** @brief Slab allocation follows Bevy's bookkeeping (Bevy
 *  `RenderLightmaps::{create_slab, allocate, remove}` and `LightmapSlab`,
 *  `bevy_pbr/src/lightmap/mod.rs:351-455`): a slab is created when none has a
 *  free slot, a full slab leaves the free set, and freeing a slot puts its slab
 *  back. Without bindless lightmaps a slab holds a single binding, which is the
 *  `LIGHTMAPS_PER_SLAB == 1` case Bevy compiles in. */
TEST(Lightmap, SlabAllocationAndFreeSlabsFollowBevy) {
    const render::texture::FallbackImage fallback{};
    pbr::RenderLightmaps lightmaps;
    lightmaps.bindless_supported = false;

    const auto first_image  = epix::assets::AssetId<image::Image>::invalid();
    const auto second_image = epix::assets::AssetId<image::Image>::invalid();

    EXPECT_TRUE(lightmaps.slabs.empty());
    EXPECT_TRUE(lightmaps.free_slabs.empty());

    const auto [slab_index, slot_index] = lightmaps.allocate(fallback, first_image);
    EXPECT_EQ(slab_index.index, 0u);
    EXPECT_EQ(slot_index.index, 0u);
    ASSERT_EQ(lightmaps.slabs.size(), 1u);
    EXPECT_TRUE(lightmaps.slabs[0].is_full()) << "a single-binding slab is full after one allocation";
    EXPECT_TRUE(lightmaps.free_slabs.empty()) << "a full slab is not free";

    // The first slab is full, so the next allocation creates a second one.
    const auto [second_slab, second_slot] = lightmaps.allocate(fallback, second_image);
    EXPECT_EQ(second_slab.index, 1u);
    EXPECT_EQ(second_slot.index, 0u);
    EXPECT_EQ(lightmaps.slabs.size(), 2u);

    // Freeing the first slot makes its slab available again, and the lowest free
    // slab is the one the next allocation uses.
    lightmaps.remove(fallback, slab_index, slot_index);
    EXPECT_FALSE(lightmaps.slabs[0].is_full());
    ASSERT_EQ(lightmaps.free_slabs.size(), 1u);
    EXPECT_EQ(*lightmaps.free_slabs.begin(), 0u);

    const auto [reused_slab, reused_slot] = lightmaps.allocate(fallback, second_image);
    EXPECT_EQ(reused_slab.index, 0u) << "the free slab is reused before another is created";
    EXPECT_EQ(reused_slot.index, 0u);
    EXPECT_EQ(lightmaps.slabs.size(), 2u);

    // An allocated slot holds the image's id until its upload is seen, when it
    // is filled with the uploaded image (Bevy `LightmapSlab::{allocate,insert}`).
    ASSERT_TRUE(lightmaps.slabs[0].lightmaps[0].asset_id.has_value());
    lightmaps.slabs[0].insert(reused_slot, render::texture::GpuImage{});
    EXPECT_FALSE(lightmaps.slabs[0].lightmaps[0].asset_id.has_value());
}

/** @brief With bindless lightmaps a slab holds `LIGHTMAPS_PER_SLAB` bindings, so
 *  four allocations fit in one slab and the fifth creates another. */
TEST(Lightmap, BindlessSlabHoldsFourSlots) {
    const render::texture::FallbackImage fallback{};
    pbr::RenderLightmaps lightmaps;
    lightmaps.bindless_supported = true;

    const auto image_id = epix::assets::AssetId<image::Image>::invalid();
    for (std::size_t i = 0; i < pbr::LIGHTMAPS_PER_SLAB; ++i) {
        const auto [slab_index, slot_index] = lightmaps.allocate(fallback, image_id);
        EXPECT_EQ(slab_index.index, 0u);
        EXPECT_EQ(slot_index.index, i);
    }
    EXPECT_EQ(lightmaps.slabs.size(), 1u);
    EXPECT_TRUE(lightmaps.slabs[0].is_full());

    const auto [next_slab, next_slot] = lightmaps.allocate(fallback, image_id);
    EXPECT_EQ(next_slab.index, 1u);
    EXPECT_EQ(next_slot.index, 0u);
    EXPECT_EQ(lightmaps.slabs.size(), 2u);
    EXPECT_EQ(lightmaps.slabs[0].lightmaps.size(), pbr::LIGHTMAPS_PER_SLAB);
}

/** @brief The slab hands the bind group its texture views and samplers: all of
 *  them in slot order for the binding array the bindless layout declares, and
 *  the first slot's for the layout that binds a single lightmap (Bevy
 *  `LightmapSlab::{build_binding_arrays, bindings_for_first_lightmap}`,
 *  `bevy_pbr/src/lightmap/mod.rs:452-474`). */
TEST(Lightmap, SlabHandsOutItsTextureViewsAndSamplers) {
    const render::texture::FallbackImage fallback{};
    const auto slab = pbr::LightmapSlab::make(fallback, /*bindless_supported=*/true);
    ASSERT_EQ(slab.lightmaps.size(), pbr::LIGHTMAPS_PER_SLAB);

    const auto [texture_views, samplers] = slab.build_binding_arrays();
    ASSERT_EQ(texture_views.size(), pbr::LIGHTMAPS_PER_SLAB);
    ASSERT_EQ(samplers.size(), pbr::LIGHTMAPS_PER_SLAB);
    for (std::size_t slot = 0; slot < pbr::LIGHTMAPS_PER_SLAB; ++slot) {
        EXPECT_EQ(texture_views[slot], &slab.lightmaps[slot].gpu_image.texture_view)
            << "the binding array follows slot order";
        EXPECT_EQ(samplers[slot], &slab.lightmaps[slot].gpu_image.sampler);
    }

    const auto [first_view, first_sampler] = slab.bindings_for_first_lightmap();
    EXPECT_EQ(first_view, &slab.lightmaps[0].gpu_image.texture_view);
    EXPECT_EQ(first_sampler, &slab.lightmaps[0].gpu_image.sampler);
}
