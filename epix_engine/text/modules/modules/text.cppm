module;

export module epix.text:text;

import :font;
import epix.assets;
import epix.core;
import epix.image;
import std;

namespace text {
/** @brief Re-export of font::Font for convenience. */
export using font::Font;

/** @brief Component specifying the font, size, and rendering options for
 * text.
 *
 * Implicitly converts to FontAtlasKey for atlas lookups. When
 * `relative_height` is true, `line_height` is multiplied by `size`.
 */
export struct TextFont {
    /** @brief Handle to the font asset to use. */
    assets::Handle<Font> font;
    /** @brief Font size in pixels. */
    float size = 16.0f;
    /** @brief Line height multiplier (or absolute if relative_height is
     * false). */
    float line_height = 1.2f;
    /** @brief If true, line_height is relative to font size. */
    bool relative_height : 1 = true;
    /** @brief If true, render glyphs with anti-aliasing. */
    bool anti_aliased : 1 = true;

    /** @brief Convert to a FontAtlasKey for atlas lookup. */
    operator font::FontAtlasKey() const { return font::FontAtlasKey{size, anti_aliased}; }
};

/** @brief Component holding the text content string to render. */
export struct Text {
    /** @brief The UTF-8 text string. */
    std::string content;
    /** @brief Create a Text with the given string view.
     * @param s String view to copy into content. */
    static Text with_str(std::string_view s) { return Text{std::string(s)}; }
};

/** @brief Information about a single shaped glyph within a text layout. */
export struct GlyphInfo {
    /** @brief Index of the glyph in the font. */
    std::uint32_t glyph_index;
    /** @brief Character cluster this glyph belongs to. */
    std::uint32_t cluster;
    /** @brief Horizontal offset from the pen position. */
    float x_offset;
    /** @brief Vertical offset from the pen position. */
    float y_offset;
    /** @brief Horizontal advance to the next glyph. */
    float x_advance;
    /** @brief Vertical advance to the next glyph. */
    float y_advance;
};

/** @brief Forward declaration for text layout parameters. */
export struct TextLayout;
/** @brief Forward declaration for text bounding constraints. */
export struct TextBounds;

/** @brief Result of text shaping: positioned glyphs with computed bounding
 * box and metrics.
 *
 * Produced by `shape_text()`. Provides accessors for the bounding box,
 * line metrics, and the glyph sequence. Internal fields are only
 * writable by `shape_text()`.
 */
export struct ShapedText {
    /** @brief Get the line height (ascent minus descent). */
    float line_height() const { return ascent_ - descent_; }
    /** @brief Get the total width of the shaped text bounding box. */
    float width() const { return right_ - left_; }
    /** @brief Get the total height of the shaped text bounding box. */
    float height() const { return top_ - bottom_; }
    /** @brief Get the left edge of the bounding box. */
    float left() const { return left_; }
    /** @brief Get the right edge of the bounding box. */
    float right() const { return right_; }
    /** @brief Get the top edge of the bounding box. */
    float top() const { return top_; }
    /** @brief Get the bottom edge of the bounding box. */
    float bottom() const { return bottom_; }
    /** @brief Get the font ascent metric. */
    float ascent() const { return ascent_; }
    /** @brief Get the font descent metric (typically negative). */
    float descent() const { return descent_; }
    /** @brief Get a read-only span of the shaped glyph positions. */
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

/** @brief Shape text into positioned glyphs using the given font, layout,
 * and bounds.
 * @param text The text content to shape.
 * @param font Font configuration (size, anti-aliasing, etc.).
 * @param layout Justification and wrapping settings.
 * @param bounds Optional width/height constraints.
 * @param atlas Font atlas used for glyph rasterization.
 * @return A ShapedText containing the final glyph positions and metrics.
 */
export ShapedText shape_text(
    const Text& text, const TextFont& font, const TextLayout& layout, const TextBounds& bounds, font::FontAtlas& atlas);

/** @brief Text horizontal justification mode. */
export enum class Justify {
    /** @brief Align text to the left edge. */
    Left,
    /** @brief Center text horizontally. */
    Center,
    /** @brief Align text to the right edge. */
    Right,
    /** @brief Distribute words evenly to fill the line width. */
    Justified,
};

/** @brief RGBA text color component. Defaults to opaque white. */
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

/** @brief Component controlling text layout: justification and wrapping
 * behaviour. */
export struct TextLayout {
    /** @brief Horizontal text justification. */
    Justify justify = Justify::Left;
    /** @brief Line wrapping mode. */
    TextWrap wrap_mode = TextWrap::WordWrap;
};

/** @brief Optional width and height constraints for text layout. When unset,
 * the corresponding dimension is unbounded. */
export struct TextBounds {
    /** @brief Maximum text width before wrapping. */
    std::optional<float> width;
    /** @brief Maximum text height (excess is clipped or ignored). */
    std::optional<float> height;
};

/** @brief Computed text measurement result. */
export struct TextMeasure {
    /** @brief Measured width of the laid-out text. */
    float width = 0.0f;
    /** @brief Measured height of the laid-out text. */
    float height = 0.0f;
};

/** @brief Bundle for spawning a text entity with content, font, layout, and
 * bounds components. */
export struct TextBundle {
    /** @brief The text content. */
    Text text;
    /** @brief Font and size configuration. */
    TextFont font;
    /** @brief Layout settings (justification, wrapping). */
    TextLayout layout;
    /** @brief Bounding constraints for the text area. */
    TextBounds bounds;
};

/** @brief Plugin that registers text shaping, layout, and measurement
 * systems. */
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