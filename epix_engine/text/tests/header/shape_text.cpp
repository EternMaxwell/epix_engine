#include <freetype/freetype.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <epix/sprite.hpp>
#include <epix/text.hpp>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

#include "../src/font_array.hpp"

using namespace epix;

namespace {
class FontFaceFixture {
   public:
    text::font::FontAtlasSet atlas_set_;
    assets::Assets<image::Image> textures_;
    assets::Assets<image::TextureAtlasLayout> atlas_layouts_;

    FontFaceFixture() : atlas_set_(nullptr) {
        if (FT_Init_FreeType(&library_) != FT_Err_Ok) {
            throw std::runtime_error("Failed to initialize FreeType.");
        }

        font_data_ = std::make_unique<std::byte[]>(font_data_array_size);
        std::memcpy(font_data_.get(), font_data_array, font_data_array_size);

        auto* raw_bytes = reinterpret_cast<const FT_Byte*>(font_data_.get());
        if (FT_New_Memory_Face(library_, raw_bytes, static_cast<FT_Long>(font_data_array_size), 0, &face_) !=
            FT_Err_Ok) {
            throw std::runtime_error("Failed to create FreeType memory face.");
        }
        atlas_set_ = text::font::FontAtlasSet(face_);
    }

    ~FontFaceFixture() {
        if (face_ != nullptr) {
            FT_Done_Face(face_);
        }
        if (library_ != nullptr) {
            FT_Done_FreeType(library_);
        }
    }

    FontFaceFixture(const FontFaceFixture&)            = delete;
    FontFaceFixture& operator=(const FontFaceFixture&) = delete;

    static text::TextFont font(float size, bool anti_aliased = true, float line_height = 1.2f,
                               bool relative_height = true) {
        return text::TextFont{
            .font            = assets::Handle<text::font::Font>(assets::AssetId<text::font::Font>::invalid()),
            .size            = size,
            .line_height     = line_height,
            .relative_height = relative_height,
            .anti_aliased    = anti_aliased,
        };
    }

    text::ShapedText shape(std::string_view content,
                           const text::TextFont& font_config,
                           text::TextLayout layout = {},
                           text::TextBounds bounds = {}) {
        return text::shape_text(text::Text::with_str(content), font_config, layout, bounds, atlas_set_, textures_,
                                atlas_layouts_);
    }

    const text::font::Glyph& metrics(std::uint32_t glyph_index, float size, bool anti_aliased = true) {
        return atlas_set_.get_glyph(text::font::FontAtlasKey{size, anti_aliased}, glyph_index);
    }

    const assets::Assets<image::TextureAtlasLayout>& atlas_layouts() const { return atlas_layouts_; }

    std::optional<std::reference_wrapper<std::vector<text::font::FontAtlas>>> atlases(float size,
                                                                                      bool anti_aliased = true) {
        return atlas_set_.get_mut(text::font::FontAtlasKey{size, anti_aliased});
    }

   private:
    FT_Library library_ = nullptr;
    FT_Face face_       = nullptr;
    std::unique_ptr<std::byte[]> font_data_;
};
}  // namespace

TEST(ShapeText, UsesTopLeftOriginAndConfiguredLineHeight) {
    FontFaceFixture fixture;
    constexpr float font_size   = 48.0f;
    constexpr float line_height = 64.0f;

    auto shaped = fixture.shape("Hello\nWorld", FontFaceFixture::font(font_size, true, line_height, false));

    ASSERT_FALSE(shaped.glyphs().empty());
    EXPECT_NEAR(shaped.top(), 0.0f, 0.001f);
    EXPECT_NEAR(shaped.height(), 2.0f * line_height, 0.001f);
    EXPECT_NEAR(shaped.bottom(), -2.0f * line_height, 0.001f);

    float max_y = -std::numeric_limits<float>::infinity();
    for (const auto& glyph_info : shaped.glyphs()) {
        const auto& glyph = fixture.metrics(glyph_info.glyph_index, font_size);
        max_y             = std::max(max_y, glyph_info.y_offset + glyph.horiBearingY);
    }
    EXPECT_LE(max_y, 0.001f);
}

// Bevy `TextLayoutInfo::size` is the block's advance box, rounded up
// (`pipeline.rs:356-358,484`), not the glyph ink extent.
TEST(ShapeText, BlockSizeIsTheRoundedAdvanceBox) {
    FontFaceFixture fixture;
    constexpr float font_size   = 48.0f;
    constexpr float line_height = 64.0f;

    auto shaped = fixture.shape("Hello\nWorld", FontFaceFixture::font(font_size, true, line_height, false));
    ASSERT_FALSE(shaped.glyphs().empty());
    EXPECT_FLOAT_EQ(shaped.size().y, 2.0f * line_height);
    EXPECT_GT(shaped.size().x, 0.0f);
    // The advance box is at least as wide as the first line's advance sum.
    EXPECT_GE(shaped.size().x + 0.001f, shaped.width());
}

// Bevy positions each glyph at the CENTER of its atlas bitmap in the block's
// +Y-down space (`pipeline.rs:441-453`); the renderer then negates Y
// (`bevy_sprite_render/src/text2d/mod.rs:248`).
TEST(ShapeText, GlyphPositionsAreQuadCentersInTopLeftOriginSpace) {
    FontFaceFixture fixture;
    constexpr float font_size = 48.0f;

    auto shaped = fixture.shape("Hi", FontFaceFixture::font(font_size));
    ASSERT_GE(shaped.glyphs().size(), 2u);

    for (const auto& glyph : shaped.glyphs()) {
        EXPECT_GT(glyph.size.x, 0.0f);
        EXPECT_GT(glyph.size.y, 0.0f);
        // The bitmap's top edge in +Y-down block space stays inside the block.
        const float top_edge = glyph.position.y - glyph.size.y * 0.5f;
        const float bottom_edge = glyph.position.y + glyph.size.y * 0.5f;
        EXPECT_GE(top_edge, -0.001f);
        EXPECT_LE(bottom_edge, shaped.size().y + 0.001f);
        // The first baseline is one ascent below the block's top edge, so the
        // glyph center cannot sit above it minus the bitmap height.
        EXPECT_GT(glyph.position.y, 0.0f);
    }

    // Horizontal advance moves later glyphs to the right.
    EXPECT_GT(shaped.glyphs()[1].position.x, shaped.glyphs()[0].position.x);
}

// Bevy's glyph atlas is a single `TextureDimension::D2` image whose glyph
// rectangles live in a `TextureAtlasLayout` (`font_atlas.rs:40-56`); the
// texture-array layout Epix used before had no Bevy counterpart.
TEST(FontAtlas, UsesASingleD2ImageWithAtlasLayoutRects) {
    FontFaceFixture fixture;
    constexpr float font_size = 48.0f;

    auto shaped = fixture.shape("Hello", FontFaceFixture::font(font_size));
    ASSERT_FALSE(shaped.glyphs().empty());

    auto atlases = fixture.atlases(font_size);
    ASSERT_TRUE(atlases.has_value());
    ASSERT_EQ(atlases->get().size(), 1u);
    const auto& atlas = atlases->get().front();

    auto image = fixture.textures_.get(atlas.texture.id());
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->get().depth_or_layers(), 1u);
    EXPECT_EQ(image->get().width(), image->get().height());
    EXPECT_GE(image->get().width(), 512u);

    auto layout = fixture.atlas_layouts().get(atlas.texture_atlas.id());
    ASSERT_TRUE(layout.has_value());
    const glm::uvec2 image_size{image->get().width(), image->get().height()};
    EXPECT_EQ(layout->get().size, image_size);
    EXPECT_EQ(layout->get().textures.size(), atlas.glyph_to_atlas_index.size());
    EXPECT_FALSE(layout->get().textures.empty());

    for (const auto& glyph : shaped.glyphs()) {
        // `AssetId` derives from `std::variant`, so compare its printed form.
        EXPECT_EQ(glyph.atlas_info.texture.to_string_short(), atlas.texture.id().to_string_short());
        EXPECT_EQ(glyph.atlas_info.texture_atlas.to_string_short(), atlas.texture_atlas.id().to_string_short());
        ASSERT_LT(glyph.atlas_info.location.glyph_index, layout->get().textures.size());
        const auto rect = layout->get().textures[glyph.atlas_info.location.glyph_index];
        // The quad is the bitmap rect (`pipeline.rs:443-457`).
        const glm::vec2 rect_size{rect.size()};
        EXPECT_EQ(rect_size, glyph.size);
        EXPECT_LE(rect.max.x, image->get().width());
        EXPECT_LE(rect.max.y, image->get().height());
    }

    // The glyph bitmaps were blitted into the atlas image.
    const auto bytes = image->get().raw_view();
    std::size_t non_zero_alpha = 0;
    for (std::size_t index = 3; index < bytes.size(); index += 4) {
        if (bytes[index] != std::byte{0}) ++non_zero_alpha;
    }
    EXPECT_GT(non_zero_alpha, 0u);
}

// Bevy's `FontSmoothing::None` thresholds the glyph alpha at 50% and asks for a
// nearest-neighbour sampler (`font_atlas.rs:48-50,212-225`).
TEST(FontAtlas, NonAntialiasedGlyphsAreThresholdedAndSampledNearest) {
    FontFaceFixture fixture;
    constexpr float font_size = 48.0f;

    auto shaped = fixture.shape("O", FontFaceFixture::font(font_size, false));
    ASSERT_FALSE(shaped.glyphs().empty());

    auto atlases = fixture.atlases(font_size, false);
    ASSERT_TRUE(atlases.has_value());
    ASSERT_FALSE(atlases->get().empty());
    const auto& atlas = atlases->get().front();

    auto image = fixture.textures_.get(atlas.texture.id());
    ASSERT_TRUE(image.has_value());
    const auto* sampler = image->get().sampler_descriptor();
    ASSERT_NE(sampler, nullptr);
    EXPECT_EQ(sampler->min_filter, image::ImageFilterMode::Nearest);
    EXPECT_EQ(sampler->mag_filter, image::ImageFilterMode::Nearest);

    const auto bytes = image->get().raw_view();
    for (std::size_t index = 3; index < bytes.size(); index += 4) {
        const auto alpha = std::to_integer<std::uint8_t>(bytes[index]);
        EXPECT_TRUE(alpha == 0 || alpha == 255);
    }
}
