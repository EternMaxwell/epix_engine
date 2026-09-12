#include <gtest/gtest.h>

#include <epix/image.hpp>
#include <epix/image/detail/atlas_allocator.hpp>
#include <optional>
#include <utility>
#include <vector>

namespace image = epix::image;

namespace {
using AtlasRect = image::detail::AtlasRectangle;

/** @brief A rectangle from its four edges, for the fixture tables below. */
constexpr AtlasRect rect(std::int32_t min_x, std::int32_t min_y, std::int32_t max_x, std::int32_t max_y) {
    return AtlasRect{{min_x, min_y}, {max_x, max_y}};
}
}  // namespace

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

// `guillotiere` 0.6.0's allocator decides where every texture lands in a dynamic atlas, so the port
// in `image/src/atlas_allocator.cpp` is asserted against the real crate's own placements rather than
// against invariants: the expected rectangles below are a recorded dump of `guillotiere` (the
// `tmp/guillotiere-fixture` Rust project, which depends on `guillotiere = "=0.6.0"`), including the
// two requests in the first scenario that legitimately fail.
TEST(AtlasAllocator, MatchesGuillotiere060Placements) {
    const auto replay = [](image::detail::AtlasSize atlas, std::int32_t padding,
                           const std::vector<std::pair<std::int32_t, std::int32_t>>& textures,
                           const std::vector<std::optional<AtlasRect>>& expected) {
        image::detail::AtlasAllocator allocator{atlas};
        ASSERT_EQ(textures.size(), expected.size());
        for (std::size_t index = 0; index < textures.size(); ++index) {
            const auto allocation = allocator.allocate(
                image::detail::AtlasSize{textures[index].first + padding, textures[index].second + padding});
            if (!expected[index].has_value()) {
                EXPECT_FALSE(allocation.has_value()) << "index " << index;
                continue;
            }
            ASSERT_TRUE(allocation.has_value()) << "index " << index;
            EXPECT_EQ(allocation->rectangle, *expected[index]) << "index " << index;
        }
    };

    // Sizes below the small threshold (32), between it and the large threshold (256), and above it,
    // plus a request larger than the atlas. `512x512`, no padding.
    replay({512, 512}, 0,
           {{300, 20},
            {40, 40},
            {16, 16},
            {200, 100},
            {250, 250},
            {600, 600},
            {64, 64},
            {32, 32},
            {8, 300},
            {128, 12},
            {48, 48},
            {256, 256},
            {1, 1}},
           {rect(0, 0, 300, 20), rect(0, 20, 40, 60), rect(300, 0, 316, 16), rect(40, 20, 240, 120),
            rect(40, 120, 290, 370), std::nullopt, rect(40, 370, 104, 434), rect(104, 370, 136, 402),
            rect(290, 120, 298, 420), rect(136, 370, 264, 382), rect(136, 382, 184, 430), std::nullopt,
            rect(300, 16, 301, 17)});

    // A glyph-like sequence with the one-pixel padding `DynamicTextureAtlasBuilder` adds.
    replay(
        {256, 256}, 1,
        {{12, 16}, {9, 16},  {14, 18}, {7, 8},   {11, 12}, {20, 24}, {6, 16},  {13, 15}, {5, 5},  {18, 20},
         {10, 11}, {24, 30}, {8, 9},   {16, 16}, {22, 18}, {3, 7},   {15, 14}, {28, 32}, {9, 10}, {30, 30}},
        {rect(0, 0, 13, 17),  rect(0, 17, 10, 34),  rect(13, 0, 28, 19),  rect(0, 34, 8, 43),   rect(0, 43, 12, 56),
         rect(28, 0, 49, 25), rect(0, 56, 7, 73),   rect(13, 19, 27, 35), rect(7, 56, 13, 62),  rect(28, 25, 47, 46),
         rect(0, 73, 11, 85), rect(49, 0, 74, 31),  rect(0, 85, 9, 95),   rect(28, 46, 45, 63), rect(49, 31, 72, 50),
         rect(9, 85, 13, 93), rect(28, 63, 44, 78), rect(74, 0, 103, 33), rect(0, 95, 10, 106), rect(103, 0, 134, 31)});

    // A tight atlas where the last request no longer fits.
    replay({64, 64}, 0, {{40, 40}, {20, 20}, {20, 20}, {20, 20}, {10, 10}, {10, 10}, {32, 32}},
           {rect(0, 0, 40, 40), rect(0, 40, 20, 60), rect(40, 0, 60, 20), rect(40, 20, 60, 40), rect(20, 40, 30, 50),
            rect(20, 50, 30, 60), std::nullopt});
}

TEST(AtlasAllocator, SizeAndEmptyRequestMatchGuillotiere) {
    image::detail::AtlasAllocator allocator{image::detail::AtlasSize{32, 16}};
    EXPECT_EQ(allocator.size(), (image::detail::AtlasSize{32, 16}));
    // `allocate` returns nothing for an empty request before touching the free lists.
    EXPECT_FALSE(allocator.allocate(image::detail::AtlasSize{0, 0}).has_value());
    EXPECT_FALSE(allocator.allocate(image::detail::AtlasSize{0, 8}).has_value());
    // The first real allocation gets the whole root rectangle.
    const auto allocation = allocator.allocate(image::detail::AtlasSize{32, 16});
    ASSERT_TRUE(allocation.has_value());
    EXPECT_EQ(allocation->rectangle, rect(0, 0, 32, 16));
    // The atlas is now full.
    EXPECT_FALSE(allocator.allocate(image::detail::AtlasSize{1, 1}).has_value());
}
