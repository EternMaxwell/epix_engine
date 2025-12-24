#pragma once

#include <array>
#include <cstddef>
#include <epix/assets.hpp>
#include <epix/core.hpp>
#include <epix/image.hpp>
#include <expected>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace epix::text::font {
struct Font {
    std::unique_ptr<std::byte[]> data;
    size_t size = 0;
};
struct FontLoader {
    static std::span<const char* const> extensions();
    static Font load(const std::filesystem::path& path, epix::assets::LoadContext& context);
};
struct FontLibrary {
    void* library;

    FontLibrary();
    ~FontLibrary();
};
struct Glyph {
    float width;
    float height;

    float horiBearingX;
    float horiBearingY;
    float horiAdvance;

    float vertBearingX;
    float vertBearingY;
    float vertAdvance;
};
struct FontAtlas {
   public:
    FontAtlas(void* face, uint32_t atlas_width, uint32_t atlas_height, float font_size, bool font_antialiased)
        : font_face(face),
          atlas_width(atlas_width),
          atlas_height(atlas_height),
          font_size(font_size),
          font_antialiased(font_antialiased) {}
    FontAtlas(const FontAtlas&)            = delete;
    FontAtlas(FontAtlas&&)                 = default;
    FontAtlas& operator=(const FontAtlas&) = delete;
    FontAtlas& operator=(FontAtlas&&)      = default;

    const std::optional<assets::Handle<image::Image>>& image_handle() const { return image; }
    void apply_pending(assets::Assets<image::Image>& assets);
    uint32_t get_glyph_index(char32_t codepoint);
    void* get_font_face() const { return font_face; }
    float get_font_size() const { return font_size; }

    image::Rect get_glyph_atlas_loc(uint32_t glyph_index);
    const Glyph& get_glyph(uint32_t glyph_index);
    uint32_t get_image_width() const { return atlas_width; }
    uint32_t get_image_height() const { return atlas_height; }
    uint32_t get_image_layers() const { return 1; }

    std::array<float, 5> get_glyph_uv_rect(uint32_t glyph_index);

   private:
    void* font_face;
    /// The image where the font glyphs are stored
    std::optional<assets::Handle<image::Image>> image;
    /// Mapping from glyph index to its position in the atlas
    std::unordered_map<uint32_t, image::Rect> atlas_locs;
    std::unordered_map<uint32_t, Glyph> glyphs;
    // Atlas physical size (pixels). The atlas is a single 2D image (not arrayed).
    // The atlas may grow in the Y direction only when more space is required.
    uint32_t atlas_width;
    uint32_t atlas_height;

    float font_size;
    bool font_antialiased;

    struct LayerState {
        uint32_t x          = 0;
        uint32_t y          = 0;
        uint32_t row_height = 0;
    };

    // Packing cursor (single-layer row-based packing). We only keep a single packing
    // state for the 2D atlas and will grow the atlas in Y as needed.
    LayerState layer_state{};

    // Pending glyph images, no loc and glyph info cause it should be stored in atlas_locs and glyphs once pending.
    struct PendingGlyph {
        uint32_t glyph_index;
        std::unique_ptr<std::byte[]> bitmap_data;  // RGBA image (4 bytes per pixel)
    };
    std::vector<PendingGlyph> pending_glyphs;

    // Error returned when placement cannot be completed. Use an enum code plus optional metadata.
    struct FontAtlasError {
        enum class Code {
            InvalidSize,       // width or height is zero
            TooLargeForLayer,  // glyph larger than a single layer
            AtlasFull,         // no layer has space and cannot create more layers
        } code;
        // Helpful diagnostics
        uint32_t glyph_width  = 0;
        uint32_t glyph_height = 0;
        uint32_t atlas_w      = 0;
        uint32_t atlas_h      = 0;
    };

    // Place a glyph of given `width` x `height` into the atlas and return the allocated Rect (3D:
    // x,y,layer,width,height,depth=1). This uses a simple row-based packing on a single 2D image.
    // If necessary, the atlas will grow in the Y direction to accommodate the glyph. Callers must
    // ensure `atlas_width`/`atlas_height` reflect the real image size.
    std::expected<image::Rect, FontAtlasError> place_char_bitmap_size(uint32_t width, uint32_t height);

    image::Image make_atlas_image();
    // Add image to store this atlas to the assets if not already added.
    bool add_image_if_missing(assets::Assets<image::Image>& assets);

    std::optional<std::tuple<Glyph, std::unique_ptr<std::byte[]>, std::pair<uint32_t, uint32_t>>> generate_glyph(
        uint32_t glyph_index);

    /// Add a character to the atlas, regardless of whether added before.
    /// This will load the glyph from the font face, render it to a bitmap and pend it for upload.
    void cache_glyph_index(uint32_t glyph_index);
};
struct FontAtlasKey {
    float size;  // binary representation of font size(float)
    bool anti_aliased;

    bool operator==(const FontAtlasKey& other) const noexcept  = default;
    auto operator<=>(const FontAtlasKey& other) const noexcept = default;
};
struct FontAtlasKeyHash {
    size_t operator()(const FontAtlasKey& key) const noexcept {
        size_t seed = std::hash<float>{}(key.size);
        seed ^= std::hash<bool>{}(key.anti_aliased) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
/// Map from Font detail to its FontAtlas, since same font face can still have different sizes/styles
struct FontAtlasSet : private std::unordered_map<FontAtlasKey, FontAtlas, FontAtlasKeyHash> {
    using base = std::unordered_map<FontAtlasKey, FontAtlas, FontAtlasKeyHash>;

    FontAtlasSet(void* face) : font_face(face) {}
    FontAtlasSet(const FontAtlasSet&)            = delete;
    FontAtlasSet(FontAtlasSet&&)                 = default;
    FontAtlasSet& operator=(const FontAtlasSet&) = delete;
    FontAtlasSet& operator=(FontAtlasSet&&)      = default;

    std::optional<std::reference_wrapper<FontAtlas>> get_mut(const FontAtlasKey& key);
    std::optional<std::reference_wrapper<const FontAtlas>> get(const FontAtlasKey& key) const;
    FontAtlas& get_or_insert(const FontAtlasKey& key);
    auto iter() const { return std::views::all((base&)(*this)); }
    auto iter_mut() { return std::views::all((base&)(*this)); }

   private:
    void* font_face = nullptr;
};
/// Map from Font asset to its FontAtlasSet
struct FontAtlasSets : private std::unordered_map<assets::AssetId<Font>, FontAtlasSet> {
    using base = std::unordered_map<assets::AssetId<Font>, FontAtlasSet>;

    FontAtlasSets()                                = default;
    FontAtlasSets(const FontAtlasSets&)            = delete;
    FontAtlasSets(FontAtlasSets&&)                 = default;
    FontAtlasSets& operator=(const FontAtlasSets&) = delete;
    FontAtlasSets& operator=(FontAtlasSets&&)      = default;

    std::optional<std::reference_wrapper<FontAtlasSet>> get_mut(const assets::AssetId<Font>& font_id);
    std::optional<std::reference_wrapper<const FontAtlasSet>> get(const assets::AssetId<Font>& font_id) const;
    using base::contains;
    using base::emplace;
    using base::erase;
    auto iter() const { return std::views::all((base&)(*this)); }
    auto iter_mut() { return std::views::all((base&)(*this)); }
};
enum class FontSystems {
    AddFontAtlasSet,
    ApplyPendingFontAtlasUpdates,
};
struct FontPlugin {
    static void apply_pending_font_atlas_updates(ResMut<FontAtlasSets> atlas_sets,
                                                 ResMut<assets::Assets<image::Image>> images);
    static void add_font_atlas_set(ResMut<FontAtlasSets> atlas_sets,
                                   Res<FontLibrary> font_lib,
                                   EventReader<assets::AssetEvent<Font>> reader,
                                   Res<assets::Assets<Font>> fonts);
    void build(App& app);
};
}  // namespace epix::text::font