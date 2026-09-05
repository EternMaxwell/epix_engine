#include <gtest/gtest.h>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <epix/task.hpp>
#include <array>

using namespace epix::app;

namespace {
struct ImageTaskPoolInit {
    ImageTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4).build()});
    }
} image_task_pool_init;
}

TEST(ImagePlugin, ExposesBevyDefaultSamplerChoices) {
    const auto linear = epix::image::ImagePlugin::default_linear();
    EXPECT_EQ(linear.default_sampler.min_filter, epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(linear.default_sampler.mag_filter, epix::image::ImageFilterMode::Linear);
    EXPECT_EQ(linear.default_sampler.mipmap_filter, epix::image::ImageFilterMode::Linear);

    const auto nearest = epix::image::ImagePlugin::default_nearest();
    EXPECT_EQ(nearest.default_sampler.min_filter, epix::image::ImageFilterMode::Nearest);
    EXPECT_EQ(nearest.default_sampler.mag_filter, epix::image::ImageFilterMode::Nearest);
    EXPECT_EQ(nearest.default_sampler.mipmap_filter, epix::image::ImageFilterMode::Nearest);
}

TEST(ImagePlugin, InstallsBevyFallbackImageAssets) {
    App app = App::create();
    app.add_plugins(epix::assets::AssetPlugin{});
    app.add_plugins(epix::image::ImagePlugin{});

    const auto& images = app.world().resource<epix::assets::Assets<epix::image::Image>>();
    const auto opaque  = images.get(epix::image::DEFAULT_IMAGE_HANDLE.id());
    ASSERT_TRUE(opaque.has_value());
    EXPECT_EQ(opaque->get().width(), 1u);
    EXPECT_EQ(opaque->get().height(), 1u);
    EXPECT_EQ(opaque->get().format(), epix::image::Format::RGBA8);
    constexpr std::array<std::uint8_t, 4> opaque_pixels{255, 255, 255, 255};
    ASSERT_EQ(opaque->get().raw_view().size(), opaque_pixels.size());
    for (std::size_t i = 0; i < opaque_pixels.size(); ++i) {
        EXPECT_EQ(opaque->get().raw_view()[i], static_cast<std::byte>(opaque_pixels[i]));
    }

    const auto transparent = images.get(epix::image::TRANSPARENT_IMAGE_HANDLE.id());
    ASSERT_TRUE(transparent.has_value());
    constexpr std::array<std::uint8_t, 4> transparent_pixels{255, 255, 255, 0};
    ASSERT_EQ(transparent->get().raw_view().size(), transparent_pixels.size());
    for (std::size_t i = 0; i < transparent_pixels.size(); ++i) {
        EXPECT_EQ(transparent->get().raw_view()[i], static_cast<std::byte>(transparent_pixels[i]));
    }
}

TEST(ImagePlugin, RequiresExplicitAssetPlugin) {
    App app = App::create();
    EXPECT_THROW(epix::image::ImagePlugin{}.attach(app), std::bad_optional_access);
}
