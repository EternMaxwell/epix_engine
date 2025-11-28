#pragma once

#include "epix/core.hpp"
#include "epix/core/component.hpp"
#include "font.hpp"

namespace epix::text {
using font::Font;
struct TextFont {
    assets::Handle<Font> font;
    float size               = 16.0f;
    float line_height        = 1.2f;
    bool relative_height : 1 = true;
    bool anti_aliased : 1    = true;

    operator font::FontAtlasKey() const { return font::FontAtlasKey{size, anti_aliased}; }
};
struct Text {
    std::string content;
    static Text with_str(std::string_view s) { return Text{std::string(s)}; }
};

struct GlyphInfo {
    uint32_t glyph_index;
    uint32_t cluster;
    float x_offset;
    float y_offset;
    float x_advance;
    float y_advance;
};
struct ShapedText {
    std::vector<GlyphInfo> glyphs;
    // actual size of the shaped block (width, height) in pixels, before applying bounds clipping
    float width  = 0.0f;
    float height = 0.0f;
};
enum class Justify {
    Left,
    Center,
    Right,
    Justified,
};
struct TextColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};
enum class TextWrap {
    WordWrap,
    CharWrap,
    WordOrCharWrap,  // Try word, then char if word is too long
    NoWrap,
};
struct TextLayout {
    Justify justify    = Justify::Left;
    TextWrap wrap_mode = TextWrap::WordWrap;
};
struct TextBounds {
    std::optional<float> width;   // in pixels.
    std::optional<float> height;  // in pixels.
};
struct TextMeasure {
    float width  = 0.0f;
    float height = 0.0f;

    static TextMeasure from_shaped_text(const ShapedText& shaped_text, const TextFont& font, font::FontAtlas& atlas);
};
ShapedText shape_text(
    const Text& text, const TextFont& font, const TextLayout& layout, const TextBounds& bounds, font::FontAtlas& atlas);

struct TextBundle {
    Text text;
    TextFont font;
    TextLayout layout;
    TextBounds bounds;
};
}  // namespace epix::text

template <>
struct epix::core::bundle::Bundle<epix::text::TextBundle> {
    static size_t write(epix::text::TextBundle& bundle, std::span<void*> dests) {
        new (dests[0]) epix::text::Text(std::move(bundle.text));
        new (dests[1]) epix::text::TextFont(std::move(bundle.font));
        new (dests[2]) epix::text::TextLayout(std::move(bundle.layout));
        new (dests[3]) epix::text::TextBounds(std::move(bundle.bounds));
        return 4;
    }
    static auto type_ids(const epix::TypeRegistry& registry) {
        return std::array{
            registry.type_id<epix::text::Text>(),
            registry.type_id<epix::text::TextFont>(),
            registry.type_id<epix::text::TextLayout>(),
            registry.type_id<epix::text::TextBounds>(),
        };
    }
    static void register_components(const epix::TypeRegistry&, epix::core::Components& components) {
        components.register_info<epix::text::Text>();
        components.register_info<epix::text::TextFont>();
        components.register_info<epix::text::TextLayout>();
        components.register_info<epix::text::TextBounds>();
    }
};
static_assert(epix::core::bundle::is_bundle<epix::text::TextBundle>);