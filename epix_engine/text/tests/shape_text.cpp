#include <freetype/freetype.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>

#include "../src/font_array.hpp"

import epix.assets;
import epix.text;

namespace {
class FontFaceFixture {
   public:
    text::font::FontAtlasSet atlas_set_;

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

    text::font::FontAtlas& get_atlas(float font_size, bool anti_aliased = true) {
        return atlas_set_.get_or_insert({.size = font_size, .anti_aliased = anti_aliased});
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

    auto& atlas = fixture.get_atlas(font_size);
    text::TextFont font{
        .font            = assets::Handle<text::font::Font>(assets::AssetId<text::font::Font>::invalid()),
        .size            = font_size,
        .line_height     = line_height,
        .relative_height = false,
    };
    auto shaped =
        text::shape_text(text::Text::with_str("Hello\nWorld"), font, text::TextLayout{}, text::TextBounds{}, atlas);

    ASSERT_FALSE(shaped.glyphs().empty());
    EXPECT_NEAR(shaped.top(), 0.0f, 0.001f);
    EXPECT_NEAR(shaped.height(), 2.0f * line_height, 0.001f);
    EXPECT_NEAR(shaped.bottom(), -2.0f * line_height, 0.001f);

    float max_y = -std::numeric_limits<float>::infinity();
    for (const auto& glyph_info : shaped.glyphs()) {
        const auto& glyph = atlas.get_glyph(glyph_info.glyph_index);
        max_y             = std::max(max_y, glyph_info.y_offset + glyph.horiBearingY);
    }
    EXPECT_LE(max_y, 0.001f);
}