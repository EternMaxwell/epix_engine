#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>

#include <ranges>

namespace assets = epix::assets;
namespace image = epix::image;
namespace sprite = epix::sprite;
namespace sprite_render = epix::sprite_render;

TEST(ComputedTextureSlices, ExtractionIsLazyAndAppliesFlipAndAnchor) {
    sprite::Sprite value;
    value.custom_size = glm::vec2(100.0f, 50.0f);
    value.flip_x = true;
    const sprite_render::ComputedTextureSlices computed({sprite::TextureSlice{
        .texture_rect = image::Rect{{0.0f, 0.0f}, {10.0f, 20.0f}},
        .draw_size = {30.0f, 40.0f},
        .offset = {15.0f, -5.0f},
    }});

    auto extracted = computed.extract_slices(value, glm::vec2(0.5f, -0.5f));
    static_assert(std::ranges::view<decltype(extracted)>);
    ASSERT_EQ(std::ranges::distance(extracted), 1);
    const auto slice = *extracted.begin();
    EXPECT_EQ(slice.offset, glm::vec2(-65.0f, 20.0f));
    EXPECT_EQ(slice.rect, (image::Rect{{0.0f, 0.0f}, {10.0f, 20.0f}}));
    EXPECT_EQ(slice.size, glm::vec2(30.0f, 40.0f));
}

TEST(ComputedTextureSlices, SpriteAndImageChangesMaintainComponent) {
    auto app = epix::app::App::create();
    app.world_mut().init_resource<assets::Assets<image::Image>>();
    app.world_mut().init_resource<assets::Assets<image::TextureAtlasLayout>>();
    app.add_events<assets::AssetEvent<image::Image>>();
    app.add_systems(epix::app::Update,
                    epix::ecs::into(sprite_render::compute_slices_on_asset_event));
    app.add_systems(epix::app::Update,
                    epix::ecs::into(sprite_render::compute_slices_on_sprite_change));

    const auto image_handle = app.resource_mut<assets::Assets<image::Image>>().emplace(
        image::Image::create2d(16, 16, image::Format::RGBA8));
    auto value = sprite::Sprite::from_image(image_handle);
    value.custom_size = glm::vec2(40.0f, 40.0f);
    value.image_mode = sprite::SpriteImageMode::Sliced{
        sprite::TextureSlicer{.border = sprite::BorderRect::all(4.0f)}};
    const auto entity = app.world_mut().spawn(std::move(value)).id();

    app.run_schedule(epix::app::Update);
    ASSERT_TRUE(app.world().entity(entity).contains<sprite_render::ComputedTextureSlices>());
    const auto computed = app.world().entity(entity).get<sprite_render::ComputedTextureSlices>();
    ASSERT_TRUE(computed);
    EXPECT_EQ(std::ranges::distance(computed->get().extract_slices(
                  app.world().entity(entity).get<sprite::Sprite>()->get(), glm::vec2(0.0f))),
              9);

    app.world_mut().entity_mut(entity).remove<sprite_render::ComputedTextureSlices>();
    app.resource_mut<epix::ecs::Events<assets::AssetEvent<image::Image>>>().push(
        assets::AssetEvent<image::Image>::modified(image_handle.id()));
    app.run_schedule(epix::app::Update);
    EXPECT_TRUE(app.world().entity(entity).contains<sprite_render::ComputedTextureSlices>());
}
