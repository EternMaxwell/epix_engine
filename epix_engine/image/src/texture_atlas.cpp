#include <epix/image/texture_atlas.hpp>

#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

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
    const auto atlas_width        = static_cast<std::size_t>(atlas_texture.width());
    const auto rect_width         = static_cast<std::size_t>(rect.width());
    const std::size_t format_size = atlas_texture.format_info().pixelSize();
    if (format_size == 0) {
        return std::unexpected(DynamicTextureAtlasBuilderError{
            TextureAccessError{TextureAccessError::UnsupportedTextureFormat{atlas_texture.texture_descriptor.format}}});
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
        const std::size_t begin         = (bound_y * atlas_width + static_cast<std::size_t>(rect.min.x)) * format_size;
        const std::size_t end           = begin + rect_width * format_size;
        const std::size_t texture_begin = static_cast<std::size_t>(row) * rect_width * format_size;
        const std::size_t texture_end   = texture_begin + rect_width * format_size;
        std::memcpy(atlas_data.data() + begin, source.data() + texture_begin, end - begin);
    }
    return {};
}

}  // namespace epix::image
