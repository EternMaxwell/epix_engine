#include <epix/sprite/texture_slice.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace epix::sprite {
namespace {
using Corners = std::array<TextureSlice, 4>;

Corners corner_slices(const TextureSlicer& slicer, image::Rect base_rect, glm::vec2 render_size) {
    const auto coefficient = render_size / base_rect.size();
    const auto left        = slicer.border.min_inset.x;
    const auto top         = slicer.border.min_inset.y;
    const auto right       = slicer.border.max_inset.x;
    const auto bottom      = slicer.border.max_inset.y;
    const auto scale       = std::min({coefficient.x, coefficient.y, slicer.max_corner_scale});

    return {
        TextureSlice{image::Rect{base_rect.min, base_rect.min + glm::vec2(left, top)},
                     glm::vec2(left, top) * scale,
                     glm::vec2(-render_size.x + left * scale, render_size.y - top * scale) * 0.5f},
        TextureSlice{image::Rect{glm::vec2(base_rect.max.x - right, base_rect.min.y),
                                 glm::vec2(base_rect.max.x, base_rect.min.y + top)},
                     glm::vec2(right, top) * scale,
                     glm::vec2(render_size.x - right * scale, render_size.y - top * scale) * 0.5f},
        TextureSlice{image::Rect{glm::vec2(base_rect.min.x, base_rect.max.y - bottom),
                                 glm::vec2(base_rect.min.x + left, base_rect.max.y)},
                     glm::vec2(left, bottom) * scale,
                     glm::vec2(-render_size.x + left * scale, -render_size.y + bottom * scale) * 0.5f},
        TextureSlice{image::Rect{glm::vec2(base_rect.max.x - right, base_rect.max.y - bottom), base_rect.max},
                     glm::vec2(right, bottom) * scale,
                     glm::vec2(render_size.x - right * scale, -render_size.y + bottom * scale) * 0.5f},
    };
}

std::array<TextureSlice, 2> horizontal_side_slices(const TextureSlicer& slicer,
                                                   const Corners& corners,
                                                   image::Rect base_rect,
                                                   glm::vec2 render_size) {
    const auto& [top_left, top_right, bottom_left, bottom_right] = corners;
    return {
        TextureSlice{image::Rect{base_rect.min + glm::vec2(0.0f, slicer.border.min_inset.y),
                                 glm::vec2(base_rect.min.x + slicer.border.min_inset.x,
                                           base_rect.max.y - slicer.border.max_inset.y)},
                     glm::vec2(top_left.draw_size.x,
                               render_size.y - (top_left.draw_size.y + bottom_left.draw_size.y)),
                     glm::vec2(top_left.draw_size.x - render_size.x,
                               bottom_left.draw_size.y - top_left.draw_size.y) *
                         0.5f},
        TextureSlice{image::Rect{glm::vec2(base_rect.max.x - slicer.border.max_inset.x,
                                           base_rect.min.y + slicer.border.min_inset.y),
                                 base_rect.max - glm::vec2(0.0f, slicer.border.max_inset.y)},
                     glm::vec2(top_right.draw_size.x,
                               render_size.y - (top_right.draw_size.y + bottom_right.draw_size.y)),
                     glm::vec2(render_size.x - top_right.draw_size.x,
                               bottom_right.draw_size.y - top_right.draw_size.y) *
                         0.5f},
    };
}

std::array<TextureSlice, 2> vertical_side_slices(const TextureSlicer& slicer,
                                                 const Corners& corners,
                                                 image::Rect base_rect,
                                                 glm::vec2 render_size) {
    const auto& [top_left, top_right, bottom_left, bottom_right] = corners;
    return {
        TextureSlice{image::Rect{base_rect.min + glm::vec2(slicer.border.min_inset.x, 0.0f),
                                 glm::vec2(base_rect.max.x - slicer.border.max_inset.x,
                                           base_rect.min.y + slicer.border.min_inset.y)},
                     glm::vec2(render_size.x - (top_left.draw_size.x + top_right.draw_size.x),
                               top_left.draw_size.y),
                     glm::vec2(top_left.draw_size.x - top_right.draw_size.x,
                               render_size.y - top_left.draw_size.y) *
                         0.5f},
        TextureSlice{image::Rect{glm::vec2(base_rect.min.x + slicer.border.min_inset.x,
                                           base_rect.max.y - slicer.border.max_inset.y),
                                 base_rect.max - glm::vec2(slicer.border.max_inset.x, 0.0f)},
                     glm::vec2(render_size.x - (bottom_left.draw_size.x + bottom_right.draw_size.x),
                               bottom_left.draw_size.y),
                     glm::vec2(bottom_left.draw_size.x - bottom_right.draw_size.x,
                               bottom_left.draw_size.y - render_size.y) *
                         0.5f},
    };
}
}  // namespace

std::vector<TextureSlice> TextureSlice::tiled(float stretch_value, std::pair<bool, bool> tile_axes) const {
    const auto [tile_x, tile_y] = tile_axes;
    if (!tile_x && !tile_y) return {*this};

    stretch_value          = std::max(stretch_value, 0.001f);
    const auto rect_size   = texture_rect.size();
    const auto expected    = glm::min(glm::vec2(tile_x ? std::max(rect_size.x * stretch_value, 1.0f) : draw_size.x,
                                                tile_y ? std::max(rect_size.y * stretch_value, 1.0f) : draw_size.y),
                                      draw_size);
    const auto base_offset = glm::vec2(-draw_size.x * 0.5f, draw_size.y * 0.5f);
    auto offset_value      = base_offset;
    std::vector<TextureSlice> slices;

    for (float remaining_y = draw_size.y; remaining_y > 0.0f;) {
        const auto size_y = std::min(expected.y, remaining_y);
        offset_value.x    = base_offset.x;
        offset_value.y -= size_y * 0.5f;
        for (float remaining_x = draw_size.x; remaining_x > 0.0f;) {
            const auto size_x = std::min(expected.x, remaining_x);
            offset_value.x += size_x * 0.5f;
            const auto tile_size = glm::vec2(size_x, size_y);
            const auto delta     = tile_size / expected;
            slices.push_back(TextureSlice{
                image::Rect{texture_rect.min, texture_rect.min + texture_rect.size() * delta},
                tile_size,
                offset + offset_value,
            });
            offset_value.x += size_x * 0.5f;
            remaining_x -= size_x;
        }
        offset_value.y -= size_y * 0.5f;
        remaining_y -= size_y;
    }

    if (slices.size() > 1000) {
        spdlog::warn("A tiled texture generated {} slices; increase its stretch value to reduce cost.", slices.size());
    }
    return slices;
}

std::vector<TextureSlice> TextureSlicer::compute_slices(image::Rect rect,
                                                        std::optional<glm::vec2> requested_size) const {
    const auto render_size = requested_size.value_or(rect.size());
    const auto insets      = border.min_inset + border.max_inset;
    const auto rect_size   = rect.size();
    if (insets.x >= rect_size.x || insets.y >= rect_size.y) {
        spdlog::error("TextureSlicer::border has out of bounds values. No slicing will be applied");
        return {TextureSlice{rect, render_size, glm::vec2(0.0f)}};
    }

    const auto corners         = corner_slices(*this, rect, render_size);
    const auto vertical_sides  = vertical_side_slices(*this, corners, rect, render_size);
    const auto horizontal_sides = horizontal_side_slices(*this, corners, rect, render_size);
    const auto center = TextureSlice{
        image::Rect{rect.min + border.min_inset, rect.max - border.max_inset},
        glm::vec2(render_size.x - (corners[0].draw_size.x + corners[1].draw_size.x),
                  render_size.y - (corners[0].draw_size.y + corners[2].draw_size.y)),
        glm::vec2(vertical_sides[0].offset.x, horizontal_sides[0].offset.y),
    };

    std::vector<TextureSlice> slices;
    slices.reserve(9);
    slices.insert(slices.end(), corners.begin(), corners.end());
    if (const auto* tile = std::get_if<SliceScaleMode::Tile>(&center_scale_mode)) {
        auto tiled = center.tiled(tile->stretch_value, {true, true});
        slices.insert(slices.end(), tiled.begin(), tiled.end());
    } else {
        slices.push_back(center);
    }

    if (const auto* tile = std::get_if<SliceScaleMode::Tile>(&sides_scale_mode)) {
        for (const auto& side : horizontal_sides) {
            auto tiled = side.tiled(tile->stretch_value, {false, true});
            slices.insert(slices.end(), tiled.begin(), tiled.end());
        }
        for (const auto& side : vertical_sides) {
            auto tiled = side.tiled(tile->stretch_value, {true, false});
            slices.insert(slices.end(), tiled.begin(), tiled.end());
        }
    } else {
        slices.insert(slices.end(), horizontal_sides.begin(), horizontal_sides.end());
        slices.insert(slices.end(), vertical_sides.begin(), vertical_sides.end());
    }
    return slices;
}

}  // namespace epix::sprite
