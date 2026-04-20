module;

#include <asio/awaitable.hpp>

export module epix.text:font;

import epix.assets;
import epix.core;
import epix.image;
import std;

namespace epix::text::font {
/** @brief Font resource holding raw font file data (e.g. TTF/OTF) in memory. */
export struct Font {
    /** @brief Raw font file bytes. */
    std::unique_ptr<std::byte[]> data;
    /** @brief Size of the font data in bytes. */
    std::size_t size = 0;
};

/** @brief Rectangle region within a font atlas image, optionally spanning
 * multiple layers.
 */
export struct AtlasRect {
    /** @brief X offset in pixels within the atlas layer. */
    std::uint32_t x = 0;
    /** @brief Y offset in pixels within the atlas layer. */
    std::uint32_t y = 0;
    /** @brief Layer index in a texture array atlas. */
    std::uint32_t layer = 0;
    /** @brief Width of the rectangle in pixels. */
    std::uint32_t width = 0;
    /** @brief Height of the rectangle in pixels. */
    std::uint32_t height = 0;
    /** @brief Depth (number of layers) of the rectangle. */
    std::uint32_t depth = 1;

    /** @brief Create a 3D rect with the given dimensions starting at origin.
     * @return AtlasRect with x=0, y=0, layer=0. */
    static AtlasRect rect3d(std::uint32_t width, std::uint32_t height, std::uint32_t depth) {
        return AtlasRect{.width = width, .height = height, .depth = depth};
    }

    /** @brief Create a 3D rect with explicit position and dimensions.
     * @return AtlasRect with the specified position and size. */
    static AtlasRect rect3d(std::uint32_t x,
                            std::uint32_t y,
                            std::uint32_t layer,
                            std::uint32_t width,
                            std::uint32_t height,
                            std::uint32_t depth) {
        return AtlasRect{.x = x, .y = y, .layer = layer, .width = width, .height = height, .depth = depth};
    }
};
}  // namespace epix::text::font

template <>
struct std::hash<epix::assets::AssetId<epix::text::font::Font>> {
    std::size_t operator()(const epix::assets::AssetId<epix::text::font::Font>& id) const noexcept {
        return std::visit([]<typename T>(const T& index) { return std::hash<T>()(index); }, id);
    }
};

namespace epix::text::font {
struct FontLoader {
    using Asset = Font;
    struct Settings {};
    using Error = std::exception_ptr;

    static std::span<std::string_view> extensions();
    static asio::awaitable<std::expected<Font, Error>> load(assets::Reader& reader,
                                                            const Settings& settings,
                                                            assets::LoadContext& context);
};

struct FontLibrary {
    void* library;

    FontLibrary();
    ~FontLibrary();
};

/** @brief Metrics for a single rendered glyph from a font face. */
export struct Glyph {
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

/** @brief Texture atlas that packs rendered glyphs for a specific font
 * face, size, and anti-aliasing setting.
 *
 * Glyphs are rasterized on demand and placed into a layered texture
 * atlas. Call `apply_pending()` to upload newly rasterized glyphs to the
 * image asset.
 */
export struct FontAtlas {
    friend struct FontAtlasSet;

   public:
    FontAtlas(const FontAtlas&)            = delete;
    FontAtlas(FontAtlas&&)                 = default;
    FontAtlas& operator=(const FontAtlas&) = delete;
    FontAtlas& operator=(FontAtlas&&)      = default;

    /** @brief Get the handle to the atlas image asset, if created. */
    const std::optional<assets::Handle<image::Image>>& image_handle() const { return image; }
    /** @brief Upload all pending rasterized glyphs to the image asset. */
    void apply_pending(assets::Assets<image::Image>& assets);
    /** @brief Map a Unicode codepoint to its glyph index, rasterizing if needed. */
    std::uint32_t get_glyph_index(char32_t codepoint);
    /** @brief Get the underlying FreeType font face pointer. */
    void* get_font_face() const { return font_face; }
    /** @brief Get the font size in pixels used for this atlas. */
    float get_font_size() const { return font_size; }

    /** @brief Get the atlas rectangle location for a glyph by index. */
    AtlasRect get_glyph_atlas_loc(std::uint32_t glyph_index);
    /** @brief Get the glyph metrics for a glyph by index. */
    const Glyph& get_glyph(std::uint32_t glyph_index);
    /** @brief Get the atlas image width in pixels. */
    std::uint32_t get_image_width() const { return atlas_width; }
    /** @brief Get the atlas image height in pixels. */
    std::uint32_t get_image_height() const { return atlas_height; }
    /** @brief Get the number of atlas image layers. */
    std::uint32_t get_image_layers() const { return atlas_layers; }

    /** @brief Get the UV rect [u0, v0, u1, v1, layer] for a glyph. */
    std::array<float, 5> get_glyph_uv_rect(std::uint32_t glyph_index);

   private:
    void* font_face;
    std::optional<assets::Handle<image::Image>> image;
    std::unordered_map<std::uint32_t, AtlasRect> atlas_locs;
    std::unordered_map<std::uint32_t, Glyph> glyphs;
    std::uint32_t atlas_width;
    std::uint32_t atlas_height;
    std::uint32_t atlas_layers;
    std::uint32_t max_texture_array_layers;

    float font_size;
    bool font_antialiased;

    struct LayerState {
        std::uint32_t x          = 0;
        std::uint32_t y          = 0;
        std::uint32_t row_height = 0;
    };

    std::vector<LayerState> layer_states;

    struct PendingGlyph {
        std::uint32_t glyph_index;
        std::unique_ptr<std::byte[]> bitmap_data;
    };
    std::vector<PendingGlyph> pending_glyphs;

    struct FontAtlasError {
        enum class Code {
            InvalidSize,
            TooLargeForLayer,
            AtlasFull,
        } code;
        std::uint32_t glyph_width  = 0;
        std::uint32_t glyph_height = 0;
        std::uint32_t atlas_w      = 0;
        std::uint32_t atlas_h      = 0;
    };

    FontAtlas(void* face,
              std::uint32_t atlas_width,
              std::uint32_t atlas_height,
              float font_size,
              bool font_antialiased,
              std::uint32_t max_texture_array_layers,
              std::uint32_t atlas_layers = 1)
        : font_face(face),
          atlas_width(atlas_width),
          atlas_height(atlas_height),
          atlas_layers(atlas_layers),
          max_texture_array_layers(max_texture_array_layers),
          font_size(font_size),
          font_antialiased(font_antialiased),
          layer_states(atlas_layers) {}

    std::expected<AtlasRect, FontAtlasError> place_char_bitmap_size(std::uint32_t width, std::uint32_t height);

    bool add_image_if_missing(assets::Assets<image::Image>& assets);

    image::Image make_atlas_image();

    std::optional<std::tuple<Glyph, std::unique_ptr<std::byte[]>, std::pair<std::uint32_t, std::uint32_t>>>
    generate_glyph(std::uint32_t glyph_index);

    void cache_glyph_index(std::uint32_t glyph_index);
};

/** @brief Key identifying a specific atlas configuration (size +
 * anti-aliasing). */
export struct FontAtlasKey {
    /** @brief Font size in pixels used for rasterization. */
    float size;
    /** @brief Whether anti-aliased rendering is enabled. */
    bool anti_aliased;

    bool operator==(const FontAtlasKey& other) const noexcept  = default;
    auto operator<=>(const FontAtlasKey& other) const noexcept = default;
};

/** @brief Hash functor for FontAtlasKey, suitable for unordered
 * containers. */
export struct FontAtlasKeyHash {
    std::size_t operator()(const FontAtlasKey& key) const noexcept {
        std::size_t seed = std::hash<float>{}(key.size);
        seed ^= std::hash<bool>{}(key.anti_aliased) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

/** @brief Collection of FontAtlas instances for a single font face,
 * keyed by size and anti-aliasing settings.
 */
export struct FontAtlasSet {
    friend struct FontAtlasSets;

   public:
    FontAtlasSet(void* face,
                 std::uint32_t max_texture_dimension_2d = 8192,
                 std::uint32_t max_texture_array_layers = 256)
        : font_face(face),
          max_texture_dimension_2d(max_texture_dimension_2d),
          max_texture_array_layers(max_texture_array_layers) {}
    FontAtlasSet(const FontAtlasSet&)            = delete;
    FontAtlasSet(FontAtlasSet&&)                 = default;
    FontAtlasSet& operator=(const FontAtlasSet&) = delete;
    FontAtlasSet& operator=(FontAtlasSet&&)      = default;

    /** @brief Get a mutable reference to the atlas for the given key, if it exists. */
    std::optional<std::reference_wrapper<FontAtlas>> get_mut(const FontAtlasKey& key);
    /** @brief Get a const reference to the atlas for the given key, if it exists. */
    std::optional<std::reference_wrapper<const FontAtlas>> get(const FontAtlasKey& key) const;
    /** @brief Get or create the atlas for the given key. */
    FontAtlas& get_or_insert(const FontAtlasKey& key);
    /** @brief Iterate over all stored (key, atlas) pairs (const). */
    auto iter() const { return std::views::all(storage); }
    /** @brief Iterate over all stored (key, atlas) pairs (mutable). */
    auto iter_mut() { return std::views::all(storage); }

   private:
    void* font_face = nullptr;
    std::uint32_t max_texture_dimension_2d;
    std::uint32_t max_texture_array_layers;
    std::unordered_map<FontAtlasKey, FontAtlas, FontAtlasKeyHash> storage;
};

/** @brief System resource managing FontAtlasSet instances for all loaded
 * fonts.
 *
 * Indexed by font asset ID. Automatically creates atlas sets when new
 * fonts are loaded and removes them when fonts are unloaded.
 */
export struct FontAtlasSets {
    FontAtlasSets(core::World& world);
    FontAtlasSets(const FontAtlasSets&)            = delete;
    FontAtlasSets(FontAtlasSets&&)                 = default;
    FontAtlasSets& operator=(const FontAtlasSets&) = delete;
    FontAtlasSets& operator=(FontAtlasSets&&)      = default;

    /** @brief Get the maximum 2D texture dimension for atlases. */
    std::uint32_t get_max_texture_dimension_2d() const { return max_texture_dimension_2d; }
    /** @brief Get the maximum texture array layer count. */
    std::uint32_t get_max_texture_array_layers() const { return max_texture_array_layers; }

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
    std::uint32_t max_texture_array_layers = 256;
    std::unordered_map<assets::AssetId<Font>, FontAtlasSet> storage;
};

/** @brief System labels for font-related systems. */
export enum class FontSystems {
    /** @brief System that creates a FontAtlasSet for newly loaded fonts. */
    AddFontAtlasSet,
    /** @brief System that uploads pending glyph data to atlas images. */
    ApplyPendingFontAtlasUpdates,
};

/** @brief Plugin that registers font loading, atlas management, and glyph
 * update systems. */
export struct FontPlugin {
    void build(core::App& app);
    void finish(core::App& app);
};
}  // namespace epix::text::font