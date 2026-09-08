#include <epix/image/texture_atlas.hpp>

#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstdint>
#include <optional>

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

}  // namespace epix::image
