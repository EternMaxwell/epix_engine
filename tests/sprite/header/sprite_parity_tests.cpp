#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/meta.hpp>
#include <epix/sprite.hpp>
#include <epix/task.hpp>
#include <epix/transform.hpp>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>

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
    EXPECT_EQ(sprite::BorderRect::ZERO, sprite::BorderRect::all(0.0f));
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
    const auto source = images.emplace(image::Image::create2d(5, 10, wgpu::TextureFormat::eRGBA8Unorm));
    const auto layout = layouts.emplace(image::TextureAtlasLayout{
        .size = {5, 10}, .textures = {image::URect{{1, 1}, {4, 4}}}});

    const auto regular = sprite::Sprite::from_image(source);
    EXPECT_EQ(regular.image.id(), source.id());
    const auto atlas = sprite::Sprite::from_atlas_image(source, image::TextureAtlas{layout, 0});
    ASSERT_TRUE(atlas.texture_atlas.has_value());
    EXPECT_EQ(atlas.texture_atlas->layout.id(), layout.id());
    const auto colored = sprite::Sprite::from_color(::epix::color::Color::linear_rgba(0.25f, 0.25f, 0.25f, 0.25f), glm::vec2(50.0f, 100.0f));
    EXPECT_EQ(colored.custom_size, glm::vec2(50.0f, 100.0f));
}

TEST(Sprite, ComputesRegularAndAtlasPixelSpacePoints) {
    epix::assets::Assets<image::Image> images;
    epix::assets::Assets<image::TextureAtlasLayout> layouts;
    const auto source = images.emplace(image::Image::create2d(5, 10, wgpu::TextureFormat::eRGBA8Unorm));

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

// Bevy `#[component(on_add = visibility::add_visibility_class::<Sprite>)]`
// (`bevy_sprite/src/sprite.rs:17`) appends the class, so an entity that already
// carries another visibility class - a `Mesh2d` entity, for example - ends up in
// both and stays visible to the sprite queue.
TEST(Sprite, VisibilityClassHookAppendsToAnExistingClass) {
    auto app = epix::app::App::create();
    app.add_plugins(epix::assets::AssetPlugin{});
    app.add_plugins(image::ImagePlugin{});
    app.add_plugins(epix::mesh::MeshPlugin{});
    app.add_plugins(epix::camera::CameraPlugin{});
    app.add_plugins(sprite::SpritePlugin{});

    const auto other = epix::meta::type_index(epix::meta::type_id<epix::mesh::Mesh2d>());
    const auto entity = app.world_mut().spawn(epix::camera::VisibilityClass{other}).id();
    app.world_mut().entity_mut(entity).insert(sprite::Sprite{});

    const auto classes = app.world().entity(entity).get<epix::camera::VisibilityClass>();
    ASSERT_TRUE(classes.has_value());
    const auto& values = classes->get().classes;
    EXPECT_NE(std::ranges::find(values, other), values.end());
    EXPECT_NE(std::ranges::find(values, epix::meta::type_index(epix::meta::type_id<sprite::Sprite>())), values.end());
}

// Bevy `#[component(on_add = visibility::add_visibility_class::<Mesh2d>)]`
// (`bevy_camera/src/visibility/mod.rs:433-435`): the class is appended to whatever is
// already there, so an entity that is both a `Sprite` and a `Mesh2d` is in both. This is
// the case CAM-20 records - supplying the class as a required component is skipped when
// the archetype already carries a `VisibilityClass`, which left the Mesh2d class out.
TEST(Mesh2d, VisibilityClassHookAppendsToAnExistingClass) {
    auto app = epix::app::App::create();
    app.add_plugins(epix::assets::AssetPlugin{});
    app.add_plugins(image::ImagePlugin{});
    app.add_plugins(epix::mesh::MeshPlugin{});
    app.add_plugins(epix::camera::CameraPlugin{});
    app.add_plugins(sprite::SpritePlugin{});

    const auto other  = epix::meta::type_index(epix::meta::type_id<sprite::Sprite>());
    const auto entity = app.world_mut().spawn(epix::camera::VisibilityClass{other}).id();
    app.world_mut().entity_mut(entity).insert(epix::mesh::Mesh2d{});

    const auto classes = app.world().entity(entity).get<epix::camera::VisibilityClass>();
    ASSERT_TRUE(classes.has_value());
    const auto& values = classes->get().classes;
    EXPECT_NE(std::ranges::find(values, other), values.end());
    EXPECT_NE(std::ranges::find(values, epix::meta::type_index(epix::meta::type_id<epix::mesh::Mesh2d>())),
              values.end());
}

// Bevy's `calculate_bounds_2d` inserts with `try_insert`
// (`bevy_sprite/src/lib.rs:153,190`), so on an entity that has both a `Mesh2d`
// and a `Sprite` the sprite bounds are the ones that survive; `insert_if_new`
// left the mesh bounds in place.
TEST(CalculateBounds2d, SpriteAabbReplacesMeshAabbForDualEntities) {
    auto app = epix::app::App::create();
    app.add_plugins(epix::assets::AssetPlugin{});
    app.add_plugins(image::ImagePlugin{});
    app.add_plugins(epix::mesh::MeshPlugin{});
    app.add_plugins(epix::camera::CameraPlugin{});
    app.add_plugins(sprite::SpritePlugin{});

    auto mesh = epix::mesh::Mesh{wgpu::PrimitiveTopology::eTriangleList,
                                 epix::assets::RenderAssetUsages::RENDER_WORLD};
    mesh.insert_attribute(epix::mesh::Mesh::ATTRIBUTE_POSITION,
                          std::array{glm::vec3{-2.0f, -2.0f, 0.0f}, glm::vec3{2.0f, -2.0f, 0.0f},
                                     glm::vec3{0.0f, 2.0f, 0.0f}});
    mesh.insert_indices(epix::mesh::Indices{std::vector<std::uint16_t>{0, 1, 2}});
    const auto mesh_handle = app.world_mut().resource_mut<epix::assets::Assets<epix::mesh::Mesh>>().emplace(
        std::move(mesh));
    const auto image_handle = app.world_mut().resource_mut<epix::assets::Assets<image::Image>>().emplace(
        image::Image::create2d(20, 10, wgpu::TextureFormat::eRGBA8Unorm));

    const auto entity = app.world_mut()
                            .spawn(epix::mesh::Mesh2d{mesh_handle}, sprite::Sprite::from_image(image_handle),
                                   sprite::Anchor::CENTER)
                            .id();
    app.update();

    const auto aabb = app.world().entity(entity).get<epix::camera::Aabb>();
    ASSERT_TRUE(aabb.has_value());
    EXPECT_EQ(aabb->get().half_extents, glm::vec3(10.0f, 5.0f, 0.0f));
}

TEST(CalculateBounds2d, CreatesAndUpdatesSpriteAabb) {
    auto app = epix::app::App::create();
    app.add_plugins(epix::assets::AssetPlugin{});
    app.add_plugins(image::ImagePlugin{});
    app.add_plugins(epix::mesh::MeshPlugin{});
    app.add_plugins(epix::camera::CameraPlugin{});
    app.add_plugins(sprite::SpritePlugin{});

    auto image_handle = app.world_mut().resource_mut<epix::assets::Assets<image::Image>>().emplace(
        image::Image::create2d(20, 10, wgpu::TextureFormat::eRGBA8Unorm));
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

// Bevy `test_horizontal_sizes_non_uniform_bigger` (slicer.rs:325-353), driven through the
// public `compute_slices` rather than the private `corner_slices` (SPR-244).
TEST(TextureSlicer, NonUniformBiggerCornersMatchBevy) {
    const sprite::TextureSlicer slicer{
        .border            = sprite::BorderRect{glm::vec2(20.0f, 10.0f), glm::vec2(10.0f, 10.0f)},
        .center_scale_mode = sprite::SliceScaleMode::Stretch{},
        .sides_scale_mode  = sprite::SliceScaleMode::Stretch{},
        .max_corner_scale  = 1.0f,
    };
    const auto slices = slicer.compute_slices(image::Rect{{0.0f, 0.0f}, {50.0f, 50.0f}}, glm::vec2(100.0f));

    ASSERT_EQ(slices.size(), 9u);
    EXPECT_EQ(slices[0],  // TL
              (sprite::TextureSlice{
                  .texture_rect = image::Rect{{0.0f, 0.0f}, {20.0f, 10.0f}},
                  .draw_size    = {20.0f, 10.0f},
                  .offset       = {-40.0f, 45.0f},
              }));
}

// Bevy `test_horizontal_sizes_non_uniform_smaller` (slicer.rs:355-396), the only Bevy test
// that asserts a side slice. Corners are `[TL, TR, BL, BR]`, then the centre, then the
// horizontal sides `[L, R]` and the vertical sides `[T, B]` (slicer.rs:233-266).
TEST(TextureSlicer, NonUniformSmallerCornersAndTopSideMatchBevy) {
    const sprite::TextureSlicer slicer{
        .border            = sprite::BorderRect{glm::vec2(5.0f, 10.0f), glm::vec2(10.0f, 10.0f)},
        .center_scale_mode = sprite::SliceScaleMode::Stretch{},
        .sides_scale_mode  = sprite::SliceScaleMode::Stretch{},
        .max_corner_scale  = 1.0f,
    };
    const auto slices = slicer.compute_slices(image::Rect{{0.0f, 0.0f}, {50.0f, 50.0f}}, glm::vec2(100.0f));

    ASSERT_EQ(slices.size(), 9u);
    EXPECT_EQ(slices[0],  // TL
              (sprite::TextureSlice{
                  .texture_rect = image::Rect{{0.0f, 0.0f}, {5.0f, 10.0f}},
                  .draw_size    = {5.0f, 10.0f},
                  .offset       = {-47.5f, 45.0f},
              }));
    EXPECT_EQ(slices[7],  // top
              (sprite::TextureSlice{
                  .texture_rect = image::Rect{{5.0f, 0.0f}, {40.0f, 10.0f}},
                  .draw_size    = {85.0f, 10.0f},
                  .offset       = {-2.5f, 45.0f},
              }));
}

// Bevy `test_horizontal_sizes_non_uniform_zero` (slicer.rs:398-426): a zero inset on one
// axis still produces a corner slice, with a zero-width rect and draw size.
TEST(TextureSlicer, NonUniformZeroWidthCornersMatchBevy) {
    const sprite::TextureSlicer slicer{
        .border            = sprite::BorderRect{glm::vec2(0.0f, 10.0f), glm::vec2(10.0f, 10.0f)},
        .center_scale_mode = sprite::SliceScaleMode::Stretch{},
        .sides_scale_mode  = sprite::SliceScaleMode::Stretch{},
        .max_corner_scale  = 1.0f,
    };
    const auto slices = slicer.compute_slices(image::Rect{{0.0f, 0.0f}, {50.0f, 50.0f}}, glm::vec2(100.0f));

    ASSERT_EQ(slices.size(), 9u);
    EXPECT_EQ(slices[0],  // TL
              (sprite::TextureSlice{
                  .texture_rect = image::Rect{{0.0f, 0.0f}, {0.0f, 10.0f}},
                  .draw_size    = {0.0f, 10.0f},
                  .offset       = {-50.0f, 45.0f},
              }));
}

// Bevy `compute_pixel_space_point_for_sprite_with_anchor_flip_x` (sprite.rs:409-431):
// `flip_x` mirrors the point about the sprite's vertical centre line, in the `Ok` arm and in
// the `Err` arm that reports the clipped pixel-space point.
TEST(Sprite, ComputesPixelSpacePointWithFlipX) {
    epix::assets::Assets<image::Image> images;
    epix::assets::Assets<image::TextureAtlasLayout> layouts;
    const auto source = images.emplace(image::Image::create2d(5, 10, wgpu::TextureFormat::eRGBA8Unorm));

    const auto flipped = sprite::Sprite{.image = source, .flip_x = true};
    const auto compute = [&](glm::vec2 point) {
        return flipped.compute_pixel_space_point(point, sprite::Anchor::BOTTOM_LEFT, images, layouts);
    };

    EXPECT_EQ(compute({0.5f, 9.5f}), (glm::vec2{4.5f, 0.5f}));
    EXPECT_EQ(compute({2.5f, 5.0f}), (glm::vec2{2.5f, 5.0f}));
    EXPECT_EQ(compute({2.5f, 9.5f}), (glm::vec2{2.5f, 0.5f}));

    const auto right_outside = compute({5.5f, 5.0f});
    ASSERT_FALSE(right_outside.has_value());
    EXPECT_EQ(right_outside.error(), (glm::vec2{-0.5f, 5.0f}));
    const auto left_outside = compute({-0.5f, 5.0f});
    ASSERT_FALSE(left_outside.has_value());
    EXPECT_EQ(left_outside.error(), (glm::vec2{5.5f, 5.0f}));
}

// Bevy `compute_pixel_space_point_for_sprite_with_anchor_flip_y` (sprite.rs:433-455).
TEST(Sprite, ComputesPixelSpacePointWithFlipY) {
    epix::assets::Assets<image::Image> images;
    epix::assets::Assets<image::TextureAtlasLayout> layouts;
    const auto source = images.emplace(image::Image::create2d(5, 10, wgpu::TextureFormat::eRGBA8Unorm));

    const auto flipped = sprite::Sprite{.image = source, .flip_y = true};
    const auto compute = [&](glm::vec2 point) {
        return flipped.compute_pixel_space_point(point, sprite::Anchor::TOP_RIGHT, images, layouts);
    };

    EXPECT_EQ(compute({-4.5f, -0.5f}), (glm::vec2{0.5f, 9.5f}));
    EXPECT_EQ(compute({-2.5f, -5.0f}), (glm::vec2{2.5f, 5.0f}));
    EXPECT_EQ(compute({-2.5f, -0.5f}), (glm::vec2{2.5f, 9.5f}));

    const auto right_outside = compute({0.5f, -5.0f});
    ASSERT_FALSE(right_outside.has_value());
    EXPECT_EQ(right_outside.error(), (glm::vec2{5.5f, 5.0f}));
    const auto left_outside = compute({-5.5f, -5.0f});
    ASSERT_FALSE(left_outside.has_value());
    EXPECT_EQ(left_outside.error(), (glm::vec2{-0.5f, 5.0f}));
}

// Bevy `compute_pixel_space_point_for_sprite_with_rect` (sprite.rs:457-477): the `rect`
// offsets the point into image space, and a point outside the rect but still inside the
// image is an `Err`.
TEST(Sprite, ComputesPixelSpacePointWithRect) {
    epix::assets::Assets<image::Image> images;
    epix::assets::Assets<image::TextureAtlasLayout> layouts;
    const auto source = images.emplace(image::Image::create2d(5, 10, wgpu::TextureFormat::eRGBA8Unorm));

    const auto cropped = sprite::Sprite{.image = source, .rect = image::Rect{{1.5f, 3.0f}, {3.0f, 9.5f}}};
    const auto compute = [&](glm::vec2 point) {
        return cropped.compute_pixel_space_point(point, sprite::Anchor::BOTTOM_LEFT, images, layouts);
    };

    EXPECT_EQ(compute({0.5f, 0.5f}), (glm::vec2{2.0f, 9.0f}));
    // The pixel is outside the rect, but is still a valid pixel in the image.
    const auto outside_rect = compute({2.0f, 2.5f});
    ASSERT_FALSE(outside_rect.has_value());
    EXPECT_EQ(outside_rect.error(), (glm::vec2{3.5f, 7.0f}));
}

// Bevy `compute_pixel_space_point_for_texture_atlas_sprite` (sprite.rs:479-506), whose
// in-rect arm the existing atlas test already covers; this ports the `Err` arm too.
TEST(Sprite, ComputesPixelSpacePointForAtlasSpriteOutsideTheAtlas) {
    epix::assets::Assets<image::Image> images;
    epix::assets::Assets<image::TextureAtlasLayout> layouts;
    const auto source = images.emplace(image::Image::create2d(5, 10, wgpu::TextureFormat::eRGBA8Unorm));
    const auto layout = layouts.emplace(image::TextureAtlasLayout{
        .size = {5, 10}, .textures = {image::URect{{1, 1}, {4, 4}}}});

    const auto atlas = sprite::Sprite::from_atlas_image(source, image::TextureAtlas{layout, 0});
    const auto compute = [&](glm::vec2 point) {
        return atlas.compute_pixel_space_point(point, sprite::Anchor::BOTTOM_LEFT, images, layouts);
    };

    EXPECT_EQ(compute({0.5f, 0.5f}), (glm::vec2{1.5f, 3.5f}));
    // The pixel is outside the texture atlas, but is still a valid pixel in the image.
    const auto outside_atlas = compute({4.0f, 2.5f});
    ASSERT_FALSE(outside_atlas.has_value());
    EXPECT_EQ(outside_atlas.error(), (glm::vec2{5.0f, 1.5f}));
}

// Bevy `compute_pixel_space_point_for_texture_atlas_sprite_with_rect` (sprite.rs:508-537): the
// rect is relative to the texture-atlas sprite.
TEST(Sprite, ComputesPixelSpacePointForAtlasSpriteWithRect) {
    epix::assets::Assets<image::Image> images;
    epix::assets::Assets<image::TextureAtlasLayout> layouts;
    const auto source = images.emplace(image::Image::create2d(5, 10, wgpu::TextureFormat::eRGBA8Unorm));
    const auto layout = layouts.emplace(image::TextureAtlasLayout{
        .size = {5, 10}, .textures = {image::URect{{1, 1}, {4, 4}}}});

    auto atlas = sprite::Sprite::from_atlas_image(source, image::TextureAtlas{layout, 0});
    atlas.rect = image::Rect{{1.5f, 1.5f}, {3.0f, 3.0f}};
    const auto compute = [&](glm::vec2 point) {
        return atlas.compute_pixel_space_point(point, sprite::Anchor::BOTTOM_LEFT, images, layouts);
    };

    EXPECT_EQ(compute({0.5f, 0.5f}), (glm::vec2{3.0f, 3.5f}));
    const auto outside_atlas = compute({4.0f, 2.5f});
    ASSERT_FALSE(outside_atlas.has_value());
    EXPECT_EQ(outside_atlas.error(), (glm::vec2{6.5f, 1.5f}));
}

// Bevy `compute_pixel_space_point_for_sprite_with_custom_size_and_rect`
// (sprite.rs:539-565): `custom_size` scales the point and the rect crops it.
TEST(Sprite, ComputesPixelSpacePointWithCustomSizeAndRect) {
    epix::assets::Assets<image::Image> images;
    epix::assets::Assets<image::TextureAtlasLayout> layouts;
    const auto source = images.emplace(image::Image::create2d(5, 10, wgpu::TextureFormat::eRGBA8Unorm));

    const auto scaled = sprite::Sprite{.image        = source,
                                       .custom_size  = glm::vec2(100.0f, 50.0f),
                                       .rect         = image::Rect{{0.0f, 0.0f}, {5.0f, 5.0f}}};
    const auto compute = [&](glm::vec2 point) {
        return scaled.compute_pixel_space_point(point, sprite::Anchor{}, images, layouts);
    };

    EXPECT_EQ(compute({30.0f, 15.0f}), (glm::vec2{4.0f, 1.0f}));
    EXPECT_EQ(compute({-10.0f, -15.0f}), (glm::vec2{2.0f, 4.0f}));
    const auto outside_rect = compute({0.0f, 35.0f});
    ASSERT_FALSE(outside_rect.has_value());
    EXPECT_EQ(outside_rect.error(), (glm::vec2{2.5f, -1.0f}));
}

// Bevy `compute_pixel_space_point_for_sprite_with_zero_custom_size` (sprite.rs:567-595): a
// zero-sized sprite has no in-range pixel, so every point reports its clipped value.
TEST(Sprite, ComputesPixelSpacePointWithZeroCustomSize) {
    epix::assets::Assets<image::Image> images;
    epix::assets::Assets<image::TextureAtlasLayout> layouts;
    const auto source = images.emplace(image::Image::create2d(5, 10, wgpu::TextureFormat::eRGBA8Unorm));

    const auto empty = sprite::Sprite{.image = source, .custom_size = glm::vec2(0.0f, 0.0f)};
    const auto compute = [&](glm::vec2 point) {
        return empty.compute_pixel_space_point(point, sprite::Anchor{}, images, layouts);
    };

    const auto first = compute({30.0f, 15.0f});
    ASSERT_FALSE(first.has_value());
    EXPECT_EQ(first.error(), (glm::vec2{30.0f, -15.0f}));
    const auto second = compute({-10.0f, -15.0f});
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), (glm::vec2{-10.0f, 15.0f}));
    const auto third = compute({0.0f, 35.0f});
    ASSERT_FALSE(third.has_value());
    EXPECT_EQ(third.error(), (glm::vec2{0.0f, -35.0f}));
}

// Bevy `calculate_bounds_2d_correct_aabb_for_sprite_with_custom_rect` (lib.rs:312-358):
// `Sprite::rect` supplies the size - ahead of the image's own - with a `TOP_RIGHT` anchor, and
// Bevy asserts the exact centre and half extents.
TEST(CalculateBounds2d, SizesAabbFromTheSpriteRect) {
    auto app = epix::app::App::create();
    app.add_plugins(epix::assets::AssetPlugin{});
    app.add_plugins(image::ImagePlugin{});
    app.add_plugins(epix::mesh::MeshPlugin{});
    app.add_plugins(epix::camera::CameraPlugin{});
    app.add_plugins(sprite::SpritePlugin{});

    // Bevy's fixture inserts `Image::default()`, a 1x1 image, so the rect is what decides the
    // size: `custom_size`, then `rect`, then the atlas rect or the image size (lib.rs:167-179).
    const auto image_handle = app.world_mut().resource_mut<epix::assets::Assets<image::Image>>().emplace(
        image::Image::create2d(1, 1, wgpu::TextureFormat::eRGBA8Unorm));
    auto sprite = sprite::Sprite::from_image(image_handle);
    sprite.rect = image::Rect{{0.0f, 0.0f}, {0.5f, 1.0f}};
    const auto entity = app.world_mut().spawn(sprite, sprite::Anchor::TOP_RIGHT).id();

    app.update();

    const auto aabb_ref = app.world().entity(entity).get<epix::camera::Aabb>();
    ASSERT_TRUE(aabb_ref.has_value()) << "a sprite with a rect should have an AABB";
    EXPECT_EQ(aabb_ref->get().center, glm::vec3(-0.25f, -0.5f, 0.0f));
    EXPECT_EQ(aabb_ref->get().half_extents, glm::vec3(0.25f, 0.5f, 0.0f));
}
