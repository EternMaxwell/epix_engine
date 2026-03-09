module;

export module epix.text:font;

import epix.assets;
import epix.core;
import epix.image;
import std;

namespace text::font {
export struct Font {
    std::unique_ptr<std::byte[]> data;
    std::size_t size = 0;
};

export struct AtlasRect {
    std::uint32_t x      = 0;
    std::uint32_t y      = 0;
    std::uint32_t layer  = 0;
    std::uint32_t width  = 0;
    std::uint32_t height = 0;
    std::uint32_t depth  = 1;

    static AtlasRect rect3d(std::uint32_t width, std::uint32_t height, std::uint32_t depth) {
        return AtlasRect{.width = width, .height = height, .depth = depth};
    }

    static AtlasRect rect3d(std::uint32_t x,
                            std::uint32_t y,
                            std::uint32_t layer,
                            std::uint32_t width,
                            std::uint32_t height,
                            std::uint32_t depth) {
        return AtlasRect{.x = x, .y = y, .layer = layer, .width = width, .height = height, .depth = depth};
    }
};
}  // namespace text::font

template <>
struct std::hash<assets::AssetId<text::font::Font>> {
    std::size_t operator()(const assets::AssetId<text::font::Font>& id) const noexcept {
        return std::visit([]<typename T>(const T& index) { return std::hash<T>()(index); }, id);
    }
};

namespace text::font {
struct FontLoader {
    static std::span<const char* const> extensions();
    static Font load(const std::filesystem::path& path, assets::LoadContext& context);
};

struct FontLibrary {
    void* library;

    FontLibrary();
    ~FontLibrary();
};

export struct Glyph {
    float width;
    float height;

    float horiBearingX;
    float horiBearingY;
    float horiAdvance;

    float vertBearingX;
    float vertBearingY;
    float vertAdvance;
};

export struct FontAtlas {
   public:
    FontAtlas(void* face,
              std::uint32_t atlas_width,
              std::uint32_t atlas_height,
              float font_size,
              bool font_antialiased,
              std::uint32_t atlas_layers = 1)
        : font_face(face),
          atlas_width(atlas_width),
          atlas_height(atlas_height),
          atlas_layers(atlas_layers),
          font_size(font_size),
          font_antialiased(font_antialiased),
          layer_states(atlas_layers) {}
    FontAtlas(const FontAtlas&)            = delete;
    FontAtlas(FontAtlas&&)                 = default;
    FontAtlas& operator=(const FontAtlas&) = delete;
    FontAtlas& operator=(FontAtlas&&)      = default;

    const std::optional<assets::Handle<image::Image>>& image_handle() const { return image; }
    void apply_pending(assets::Assets<image::Image>& assets);
    std::uint32_t get_glyph_index(char32_t codepoint);
    void* get_font_face() const { return font_face; }
    float get_font_size() const { return font_size; }

    AtlasRect get_glyph_atlas_loc(std::uint32_t glyph_index);
    const Glyph& get_glyph(std::uint32_t glyph_index);
    std::uint32_t get_image_width() const { return atlas_width; }
    std::uint32_t get_image_height() const { return atlas_height; }
    std::uint32_t get_image_layers() const { return atlas_layers; }

    std::array<float, 5> get_glyph_uv_rect(std::uint32_t glyph_index);

   private:
    void* font_face;
    std::optional<assets::Handle<image::Image>> image;
    std::unordered_map<std::uint32_t, AtlasRect> atlas_locs;
    std::unordered_map<std::uint32_t, Glyph> glyphs;
    std::uint32_t atlas_width;
    std::uint32_t atlas_height;
    std::uint32_t atlas_layers;

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

    std::expected<AtlasRect, FontAtlasError> place_char_bitmap_size(std::uint32_t width, std::uint32_t height);

    image::Image make_atlas_image();
    bool add_image_if_missing(assets::Assets<image::Image>& assets);

    std::optional<std::tuple<Glyph, std::unique_ptr<std::byte[]>, std::pair<std::uint32_t, std::uint32_t>>>
    generate_glyph(std::uint32_t glyph_index);

    void cache_glyph_index(std::uint32_t glyph_index);
};

export struct FontAtlasKey {
    float size;
    bool anti_aliased;

    bool operator==(const FontAtlasKey& other) const noexcept  = default;
    auto operator<=>(const FontAtlasKey& other) const noexcept = default;
};

export struct FontAtlasKeyHash {
    std::size_t operator()(const FontAtlasKey& key) const noexcept {
        std::size_t seed = std::hash<float>{}(key.size);
        seed ^= std::hash<bool>{}(key.anti_aliased) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

export struct FontAtlasSet {
    FontAtlasSet(void* face) : font_face(face) {}
    FontAtlasSet(const FontAtlasSet&)            = delete;
    FontAtlasSet(FontAtlasSet&&)                 = default;
    FontAtlasSet& operator=(const FontAtlasSet&) = delete;
    FontAtlasSet& operator=(FontAtlasSet&&)      = default;

    std::optional<std::reference_wrapper<FontAtlas>> get_mut(const FontAtlasKey& key);
    std::optional<std::reference_wrapper<const FontAtlas>> get(const FontAtlasKey& key) const;
    FontAtlas& get_or_insert(const FontAtlasKey& key);
    auto iter() const { return std::views::all(storage); }
    auto iter_mut() { return std::views::all(storage); }

   private:
    void* font_face = nullptr;
    std::unordered_map<FontAtlasKey, FontAtlas, FontAtlasKeyHash> storage;
};

export struct FontAtlasSets {
    FontAtlasSets()                                = default;
    FontAtlasSets(const FontAtlasSets&)            = delete;
    FontAtlasSets(FontAtlasSets&&)                 = default;
    FontAtlasSets& operator=(const FontAtlasSets&) = delete;
    FontAtlasSets& operator=(FontAtlasSets&&)      = default;

    std::optional<std::reference_wrapper<FontAtlasSet>> get_mut(const assets::AssetId<Font>& font_id);
    std::optional<std::reference_wrapper<const FontAtlasSet>> get(const assets::AssetId<Font>& font_id) const;
    bool contains(const assets::AssetId<Font>& font_id) const;
    void erase(const assets::AssetId<Font>& font_id);
    auto emplace(const assets::AssetId<Font>& font_id, FontAtlasSet&& set) {
        return storage.emplace(font_id, std::move(set));
    }
    auto iter() const { return std::views::all(storage); }
    auto iter_mut() { return std::views::all(storage); }

   private:
    std::unordered_map<assets::AssetId<Font>, FontAtlasSet> storage;
};

export enum class FontSystems {
    AddFontAtlasSet,
    ApplyPendingFontAtlasUpdates,
};

export struct FontPlugin {
    void build(core::App& app);
};
}  // namespace text::font