#pragma once


#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <exception>
#include <expected>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <epix/common.hpp>
#endif

namespace epix::text::font {
/** @brief Font resource holding raw font file data (e.g. TTF/OTF) in memory. */
EPIX_EXPORT struct Font {
    /** @brief Raw font file bytes. */
    std::unique_ptr<std::byte[]> data;
    /** @brief Size of the font data in bytes. */
    std::size_t size = 0;
};
}  // namespace epix::text::font

template <>
struct std::hash<epix::assets::AssetId<epix::text::font::Font>> {
    std::size_t operator()(const epix::assets::AssetId<epix::text::font::Font>& id) const noexcept {
        return std::visit([]<typename T>(const T& index) { return std::hash<T>()(index); }, id);
    }
};

namespace epix::text::font {
struct EPIX_API FontLoader {
    using Asset = Font;
    struct Settings {};
    using Error = std::exception_ptr;

    static std::span<std::string_view> extensions() noexcept;
    static STDEXEC::task<std::expected<Font, Error>> load(assets::Reader& reader,
                                                          const Settings& settings,
                                                          assets::LoadContext& context);
};

/** @brief Owning handle to a FreeType library instance.
 *
 * Stored as an ECS resource, whose entity-component storage may relocate
 * values (move-construct into new storage, then destroy the source). The
 * handle is therefore move-only: copying the raw pointer would leave two
 * owners and the moved-from destructor would free the library out from
 * under the copy, leaving a dangling FT_Library.
 */
struct EPIX_API FontLibrary {
    void* library;

    FontLibrary();
    ~FontLibrary();

    FontLibrary(const FontLibrary&)            = delete;
    FontLibrary& operator=(const FontLibrary&) = delete;

    FontLibrary(FontLibrary&& other) noexcept;
    FontLibrary& operator=(FontLibrary&& other) noexcept;
};

/** @brief Metrics for a single rendered glyph from a font face. */
EPIX_EXPORT struct Glyph {
    /** @brief Glyph bitmap width in pixels. */
    float width;
    /** @brief Glyph bitmap height in pixels. */
    float height;

    /** @brief Horizontal bearing X (distance from origin to left edge). */
    float horiBearingX;
    /** @brief Horizontal bearing Y (distance from baseline to top edge). */
    float horiBearingY;
    /** @brief Horizontal advance width (distance to next glyph origin). */
    float horiAdvance;

    /** @brief Vertical bearing X. */
    float vertBearingX;
    /** @brief Vertical bearing Y. */
    float vertBearingY;
    /** @brief Vertical advance height. */
    float vertAdvance;
};

/** @brief Location of a rasterized glyph inside a font atlas image (Bevy
 * `GlyphAtlasLocation`, `glyph.rs:54-65`).
 *
 * `glyph_index` is the index into the atlas image's `TextureAtlasLayout`
 * rectangles, and `offset` is the bitmap's placement relative to the glyph's
 * pen position, in the atlas image's +Y-down space.
 */
EPIX_EXPORT struct GlyphAtlasLocation {
    /** @brief Index of the glyph in the atlas layout's texture list. */
    std::size_t glyph_index = 0;
    /** @brief Required placement offset of the glyph bitmap (Bevy `offset`). */
    glm::ivec2 offset{0};

    bool operator==(const GlyphAtlasLocation&) const = default;
};

/** @brief Atlas placement of a rasterized glyph (Bevy `GlyphAtlasInfo`,
 * `glyph.rs:32-52`). */
EPIX_EXPORT struct GlyphAtlasInfo {
    /** @brief The `Image` asset holding the atlas this glyph was placed in. */
    assets::AssetId<image::Image> texture{assets::AssetId<image::Image>::invalid()};
    /** @brief The `TextureAtlasLayout` asset describing that atlas. */
    assets::AssetId<image::TextureAtlasLayout> texture_atlas{
        assets::AssetId<image::TextureAtlasLayout>::invalid()};
    /** @brief Location and offset of the glyph within the atlas. */
    GlyphAtlasLocation location{};

    bool operator==(const GlyphAtlasInfo&) const = default;
};

/** @brief Failure modes of the glyph atlas path (the `TextError` alternatives
 * `bevy_text` reports while rasterizing and placing glyphs,
 * `font_atlas.rs:110` / `pipeline.rs:263-271`). */
EPIX_EXPORT enum class TextErrorCode {
    /** @brief The glyph did not fit in any atlas, or space allocation failed. */
    FailedToAddGlyph,
    /** @brief The glyph's bitmap could not be produced. */
    FailedToGetGlyphImage,
    /** @brief The atlas' `TextureAtlasLayout` asset is missing. */
    MissingAtlasLayout,
    /** @brief The atlas' `Image` asset is missing. */
    MissingAtlasTexture,
    /** @brief The glyph could not be found after it was added. */
    InconsistentAtlasState,
};

/** @brief Error produced while rasterizing or placing a glyph (Bevy
 * `TextError`). */
EPIX_EXPORT struct EPIX_API TextError {
    /** @brief Which failure occurred. */
    TextErrorCode code = TextErrorCode::FailedToAddGlyph;
    /** @brief The font glyph id the failure refers to, when applicable. */
    std::uint32_t glyph_id = 0;

    /** @brief Bevy `TextError`'s `Display` text. */
    std::string to_string() const;
    bool operator==(const TextError&) const = default;
};

/** @brief One texture atlas holding rasterized glyphs (Bevy `FontAtlas`,
 * `font_atlas.rs:21-30`).
 *
 * The atlas is a single `TextureDimension::D2` image whose glyph rectangles
 * are recorded in a `TextureAtlasLayout`; glyphs that do not fit any existing
 * atlas cause an additional `FontAtlas` to be created for the same
 * `FontAtlasKey` (`font_atlas.rs:149-178`).
 */
EPIX_EXPORT struct EPIX_API FontAtlas {
    /** @brief Updates the atlas' `TextureAtlasLayout` when glyphs are added. */
    image::DynamicTextureAtlasBuilder dynamic_texture_atlas_builder;
    /** @brief Glyph key to its location in this atlas. */
    std::unordered_map<std::uint32_t, GlyphAtlasLocation> glyph_to_atlas_index;
    /** @brief The layout that holds the rasterized glyph rectangles. */
    assets::Handle<image::TextureAtlasLayout> texture_atlas;
    /** @brief The texture where this font atlas is located. */
    assets::Handle<image::Image> texture;

    FontAtlas(image::DynamicTextureAtlasBuilder builder,
              assets::Handle<image::TextureAtlasLayout> texture_atlas,
              assets::Handle<image::Image> texture)
        : dynamic_texture_atlas_builder(std::move(builder)),
          texture_atlas(std::move(texture_atlas)),
          texture(std::move(texture)) {}

    /** @brief Create a new `FontAtlas` of `size`, adding it to the asset
     * collections (Bevy `FontAtlas::new`, `font_atlas.rs:34-59`). */
    static FontAtlas create(assets::Assets<image::Image>& textures,
                            assets::Assets<image::TextureAtlasLayout>& atlas_layouts,
                            glm::uvec2 size,
                            bool font_smoothing);

    /** @brief Get the location of a glyph in this atlas, if present. */
    std::optional<GlyphAtlasLocation> get_glyph_index(std::uint32_t key) const {
        if (auto it = glyph_to_atlas_index.find(key); it != glyph_to_atlas_index.end()) return it->second;
        return std::nullopt;
    }
    /** @brief Whether this atlas contains the given glyph. */
    bool has_glyph(std::uint32_t key) const { return glyph_to_atlas_index.contains(key); }

    /** @brief Add a glyph to the atlas, updating both its texture and layout
     * (Bevy `FontAtlas::add_glyph`, `font_atlas.rs:82-112`). */
    std::expected<void, TextError> add_glyph(assets::Assets<image::Image>& textures,
                                             assets::Assets<image::TextureAtlasLayout>& atlas_layouts,
                                             std::uint32_t key,
                                             const image::Image& glyph_texture,
                                             glm::ivec2 offset);
};

/** @brief Key identifying a specific atlas configuration (size +
 * anti-aliasing).
 *
 * Bevy keys `FontAtlasSet` by `(font, font_size.to_bits(), font_smoothing)`
 * (`font_atlas_set.rs:7-12`); Epix stores one set per font asset, so the key
 * only has to distinguish size and smoothing.
 */
EPIX_EXPORT struct FontAtlasKey {
    /** @brief Font size in pixels used for rasterization. */
    float size;
    /** @brief Whether anti-aliased rendering is enabled. */
    bool anti_aliased;

    bool operator==(const FontAtlasKey& other) const noexcept  = default;
    auto operator<=>(const FontAtlasKey& other) const noexcept = default;
};

/** @brief Hash functor for FontAtlasKey, suitable for unordered
 * containers. */
EPIX_EXPORT struct FontAtlasKeyHash {
    std::size_t operator()(const FontAtlasKey& key) const noexcept {
        std::size_t seed = std::hash<float>{}(key.size);
        seed ^= std::hash<bool>{}(key.anti_aliased) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

/** @brief Collection of `FontAtlas` instances for a single font face, keyed by
 * size and anti-aliasing settings (Bevy `FontAtlasSet`, `font_atlas_set.rs:19-25`).
 *
 * Each key maps to a `std::vector<FontAtlas>`: glyphs are placed in the first
 * atlas with room for them and a new one is appended when none fits
 * (`font_atlas.rs:149-178`).
 */
EPIX_EXPORT struct EPIX_API FontAtlasSet {
    friend struct FontAtlasSets;

   public:
    /** @brief Metrics and atlas placement of one rasterized glyph.
     *
     * Epix keeps this cache per `(FontAtlasKey, glyph)`; Bevy keeps the same
     * information in cosmic-text's glyph cache and in
     * `FontAtlas::glyph_to_atlas_index` (`pipeline.rs:419-439`). */
    struct GlyphEntry {
        /** @brief Font metrics of the rasterized glyph. */
        Glyph metrics;
        /** @brief Where the glyph bitmap was placed in an atlas. */
        GlyphAtlasInfo atlas_info;
        /** @brief Whether `atlas_info` has been filled in yet. */
        bool placed = false;
    };

    FontAtlasSet(void* face, std::uint32_t max_texture_dimension_2d = 8192)
        : font_face(face), max_texture_dimension_2d(max_texture_dimension_2d) {}
    FontAtlasSet(const FontAtlasSet&)            = delete;
    FontAtlasSet(FontAtlasSet&&)                 = default;
    FontAtlasSet& operator=(const FontAtlasSet&) = delete;
    FontAtlasSet& operator=(FontAtlasSet&&)      = default;

    /** @brief Get the atlases for the given key, if any exist. */
    std::optional<std::reference_wrapper<std::vector<FontAtlas>>> get_mut(const FontAtlasKey& key);
    /** @brief Get the atlases for the given key, if any exist. */
    std::optional<std::reference_wrapper<const std::vector<FontAtlas>>> get(const FontAtlasKey& key) const;
    /** @brief Iterate over all stored (key, atlases) pairs (const). */
    auto iter() const { return std::views::all(storage); }
    /** @brief Iterate over all stored (key, atlases) pairs (mutable). */
    auto iter_mut() { return std::views::all(storage); }

    /** @brief Rasterize a glyph and place it into an atlas, creating a new
     * atlas when no existing one has room (Bevy `add_glyph_to_atlas`,
     * `font_atlas.rs:126-182`).
     * @return The glyph's atlas placement, or a `TextError`. */
    std::expected<GlyphAtlasInfo, TextError> add_glyph_to_atlas(
        assets::Assets<image::Image>& textures,
        assets::Assets<image::TextureAtlasLayout>& atlas_layouts,
        const FontAtlasKey& key,
        std::uint32_t glyph_index);

    /** @brief Get the font metrics of a rasterized glyph, rasterizing it and
     * caching it on first use. */
    const Glyph& get_glyph(const FontAtlasKey& key, std::uint32_t glyph_index);

    /** @brief Get the underlying FreeType font face pointer. */
    void* get_font_face() const noexcept { return font_face; }
    /** @brief Get the maximum 2D texture dimension available to atlases. */
    std::uint32_t get_max_texture_dimension_2d() const noexcept { return max_texture_dimension_2d; }

   private:
    void* font_face = nullptr;
    std::uint32_t max_texture_dimension_2d;
    std::unordered_map<FontAtlasKey, std::vector<FontAtlas>, FontAtlasKeyHash> storage;
    std::unordered_map<FontAtlasKey, std::unordered_map<std::uint32_t, GlyphEntry>, FontAtlasKeyHash> glyphs;

    /** @brief Rasterize a glyph, caching its metrics and bitmap placement
     * metadata.
     *
     * Epix's counterpart to `bevy_text`'s `get_outlined_glyph_texture`
     * (`font_atlas.rs:185-248`): the rasterizer is FreeType rather than swash,
     * but the produced bitmap is the same RGBA (white RGB, coverage alpha)
     * top-row-first image, with the placement offset in the atlas image's
     * +Y-down space. */
    std::optional<std::tuple<Glyph, image::Image, glm::ivec2>> generate_glyph(float font_size,
                                                                              std::uint32_t glyph_index);
};

/** @brief System resource managing FontAtlasSet instances for all loaded
 * fonts.
 *
 * Indexed by font asset ID. Automatically creates atlas sets when new
 * fonts are loaded and removes them when fonts are unloaded.
 */
EPIX_EXPORT struct EPIX_API FontAtlasSets {
    FontAtlasSets(ecs::World& world);
    FontAtlasSets(const FontAtlasSets&)            = delete;
    FontAtlasSets(FontAtlasSets&&)                 = default;
    FontAtlasSets& operator=(const FontAtlasSets&) = delete;
    FontAtlasSets& operator=(FontAtlasSets&&)      = default;

    /** @brief Get the maximum 2D texture dimension for atlases. */
    std::uint32_t get_max_texture_dimension_2d() const noexcept { return max_texture_dimension_2d; }

    /** @brief Get a mutable reference to the atlas set for a font, if it exists. */
    std::optional<std::reference_wrapper<FontAtlasSet>> get_mut(const assets::AssetId<Font>& font_id);
    /** @brief Get a const reference to the atlas set for a font, if it exists. */
    std::optional<std::reference_wrapper<const FontAtlasSet>> get(const assets::AssetId<Font>& font_id) const;
    /** @brief Check whether an atlas set exists for the given font. */
    bool contains(const assets::AssetId<Font>& font_id) const;
    /** @brief Remove the atlas set for the given font. */
    void erase(const assets::AssetId<Font>& font_id);
    /** @brief Add a new atlas set for the given font face. */
    void add(const assets::AssetId<Font>& font_id, void* face);
    /** @brief Iterate over all stored (font id, atlas set) pairs (const). */
    auto iter() const { return std::views::all(storage); }
    /** @brief Iterate over all stored (font id, atlas set) pairs (mutable). */
    auto iter_mut() { return std::views::all(storage); }

   private:
    std::uint32_t max_texture_dimension_2d = 8192;
    std::unordered_map<assets::AssetId<Font>, FontAtlasSet> storage;
};

/** @brief System labels for font-related systems. */
EPIX_EXPORT enum class FontSystems {
    /** @brief System that creates a FontAtlasSet for newly loaded fonts. */
    AddFontAtlasSet,
};

/** @brief Plugin that registers font loading and atlas management systems. */
EPIX_EXPORT struct EPIX_API FontPlugin {
    void attach(app::App& app);
    void ready(app::App& app);
};
}  // namespace epix::text::font
