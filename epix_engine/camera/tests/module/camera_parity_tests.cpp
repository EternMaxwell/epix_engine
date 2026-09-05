#include <gtest/gtest.h>
#include <epix/app.hpp>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <array>

using namespace epix::ecs;

TEST(VisibilityPlugin, StandaloneRegistersVisibilityRequirements) {
    auto app = epix::app::App::create();
    ::epix::camera::VisibilityPlugin{}.attach(app);
    const Entity entity = app.world_mut().spawn(::epix::camera::Visibility{}).id();
    EXPECT_TRUE(app.world().entity(entity).contains<::epix::camera::InheritedVisibility>());
    EXPECT_TRUE(app.world().entity(entity).contains<::epix::camera::ViewVisibility>());
}

// Bevy RenderLayers uses an unbounded positive bit mask. Epix retains its
// intentional inverted-mask helpers on the same dynamically growing storage.
TEST(RenderLayers, Intersects) {
    auto layer0 = ::epix::camera::RenderLayers::layer(0);
    auto layer1 = ::epix::camera::RenderLayers::layer(1);
    auto none   = ::epix::camera::RenderLayers::none();
    EXPECT_TRUE(layer0.intersects(layer0));
    EXPECT_FALSE(layer0.intersects(layer1));
    EXPECT_FALSE(none.intersects(layer0));
    EXPECT_FALSE(layer0.intersects(none));
    EXPECT_TRUE(layer0.contains(0));
    EXPECT_FALSE(layer0.contains(1));

    // Epix's complement helpers are intentional extensions and must remain
    // dynamically extensible too.
    const auto all_except_130 = ::epix::camera::RenderLayers::all_except(std::array<std::size_t, 1>{130});
    EXPECT_TRUE(::epix::camera::RenderLayers::all().contains(10'000));
    EXPECT_FALSE(all_except_130.contains(130));
    EXPECT_TRUE(all_except_130.contains(131));

    const auto dynamic = ::epix::camera::RenderLayers::from_layers(std::array<std::size_t, 3>{0, 64, 130});
    EXPECT_TRUE(dynamic.contains(130));
    EXPECT_TRUE(dynamic.intersects(::epix::camera::RenderLayers::layer(130)));
    static_assert(std::ranges::view<decltype(dynamic.iter())>);
    EXPECT_TRUE(std::ranges::equal(dynamic.iter(), std::array<std::size_t, 3>{0, 64, 130}));
    EXPECT_EQ(dynamic.without(130).without(64).without(0), none);
    EXPECT_EQ(dynamic & ::epix::camera::RenderLayers::layer(64), ::epix::camera::RenderLayers::layer(64));
    EXPECT_EQ(dynamic | layer1, ::epix::camera::RenderLayers::from_layers(std::array<std::size_t, 4>{0, 1, 64, 130}));
}
