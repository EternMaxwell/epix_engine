#pragma once

#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <epix/assets.hpp>
#include <epix/core.hpp>
#include <epix/image.hpp>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>

namespace epix::text::font {
struct Font {
    std::unique_ptr<std::byte[]> data;
    size_t size = 0;
};
struct FontLoader {
    static auto& extensions() {
        static const auto exts = std::array{".ttf", ".otf", ".woff", ".woff2"};
        return exts;
    }
    static Font load(const std::filesystem::path& path, epix::assets::LoadContext& context) {
        auto file_size = std::filesystem::file_size(path);
        auto buffer    = std::make_unique<std::byte[]>(file_size);
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open font file: " + path.string());
        }
        file.read(reinterpret_cast<char*>(buffer.get()), file_size);
        return Font{std::move(buffer), static_cast<size_t>(file_size)};
    }
};
struct FontLibrary {
    FT_Library library;

    FontLibrary() {
        if (FT_Init_FreeType(&library)) {
            throw std::runtime_error("Failed to initialize FreeType library");
        }
    }

    ~FontLibrary() { FT_Done_FreeType(library); }
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
    FontAtlas(FT_Face face,
              uint32_t atlas_width,
              uint32_t atlas_height,
              uint32_t atlas_layers,
              uint32_t font_size,
              bool font_antialiased)
        : font_face(face),
          atlas_width(atlas_width),
          atlas_height(atlas_height),
          atlas_layers(atlas_layers),
          font_size(font_size),
          font_antialiased(font_antialiased) {}
    FontAtlas(const FontAtlas&)            = delete;
    FontAtlas(FontAtlas&&)                 = default;
    FontAtlas& operator=(const FontAtlas&) = delete;
    FontAtlas& operator=(FontAtlas&&)      = default;

    const std::optional<assets::Handle<image::Image>>& image_handle() const { return image; }
    void apply_pending(assets::Assets<image::Image>& assets) {
        add_image_if_missing(assets);
        if (pending_glyphs.empty()) return;
        auto& image = *assets.get_mut(this->image.value());
        if (image.get_desc().arraySize < atlas_layers) {
            auto new_image = make_atlas_image();
            // copy data from old image to new image
            auto res = new_image.write_data(
                image::Rect::rect3d(image.get_desc().width, image.get_desc().height, image.get_desc().arraySize),
                image.get_data().data(), image.get_data().size());
            std::swap(image, new_image);
        }
        for (const auto& pending : pending_glyphs) {
            auto it = atlas_locs.find(pending.glyph_index);
            if (it == atlas_locs.end()) {
                continue;  // should not happen
            }
            const image::Rect& loc = it->second;

            auto res = image.write_data(loc, pending.bitmap_data.get(), loc.width * loc.height);
        }
        pending_glyphs.clear();
    }
    uint32_t get_glyph_index(char32_t codepoint) {
        return static_cast<uint32_t>(FT_Get_Char_Index(font_face, codepoint));
    }

    image::Rect get_glyph_atlas_loc(uint32_t glyph_index) {
        auto it = atlas_locs.find(glyph_index);
        if (it != atlas_locs.end()) {
            return it->second;
        }
        cache_glyph_index(glyph_index);
        return atlas_locs.at(glyph_index);
    }
    const Glyph& get_glyph(uint32_t glyph_index) {
        auto it = glyphs.find(glyph_index);
        if (it != glyphs.end()) {
            return it->second;
        }
        cache_glyph_index(glyph_index);
        return glyphs.at(glyph_index);
    }
    uint32_t get_image_width() const { return atlas_width; }
    uint32_t get_image_height() const { return atlas_height; }
    uint32_t get_image_layers() const { return atlas_layers; }

    std::array<float, 5> get_glyph_uv_rect(uint32_t glyph_index) {
        auto loc = get_glyph_atlas_loc(glyph_index);
        float u0 = static_cast<float>(loc.x) / static_cast<float>(atlas_width);
        float v0 = static_cast<float>(loc.y) / static_cast<float>(atlas_height);
        float u1 = static_cast<float>(loc.x + loc.width) / static_cast<float>(atlas_width);
        float v1 = static_cast<float>(loc.y + loc.height) / static_cast<float>(atlas_height);
        float layer = static_cast<float>(loc.layer);
        return {u0, v0, u1, v1, layer};
    }

   private:
    FT_Face font_face;
    /// The image where the font glyphs are stored
    std::optional<assets::Handle<image::Image>> image;
    /// Mapping from glyph index to its position in the atlas
    std::unordered_map<uint32_t, image::Rect> atlas_locs;
    std::unordered_map<uint32_t, Glyph> glyphs;
    // Atlas physical size (pixels) and number of layers.
    // These should be set to the actual `image` dimensions when the atlas image is created.
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint32_t atlas_layers;

    uint32_t font_size;
    bool font_antialiased;

    struct LayerState {
        uint32_t x          = 0;
        uint32_t y          = 0;
        uint32_t row_height = 0;
    };

    // Packing cursor per layer
    std::vector<LayerState> layer_states{LayerState{}};

    // Pending glyph images, no loc and glyph info cause it should be stored in atlas_locs and glyphs once pending.
    struct PendingGlyph {
        uint32_t glyph_index;
        std::unique_ptr<std::byte[]> bitmap_data;  // always 1 byte per pixel grey image
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
    // x,y,layer,width,height,depth=1). This uses a simple row-based packing per-layer. If no existing layer has room, a
    // new layer is created. Note: callers must ensure `atlas_width`/`atlas_height`/`atlas_layers` reflect the real
    // image size.
    std::expected<image::Rect, FontAtlasError> place_char_bitmap_size(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0) {
            // a 0-size glyph is allowed (e.g. space); no pixels to place
            return image::Rect::rect3d(0, 0, 0, 0, 0, 1);
        }
        // reserve 1 pixel padding to avoid bleeding between glyphs
        constexpr uint32_t padding = 1;
        const uint32_t padded_w    = width + padding;
        const uint32_t padded_h    = height + padding;
        // Ensure we have at least one layer state
        if (layer_states.empty()) layer_states.emplace_back();

        // Try to fit in existing layers
        for (uint32_t layer = 0; layer < layer_states.size(); ++layer) {
            auto& st = layer_states[layer];
            // if current row can fit (use padded width)
            if (st.x + padded_w <= atlas_width) {
                // if padded height fits in remaining vertical space
                if (st.y + std::max(st.row_height, padded_h) <= atlas_height) {
                    uint32_t placed_x = st.x;
                    uint32_t placed_y = st.y;
                    st.x += padded_w;  // advance cursor by padded width
                    st.row_height = std::max(st.row_height, padded_h);
                    return image::Rect::rect3d(placed_x, placed_y, layer, width, height, 1);
                }
            }
            // try move to next row in this layer (use padded height)
            if (st.y + st.row_height + padded_h <= atlas_height) {
                st.x = 0;
                st.y += st.row_height;
                st.row_height     = padded_h;
                uint32_t placed_x = st.x;
                uint32_t placed_y = st.y;
                st.x += padded_w;
                return image::Rect::rect3d(placed_x, placed_y, layer, width, height, 1);
            }
            // otherwise this layer is full; continue to next layer
        }

        // No existing layer fits — create a new layer if allowed
        if (layer_states.size() < atlas_layers) {
            // create remaining layer states up to atlas_layers
            while (layer_states.size() < atlas_layers) layer_states.emplace_back();
        }

        if (layer_states.size() < atlas_layers) {
            // should not happen, but just in case
            layer_states.emplace_back();
        }

        // Place in the newly created layer (last)
        uint32_t new_layer = static_cast<uint32_t>(layer_states.size() - 1);
        auto& nst          = layer_states[new_layer];
        // Place at origin of new layer if fits
        if (padded_w <= atlas_width && padded_h <= atlas_height) {
            uint32_t placed_x = 0;
            uint32_t placed_y = 0;
            nst.x             = padded_w;  // reserve padded width
            nst.y             = 0;
            nst.row_height    = padded_h;
            return image::Rect::rect3d(placed_x, placed_y, new_layer, width, height, 1);
        }

        return std::unexpected(
            FontAtlasError{FontAtlasError::Code::TooLargeForLayer, width, height, atlas_width, atlas_height});
    }

    image::Image make_atlas_image() {
        return image::Image::with_desc(nvrhi::TextureDesc()
                                           .setWidth(atlas_width)
                                           .setHeight(atlas_height)
                                           .setArraySize(atlas_layers)
                                           .setFormat(nvrhi::Format::R8_UNORM)
                                           .setDimension(nvrhi::TextureDimension::Texture2DArray)
                                           .setKeepInitialState(true)
                                           .setInitialState(nvrhi::ResourceStates::ShaderResource))
            .value();
    }
    // Add image to store this atlas to the assets if not already added.
    bool add_image_if_missing(assets::Assets<image::Image>& assets) {
        if (image) return false;
        // create a new image with the atlas size and layers
        image::Image atlas_image = make_atlas_image();
        image                    = assets.emplace(std::move(atlas_image));
        return true;
    }

    std::optional<std::tuple<Glyph, std::unique_ptr<std::byte[]>, std::pair<uint32_t, uint32_t>>> generate_glyph(
        uint32_t glyph_index) {
        FT_Set_Char_Size(font_face, font_size << 6, 0, 96, 96);
        // Load glyph
        if (FT_Load_Glyph(font_face, glyph_index, FT_LOAD_RENDER)) {
            spdlog::error("[font] Failed to load glyph for index {}", glyph_index);
            return std::nullopt;  // failed to load glyph
        }
        FT_Glyph ft_glyph;
        if (FT_Get_Glyph(font_face->glyph, &ft_glyph)) {
            spdlog::error("[font] Failed to get glyph for index {}", glyph_index);
            return std::nullopt;  // failed to get glyph
        }
        // Convert to bitmap
        if (FT_Glyph_To_Bitmap(&ft_glyph, FT_RENDER_MODE_NORMAL, 0, 1)) {
            spdlog::error("[font] Failed to convert glyph to bitmap for index {}", glyph_index);
            return std::nullopt;  // failed to convert to bitmap
        }

        FT_Bitmap& bitmap = font_face->glyph->bitmap;
        auto bitmap_data  = std::make_unique<std::byte[]>(bitmap.width * bitmap.rows);
        // data in bitmap.buffer is from top to bottom, but we need bottom to top
        for (uint32_t row = 0; row < static_cast<uint32_t>(bitmap.rows); ++row) {
            std::memcpy(&bitmap_data[row * bitmap.width], &bitmap.buffer[(bitmap.rows - 1 - row) * bitmap.width],
                        bitmap.width);
        }
        FT_Done_Glyph(ft_glyph);

        Glyph glyph;
        FT_Glyph_Metrics& m = font_face->glyph->metrics;
        glyph.width         = static_cast<float>(m.width) / 64.0f;
        glyph.height        = static_cast<float>(m.height) / 64.0f;
        glyph.horiBearingX  = static_cast<float>(m.horiBearingX) / 64.0f;
        glyph.horiBearingY  = static_cast<float>(m.horiBearingY) / 64.0f;
        glyph.horiAdvance   = static_cast<float>(m.horiAdvance) / 64.0f;
        glyph.vertBearingX  = static_cast<float>(m.vertBearingX) / 64.0f;
        glyph.vertBearingY  = static_cast<float>(m.vertBearingY) / 64.0f;
        glyph.vertAdvance   = static_cast<float>(m.vertAdvance) / 64.0f;

        return std::make_tuple(glyph, std::move(bitmap_data), std::make_pair(bitmap.width, bitmap.rows));
    }

    /// Add a character to the atlas, regardless of whether added before.
    /// This will load the glyph from the font face, render it to a bitmap and pend it for upload.
    void cache_glyph_index(uint32_t glyph_index) {
        auto generated = generate_glyph(glyph_index);
        if (!generated) {
            // failed to generate, will also fail in the future, add an empty glyph to avoid repeated attempts
            atlas_locs[glyph_index] = image::Rect::rect3d(0, 0, 0, 0, 0, 1);
            Glyph empty_glyph{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            glyphs[glyph_index] = empty_glyph;
            return;
        }
        auto& [glyph, bitmap_data, size] = *generated;
        // Place in atlas
        auto place_res = place_char_bitmap_size(size.first, size.second);
        if (!place_res) {
            auto err = place_res.error();
            spdlog::error(
                "[font] Failed to place glyph in atlas for glyph_index={}, glyph_size={}x{}, "
                "atlas_size={}x{}, cause '{}'",
                glyph_index, static_cast<int>(err.code), err.glyph_width, err.glyph_height, err.atlas_w, err.atlas_h,
                err.code == FontAtlasError::Code::AtlasFull
                    ? "atlas full"
                    : (err.code == FontAtlasError::Code::TooLargeForLayer ? "too large for layer" : "invalid size"));
            // will fail in the future, add an empty loc
            atlas_locs[glyph_index] = image::Rect::rect3d(0, 0, 0, 0, 0, 1);
            glyphs[glyph_index]     = glyph;
            return;
        }
        image::Rect loc         = place_res.value();
        atlas_locs[glyph_index] = loc;
        glyphs[glyph_index]     = glyph;

        pending_glyphs.push_back(PendingGlyph{glyph_index, std::move(bitmap_data)});
    }
};
struct FontAtlasKey {
    uint32_t size;  // binary representation of font size(float)
    bool anti_aliased;

    bool operator==(const FontAtlasKey& other) const noexcept  = default;
    auto operator<=>(const FontAtlasKey& other) const noexcept = default;
};
struct FontAtlasKeyHash {
    size_t operator()(const FontAtlasKey& key) const noexcept {
        size_t seed = std::hash<uint32_t>{}(key.size);
        seed ^= std::hash<bool>{}(key.anti_aliased) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
/// Map from Font detail to its FontAtlas, since same font face can still have different sizes/styles
struct FontAtlasSet : private std::unordered_map<FontAtlasKey, FontAtlas, FontAtlasKeyHash> {
    using base = std::unordered_map<FontAtlasKey, FontAtlas, FontAtlasKeyHash>;

    FontAtlasSet(FT_Face face) : font_face(face) {}
    FontAtlasSet(const FontAtlasSet&)            = delete;
    FontAtlasSet(FontAtlasSet&&)                 = default;
    FontAtlasSet& operator=(const FontAtlasSet&) = delete;
    FontAtlasSet& operator=(FontAtlasSet&&)      = default;

    std::optional<std::reference_wrapper<FontAtlas>> get_mut(const FontAtlasKey& key) {
        auto it = this->find(key);
        if (it == this->end()) {
            return std::nullopt;
        }
        return std::ref(it->second);
    }
    std::optional<std::reference_wrapper<const FontAtlas>> get(const FontAtlasKey& key) {
        auto it = this->find(key);
        if (it == this->end()) {
            return std::nullopt;
        }
        return std::cref(it->second);
    }
    FontAtlas& get_or_insert(const FontAtlasKey& key) {
        auto it = this->find(key);
        if (it != this->end()) {
            return it->second;
        }
        // create new FontAtlas
        uint32_t atlas_size = std::max(512u, key.size * 64);
        auto [new_it, _] =
            this->emplace(key, FontAtlas(font_face, atlas_size, atlas_size, 4, key.size, key.anti_aliased));
        return new_it->second;
    }
    auto iter() const { return std::views::all((base&)(*this)); }
    auto iter_mut() { return std::views::all((base&)(*this)); }

   private:
    FT_Face font_face = nullptr;
};
/// Map from Font asset to its FontAtlasSet
struct FontAtlasSets : private std::unordered_map<assets::AssetId<Font>, FontAtlasSet> {
    using base = std::unordered_map<assets::AssetId<Font>, FontAtlasSet>;

    FontAtlasSets()                                = default;
    FontAtlasSets(const FontAtlasSets&)            = delete;
    FontAtlasSets(FontAtlasSets&&)                 = default;
    FontAtlasSets& operator=(const FontAtlasSets&) = delete;
    FontAtlasSets& operator=(FontAtlasSets&&)      = default;

    std::optional<std::reference_wrapper<FontAtlasSet>> get_mut(const assets::AssetId<Font>& font_id) {
        auto it = find(font_id);
        if (it == end()) {
            return std::nullopt;
        }
        return std::ref(it->second);
    }
    std::optional<std::reference_wrapper<const FontAtlasSet>> get(const assets::AssetId<Font>& font_id) {
        auto it = find(font_id);
        if (it == end()) {
            return std::nullopt;
        }
        return std::cref(it->second);
    }
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
                                                 ResMut<assets::Assets<image::Image>> images) {
        for (auto& [font_id, atlas_set] : atlas_sets->iter_mut()) {
            for (auto& [key, atlas] : atlas_set.iter_mut()) {
                atlas.apply_pending(*images);
            }
        }
    }
    static void add_font_atlas_set(ResMut<FontAtlasSets> atlas_sets,
                                   Res<FontLibrary> font_lib,
                                   EventReader<assets::AssetEvent<Font>> reader,
                                   Res<assets::Assets<Font>> fonts) {
        std::unordered_set<assets::AssetId<Font>> to_remove;
        std::unordered_set<assets::AssetId<Font>> modified;
        for (auto&& event : reader.read()) {
            if (event.is_unused()) to_remove.insert(event.id);
            if (event.is_added() || event.is_modified()) modified.insert(event.id);
        }
        for (const auto& id : to_remove) {
            atlas_sets->erase(id);
            modified.erase(id);
        }
        for (const auto& id : modified) {
            // create FT_Face for the font
            auto&& font = fonts->get(id);
            if (!font) continue;
            FT_Face face;
            if (FT_New_Memory_Face(font_lib->library, reinterpret_cast<const FT_Byte*>(font->data.get()),
                                   static_cast<FT_Long>(font->size), 0, &face)) {
                spdlog::error("[font] Failed to create FreeType face for font asset id {}", id.to_string_short());
                continue;
            }
            if (atlas_sets->contains(id)) {
                atlas_sets->erase(id);
            }
            atlas_sets->emplace(id, FontAtlasSet(face));
        }
    }
    void build(App& app) {
        app.plugin_mut<assets::AssetPlugin>().register_asset<Font>().register_loader(FontLoader{});
        app.world_mut().init_resource<FontLibrary>();
        app.world_mut().init_resource<FontAtlasSets>();
        // apply pending may modify images, and we want extract asset to react to this change immediately
        app.configure_sets(sets(FontSystems::AddFontAtlasSet, FontSystems::ApplyPendingFontAtlasUpdates));
        app.add_systems(
            First,
            into(add_font_atlas_set).in_set(FontSystems::AddFontAtlasSet).after(assets::AssetSystems::WriteEvents));
        app.add_systems(Last, into(apply_pending_font_atlas_updates)
                                  .in_set(FontSystems::ApplyPendingFontAtlasUpdates)
                                  .before(assets::AssetSystems::WriteEvents));
    }
};
}  // namespace epix::text::font