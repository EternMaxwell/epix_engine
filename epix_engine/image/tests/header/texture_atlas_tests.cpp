#include <gtest/gtest.h>

#include <epix/image.hpp>

namespace image = epix::image;

TEST(TextureAtlasLayout, GridMatchesBevyOrderingAndExtent) {
    const auto layout = image::TextureAtlasLayout::from_grid(
        glm::uvec2(8, 4), 3, 2, glm::uvec2(2, 3), glm::uvec2(1, 2));

    EXPECT_EQ(layout.size, glm::uvec2(28, 11));
    ASSERT_EQ(layout.len(), 6u);
    EXPECT_EQ(layout.textures[0], (image::URect{{1, 2}, {9, 6}}));
    EXPECT_EQ(layout.textures[1], (image::URect{{11, 2}, {19, 6}}));
    EXPECT_EQ(layout.textures[3], (image::URect{{1, 9}, {9, 13}}));
}

TEST(TextureAtlasLayout, AddTextureAndEmptyMatchBevy) {
    auto layout = image::TextureAtlasLayout::new_empty(glm::uvec2(64, 32));
    EXPECT_TRUE(layout.is_empty());
    EXPECT_EQ(layout.add_texture(image::URect{{2, 3}, {10, 11}}), 0u);
    EXPECT_EQ(layout.add_texture(image::URect{{12, 3}, {20, 11}}), 1u);
    EXPECT_FALSE(layout.is_empty());
    EXPECT_EQ(layout.len(), 2u);
}

TEST(TextureAtlas, ResolvesSelectedTextureRectangle) {
    epix::assets::Assets<image::TextureAtlasLayout> layouts;
    auto layout = image::TextureAtlasLayout::new_empty(glm::uvec2(32, 16));
    layout.textures = {
        image::URect{{0, 0}, {8, 8}},
        image::URect{{8, 0}, {24, 8}},
    };
    const auto handle = layouts.emplace(std::move(layout));

    const image::TextureAtlas atlas{handle, 1};
    EXPECT_EQ(atlas.texture_rect(layouts), (image::URect{{8, 0}, {24, 8}}));
    EXPECT_FALSE(atlas.with_index(9).texture_rect(layouts).has_value());
}

TEST(TextureAtlasSources, ResolvesIndexRectUvAndHandle) {
    epix::assets::Assets<image::Image> images;
    epix::assets::Assets<image::TextureAtlasLayout> layouts;
    const auto source = images.emplace(image::Image::create2d(8, 8, image::Format::RGBA8));
    auto layout       = image::TextureAtlasLayout::new_empty(glm::uvec2(32, 16));
    layout.textures.push_back(image::URect{{8, 4}, {24, 12}});
    const auto layout_handle = layouts.emplace(layout);

    image::TextureAtlasSources sources;
    sources.texture_ids.emplace(source.id(), 0);
    EXPECT_EQ(sources.texture_index(source.id()), 0u);
    EXPECT_EQ(sources.texture_rect(layout, source.id()), layout.textures[0]);
    EXPECT_EQ(sources.uv_rect(layout, source.id()), (image::Rect{{0.25f, 0.25f}, {0.75f, 0.75f}}));
    ASSERT_TRUE(sources.handle(layout_handle, source.id()).has_value());
    EXPECT_EQ(sources.handle(layout_handle, source.id())->index, 0u);
}
