#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <epix/image/detail/rectangle_pack.hpp>
#include <epix/image/texture_atlas.hpp>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace epix::image {

TextureAtlasLayout TextureAtlasLayout::from_grid(glm::uvec2 tile_size,
                                                 std::uint32_t columns,
                                                 std::uint32_t rows,
                                                 std::optional<glm::uvec2> padding,
                                                 std::optional<glm::uvec2> offset) {
    const auto cell_padding = padding.value_or(glm::uvec2{0u});
    const auto grid_offset  = offset.value_or(glm::uvec2{0u});
    auto layout             = TextureAtlasLayout::new_empty(glm::uvec2{0u});
    layout.textures.reserve(static_cast<std::size_t>(columns) * rows);

    glm::uvec2 current_padding{0u};
    for (std::uint32_t y = 0; y < rows; ++y) {
        if (y > 0) current_padding.y = cell_padding.y;
        for (std::uint32_t x = 0; x < columns; ++x) {
            if (x > 0) current_padding.x = cell_padding.x;
            const auto cell     = glm::uvec2{x, y};
            const auto rect_min = (tile_size + current_padding) * cell + grid_offset;
            layout.textures.push_back(URect{rect_min, rect_min + tile_size});
        }
    }

    const auto grid_size = glm::uvec2{columns, rows};
    layout.size          = (tile_size + current_padding) * grid_size - current_padding;
    return layout;
}

void TextureAtlasPlugin::attach(epix::app::App& app) {
    spdlog::debug("[image] Attaching TextureAtlasPlugin.");
    assets::app_register_asset<TextureAtlasLayout>(app);
}

std::string DynamicTextureAtlasBuilderError::to_string() const {
    return std::visit(
        [](const auto& value) -> std::string {
            using Alternative = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Alternative, dynamic_texture_atlas_builder_error::FailedToAllocateSpace>) {
                return std::string("Couldn't allocate space to add the image requested");
            } else if constexpr (std::same_as<Alternative, dynamic_texture_atlas_builder_error::UninitializedAtlas>) {
                return std::string("cannot add texture to uninitialized atlas texture");
            } else if constexpr (std::same_as<Alternative,
                                              dynamic_texture_atlas_builder_error::UninitializedSourceTexture>) {
                return std::string("cannot add uninitialized texture to atlas");
            } else {
                return "texture access error: " + epix::image::to_string(value);
            }
        },
        static_cast<const Base&>(*this));
}

DynamicTextureAtlasBuilder::DynamicTextureAtlasBuilder(glm::uvec2 size, std::uint32_t padding)
    : atlas_allocator(detail::AtlasSize{static_cast<std::int32_t>(size.x), static_cast<std::int32_t>(size.y)}),
      padding(padding) {}

std::expected<std::size_t, DynamicTextureAtlasBuilderError> DynamicTextureAtlasBuilder::add_texture(
    TextureAtlasLayout& atlas_layout, const Image& texture, Image& atlas_texture) {
    const auto allocation = atlas_allocator.allocate(detail::AtlasSize{
        static_cast<std::int32_t>(texture.width() + padding), static_cast<std::int32_t>(texture.height() + padding)});
    if (!allocation)
        return std::unexpected(
            DynamicTextureAtlasBuilderError{dynamic_texture_atlas_builder_error::FailedToAllocateSpace{}});

    if ((atlas_texture.usage() & assets::MAIN_WORLD) == 0) {
        throw std::invalid_argument(
            "The atlas_texture image must have the RenderAssetUsages::MAIN_WORLD usage flag set");
    }

    auto placed = place_texture(atlas_texture, *allocation, texture);
    if (!placed) return std::unexpected(placed.error());

    URect rect{
        glm::uvec2{static_cast<std::uint32_t>(allocation->rectangle.min.x),
                   static_cast<std::uint32_t>(allocation->rectangle.min.y)},
        glm::uvec2{static_cast<std::uint32_t>(allocation->rectangle.max.x),
                   static_cast<std::uint32_t>(allocation->rectangle.max.y)},
    };
    // `saturating_sub` on both edges.
    const auto shrink = glm::uvec2{padding};
    rect.max.x        = rect.max.x > shrink.x ? rect.max.x - shrink.x : 0u;
    rect.max.y        = rect.max.y > shrink.y ? rect.max.y - shrink.y : 0u;
    return atlas_layout.add_texture(rect);
}

std::expected<void, DynamicTextureAtlasBuilderError> DynamicTextureAtlasBuilder::place_texture(
    Image& atlas_texture, const detail::AtlasAllocation& allocation, const Image& texture) {
    detail::AtlasRectangle rect = allocation.rectangle;
    rect.max.x -= static_cast<std::int32_t>(padding);
    rect.max.y -= static_cast<std::int32_t>(padding);
    const auto atlas_width = static_cast<std::size_t>(atlas_texture.width());
    const auto rect_width  = static_cast<std::size_t>(rect.width());
    const auto format_size = texture_format_pixel_size(atlas_texture.texture_format());
    if (!format_size) {
        return std::unexpected(DynamicTextureAtlasBuilderError{format_size.error()});
    }

    if (!atlas_texture.has_data()) {
        return std::unexpected(
            DynamicTextureAtlasBuilderError{dynamic_texture_atlas_builder_error::UninitializedAtlas{}});
    }
    if (!texture.has_data()) {
        return std::unexpected(
            DynamicTextureAtlasBuilderError{dynamic_texture_atlas_builder_error::UninitializedSourceTexture{}});
    }

    auto atlas_data   = atlas_texture.raw_view_mut();
    const auto source = texture.raw_view();
    for (std::int32_t texture_y = rect.min.y, row = 0; texture_y < rect.max.y; ++texture_y, ++row) {
        const auto bound_y              = static_cast<std::size_t>(texture_y);
        const std::size_t begin         = (bound_y * atlas_width + static_cast<std::size_t>(rect.min.x)) * *format_size;
        const std::size_t end           = begin + rect_width * *format_size;
        const std::size_t texture_begin = static_cast<std::size_t>(row) * rect_width * *format_size;
        const std::size_t texture_end   = texture_begin + rect_width * *format_size;
        std::memcpy(atlas_data.data() + begin, source.data() + texture_begin, end - begin);
    }
    return {};
}

std::string TextureAtlasBuilderError::to_string() const {
    return std::visit(
        [](const auto& value) -> std::string {
            using Alternative = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Alternative, texture_atlas_builder_error::NotEnoughSpace>) {
                return std::string("could not pack textures into an atlas within the given bounds");
            } else if constexpr (std::same_as<Alternative, texture_atlas_builder_error::WrongFormat>) {
                return std::string("added a texture with the wrong format in an atlas");
            } else if constexpr (std::same_as<Alternative, texture_atlas_builder_error::UninitializedAtlas>) {
                return std::string("cannot add texture to uninitialized atlas texture");
            } else if constexpr (std::same_as<Alternative, texture_atlas_builder_error::UninitializedSourceTexture>) {
                return std::string("cannot add uninitialized texture to atlas");
            } else {
                return "texture access error: " + epix::image::to_string(value);
            }
        },
        static_cast<const Base&>(*this));
}

TextureAtlasBuilderResult<void> TextureAtlasBuilder::copy_texture_to_atlas(
    Image& atlas_texture, const Image& texture, const detail::PackedLocation& packed_location, glm::uvec2 padding) {
    const std::size_t rect_width  = packed_location.width() - padding.x;
    const std::size_t rect_height = packed_location.height() - padding.y;
    const std::size_t rect_x      = packed_location.x();
    const std::size_t rect_y      = packed_location.y();
    const std::size_t atlas_width = atlas_texture.width();
    const auto format_size        = texture_format_pixel_size(atlas_texture.texture_format());
    if (!format_size) {
        return std::unexpected(TextureAtlasBuilderError{format_size.error()});
    }

    if (!atlas_texture.has_data()) {
        return std::unexpected(TextureAtlasBuilderError{texture_atlas_builder_error::UninitializedAtlas{}});
    }
    if (!texture.has_data()) {
        return std::unexpected(TextureAtlasBuilderError{texture_atlas_builder_error::UninitializedSourceTexture{}});
    }

    auto atlas_data   = atlas_texture.raw_view_mut();
    const auto source = texture.raw_view();
    for (std::size_t texture_y = 0, bound_y = rect_y; bound_y < rect_y + rect_height; ++texture_y, ++bound_y) {
        const std::size_t begin         = (bound_y * atlas_width + rect_x) * *format_size;
        const std::size_t end           = begin + rect_width * *format_size;
        const std::size_t texture_begin = texture_y * rect_width * *format_size;
        const std::size_t texture_end   = texture_begin + rect_width * *format_size;
        std::memcpy(atlas_data.data() + begin, source.data() + texture_begin, end - begin);
    }

    return {};
}

TextureAtlasBuilderResult<void> TextureAtlasBuilder::copy_converted_texture(
    Image& atlas_texture, const Image& texture, const detail::PackedLocation& packed_location) const {
    if (format_ == texture.texture_format()) {
        return copy_texture_to_atlas(atlas_texture, texture, packed_location, padding_);
    }

    if (auto converted_texture = texture.convert(format_)) {
        spdlog::debug("Converting texture from '{}' to '{}'", wgpu::to_string(texture.texture_format()),
                      wgpu::to_string(format_));
        return copy_texture_to_atlas(atlas_texture, *converted_texture, packed_location, padding_);
    }

    spdlog::error("Error converting texture from '{}' to '{}', ignoring", wgpu::to_string(texture.texture_format()),
                  wgpu::to_string(format_));
    return {};
}

TextureAtlasBuilderResult<std::tuple<TextureAtlasLayout, TextureAtlasSources, Image>> TextureAtlasBuilder::build() {
    const std::uint32_t max_width  = max_size_.x;
    const std::uint32_t max_height = max_size_.y;

    std::uint32_t current_width  = initial_size_.x;
    std::uint32_t current_height = initial_size_.y;
    std::optional<detail::RectanglePackOk<std::size_t, std::size_t>> rect_placements;
    Image atlas_texture;
    detail::GroupedRectsToPlace<std::size_t> rects_to_place;

    // Adds textures to rectangle group packer
    for (std::size_t index = 0; index < textures_to_place_.size(); ++index) {
        const Image& texture = *textures_to_place_[index].second;
        rects_to_place.push_rect(index, std::nullopt,
                                 detail::RectToInsert{texture.width() + padding_.x, texture.height() + padding_.y, 1});
    }

    while (!rect_placements) {
        if (current_width > max_width || current_height > max_height) {
            break;
        }

        const bool last_attempt = current_height == max_height && current_width == max_width;

        std::map<std::size_t, detail::TargetBin> target_bins;
        target_bins.emplace(0, detail::TargetBin{current_width, current_height, 1});
        auto packed =
            detail::pack_rects(rects_to_place, target_bins, detail::volume_heuristic, detail::contains_smallest_box);
        if (packed) {
            const auto pixel_size = texture_format_pixel_size(format_);
            if (!pixel_size) {
                return std::unexpected(TextureAtlasBuilderError{pixel_size.error()});
            }
            atlas_texture =
                Image{wgpu::Extent3D{current_width, current_height, 1}, wgpu::TextureDimension::e2D,
                      std::vector<std::byte>(*pixel_size * current_width * current_height, std::byte{0}), format_,
                      static_cast<assets::RenderAssetUsages>(assets::MAIN_WORLD | assets::RENDER_WORLD)};
            rect_placements = *packed;
        } else {
            // `RectanglePackError::NotEnoughBinSpace`
            current_height = std::clamp(current_height * 2, 0u, max_height);
            current_width  = std::clamp(current_width * 2, 0u, max_width);
        }

        if (last_attempt) {
            break;
        }
    }

    if (!rect_placements) {
        return std::unexpected(TextureAtlasBuilderError{texture_atlas_builder_error::NotEnoughSpace{}});
    }

    std::vector<URect> texture_rects;
    texture_rects.reserve(rect_placements->packed_locations().size());
    TextureAtlasSources sources;
    // We iterate through the textures to place to respect the insertion order for the texture
    // indices
    for (std::size_t index = 0; index < textures_to_place_.size(); ++index) {
        const Image& texture        = *textures_to_place_[index].second;
        const auto& packed_location = rect_placements->packed_locations().at(index).second;

        const glm::uvec2 min = glm::uvec2{packed_location.x(), packed_location.y()};
        const glm::uvec2 max = min + glm::uvec2{packed_location.width(), packed_location.height()} - padding_;
        if (textures_to_place_[index].first) {
            sources.texture_ids.insert_or_assign(*textures_to_place_[index].first, index);
        }
        texture_rects.push_back(URect{min, max});
        if (texture.texture_format() != format_ && !auto_format_conversion_) {
            spdlog::warn("Loading a texture of format '{}' in an atlas with format '{}'",
                         wgpu::to_string(texture.texture_format()), wgpu::to_string(format_));
            return std::unexpected(TextureAtlasBuilderError{texture_atlas_builder_error::WrongFormat{}});
        }
        auto copied = copy_converted_texture(atlas_texture, texture, packed_location);
        if (!copied) {
            return std::unexpected(copied.error());
        }
    }

    return std::make_tuple(TextureAtlasLayout{atlas_texture.size(), std::move(texture_rects)}, std::move(sources),
                           std::move(atlas_texture));
}

}  // namespace epix::image
