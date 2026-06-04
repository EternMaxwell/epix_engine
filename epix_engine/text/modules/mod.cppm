module;
#include <epix/text.hpp>

export module epix.text;

export namespace epix::text {
using epix::text::GlyphInfo;
using epix::text::Justify;
using epix::text::shape_text;
using epix::text::ShapedText;
using epix::text::Text;
using epix::text::Text2d;
using epix::text::Text2dBundle;
using epix::text::TextBounds;
using epix::text::TextBundle;
using epix::text::TextColor;
using epix::text::TextFont;
using epix::text::TextImage;
using epix::text::TextLayout;
using epix::text::TextMeasure;
using epix::text::TextMesh;
using epix::text::TextPlugin;
using epix::text::TextRenderPlugin;
using epix::text::TextWrap;
using epix::text::font::Font;
}  // namespace epix::text

export namespace epix::text::font {
using epix::text::font::AtlasRect;
using epix::text::font::Font;
using epix::text::font::FontAtlas;
using epix::text::font::FontAtlasKey;
using epix::text::font::FontAtlasKeyHash;
using epix::text::font::FontAtlasSet;
using epix::text::font::FontAtlasSets;
using epix::text::font::FontPlugin;
using epix::text::font::FontSystems;
using epix::text::font::Glyph;
}  // namespace epix::text::font
