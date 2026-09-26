#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <expected>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#endif

#include <epix/image/detail/atlas_allocator.hpp>
#include <epix/image/image.hpp>

namespace epix::image {

namespace detail {
/** @brief `rectangle-pack`'s `PackedLocation`, defined in `detail/rectangle_pack.hpp`.
 *
 * Forward declared because only the builder's private copy helpers name it. */
struct PackedLocation;
}  // namespace detail

/** @brief Asset describing the pixel rectangles in a texture atlas. */
EPIX_EXPORT struct EPIX_API TextureAtlasLayout {
    glm::uvec2 size{0u};
    std::vector<URect> textures;

    static TextureAtlasLayout new_empty(glm::uvec2 dimensions) { return TextureAtlasLayout{.size = dimensions}; }
    static TextureAtlasLayout from_grid(glm::uvec2 tile_size,
                                        std::uint32_t columns,
                                        std::uint32_t rows,
                                        std::optional<glm::uvec2> padding = std::nullopt,
                                        std::optional<glm::uvec2> offset  = std::nullopt);
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
EPIX_EXPORT struct EPIX_API TextureAtlasPlugin {
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
EPIX_EXPORT struct EPIX_API DynamicTextureAtlasBuilderError
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
EPIX_EXPORT class EPIX_API DynamicTextureAtlasBuilder {
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

/** @brief C++ counterparts to `TextureAtlasBuilderError`'s alternatives
 * (`texture_atlas_builder.rs:16-30`). */
EPIX_EXPORT namespace texture_atlas_builder_error {
    /** @brief Could not pack the textures into an atlas within the given bounds. */
    struct NotEnoughSpace {
        bool operator==(const NotEnoughSpace&) const = default;
    };
    /** @brief A texture with the wrong format was added to an atlas. */
    struct WrongFormat {
        bool operator==(const WrongFormat&) const = default;
    };
    /** @brief Attempted to add a texture to an uninitialized atlas. */
    struct UninitializedAtlas {
        bool operator==(const UninitializedAtlas&) const = default;
    };
    /** @brief Attempted to add an uninitialized texture to an atlas. */
    struct UninitializedSourceTexture {
        bool operator==(const UninitializedSourceTexture&) const = default;
    };
}  // namespace texture_atlas_builder_error

/** @brief Error produced by `TextureAtlasBuilder` (Bevy `TextureAtlasBuilderError`). */
EPIX_EXPORT struct EPIX_API TextureAtlasBuilderError
    : std::variant<texture_atlas_builder_error::NotEnoughSpace,
                   texture_atlas_builder_error::WrongFormat,
                   texture_atlas_builder_error::UninitializedAtlas,
                   texture_atlas_builder_error::UninitializedSourceTexture,
                   TextureAccessError> {
    using Base = std::variant<texture_atlas_builder_error::NotEnoughSpace,
                              texture_atlas_builder_error::WrongFormat,
                              texture_atlas_builder_error::UninitializedAtlas,
                              texture_atlas_builder_error::UninitializedSourceTexture,
                              TextureAccessError>;
    using Base::Base;

    /** @brief Bevy `TextureAtlasBuilderError`'s `Display` text. */
    std::string to_string() const;
};

/** @brief `TextureAtlasBuilder`'s result type (Bevy `TextureAtlasBuilderResult`). */
template <typename T>
using TextureAtlasBuilderResult = std::expected<T, TextureAtlasBuilderError>;

/** @brief A builder which is used to create a texture atlas from many individual sprites
 * (Bevy `TextureAtlasBuilder`).
 *
 * Like Bevy's `&'a Image`, the builder *borrows* the images it packs: the referenced images must
 * outlive the builder and its `build()` call. */
EPIX_EXPORT class EPIX_API [[nodiscard]] TextureAtlasBuilder {
   public:
    /** @brief Bevy's `Default`: a 256x256 initial atlas, at most 2048x2048,
     * `Rgba8UnormSrgb`, automatic format conversion, no padding. */
    TextureAtlasBuilder() = default;

    /** @brief Sets the initial size of the atlas in pixels. */
    TextureAtlasBuilder& initial_size(glm::uvec2 size) {
        initial_size_ = size;
        return *this;
    }

    /** @brief Sets the max size of the atlas in pixels. */
    TextureAtlasBuilder& max_size(glm::uvec2 size) {
        max_size_ = size;
        return *this;
    }

    /** @brief Sets the texture format for the textures that will be loaded in the atlas. */
    TextureAtlasBuilder& format(wgpu::TextureFormat format) {
        format_ = format;
        return *this;
    }

    /** @brief Control whether the added texture should be converted to the atlas format, if
     * different. */
    TextureAtlasBuilder& auto_format_conversion(bool auto_format_conversion) {
        auto_format_conversion_ = auto_format_conversion;
        return *this;
    }

    /** @brief Adds a texture to be copied to the texture atlas.
     *
     * Optionally an asset id can be passed that can later be used with the texture layout to
     * retrieve the index of this texture. The insertion order will reflect the index of the added
     * texture in the finished texture atlas. */
    TextureAtlasBuilder& add_texture(std::optional<assets::AssetId<Image>> image_id, const Image& texture) {
        textures_to_place_.emplace_back(std::move(image_id), &texture);
        return *this;
    }

    /** @brief Sets the amount of padding in pixels to add between the textures in the texture
     * atlas.
     *
     * The `x` value provided will be added to the right edge, while the `y` value will be added to
     * the bottom edge. */
    TextureAtlasBuilder& padding(glm::uvec2 padding) {
        padding_ = padding;
        return *this;
    }

    /** @brief Consumes the builder, and returns the newly created texture atlas and the associated
     * atlas layout.
     *
     * Assigns indices to the textures based on the insertion order. Internally it copies all
     * rectangles from the textures and copies them into a new texture.
     *
     * If there is not enough space in the atlas texture, an error will be returned. It is then
     * recommended to make a larger sprite sheet. */
    TextureAtlasBuilderResult<std::tuple<TextureAtlasLayout, TextureAtlasSources, Image>> build();

   private:
    /** @brief Copy `texture` into `atlas_texture` at `packed_location`, row by row, leaving
     * `padding` rows and columns of the packed rectangle untouched. */
    static TextureAtlasBuilderResult<void> copy_texture_to_atlas(Image& atlas_texture,
                                                                 const Image& texture,
                                                                 const detail::PackedLocation& packed_location,
                                                                 glm::uvec2 padding);

    /** @brief Copy `texture` into `atlas_texture`, converting it to the atlas format first when
     * the two differ. */
    TextureAtlasBuilderResult<void> copy_converted_texture(Image& atlas_texture,
                                                           const Image& texture,
                                                           const detail::PackedLocation& packed_location) const;

    /** @brief Collection of texture's asset id (optional) and image data to be packed into an
     * atlas. */
    std::vector<std::pair<std::optional<assets::AssetId<Image>>, const Image*>> textures_to_place_;
    /** @brief The initial atlas size in pixels. */
    glm::uvec2 initial_size_{256u};
    /** @brief The absolute maximum size of the texture atlas in pixels. */
    glm::uvec2 max_size_{2048u};
    /** @brief The texture format for the textures that will be loaded in the atlas. */
    wgpu::TextureFormat format_ = wgpu::TextureFormat::eRGBA8UnormSrgb;
    /** @brief Enable automatic format conversion for textures if they are not in the atlas
     * format. */
    bool auto_format_conversion_ = true;
    /** @brief The amount of padding in pixels to add along the right and bottom edges of the
     * texture rects. */
    glm::uvec2 padding_{0u};
};

}  // namespace epix::image
