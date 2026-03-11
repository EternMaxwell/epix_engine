module;

export module epix.text:text;

import :font;
import epix.assets;
import epix.core;
import epix.image;
import std;

namespace text {
export using font::Font;

export struct TextFont {
    assets::Handle<Font> font;
    float size               = 16.0f;
    float line_height        = 1.2f;
    bool relative_height : 1 = true;
    bool anti_aliased : 1    = true;

    operator font::FontAtlasKey() const { return font::FontAtlasKey{size, anti_aliased}; }
};

export struct Text {
    std::string content;
    static Text with_str(std::string_view s) { return Text{std::string(s)}; }
};

export struct GlyphInfo {
    std::uint32_t glyph_index;
    std::uint32_t cluster;
    float x_offset;
    float y_offset;
    float x_advance;
    float y_advance;
};

export struct TextLayout;
export struct TextBounds;

export struct ShapedText {
    float line_height() const { return ascent_ - descent_; }
    float width() const { return right_ - left_; }
    float height() const { return top_ - bottom_; }
    float left() const { return left_; }
    float right() const { return right_; }
    float top() const { return top_; }
    float bottom() const { return bottom_; }
    float ascent() const { return ascent_; }
    float descent() const { return descent_; }
    std::span<const GlyphInfo> glyphs() const { return std::span<const GlyphInfo>(glyphs_); }

   private:
    std::vector<GlyphInfo> glyphs_;
    float left_    = 0.0f;
    float right_   = 0.0f;
    float top_     = 0.0f;
    float bottom_  = 0.0f;
    float ascent_  = 0.0f;
    float descent_ = 0.0f;

    friend ShapedText shape_text(const Text& text,
                                 const TextFont& font,
                                 const TextLayout& layout,
                                 const TextBounds& bounds,
                                 font::FontAtlas& atlas);
};

export ShapedText shape_text(
    const Text& text, const TextFont& font, const TextLayout& layout, const TextBounds& bounds, font::FontAtlas& atlas);

export enum class Justify {
    Left,
    Center,
    Right,
    Justified,
};

export struct TextColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

export enum class TextWrap {
    /**
     * @brief Break lines at word boundaries (whitespace).
     *
     * A word that exceeds the line width is placed on its own line without
     * splitting.
     */
    WordWrap,
    /**
     * @brief Break lines at glyph (character) boundaries.
     *
     * Fills remaining space on the current line with as many glyphs as
     * possible before wrapping, ignoring word boundaries entirely.
     */
    CharWrap,
    /**
     * @brief Break lines at word boundaries first; fall back to glyph-level
     * splitting when a single word is wider than the entire line width.
     */
    WordOrCharWrap,
    /**
     * @brief No automatic line wrapping.
     *
     * Text extends in a single line regardless of bounds width. Manual line
     * breaks (\\n) are still respected.
     */
    NoWrap,
};

export struct TextLayout {
    Justify justify    = Justify::Left;
    TextWrap wrap_mode = TextWrap::WordWrap;
};

export struct TextBounds {
    std::optional<float> width;
    std::optional<float> height;
};

export struct TextMeasure {
    float width  = 0.0f;
    float height = 0.0f;
};

export struct TextBundle {
    Text text;
    TextFont font;
    TextLayout layout;
    TextBounds bounds;
};

export struct TextPlugin {
    void build(core::App& app);
};
}  // namespace text

template <>
struct core::Bundle<text::TextBundle> {
    static std::size_t write(text::TextBundle& bundle, std::span<void*> dests) {
        new (dests[0]) text::Text(std::move(bundle.text));
        new (dests[1]) text::TextFont(std::move(bundle.font));
        new (dests[2]) text::TextLayout(std::move(bundle.layout));
        new (dests[3]) text::TextBounds(std::move(bundle.bounds));
        return 4;
    }

    static std::array<TypeId, 4> type_ids(const TypeRegistry& registry) {
        return std::array{
            registry.type_id<text::Text>(),
            registry.type_id<text::TextFont>(),
            registry.type_id<text::TextLayout>(),
            registry.type_id<text::TextBounds>(),
        };
    }

    static void register_components(const TypeRegistry&, Components& components) {
        components.register_info<text::Text>();
        components.register_info<text::TextFont>();
        components.register_info<text::TextLayout>();
        components.register_info<text::TextBounds>();
    }
};

static_assert(core::is_bundle<text::TextBundle>);