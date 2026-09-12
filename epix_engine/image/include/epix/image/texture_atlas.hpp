#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <expected>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#endif

#include <epix/image/detail/atlas_allocator.hpp>
#include <epix/image/image.hpp>

namespace epix::image {

/** @brief Asset describing the pixel rectangles in a texture atlas. */
EPIX_EXPORT struct TextureAtlasLayout {
    glm::uvec2 size{0u};
    std::vector<URect> textures;

    static TextureAtlasLayout new_empty(glm::uvec2 dimensions) { return TextureAtlasLayout{.size = dimensions}; }
    static TextureAtlasLayout from_grid(glm::uvec2 tile_size,
                                        std::uint32_t columns,
                                        std::uint32_t rows,
                                        std::optional<glm::uvec2> padding = std::nullopt,
                                        std::optional<glm::uvec2> offset = std::nullopt);
    std::size_t add_texture(URect rect) {
        textures.push_back(rect);
        return textures.size() - 1;
    }
    std::size_t len() const noexcept { return textures.size(); }
    bool is_empty() const noexcept { return textures.empty(); }
    bool operator==(const TextureAtlasLayout&) const = default;
};

/** @brief Atlas layout handle and selected region index. */
EPIX_EXPORT struct TextureAtlas {
    assets::Handle<TextureAtlasLayout> layout{assets::AssetId<TextureAtlasLayout>::invalid()};
    std::size_t index = 0;

    TextureAtlas() = default;
    TextureAtlas(assets::Handle<TextureAtlasLayout> layout, std::size_t index = 0)
        : layout(std::move(layout)), index(index) {}

    std::optional<URect> texture_rect(const assets::Assets<TextureAtlasLayout>& atlases) const {
        const auto atlas = atlases.get(layout.id());
        if (!atlas || index >= atlas->get().textures.size()) return std::nullopt;
        return atlas->get().textures[index];
    }
    TextureAtlas with_index(std::size_t value) const {
        auto result  = *this;
        result.index = value;
        return result;
    }
    TextureAtlas with_layout(assets::Handle<TextureAtlasLayout> value) const {
        auto result   = *this;
        result.layout = std::move(value);
        return result;
    }
    bool operator==(const TextureAtlas&) const = default;
};

/** @brief Source-image-to-atlas-index lookup emitted by atlas builders. */
EPIX_EXPORT struct TextureAtlasSources {
    std::unordered_map<assets::AssetId<Image>, std::size_t> texture_ids;

    std::optional<std::size_t> texture_index(assets::AssetId<Image> texture) const {
        const auto found = texture_ids.find(texture);
        if (found == texture_ids.end()) return std::nullopt;
        return found->second;
    }
    std::optional<TextureAtlas> handle(assets::Handle<TextureAtlasLayout> layout,
                                       assets::AssetId<Image> texture) const {
        const auto index = texture_index(texture);
        if (!index) return std::nullopt;
        return TextureAtlas{std::move(layout), *index};
    }
    std::optional<URect> texture_rect(const TextureAtlasLayout& layout, assets::AssetId<Image> texture) const {
        const auto index = texture_index(texture);
        if (!index || *index >= layout.textures.size()) return std::nullopt;
        return layout.textures[*index];
    }
    std::optional<Rect> uv_rect(const TextureAtlasLayout& layout, assets::AssetId<Image> texture) const {
        const auto rect = texture_rect(layout, texture);
        if (!rect || layout.size.x == 0 || layout.size.y == 0) return std::nullopt;
        const auto dimensions = glm::vec2(layout.size);
        return Rect{glm::vec2(rect->min) / dimensions, glm::vec2(rect->max) / dimensions};
    }
};

/** @brief Registers `TextureAtlasLayout` assets. */
EPIX_EXPORT struct TextureAtlasPlugin {
    void attach(epix::app::App& app);
};

/** @brief C++ counterparts to `DynamicTextureAtlasBuilderError`'s alternatives
 * (`dynamic_texture_atlas_builder.rs:10-23`). */
EPIX_EXPORT namespace dynamic_texture_atlas_builder_error {
    /** @brief Unable to allocate space within the atlas for the new texture. */
    struct FailedToAllocateSpace {
        bool operator==(const FailedToAllocateSpace&) const = default;
    };
    /** @brief Attempted to add a texture to an uninitialized atlas. */
    struct UninitializedAtlas {
        bool operator==(const UninitializedAtlas&) const = default;
    };
    /** @brief Attempted to add an uninitialized texture to an atlas. */
    struct UninitializedSourceTexture {
        bool operator==(const UninitializedSourceTexture&) const = default;
    };
}  // namespace dynamic_texture_atlas_builder_error

/** @brief Error produced by `DynamicTextureAtlasBuilder` (Bevy
 * `DynamicTextureAtlasBuilderError`). */
EPIX_EXPORT struct DynamicTextureAtlasBuilderError
    : std::variant<dynamic_texture_atlas_builder_error::FailedToAllocateSpace,
                   dynamic_texture_atlas_builder_error::UninitializedAtlas,
                   dynamic_texture_atlas_builder_error::UninitializedSourceTexture,
                   TextureAccessError> {
    using Base = std::variant<dynamic_texture_atlas_builder_error::FailedToAllocateSpace,
                              dynamic_texture_atlas_builder_error::UninitializedAtlas,
                              dynamic_texture_atlas_builder_error::UninitializedSourceTexture,
                              TextureAccessError>;
    using Base::Base;

    /** @brief Bevy `DynamicTextureAtlasBuilderError`'s `Display` text. */
    std::string to_string() const;
};

/** @brief Helper utility to update a `TextureAtlasLayout` on the fly (Bevy
 * `DynamicTextureAtlasBuilder`, `dynamic_texture_atlas_builder.rs:29-112`).
 *
 * Helpful when textures are created procedurally, e.g. a font glyph atlas.
 *
 * Like Bevy's, the allocator and the padding are private: the only surface is the constructor and
 * `add_texture`. */
EPIX_EXPORT class DynamicTextureAtlasBuilder {
   public:
    /** @brief Create a builder for an atlas of `size`, with `padding` pixels of gap between
     * textures on both axes. */
    DynamicTextureAtlasBuilder(glm::uvec2 size, std::uint32_t padding);

    /** @brief Add `texture` to `atlas_texture`, registering its rectangle in `atlas_layout`.
     *
     * The caller owns `atlas_layout`, and `atlas_texture` must carry the main-world usage flag.
     * @return The index of the added rectangle in `atlas_layout`. */
    std::expected<std::size_t, DynamicTextureAtlasBuilderError> add_texture(TextureAtlasLayout& atlas_layout,
                                                                            const Image& texture,
                                                                            Image& atlas_texture);

   private:
    std::expected<void, DynamicTextureAtlasBuilderError> place_texture(Image& atlas_texture,
                                                                       const detail::AtlasAllocation& allocation,
                                                                       const Image& texture);

    detail::AtlasAllocator atlas_allocator;
    std::uint32_t padding = 0;
};

}  // namespace epix::image
