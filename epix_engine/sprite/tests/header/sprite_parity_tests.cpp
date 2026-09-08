#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/sprite.hpp>
#include <epix/task.hpp>
#include <epix/transform.hpp>

namespace sprite = epix::sprite;
namespace image  = epix::image;

namespace {
struct SpriteTaskPoolInit {
    SpriteTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{
            epix::task::TaskPoolBuilder{}.num_threads(4).build()});
    }
} sprite_task_pool_init;
}

TEST(Anchor, ConstantsMatchBevy) {
    EXPECT_EQ(sprite::Anchor::BOTTOM_LEFT.as_vec(), glm::vec2(-0.5f, -0.5f));
    EXPECT_EQ(sprite::Anchor::BOTTOM_CENTER.as_vec(), glm::vec2(0.0f, -0.5f));
    EXPECT_EQ(sprite::Anchor::BOTTOM_RIGHT.as_vec(), glm::vec2(0.5f, -0.5f));
    EXPECT_EQ(sprite::Anchor::CENTER_LEFT.as_vec(), glm::vec2(-0.5f, 0.0f));
    EXPECT_EQ(sprite::Anchor::CENTER.as_vec(), glm::vec2(0.0f));
    EXPECT_EQ(sprite::Anchor::CENTER_RIGHT.as_vec(), glm::vec2(0.5f, 0.0f));
    EXPECT_EQ(sprite::Anchor::TOP_LEFT.as_vec(), glm::vec2(-0.5f, 0.5f));
    EXPECT_EQ(sprite::Anchor::TOP_CENTER.as_vec(), glm::vec2(0.0f, 0.5f));
    EXPECT_EQ(sprite::Anchor::TOP_RIGHT.as_vec(), glm::vec2(0.5f, 0.5f));
}

TEST(SpriteImageMode, HelpersMatchBevy) {
    const sprite::SpriteImageMode automatic;
    EXPECT_FALSE(automatic.uses_slices());
    EXPECT_FALSE(automatic.scale().has_value());

    const sprite::SpriteImageMode scale = sprite::SpriteImageMode::Scale{sprite::SpriteScalingMode::FitEnd};
    EXPECT_EQ(scale.scale(), sprite::SpriteScalingMode::FitEnd);
    EXPECT_FALSE(scale.uses_slices());
    EXPECT_TRUE(sprite::SpriteImageMode{sprite::SpriteImageMode::Sliced{}}.uses_slices());
    EXPECT_TRUE(sprite::SpriteImageMode{sprite::SpriteImageMode::Tiled{}}.uses_slices());
}

TEST(BorderRect, ConstructorsAndArithmeticMatchBevy) {
    EXPECT_EQ(sprite::BorderRect::zero(), sprite::BorderRect::all(0.0f));
    EXPECT_EQ(sprite::BorderRect::axes(2.0f, 3.0f),
              (sprite::BorderRect{glm::vec2(2.0f, 3.0f), glm::vec2(2.0f, 3.0f)}));

    const sprite::BorderRect lhs{glm::vec2(1.0f, 2.0f), glm::vec2(3.0f, 4.0f)};
    const sprite::BorderRect rhs{glm::vec2(2.0f, 3.0f), glm::vec2(4.0f, 5.0f)};
    EXPECT_EQ(lhs + rhs, (sprite::BorderRect{glm::vec2(3.0f, 5.0f), glm::vec2(7.0f, 9.0f)}));
    EXPECT_EQ(rhs - lhs, sprite::BorderRect::all(1.0f));
    EXPECT_EQ(lhs * 2.0f, (sprite::BorderRect{glm::vec2(2.0f, 4.0f), glm::vec2(6.0f, 8.0f)}));
    EXPECT_EQ((lhs * 2.0f) / 2.0f, lhs);
}

TEST(TextureSlicer, ComputesNineStretchedSlices) {
    const sprite::TextureSlicer slicer{
        .border = sprite::BorderRect::all(10.0f),
        .center_scale_mode = sprite::SliceScaleMode::Stretch{},
        .sides_scale_mode = sprite::SliceScaleMode::Stretch{},
    };
    const auto slices = slicer.compute_slices(image::Rect{{0.0f, 0.0f}, {50.0f, 50.0f}},
                                               glm::vec2(100.0f, 100.0f));

    ASSERT_EQ(slices.size(), 9u);
    EXPECT_EQ(slices[0],
              (sprite::TextureSlice{
                  .texture_rect = image::Rect{{0.0f, 0.0f}, {10.0f, 10.0f}},
                  .draw_size = {10.0f, 10.0f},
                  .offset = {-45.0f, 45.0f},
              }));
    EXPECT_EQ(slices[4],
              (sprite::TextureSlice{
                  .texture_rect = image::Rect{{10.0f, 10.0f}, {40.0f, 40.0f}},
                  .draw_size = {80.0f, 80.0f},
                  .offset = {0.0f, 0.0f},
              }));
}

TEST(TextureSlicer, TilesAndCropsPartialTiles) {
    const sprite::TextureSlice source{
        .texture_rect = image::Rect{{0.0f, 0.0f}, {10.0f, 10.0f}},
        .draw_size = {25.0f, 10.0f},
        .offset = {3.0f, 4.0f},
    };
    const auto slices = source.tiled(1.0f, {true, false});

    ASSERT_EQ(slices.size(), 3u);
    EXPECT_EQ(slices[0].draw_size, glm::vec2(10.0f, 10.0f));
    EXPECT_EQ(slices[1].draw_size, glm::vec2(10.0f, 10.0f));
    EXPECT_EQ(slices[2].draw_size, glm::vec2(5.0f, 10.0f));
    EXPECT_EQ(slices[2].texture_rect, (image::Rect{{0.0f, 0.0f}, {5.0f, 10.0f}}));
    EXPECT_EQ(slices[2].offset, glm::vec2(13.0f, 4.0f));
}

TEST(TextureSlicer, InvalidInsetsFallBackToOneSlice) {
    const sprite::TextureSlicer slicer{.border = sprite::BorderRect::all(25.0f)};
    const image::Rect rect{{0.0f, 0.0f}, {50.0f, 50.0f}};
    const auto slices = slicer.compute_slices(rect, glm::vec2(100.0f, 75.0f));

    ASSERT_EQ(slices.size(), 1u);
    EXPECT_EQ(slices.front(),
              (sprite::TextureSlice{.texture_rect = rect, .draw_size = {100.0f, 75.0f}, .offset = {0.0f, 0.0f}}));
}

TEST(Sprite, ConstructorsOwnImageAndAtlas) {
    epix::assets::Assets<image::Image> images;
    epix::assets::Assets<image::TextureAtlasLayout> layouts;
    const auto source = images.emplace(image::Image::create2d(5, 10, image::Format::RGBA8));
    const auto layout = layouts.emplace(image::TextureAtlasLayout{
        .size = {5, 10}, .textures = {image::URect{{1, 1}, {4, 4}}}});

    const auto regular = sprite::Sprite::from_image(source);
    EXPECT_EQ(regular.image.id(), source.id());
    const auto atlas = sprite::Sprite::from_atlas_image(source, image::TextureAtlas{layout, 0});
    ASSERT_TRUE(atlas.texture_atlas.has_value());
    EXPECT_EQ(atlas.texture_atlas->layout.id(), layout.id());
    const auto colored = sprite::Sprite::from_color(glm::vec4(0.25f), glm::vec2(50.0f, 100.0f));
    EXPECT_EQ(colored.custom_size, glm::vec2(50.0f, 100.0f));
}

TEST(Sprite, ComputesRegularAndAtlasPixelSpacePoints) {
    epix::assets::Assets<image::Image> images;
    epix::assets::Assets<image::TextureAtlasLayout> layouts;
    const auto source = images.emplace(image::Image::create2d(5, 10, image::Format::RGBA8));

    const auto regular = sprite::Sprite::from_image(source);
    EXPECT_EQ(regular.compute_pixel_space_point({0.0f, 0.0f}, sprite::Anchor::CENTER, images, layouts),
              (glm::vec2{2.5f, 5.0f}));
    EXPECT_FALSE(regular.compute_pixel_space_point({3.0f, 0.0f}, sprite::Anchor::CENTER, images, layouts));

    const auto layout = layouts.emplace(image::TextureAtlasLayout{
        .size = {5, 10}, .textures = {image::URect{{1, 1}, {4, 4}}}});
    const auto atlas = sprite::Sprite::from_atlas_image(source, image::TextureAtlas{layout, 0});
    EXPECT_EQ(atlas.compute_pixel_space_point({0.5f, 0.5f}, sprite::Anchor::BOTTOM_LEFT, images, layouts),
              (glm::vec2{1.5f, 3.5f}));
}

TEST(SpritePlugin, InstallsAtlasPluginAndRequiredComponents) {
    auto app = epix::app::App::create();
    app.add_plugins(epix::assets::AssetPlugin{});
    app.add_plugins(image::ImagePlugin{});
    app.add_plugins(epix::mesh::MeshPlugin{});
    app.add_plugins(epix::camera::CameraPlugin{});
    app.add_plugins(sprite::SpritePlugin{});

    EXPECT_TRUE(app.get_plugin<image::TextureAtlasPlugin>().has_value());
    const auto entity = app.world_mut().spawn(sprite::Sprite{}).id();
    EXPECT_TRUE(app.world().entity(entity).contains<epix::transform::Transform>());
    EXPECT_TRUE(app.world().entity(entity).contains<epix::camera::Visibility>());
    EXPECT_TRUE(app.world().entity(entity).contains<epix::camera::VisibilityClass>());
    EXPECT_TRUE(app.world().entity(entity).contains<sprite::Anchor>());
}

TEST(CalculateBounds2d, CreatesAndUpdatesSpriteAabb) {
    auto app = epix::app::App::create();
    app.add_plugins(epix::assets::AssetPlugin{});
    app.add_plugins(image::ImagePlugin{});
    app.add_plugins(epix::mesh::MeshPlugin{});
    app.add_plugins(epix::camera::CameraPlugin{});
    app.add_plugins(sprite::SpritePlugin{});

    auto image_handle = app.world_mut().resource_mut<epix::assets::Assets<image::Image>>().emplace(
        image::Image::create2d(20, 10, image::Format::RGBA8));
    const auto entity = app.world_mut().spawn(sprite::Sprite::from_image(image_handle), sprite::Anchor::TOP_RIGHT).id();

    EXPECT_FALSE(app.world().entity(entity).contains<epix::camera::Aabb>());
    app.update();
    const auto first_ref = app.world().entity(entity).get<epix::camera::Aabb>();
    ASSERT_TRUE(first_ref.has_value());
    const auto first = first_ref->get();
    EXPECT_EQ(first.center, glm::vec3(-10.0f, -5.0f, 0.0f));
    EXPECT_EQ(first.half_extents, glm::vec3(10.0f, 5.0f, 0.0f));

    app.world_mut().entity_mut(entity).get_mut<sprite::Sprite>()->get_mut().custom_size = glm::vec2(8.0f, 4.0f);
    app.update();
    const auto second_ref = app.world().entity(entity).get<epix::camera::Aabb>();
    ASSERT_TRUE(second_ref.has_value());
    const auto second = second_ref->get();
    EXPECT_EQ(second.center, glm::vec3(-4.0f, -2.0f, 0.0f));
    EXPECT_EQ(second.half_extents, glm::vec3(4.0f, 2.0f, 0.0f));
}
