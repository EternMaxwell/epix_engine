#include <freetype/freetype.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
#include <epix/text.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

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

    text::ShapedText shape(std::string_view content, float font_size) {
        text::TextFont font{
            .font            = assets::Handle<text::font::Font>(assets::AssetId<text::font::Font>::invalid()),
            .size            = font_size,
            .line_height     = 1.2f,
            .relative_height = true,
            .anti_aliased    = true,
        };
        return text::shape_text(text::Text::with_str(content), font, text::TextLayout{}, text::TextBounds{},
                                atlas_set_, textures_, atlas_layouts_);
    }

   private:
    FT_Library library_ = nullptr;
    FT_Face face_       = nullptr;
    std::unique_ptr<std::byte[]> font_data_;
};
}  // namespace

// Bevy's `Text2d` requires a `Transform`, `TextColor`, `Visibility`,
// `VisibilityClass`, `Anchor` and registers the *Sprite* visibility class
// through an `on_add` hook, so the sprite queue renders its glyphs without a
// `Sprite` component (`bevy_sprite/src/text2d.rs:82-96`).
TEST(Text2d, RegistersTheSpriteVisibilityClassAndItsRequiredComponents) {
    ecs::World world(ecs::WorldId(0));
    const auto entity = world.spawn(text::Text2d{}).id();

    ASSERT_TRUE(world.get_entity(entity)->get<transform::Transform>().has_value());
    ASSERT_TRUE(world.get_entity(entity)->get<text::TextColor>().has_value());
    ASSERT_TRUE(world.get_entity(entity)->get<camera::Visibility>().has_value());
    ASSERT_TRUE(world.get_entity(entity)->get<sprite::Anchor>().has_value());

    auto visibility_class = world.get_entity(entity)->get<camera::VisibilityClass>();
    ASSERT_TRUE(visibility_class.has_value());
    ASSERT_EQ(visibility_class->get().classes.size(), 1u);
    EXPECT_EQ(visibility_class->get().classes.front(), meta::type_index(meta::type_id<sprite::Sprite>()));
}

// Bevy's `extract_text2d_sprite` emits one `ExtractedSlice` per glyph, whose
// offset is the glyph quad's center negated into 2D world space, and one
// `ExtractedSprite` per run of glyphs sharing an atlas texture
// (`bevy_sprite_render/src/text2d/mod.rs:242-270`). Text is therefore drawn by
// `SpritePipeline`, not by a text-specific pipeline.
TEST(Text2dExtraction, EmitsOneSlicePerGlyphAndGroupsThemIntoSpriteRuns) {
    FontFaceFixture fixture;
    constexpr float font_size = 48.0f;

    auto shaped = fixture.shape("Hi", font_size);
    ASSERT_FALSE(shaped.glyphs().empty());

    sprite_render::ExtractedSprites sprites;
    sprite_render::ExtractedSlices slices;
    std::uint32_t next_render_entity = 0;
    auto alloc_render_entity         = [&next_render_entity] { return ecs::Entity::from_index(next_render_entity++); };
    const auto main_entity           = ecs::Entity::from_index(1000);
    const glm::vec4 color{1.0f, 0.0f, 0.0f, 1.0f};

    text::detail::extract_text2d_layout(shaped, main_entity, text::TextBounds{},
                                        sprite::Anchor{sprite::Anchor::CENTER}, color, transform::GlobalTransform{},
                                        fixture.atlas_layouts_, sprites.sprites, slices.slices, alloc_render_entity);

    ASSERT_EQ(slices.slices.size(), shaped.glyphs().size());
    ASSERT_EQ(sprites.sprites.size(), 1u);

    const auto& sprite = sprites.sprites.front();
    EXPECT_EQ(sprite.main_entity, main_entity);
    EXPECT_EQ(sprite.image_handle_id, shaped.glyphs().front().atlas_info.texture);
    EXPECT_EQ(sprite.color, color);
    EXPECT_EQ(sprite.flip_x, false);
    EXPECT_EQ(sprite.flip_y, false);
    ASSERT_TRUE(std::holds_alternative<sprite_render::ExtractedSpriteSlices>(sprite.kind));
    const auto& range = std::get<sprite_render::ExtractedSpriteSlices>(sprite.kind);
    EXPECT_EQ(range.begin, 0u);
    EXPECT_EQ(range.end, slices.slices.size());
    EXPECT_EQ(sprite.render_entity, ecs::Entity::from_index(0u));

    for (std::size_t index = 0; index < slices.slices.size(); ++index) {
        const auto& glyph = shaped.glyphs()[index];
        EXPECT_FLOAT_EQ(slices.slices[index].offset.x, glyph.position.x);
        EXPECT_FLOAT_EQ(slices.slices[index].offset.y, -glyph.position.y);
        EXPECT_EQ(slices.slices[index].size, glyph.size);
        EXPECT_EQ(slices.slices[index].rect.size(), glyph.size);
    }

    // `top_left = (Anchor::TOP_LEFT - anchor) * size` (`text2d/mod.rs:80`):
    // with a centered anchor the block's top-left corner lands at
    // `(-size.x / 2, +size.y / 2)` in world space.
    EXPECT_NEAR(sprite.transform.matrix[3].x, -shaped.size().x * 0.5f, 0.001f);
    EXPECT_NEAR(sprite.transform.matrix[3].y, shaped.size().y * 0.5f, 0.001f);
}

// `TextBounds` overrides the layout size used for anchoring, exactly as in
// Bevy (`bevy_sprite_render/src/text2d/mod.rs:75-78`).
TEST(Text2dExtraction, TextBoundsOverrideTheLayoutSizeForAnchoring) {
    FontFaceFixture fixture;
    constexpr float font_size = 48.0f;

    auto shaped          = fixture.shape("Hi", font_size);
    text::TextBounds bounds{.width = 300.0f, .height = 90.0f};

    sprite_render::ExtractedSprites sprites;
    sprite_render::ExtractedSlices slices;
    auto alloc_render_entity = [] { return ecs::Entity::from_index(0u); };

    text::detail::extract_text2d_layout(shaped, ecs::Entity::from_index(1u), bounds,
                                        sprite::Anchor{sprite::Anchor::TOP_LEFT}, glm::vec4{1.0f},
                                        transform::GlobalTransform{}, fixture.atlas_layouts_, sprites.sprites,
                                        slices.slices, alloc_render_entity);

    ASSERT_EQ(sprites.sprites.size(), 1u);
    // `TOP_LEFT - TOP_LEFT` is zero, so a top-left anchored block is placed at
    // the entity's own position.
    EXPECT_NEAR(sprites.sprites.front().transform.matrix[3].x, 0.0f, 0.001f);
    EXPECT_NEAR(sprites.sprites.front().transform.matrix[3].y, 0.0f, 0.001f);

    sprites.sprites.clear();
    slices.slices.clear();
    text::detail::extract_text2d_layout(shaped, ecs::Entity::from_index(1u), bounds,
                                        sprite::Anchor{sprite::Anchor::CENTER}, glm::vec4{1.0f},
                                        transform::GlobalTransform{}, fixture.atlas_layouts_, sprites.sprites,
                                        slices.slices, alloc_render_entity);

    ASSERT_EQ(sprites.sprites.size(), 1u);
    EXPECT_NEAR(sprites.sprites.front().transform.matrix[3].x, -150.0f, 0.001f);
    EXPECT_NEAR(sprites.sprites.front().transform.matrix[3].y, 45.0f, 0.001f);
}
