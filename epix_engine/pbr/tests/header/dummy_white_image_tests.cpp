#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <epix/pbr/dummy_white_image.hpp>
#include <epix/render/assets.hpp>
#include <epix/render/fallback_image.hpp>
#include <gtest/gtest.h>

#include <optional>
#include <utility>

using namespace epix::assets;

namespace pbr    = epix::pbr;
namespace image  = epix::image;
namespace render = epix::render;

/** @brief A material's optional texture binds the uploaded image's own view and
 *  sampler, and the 1x1 white fallback's when the handle is absent (Bevy
 *  `get_image_texture`, `bevy_pbr/src/render/mesh.rs:1964-1978`).
 *
 *  The three cases are the ones Bevy distinguishes: no handle, a handle whose
 *  image has been uploaded, and a handle whose image has not — the last of which
 *  fails rather than silently binding the fallback. */
TEST(DummyWhiteImage, GetImageTextureFallsBackAndLooksUp) {
    render::RenderAssets<render::texture::GpuImage> gpu_images;

    render::texture::GpuImage dummy;
    const auto uploaded_id  = AssetId<image::Image>::invalid();
    const auto missing_id   = AssetId<image::Image>(uuids::uuid::from_string("6ba7b810-9dad-11d1-80b4-00c04fd430c8").value());
    gpu_images.emplace(uploaded_id, render::texture::GpuImage{});
    const auto* uploaded = gpu_images.get(uploaded_id);
    ASSERT_NE(uploaded, nullptr);
    EXPECT_NE(uploaded, &dummy);

    // No handle: the fallback image's own view and sampler.
    const auto fallback = pbr::get_image_texture(dummy, gpu_images, std::nullopt);
    ASSERT_TRUE(fallback.has_value());
    EXPECT_EQ(fallback->first, &dummy.texture_view);
    EXPECT_EQ(fallback->second, &dummy.sampler);

    // A handle whose image is uploaded: that image, not the fallback.
    const auto looked_up = pbr::get_image_texture(dummy, gpu_images, Handle<image::Image>{uploaded_id});
    ASSERT_TRUE(looked_up.has_value());
    EXPECT_EQ(looked_up->first, &uploaded->texture_view);
    EXPECT_EQ(looked_up->second, &uploaded->sampler);
    EXPECT_NE(looked_up->first, &dummy.texture_view);

    // A handle whose image has not been uploaded: no view or sampler, rather
    // than the fallback - an absent handle is fine, a pending upload is not.
    const auto pending = pbr::get_image_texture(dummy, gpu_images, Handle<image::Image>{missing_id});
    EXPECT_FALSE(pending.has_value());
}
