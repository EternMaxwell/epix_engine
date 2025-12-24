#include <freetype/freetype.h>
#include <hb-ft.h>
#include <hb.h>

#include <cmath>
#include <limits>

#include "epix/text/font.hpp"
#include "epix/text/text.hpp"

using namespace epix;
using namespace epix::text;

namespace epix::text {
ShapedText shape_text(const Text& text,
                      const TextFont& font,
                      const TextLayout& layout,
                      const TextBounds& bounds,
                      font::FontAtlas& atlas) {
    ShapedText out;

    // Obtain the FT_Face used by the atlas. If missing, return empty shaped text.
    auto face_ptr = atlas.get_font_face();
    if (!face_ptr) return out;
    FT_Face ft_face = reinterpret_cast<FT_Face>(face_ptr);
    // Ensure FreeType face size matches requested font size so HarfBuzz positions are in pixels
    FT_Set_Char_Size(ft_face, static_cast<FT_F26Dot6>(font.size * 64.0f), 0, 0, 0);
    // Create an hb_font from the FT_Face.
    hb_font_t* hbfont = hb_ft_font_create(ft_face, nullptr);
    if (!hbfont) return out;

    // After setting char size, FreeType populates face->size->metrics. Use it
    // to obtain an accurate line height and ascender/descender in pixels (26.6 fixed point -> pixels).
    float ft_line_height = static_cast<float>(static_cast<double>(ft_face->size->metrics.height) / 64.0);
    float ft_ascender    = static_cast<float>(static_cast<double>(ft_face->size->metrics.ascender) / 64.0);
    // FreeType descender is negative; convert to positive distance below baseline
    float ft_descender = -static_cast<float>(static_cast<double>(ft_face->size->metrics.descender) / 64.0);
    // Compute desired line height from TextFont: if `relative_height` is true, interpret
    // `line_height` as multiplier of font.size, otherwise treat it as absolute pixels.
    float desired_line_height = 0.0f;
    if (font.relative_height)
        desired_line_height = ft_face->size->metrics.height / 64.0f * font.line_height;
    else
        desired_line_height = font.line_height;
    // Final line height: prefer the user's desired value but defer to FreeType if it is larger.
    float final_line_height = std::max(ft_line_height, desired_line_height);

    // Fast-path: if no wrapping requested, shape entire string once and return glyphs (bounds ignored)
    if (layout.wrap_mode == TextWrap::NoWrap) {
        hb_buffer_t* buf = hb_buffer_create();
        hb_buffer_add_utf8(buf, text.content.c_str(), static_cast<int>(text.content.size()), 0,
                           static_cast<int>(text.content.size()));
        hb_buffer_guess_segment_properties(buf);
        hb_shape(hbfont, buf, nullptr, 0);

        unsigned int glyph_count       = 0;
        hb_glyph_info_t* infos         = hb_buffer_get_glyph_infos(buf, &glyph_count);
        hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buf, &glyph_count);
        out.glyphs_.reserve(glyph_count);
        // Track pen positions to compute glyph extents including bearings (floating point)
        std::vector<float> pen_positions;
        pen_positions.reserve(glyph_count);
        float pen_x = 0.0f;
        for (unsigned int i = 0; i < glyph_count; ++i) {
            pen_positions.push_back(pen_x);
            GlyphInfo gi;
            gi.glyph_index = static_cast<uint32_t>(infos[i].codepoint);
            gi.cluster     = static_cast<uint32_t>(infos[i].cluster);
            gi.x_offset    = static_cast<float>(static_cast<double>(positions[i].x_offset) / 64.0);
            gi.y_offset    = static_cast<float>(static_cast<double>(positions[i].y_offset) / 64.0);
            gi.x_advance   = static_cast<float>(static_cast<double>(positions[i].x_advance) / 64.0);
            gi.y_advance   = static_cast<float>(static_cast<double>(positions[i].y_advance) / 64.0);
            // ensure glyph present in atlas (will pend upload if missing)
            atlas.get_glyph_atlas_loc(gi.glyph_index);

            out.glyphs_.push_back(gi);
            pen_x += gi.x_advance;
        }

        // Compute accurate horizontal extents using glyph bearings (float)
        float min_x = std::numeric_limits<float>::infinity();
        float max_x = -std::numeric_limits<float>::infinity();
        // use FreeType face ascender/descender (already converted above)
        float ascent  = ft_ascender;
        float descent = ft_descender;
        for (size_t i = 0; i < out.glyphs_.size(); ++i) {
            const auto& g      = out.glyphs_[i];
            auto glyph_metrics = atlas.get_glyph(static_cast<uint32_t>(g.glyph_index));
            float pen          = pen_positions[i];
            float glyph_left   = pen + (g.x_offset + std::min(0.0f, glyph_metrics.horiBearingX));
            float glyph_right  = pen + (g.x_offset + glyph_metrics.horiBearingX + glyph_metrics.width);
            min_x              = std::min(min_x, glyph_left);
            max_x              = std::max(max_x, glyph_right);
            // do not modify ascent/descent per-glyph here; use FreeType face ascender/descender
        }
        if (out.glyphs_.empty()) {
            min_x = 0.0f;
            max_x = 0.0f;
        }
        // Allow user-specified final_line_height to be smaller than font ascender+descender
        // so lines may overlap if requested.
        float line_h = final_line_height;
        // Single-line block height
        float full_height = line_h;
        out.left_         = min_x;
        out.right_        = max_x;
        out.ascent_       = ascent;
        out.descent_      = -descent;
        // With bottom-left origin, bottom is 0 and top is the full block height
        out.top_    = full_height;
        out.bottom_ = 0.0f;

        // shift glyph x offsets so they are absolute pen positions; compute glyph y as baseline-from-bottom + per-entry
        // offset
        float baseline_y = full_height - ascent;  // first/only baseline measured from bottom
        for (size_t i = 0; i < out.glyphs_.size(); ++i) {
            // keep glyph x_offset as the absolute pen position + HarfBuzz x_offset (not normalized to left bound)
            out.glyphs_[i].x_offset = pen_positions[i] + out.glyphs_[i].x_offset;
            // HarfBuzz y_offset was stored as raw value; convert to up-positive and add baseline-from-bottom
            out.glyphs_[i].y_offset = baseline_y + (-out.glyphs_[i].y_offset);
        }

        hb_buffer_destroy(buf);
        hb_font_destroy(hbfont);
        return out;
    }

    // Word-based wrap path: split input into words (preserve trailing spaces), shape each word, then pack words into
    // lines
    const std::string& s = text.content;
    struct Token {
        std::string text;
        bool force_break_after = false;
    };  // force_break_after for explicit newlines
    std::vector<Token> tokens;
    tokens.reserve(64);

    // Split into tokens: word tokens include following ASCII whitespace (space, tab). Newline forces break.
    std::string cur;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c <= 0x7F && (c == '\n' || c == '\r')) {
            if (!cur.empty()) {
                tokens.push_back({cur, true});
                cur.clear();
            }
            // consume newline (could be CRLF)
            if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n')
                i += 2;
            else
                i += 1;
            // push an empty token as explicit break (handled by force_break)
            tokens.push_back({std::string(), true});
            continue;
        }

        // accumulate non-newline or whitespace characters into word, and include following spaces into same token
        // gather bytes until we encounter ASCII whitespace or newline
        size_t start = i;
        while (i < s.size()) {
            unsigned char cc = static_cast<unsigned char>(s[i]);
            if (cc <= 0x7F && (cc == ' ' || cc == '\t' || cc == '\n' || cc == '\r')) break;
            // advance by UTF-8 codepoint width
            if ((cc & 0x80) == 0)
                i += 1;
            else if ((cc & 0xE0) == 0xC0)
                i += 2;
            else if ((cc & 0xF0) == 0xE0)
                i += 3;
            else
                i += 4;
        }
        std::string word = s.substr(start, i - start);
        // now collect trailing ascii spaces/tabs (but stop on newline)
        size_t j = i;
        while (j < s.size()) {
            unsigned char cc = static_cast<unsigned char>(s[j]);
            if (cc == ' ' || cc == '\t') {
                j++;
            } else
                break;
        }
        if (j > i) {
            word.append(s.substr(i, j - i));
            i = j;
        }
        tokens.push_back({std::move(word), false});
    }
    if (!cur.empty()) tokens.push_back({cur, false});

    // Helper: shape a token and return per-glyph entries and token width in pixels
    struct GEntry {
        uint32_t glyph_index;
        uint32_t cluster;
        int32_t x_advance;
        int32_t x_offset;
        int32_t y_offset;
    };
    auto shape_token = [&](const std::string& tok) {
        std::vector<GEntry> out_entries;
        int token_width = 0;
        if (tok.empty()) return std::pair<std::vector<GEntry>, int>(std::move(out_entries), token_width);
        hb_buffer_t* tb = hb_buffer_create();
        hb_buffer_add_utf8(tb, tok.c_str(), static_cast<int>(tok.size()), 0, static_cast<int>(tok.size()));
        hb_buffer_guess_segment_properties(tb);
        hb_shape(hbfont, tb, nullptr, 0);
        unsigned int gc          = 0;
        hb_glyph_info_t* infos   = hb_buffer_get_glyph_infos(tb, &gc);
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(tb, &gc);
        out_entries.reserve(gc);
        for (unsigned int gi = 0; gi < gc; ++gi) {
            GEntry e;
            e.glyph_index = static_cast<uint32_t>(infos[gi].codepoint);
            e.cluster     = static_cast<uint32_t>(infos[gi].cluster);
            e.x_offset    = static_cast<int32_t>(std::lround(static_cast<double>(pos[gi].x_offset) / 64.0));
            // Convert HarfBuzz y_offset to up-positive convention here
            e.y_offset  = static_cast<int32_t>(-std::lround(static_cast<double>(pos[gi].y_offset) / 64.0));
            e.x_advance = static_cast<int32_t>(std::lround(static_cast<double>(pos[gi].x_advance) / 64.0));
            token_width += e.x_advance;
            out_entries.push_back(e);
        }
        hb_buffer_destroy(tb);
        return std::pair<std::vector<GEntry>, int>(std::move(out_entries), token_width);
    };

    // Pre-shape tokens to avoid repeated shaping while packing (faster for many words)
    struct TokenShape {
        std::vector<GEntry> entries;
        int width;
        bool force_break;
        std::string text;
    };
    std::vector<TokenShape> shaped_tokens;
    shaped_tokens.reserve(tokens.size());
    for (auto& t : tokens) {
        auto [ents, w] = shape_token(t.text);
        shaped_tokens.push_back({std::move(ents), w, t.force_break_after, t.text});
    }

    // Pack tokens into lines
    const float max_width_f = bounds.width.has_value() ? *bounds.width : std::numeric_limits<float>::infinity();
    const int max_width     = (max_width_f == INFINITY) ? INT_MAX : static_cast<int>(std::floor(max_width_f));
    std::vector<std::pair<size_t, size_t>> lines;  // indices into shaped_tokens [start,end)
    size_t tidx = 0;
    while (tidx < shaped_tokens.size()) {
        if (shaped_tokens[tidx].entries.empty() && shaped_tokens[tidx].force_break) {
            // explicit blank line
            lines.emplace_back(tidx, tidx);
            tidx++;
            continue;
        }
        int line_width   = 0;
        size_t start     = tidx;
        bool first_token = true;
        while (tidx < shaped_tokens.size()) {
            auto& ts = shaped_tokens[tidx];
            // if token is explicit break token (empty text with force_break), end line here
            if (ts.entries.empty() && ts.force_break) {
                tidx++;
                break;
            }
            int new_width = line_width + ts.width;
            if (first_token) {
                // always accept first token even if it overflows (will be handled later)
                line_width = new_width;
                tidx++;
                first_token = false;
            } else if (new_width <= max_width) {
                line_width = new_width;
                tidx++;
            } else {
                // doesn't fit
                if (ts.width > max_width) {
                    // token itself larger than line width
                    if (layout.wrap_mode == TextWrap::WordWrap) {
                        // if current line is empty (shouldn't happen because first_token handled), otherwise push to
                        // next line
                        if (line_width == 0) {
                            // force include (allow overflow)
                            line_width = new_width;
                            tidx++;
                        }
                        break;  // finish this line
                    } else if (layout.wrap_mode == TextWrap::WordOrCharWrap || layout.wrap_mode == TextWrap::CharWrap) {
                        // split token by glyphs to fit
                        // we will consume some glyphs from ts.entries onto this line
                        int avail = max_width - line_width;
                        if (avail <= 0) break;
                        int acc        = 0;
                        size_t consume = 0;
                        for (; consume < ts.entries.size(); ++consume) {
                            if (acc + ts.entries[consume].x_advance > avail) break;
                            acc += ts.entries[consume].x_advance;
                        }
                        if (consume == 0) {
                            // cannot fit any glyph, if line_width==0 we'll place at least one glyph
                            if (line_width == 0) {
                                consume = 1;
                                acc     = ts.entries[0].x_advance;
                            } else
                                break;
                        }
                        // create a new shaped token for the leftover glyphs and insert before next iteration
                        TokenShape leftover;
                        leftover.force_break = false;
                        leftover.width       = 0;
                        leftover.entries.reserve(ts.entries.size() - consume);
                        for (size_t k = consume; k < ts.entries.size(); ++k) {
                            leftover.entries.push_back(ts.entries[k]);
                            leftover.width += ts.entries[k].x_advance;
                        }
                        // shrink current token to consumed glyphs
                        ts.entries.resize(consume);
                        ts.width = acc;
                        // insert leftover as next token
                        shaped_tokens.insert(shaped_tokens.begin() + tidx + 1, std::move(leftover));
                        // accept current (consumed) part
                        line_width += ts.width;
                        tidx++;
                        break;  // finish line after partial token
                    }
                } else {
                    // token fits alone but not with previous tokens -> push to next line
                    break;
                }
            }
            if (ts.force_break) {
                tidx++;
                break;
            }
        }
        lines.emplace_back(start, tidx);
    }

    // Use the requested line height for layout
    const float line_height = final_line_height;
    // Compute per-line extents including bearings and overall ascent/descent
    std::vector<float> line_mins(lines.size(), 0.0f), line_maxs(lines.size(), 0.0f);
    std::vector<float> line_widths(lines.size());
    float full_width = 0.0f;
    // use FreeType face ascender/descender for global metrics (descender converted to positive distance above)
    float global_ascent  = ft_ascender;
    float global_descent = ft_descender;
    for (size_t li = 0; li < lines.size(); ++li) {
        auto [start, end] = lines[li];
        float pen         = 0.0f;
        float min_x       = std::numeric_limits<float>::infinity();
        float max_x       = -std::numeric_limits<float>::infinity();
        for (size_t ti = start; ti < end; ++ti) {
            auto& ts = shaped_tokens[ti];
            for (size_t gi = 0; gi < ts.entries.size(); ++gi) {
                auto& e            = ts.entries[gi];
                auto glyph_metrics = atlas.get_glyph(e.glyph_index);
                float glyph_left   = pen + (e.x_offset + std::min(0.0f, glyph_metrics.horiBearingX));
                float glyph_right  = pen + (e.x_offset + glyph_metrics.horiBearingX + glyph_metrics.width);
                min_x              = std::min(min_x, glyph_left);
                max_x              = std::max(max_x, glyph_right);
                pen += e.x_advance;
            }
        }
        if (!std::isfinite(min_x)) {
            min_x = 0.0f;
            max_x = 0.0f;
        }
        line_mins[li]   = min_x;
        line_maxs[li]   = max_x;
        float lw        = max_x - min_x;
        line_widths[li] = lw;
        if (lw > full_width) full_width = lw;
    }
    // Use the requested line_height directly (do not enforce ascender+descender minimum).
    float line_h_i = line_height;
    // full block height (may allow overlap if line_h_i is small)
    float full_height = static_cast<float>(lines.size()) * line_h_i;
    // populate ShapedText bounds/internal metrics
    // compute overall left/right from per-line mins/maxs
    float overall_min = std::numeric_limits<float>::infinity();
    float overall_max = -std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < line_mins.size(); ++i) {
        overall_min = std::min(overall_min, line_mins[i]);
        overall_max = std::max(overall_max, line_maxs[i]);
    }
    if (!std::isfinite(overall_min)) {
        overall_min = 0.0f;
        overall_max = 0.0f;
    }
    out.left_   = overall_min;
    out.right_  = overall_max;
    out.ascent_ = global_ascent;
    // store descent_ as negative to match ShapedText semantics (descent_ < 0)
    out.descent_ = -global_descent;
    // With bottom-left origin, bottom is 0 and top is full block height
    out.top_    = full_height;
    out.bottom_ = 0.0f;

    // Enforce height bound: compute max lines allowed and clip visible lines only (size remains the full block size)
    if (bounds.height.has_value()) {
        int max_lines = static_cast<int>(std::floor((*bounds.height) / line_height));
        if (max_lines < static_cast<int>(lines.size())) lines.resize(std::max(0, max_lines));
    }

    // Build final GlyphInfo list applying justification per line
    // first baseline measured from bottom: baseline is `full_height - global_ascent`
    float first_baseline_from_bottom = full_height - global_ascent;
    for (size_t li = 0; li < lines.size(); ++li) {
        auto [start, end] = lines[li];
        // compute line width and count spaces for justification
        int spaces       = 0;
        float line_width = 0.0f;
        for (size_t ti = start; ti < end; ++ti) {
            line_width += shaped_tokens[ti].width;
            // count ASCII spaces in the original token text
            const std::string& tt = shaped_tokens[ti].text;
            for (char ch : tt)
                if (ch == ' ') ++spaces;
        }

        // Compute start_x relative to the block's top-left (use full block width for justification)
        float start_x = 0.0f;
        if (layout.justify == Justify::Center)
            start_x = (full_width - line_width) / 2.0f;
        else if (layout.justify == Justify::Right)
            start_x = (full_width - line_width);

        float extra_per_space = 0.0f;
        if (layout.justify == Justify::Justified && li + 1 < lines.size() && spaces > 0 && full_width > line_width) {
            extra_per_space = (full_width - line_width) / static_cast<float>(spaces);
        }

        float pen_x = start_x;
        for (size_t ti = start; ti < end; ++ti) {
            auto& ts = shaped_tokens[ti];
            for (size_t gi = 0; gi < ts.entries.size(); ++gi) {
                GlyphInfo out_g;
                out_g.glyph_index = ts.entries[gi].glyph_index;
                out_g.cluster     = ts.entries[gi].cluster;
                // position relative to block top-left: account for per-line min_x and start_x
                float glyph_origin_x =
                    pen_x + ts.entries[gi].x_offset;  // includes start_x since pen_x starts at start_x
                // keep glyph x_offset as the pen-origin + glyph x_offset (do not normalize by line min)
                out_g.x_offset = glyph_origin_x;
                // per-entry y_offset is already up-positive (we negated at shape time). Compute glyph y
                // as baseline-from-bottom + per-entry offset.
                float baseline_y = first_baseline_from_bottom - static_cast<float>(li) * line_h_i;
                out_g.y_offset   = baseline_y + ts.entries[gi].y_offset;
                out_g.x_advance  = ts.entries[gi].x_advance;
                out_g.y_advance  = 0;

                atlas.get_glyph_atlas_loc(out_g.glyph_index);
                out.glyphs_.push_back(out_g);
                pen_x += ts.entries[gi].x_advance;
            }
            // Add extra spacing for ASCII spaces contained in this token
            if (extra_per_space != 0) {
                int trailing_spaces = 0;
                for (char ch : ts.text)
                    if (ch == ' ') ++trailing_spaces;
                pen_x += static_cast<float>(trailing_spaces) * extra_per_space;
            }
        }
        // next line handled by computing baseline_y from line index
    }

    hb_font_destroy(hbfont);
    return out;
}
}  // namespace epix::text