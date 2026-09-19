#include <freetype/freetype.h>
#include <hb-ft.h>
#include <hb.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <epix/image.hpp>
#include <epix/text.hpp>
#include <limits>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

using namespace epix;
using namespace epix::ecs;
using namespace epix::app;
using namespace epix::text;

void Text::register_required_components(ecs::RequiredComponentsRegistrator& registrator) {
    registrator.register_required<TextLayout>([] { return TextLayout{}; });
    registrator.register_required<TextBounds>([] { return TextBounds{}; });
}

namespace {
void shape_changed_text(Commands cmd,
                        Query<Item<Entity, Ref<Text>, Ref<TextFont>, Ref<TextLayout>, Ref<TextBounds>, Has<ShapedText>>>
                            texts,
                        ResMut<font::FontAtlasSets> atlas_sets,
                        ResMut<assets::Assets<image::Image>> textures,
                        ResMut<assets::Assets<image::TextureAtlasLayout>> atlas_layouts) {
    for (auto&& [entity, text_item, font_item, layout_item, bounds_item, has_shaped] : texts.iter()) {
        if (has_shaped && !text_item.is_modified() && !text_item.is_added() && !font_item.is_modified() &&
            !font_item.is_added() && !layout_item.is_modified() && !layout_item.is_added() &&
            !bounds_item.is_modified() && !bounds_item.is_added()) {
            continue;
        }
        spdlog::trace("[text] shape_changed_text: shaping text for entity {}.", entity.index);

        auto atlas_set = atlas_sets->get_mut(font_item.get().font.id());
        if (!atlas_set) {
            continue;
        }
        // Bevy lays text out and places the glyphs in the atlas in the same
        // step (`TextPipeline::update_text_layout_info`), so both asset stores
        // are threaded through here.
        auto shaped =
            shape_text(text_item.get(), font_item.get(), layout_item.get(), bounds_item.get(), atlas_set->get(),
                       *textures, *atlas_layouts);
        auto measure = TextMeasure{.width = shaped.width(), .height = shaped.height()};
        cmd.entity(entity).insert(std::move(shaped), measure);
    }
}
}  // namespace

ShapedText epix::text::shape_text(const Text& text,
                                  const TextFont& font,
                                  const TextLayout& layout,
                                  const TextBounds& bounds,
                                  font::FontAtlasSet& atlas_set,
                                  assets::Assets<image::Image>& textures,
                                  assets::Assets<image::TextureAtlasLayout>& atlas_layouts) {
    ShapedText out;

    auto face_ptr = atlas_set.get_font_face();
    if (!face_ptr) {
        return out;
    }

    const auto atlas_key = static_cast<font::FontAtlasKey>(font);

    /** @brief Rasterize and place a glyph, returning its atlas placement and
     * bitmap size (Bevy `pipeline.rs:427-446`). */
    auto place_glyph = [&](std::uint32_t glyph_index)
        -> std::optional<std::tuple<font::GlyphAtlasInfo, glm::vec2, glm::vec2>> {
        auto atlas_info = atlas_set.add_glyph_to_atlas(textures, atlas_layouts, atlas_key, glyph_index);
        if (!atlas_info) {
            spdlog::error("[text] Failed to place glyph {}: {}.", glyph_index, atlas_info.error().to_string());
            return std::nullopt;
        }
        auto layout_asset = atlas_layouts.get(atlas_info->texture_atlas);
        if (!layout_asset || atlas_info->location.glyph_index >= layout_asset->get().textures.size()) {
            spdlog::error("[text] Atlas layout {} has no rectangle {}.", atlas_info->texture_atlas.to_string_short(),
                          atlas_info->location.glyph_index);
            return std::nullopt;
        }
        return std::make_tuple(*atlas_info,
                               glm::vec2{layout_asset->get().textures[atlas_info->location.glyph_index].size()},
                               glm::vec2{atlas_info->location.offset});
    };

    /** @brief Build one laid-out glyph.
     *
     * `x_offset`/`y_offset` stay the layout's absolute +Y-up pen position, as
     * they always were; `position` is Bevy's glyph quad center in the block's
     * +Y-down space (`pipeline.rs:441-453`):
     * `x = glyph_size.x / 2 + location.offset.x + physical_glyph.x` and
     * `y = line_y.round() + physical_glyph.y - location.offset.y + glyph_size.y / 2`. */
    auto make_glyph_info = [](std::uint32_t glyph_index, std::uint32_t cluster, float x_offset, float y_offset,
                              float pen_x, float baseline_down, float y_offset_down, float x_advance, float y_advance,
                              const font::GlyphAtlasInfo& atlas_info, glm::vec2 glyph_size,
                              glm::vec2 placement) -> GlyphInfo {
        return GlyphInfo{
            .glyph_index = glyph_index,
            .cluster     = cluster,
            .x_offset    = x_offset,
            .y_offset    = y_offset,
            .x_advance   = x_advance,
            .y_advance   = y_advance,
            .position    = glm::vec2{std::round(pen_x) + placement.x + glyph_size.x * 0.5f,
                                     std::round(baseline_down) + y_offset_down - placement.y + glyph_size.y * 0.5f},
            .size        = glyph_size,
            .atlas_info  = atlas_info,
        };
    };

    auto ft_face = reinterpret_cast<FT_Face>(face_ptr);
    FT_Set_Char_Size(ft_face, static_cast<FT_F26Dot6>(font.size * 64.0f), 0, 0, 0);
    auto* hb_font = hb_ft_font_create(ft_face, nullptr);
    if (!hb_font) {
        return out;
    }

    float ft_line_height      = static_cast<float>(static_cast<double>(ft_face->size->metrics.height) / 64.0);
    float ft_ascender         = static_cast<float>(static_cast<double>(ft_face->size->metrics.ascender) / 64.0);
    float ft_descender        = -static_cast<float>(static_cast<double>(ft_face->size->metrics.descender) / 64.0);
    float desired_line_height = font.relative_height ? ft_line_height * font.line_height : font.line_height;
    float final_line_height   = std::max(ft_line_height, desired_line_height);

    if (layout.wrap_mode == TextWrap::NoWrap) {
        auto* buffer = hb_buffer_create();
        hb_buffer_add_utf8(buffer, text.content.c_str(), static_cast<int>(text.content.size()), 0,
                           static_cast<int>(text.content.size()));
        hb_buffer_guess_segment_properties(buffer);
        hb_shape(hb_font, buffer, nullptr, 0);

        unsigned int glyph_count = 0;
        auto* infos              = hb_buffer_get_glyph_infos(buffer, &glyph_count);
        auto* positions          = hb_buffer_get_glyph_positions(buffer, &glyph_count);
        std::vector<float> pen_positions;
        pen_positions.reserve(glyph_count);
        out.glyphs_.reserve(glyph_count);

        // Bevy's `run.line_y` for a single-line block: the first baseline sits
        // one ascent below the block's top edge, and the block's +Y axis points
        // down (`pipeline.rs:450-451`).
        const float baseline_down = ft_ascender;
        float pen_x               = 0.0f;
        float advance_width       = 0.0f;
        for (unsigned int index = 0; index < glyph_count; ++index) {
            const auto glyph_index = static_cast<std::uint32_t>(infos[index].codepoint);
            const auto cluster     = static_cast<std::uint32_t>(infos[index].cluster);
            const float x_offset   = static_cast<float>(static_cast<double>(positions[index].x_offset) / 64.0);
            // HarfBuzz offsets/advances are +Y-up; the layout space is +Y-down.
            const float y_offset_down = -static_cast<float>(static_cast<double>(positions[index].y_offset) / 64.0);
            const float x_advance     = static_cast<float>(static_cast<double>(positions[index].x_advance) / 64.0);
            const float y_advance_down = -static_cast<float>(static_cast<double>(positions[index].y_advance) / 64.0);
            pen_positions.push_back(pen_x);

            if (auto placed = place_glyph(glyph_index)) {
                auto&& [atlas_info, glyph_size, placement] = *placed;
                out.glyphs_.push_back(make_glyph_info(glyph_index, cluster, pen_x + x_offset,
                                                      baseline_down + y_offset_down, pen_x, baseline_down,
                                                      y_offset_down, x_advance, y_advance_down, atlas_info, glyph_size,
                                                      placement));
            }
            pen_x += x_advance;
            advance_width += x_advance;
        }

        float min_x = std::numeric_limits<float>::infinity();
        float max_x = -std::numeric_limits<float>::infinity();
        for (std::size_t index = 0; index < out.glyphs_.size(); ++index) {
            const auto& glyph = out.glyphs_[index];
            auto metrics      = atlas_set.get_glyph(atlas_key, glyph.glyph_index);
            auto pen          = pen_positions[index];
            min_x             = std::min(min_x, pen + (glyph.x_offset + std::min(0.0f, metrics.horiBearingX)));
            max_x             = std::max(max_x, pen + (glyph.x_offset + metrics.horiBearingX + metrics.width));
        }
        if (out.glyphs_.empty()) {
            min_x = 0.0f;
            max_x = 0.0f;
        }

        out.left_    = min_x;
        out.right_   = max_x;
        out.ascent_  = ft_ascender;
        out.descent_ = -ft_descender;
        out.top_     = 0.0f;
        out.bottom_  = -final_line_height;
        // Bevy `TextLayoutInfo::size` is the block's advance box, rounded up
        // (`box_size.x = run.line_w`, `box_size.y += run.line_height`,
        // `layout_info.size = box_size.ceil()`, `pipeline.rs:356-358,484`).
        out.size_ = glm::vec2{std::ceil(advance_width), std::ceil(final_line_height)};

        hb_buffer_destroy(buffer);
        hb_font_destroy(hb_font);
        return out;
    }

    struct Token {
        std::string text;
        bool force_break_after = false;
    };
    std::vector<Token> tokens;
    const auto& source = text.content;
    for (std::size_t index = 0; index < source.size();) {
        unsigned char c = static_cast<unsigned char>(source[index]);
        if (c <= 0x7F && (c == '\n' || c == '\r')) {
            if (!tokens.empty() && !tokens.back().force_break_after && !tokens.back().text.empty()) {
                tokens.back().force_break_after = true;
            } else {
                tokens.push_back(Token{.text = std::string(), .force_break_after = true});
            }
            if (c == '\r' && index + 1 < source.size() && source[index + 1] == '\n') {
                index += 2;
            } else {
                index += 1;
            }
            continue;
        }

        auto start = index;
        while (index < source.size()) {
            unsigned char cc = static_cast<unsigned char>(source[index]);
            if (cc <= 0x7F && (cc == ' ' || cc == '\t' || cc == '\n' || cc == '\r')) {
                break;
            }
            if ((cc & 0x80) == 0) {
                index += 1;
            } else if ((cc & 0xE0) == 0xC0) {
                index += 2;
            } else if ((cc & 0xF0) == 0xE0) {
                index += 3;
            } else {
                index += 4;
            }
        }
        std::string word = source.substr(start, index - start);
        auto tail        = index;
        while (tail < source.size() && (source[tail] == ' ' || source[tail] == '\t')) {
            ++tail;
        }
        if (tail > index) {
            word.append(source.substr(index, tail - index));
            index = tail;
        }
        tokens.push_back(Token{.text = std::move(word), .force_break_after = false});
    }

    struct GEntry {
        std::uint32_t glyph_index;
        std::uint32_t cluster;
        std::int32_t x_advance;
        std::int32_t x_offset;
        std::int32_t y_offset;
    };

    auto shape_token = [&](const std::string& token) {
        std::vector<GEntry> entries;
        int token_width = 0;
        if (token.empty()) {
            return std::pair<std::vector<GEntry>, int>(std::move(entries), token_width);
        }
        auto* buffer = hb_buffer_create();
        hb_buffer_add_utf8(buffer, token.c_str(), static_cast<int>(token.size()), 0, static_cast<int>(token.size()));
        hb_buffer_guess_segment_properties(buffer);
        hb_shape(hb_font, buffer, nullptr, 0);
        unsigned int count = 0;
        auto* infos        = hb_buffer_get_glyph_infos(buffer, &count);
        auto* positions    = hb_buffer_get_glyph_positions(buffer, &count);
        entries.reserve(count);
        for (unsigned int index = 0; index < count; ++index) {
            entries.push_back(GEntry{
                .glyph_index = static_cast<std::uint32_t>(infos[index].codepoint),
                .cluster     = static_cast<std::uint32_t>(infos[index].cluster),
                .x_advance =
                    static_cast<std::int32_t>(std::lround(static_cast<double>(positions[index].x_advance) / 64.0)),
                .x_offset =
                    static_cast<std::int32_t>(std::lround(static_cast<double>(positions[index].x_offset) / 64.0)),
                .y_offset =
                    static_cast<std::int32_t>(-std::lround(static_cast<double>(positions[index].y_offset) / 64.0)),
            });
            token_width += entries.back().x_advance;
        }
        hb_buffer_destroy(buffer);
        return std::pair<std::vector<GEntry>, int>(std::move(entries), token_width);
    };

    struct TokenShape {
        std::vector<GEntry> entries;
        int width;
        bool force_break;
        std::string text;
    };
    std::vector<TokenShape> shaped_tokens;
    shaped_tokens.reserve(tokens.size());
    for (auto& token : tokens) {
        auto [entries, width] = shape_token(token.text);
        shaped_tokens.push_back(TokenShape{
            .entries = std::move(entries), .width = width, .force_break = token.force_break_after, .text = token.text});
    }

    float max_width_f = bounds.width.value_or(std::numeric_limits<float>::infinity());
    int max_width     = std::isinf(max_width_f) ? INT_MAX : static_cast<int>(std::floor(max_width_f));

    bool always_char_split   = layout.wrap_mode == TextWrap::CharWrap;
    bool fallback_char_split = layout.wrap_mode == TextWrap::WordOrCharWrap;

    // Split shaped_tokens[idx] so that at most `available` width of glyphs stays
    // in the original entry; excess glyphs move to a new entry at idx+1.
    // Always takes at least one glyph. Returns the width kept in the original.
    auto split_token = [&](std::size_t idx, int available) -> int {
        auto& tk         = shaped_tokens[idx];
        int acc          = 0;
        std::size_t take = 0;
        while (take < tk.entries.size() && acc + tk.entries[take].x_advance <= available) {
            acc += tk.entries[take].x_advance;
            ++take;
        }
        if (take == 0) {
            take = 1;
            acc  = tk.entries[0].x_advance;
        }
        if (take < tk.entries.size()) {
            TokenShape leftover{.width = 0, .force_break = false};
            leftover.entries.assign(tk.entries.begin() + take, tk.entries.end());
            for (const auto& e : leftover.entries) leftover.width += e.x_advance;
            tk.entries.resize(take);
            tk.width = acc;
            shaped_tokens.insert(shaped_tokens.begin() + idx + 1, std::move(leftover));
        }
        return acc;
    };

    std::vector<std::pair<std::size_t, std::size_t>> lines;
    std::size_t token_index = 0;
    while (token_index < shaped_tokens.size()) {
        if (shaped_tokens[token_index].entries.empty() && shaped_tokens[token_index].force_break) {
            lines.emplace_back(token_index, token_index);
            ++token_index;
            continue;
        }

        int line_width     = 0;
        auto start         = token_index;
        bool first_on_line = true;
        while (token_index < shaped_tokens.size()) {
            auto& shaped = shaped_tokens[token_index];
            if (shaped.entries.empty() && shaped.force_break) {
                ++token_index;
                break;
            }

            int new_width = line_width + shaped.width;
            if (new_width <= max_width) {
                // Token fits on the current line.
                line_width    = new_width;
                first_on_line = false;
                ++token_index;
            } else if (first_on_line) {
                bool should_split = always_char_split || (fallback_char_split && shaped.width > max_width);
                if (should_split) {
                    // split_token invalidates `shaped`; use return value only.
                    line_width = split_token(token_index, max_width);
                    ++token_index;
                    break;
                }
                // Must accept at least one token per line even if it overflows.
                line_width    = new_width;
                first_on_line = false;
                ++token_index;
            } else {
                // Not first on line and doesn't fit.
                bool should_split = always_char_split || (fallback_char_split && shaped.width > max_width);
                if (should_split) {
                    int available = max_width - line_width;
                    if (available > 0) {
                        line_width += split_token(token_index, available);
                        ++token_index;
                    }
                }
                break;
            }

            if (shaped.force_break) {
                break;
            }
        }
        lines.emplace_back(start, token_index);
    }

    std::vector<float> line_mins(lines.size(), 0.0f);
    std::vector<float> line_maxs(lines.size(), 0.0f);
    std::vector<float> line_widths(lines.size(), 0.0f);
    float full_width = 0.0f;
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        auto [begin, end] = lines[line_index];
        float pen         = 0.0f;
        float min_x       = std::numeric_limits<float>::infinity();
        float max_x       = -std::numeric_limits<float>::infinity();
        for (std::size_t index = begin; index < end; ++index) {
            for (const auto& entry : shaped_tokens[index].entries) {
                auto metrics = atlas_set.get_glyph(atlas_key, entry.glyph_index);
                min_x        = std::min(min_x, pen + (entry.x_offset + std::min(0.0f, metrics.horiBearingX)));
                max_x        = std::max(max_x, pen + (entry.x_offset + metrics.horiBearingX + metrics.width));
                pen += entry.x_advance;
            }
        }
        if (!std::isfinite(min_x)) {
            min_x = 0.0f;
            max_x = 0.0f;
        }
        line_mins[line_index]   = min_x;
        line_maxs[line_index]   = max_x;
        line_widths[line_index] = max_x - min_x;
        full_width              = std::max(full_width, line_widths[line_index]);
    }

    out.left_    = line_mins.empty() ? 0.0f : *std::ranges::min_element(line_mins);
    out.right_   = line_maxs.empty() ? 0.0f : *std::ranges::max_element(line_maxs);
    out.ascent_  = ft_ascender;
    out.descent_ = -ft_descender;
    out.top_     = 0.0f;
    out.bottom_  = -static_cast<float>(lines.size()) * final_line_height;

    float box_width = 0.0f;
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        auto [begin, end] = lines[line_index];
        int spaces        = 0;
        float width       = 0.0f;
        for (std::size_t index = begin; index < end; ++index) {
            width += static_cast<float>(shaped_tokens[index].width);
            for (char ch : shaped_tokens[index].text) {
                if (ch == ' ') {
                    ++spaces;
                }
            }
        }
        box_width = std::max(box_width, width);

        float start_x = 0.0f;
        if (layout.justify == Justify::Center) {
            start_x = (full_width - width) / 2.0f;
        } else if (layout.justify == Justify::Right) {
            start_x = full_width - width;
        }

        float extra_per_space = 0.0f;
        if (layout.justify == Justify::Justified && line_index + 1 < lines.size() && spaces > 0 && full_width > width) {
            extra_per_space = (full_width - width) / static_cast<float>(spaces);
        }

        float pen_x = start_x;
        // The layout's +Y-up baselines (`baseline_y`) converted to Bevy's
        // +Y-down block space, where the first baseline is one ascent below the
        // block's top edge (`pipeline.rs:450-451`).
        float baseline_y          = -ft_ascender - static_cast<float>(line_index) * final_line_height;
        const float baseline_down = ft_ascender + static_cast<float>(line_index) * final_line_height;
        for (std::size_t index = begin; index < end; ++index) {
            for (const auto& entry : shaped_tokens[index].entries) {
                if (auto placed = place_glyph(entry.glyph_index)) {
                    auto&& [atlas_info, glyph_size, placement] = *placed;
                    out.glyphs_.push_back(make_glyph_info(
                        entry.glyph_index, entry.cluster, pen_x + static_cast<float>(entry.x_offset),
                        baseline_y + static_cast<float>(entry.y_offset), pen_x, baseline_down,
                        static_cast<float>(entry.y_offset), static_cast<float>(entry.x_advance), 0.0f, atlas_info,
                        glyph_size, placement));
                }
                pen_x += static_cast<float>(entry.x_advance);
            }
            if (extra_per_space != 0.0f) {
                int trailing_spaces = 0;
                for (char ch : shaped_tokens[index].text) {
                    if (ch == ' ') {
                        ++trailing_spaces;
                    }
                }
                pen_x += static_cast<float>(trailing_spaces) * extra_per_space;
            }
        }
    }

    // Bevy `pipeline.rs:356-358,484`.
    out.size_ = glm::vec2{std::ceil(box_width), std::ceil(static_cast<float>(lines.size()) * final_line_height)};

    hb_font_destroy(hb_font);
    return out;
}

void TextPlugin::attach(App& app) {
    app.add_plugins(font::FontPlugin{});
    app.add_systems(PostUpdate, into(shape_changed_text)
                                    .set_name("shape_changed_text")
                                    .after(font::FontSystems::AddFontAtlasSet));
}
