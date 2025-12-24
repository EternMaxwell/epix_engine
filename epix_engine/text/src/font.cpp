#include <freetype/freetype.h>
#include <freetype/ftglyph.h>

#include <fstream>

#include "epix/text/font.hpp"

using namespace epix::text::font;
using namespace epix;

std::span<const char* const> FontLoader::extensions() {
    static const auto exts = std::array{".ttf", ".otf", ".woff", ".woff2"};
    return exts;
}
Font FontLoader::load(const std::filesystem::path& path, epix::assets::LoadContext& context) {
    auto file_size = std::filesystem::file_size(path);
    auto buffer    = std::make_unique<std::byte[]>(file_size);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open font file: " + path.string());
    }
    file.read(reinterpret_cast<char*>(buffer.get()), file_size);
    return Font{std::move(buffer), static_cast<size_t>(file_size)};
}

FontLibrary::FontLibrary() {
    if (FT_Init_FreeType(reinterpret_cast<FT_Library*>(&library))) {
        throw std::runtime_error("Failed to initialize FreeType library");
    }
}
FontLibrary::~FontLibrary() { FT_Done_FreeType(reinterpret_cast<FT_Library>(library)); }

uint32_t FontAtlas::get_glyph_index(char32_t codepoint) {
    return static_cast<uint32_t>(FT_Get_Char_Index(reinterpret_cast<FT_Face>(font_face), codepoint));
}
void FontAtlas::apply_pending(assets::Assets<image::Image>& assets) {
    add_image_if_missing(assets);
    if (pending_glyphs.empty()) return;
    auto& image = *assets.get_mut(this->image.value());
    if (image.get_desc().height < atlas_height) {
        auto new_image = make_atlas_image();
        // copy data from old image to new image (depth/array = 1 for 2D)
        auto res = new_image.write_data(image::Rect::rect3d(image.get_desc().width, image.get_desc().height, 1),
                                        image.get_data().data(), image.get_data().size());
        std::swap(image, new_image);
    }
    for (const auto& pending : pending_glyphs) {
        auto it = atlas_locs.find(pending.glyph_index);
        if (it == atlas_locs.end()) {
            continue;  // should not happen
        }
        const image::Rect& loc = it->second;

        // pending.bitmap_data is RGBA (4 bytes per pixel)
        auto res = image.write_data(loc, pending.bitmap_data.get(), static_cast<size_t>(loc.width) * loc.height * 4);
    }
    pending_glyphs.clear();
}
image::Rect FontAtlas::get_glyph_atlas_loc(uint32_t glyph_index) {
    auto it = atlas_locs.find(glyph_index);
    if (it != atlas_locs.end()) {
        return it->second;
    }
    cache_glyph_index(glyph_index);
    return atlas_locs.at(glyph_index);
}
const Glyph& FontAtlas::get_glyph(uint32_t glyph_index) {
    auto it = glyphs.find(glyph_index);
    if (it != glyphs.end()) {
        return it->second;
    }
    cache_glyph_index(glyph_index);
    return glyphs.at(glyph_index);
}
std::array<float, 5> FontAtlas::get_glyph_uv_rect(uint32_t glyph_index) {
    auto loc    = get_glyph_atlas_loc(glyph_index);
    float u0    = static_cast<float>(loc.x) / static_cast<float>(atlas_width);
    float v0    = static_cast<float>(loc.y) / static_cast<float>(atlas_height);
    float u1    = static_cast<float>(loc.x + loc.width) / static_cast<float>(atlas_width);
    float v1    = static_cast<float>(loc.y + loc.height) / static_cast<float>(atlas_height);
    float layer = 0.0f;  // single 2D atlas -> layer 0
    return {u0, v0, u1, v1, layer};
}
std::expected<image::Rect, FontAtlas::FontAtlasError> FontAtlas::place_char_bitmap_size(uint32_t width,
                                                                                        uint32_t height) {
    if (width == 0 || height == 0) {
        // a 0-size glyph is allowed (e.g. space); no pixels to place
        return image::Rect::rect3d(0, 0, 0, 0, 0, 1);
    }
    // reserve 1 pixel padding to avoid bleeding between glyphs
    constexpr uint32_t padding = 1;
    const uint32_t padded_w    = width + padding;
    const uint32_t padded_h    = height + padding;
    // Single 2D atlas packing. We use the single `layer_state` cursor and grow
    // the atlas height if needed (only grow in Y direction).
    auto& st = layer_state;
    // Reject glyphs wider than the atlas width (can't grow X)
    if (padded_w > atlas_width) {
        return std::unexpected(
            FontAtlasError{FontAtlasError::Code::TooLargeForLayer, width, height, atlas_width, atlas_height});
    }

    // Try to place in current row if it fits horizontally
    if (st.x + padded_w <= atlas_width) {
        // If the row height after placement fits within atlas height, place it
        uint32_t new_row_height = std::max(st.row_height, padded_h);
        if (st.y + new_row_height <= atlas_height) {
            uint32_t placed_x = st.x;
            uint32_t placed_y = st.y;
            st.x += padded_w;
            st.row_height = new_row_height;
            return image::Rect::rect3d(placed_x, placed_y, 0, width, height, 1);
        }
    }

    // Move to next row
    if (st.y + st.row_height + padded_h <= atlas_height) {
        st.x = 0;
        st.y += st.row_height;
        st.row_height     = padded_h;
        uint32_t placed_x = st.x;
        uint32_t placed_y = st.y;
        st.x += padded_w;
        return image::Rect::rect3d(placed_x, placed_y, 0, width, height, 1);
    }

    // Need to grow atlas height to fit new row. Double height until it fits (with a reasonable cap).
    const uint32_t max_height = 1u << 16;  // 65536 px cap
    uint32_t new_height       = atlas_height;
    while (new_height < (st.y + st.row_height + padded_h) && new_height < max_height) {
        new_height = std::min(new_height * 2u, max_height);
    }
    if (new_height >= (st.y + st.row_height + padded_h)) {
        // commit new height
        atlas_height = new_height;
        // place at next row
        st.x = 0;
        st.y += st.row_height;
        st.row_height     = padded_h;
        uint32_t placed_x = st.x;
        uint32_t placed_y = st.y;
        st.x += padded_w;
        return image::Rect::rect3d(placed_x, placed_y, 0, width, height, 1);
    }

    // If we couldn't grow enough, return error
    return std::unexpected(FontAtlasError{FontAtlasError::Code::AtlasFull, width, height, atlas_width, atlas_height});
}
image::Image FontAtlas::make_atlas_image() {
    return image::Image::with_desc(nvrhi::TextureDesc()
                                       .setWidth(atlas_width)
                                       .setHeight(atlas_height)
                                       .setArraySize(1)
                                       .setFormat(nvrhi::Format::RGBA8_UNORM)
                                       .setDimension(nvrhi::TextureDimension::Texture2D)
                                       .setKeepInitialState(true)
                                       .setInitialState(nvrhi::ResourceStates::ShaderResource))
        .value();
}
bool FontAtlas::add_image_if_missing(assets::Assets<image::Image>& assets) {
    if (image) return false;
    // create a new image with the atlas size and layers
    image::Image atlas_image = make_atlas_image();
    image                    = assets.emplace(std::move(atlas_image));
    return true;
}
std::optional<std::tuple<Glyph, std::unique_ptr<std::byte[]>, std::pair<uint32_t, uint32_t>>> FontAtlas::generate_glyph(
    uint32_t glyph_index) {
    auto font_face = reinterpret_cast<FT_Face>(this->font_face);
    // convert size to 26.6 format
    uint32_t font_size = static_cast<uint32_t>(this->font_size * 64.0f);
    FT_Set_Char_Size(font_face, font_size, 0, 0, 0);
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
    // Convert grayscale bitmap to RGBA (R=255,G=255,B=255,A=gray)
    const uint32_t w = static_cast<uint32_t>(bitmap.width);
    const uint32_t h = static_cast<uint32_t>(bitmap.rows);
    auto bitmap_data = std::make_unique<std::byte[]>(static_cast<size_t>(w) * h * 4);
    // bitmap.buffer rows are top-to-bottom; we want to store rows bottom-to-top in atlas
    for (uint32_t row = 0; row < h; ++row) {
        const uint8_t* src_row = reinterpret_cast<const uint8_t*>(bitmap.buffer) + (h - 1 - row) * bitmap.pitch;
        for (uint32_t col = 0; col < w; ++col) {
            uint8_t a            = src_row[col];
            size_t dst           = (static_cast<size_t>(row) * w + col) * 4;
            bitmap_data[dst + 0] = static_cast<std::byte>(255);
            bitmap_data[dst + 1] = static_cast<std::byte>(255);
            bitmap_data[dst + 2] = static_cast<std::byte>(255);
            bitmap_data[dst + 3] = static_cast<std::byte>(a);
        }
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
void FontAtlas::cache_glyph_index(uint32_t glyph_index) {
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

std::optional<std::reference_wrapper<FontAtlas>> FontAtlasSet::get_mut(const FontAtlasKey& key) {
    auto it = this->find(key);
    if (it == this->end()) {
        return std::nullopt;
    }
    return std::ref(it->second);
}
std::optional<std::reference_wrapper<const FontAtlas>> FontAtlasSet::get(const FontAtlasKey& key) const {
    auto it = this->find(key);
    if (it == this->end()) {
        return std::nullopt;
    }
    return std::cref(it->second);
}
FontAtlas& FontAtlasSet::get_or_insert(const FontAtlasKey& key) {
    auto it = this->find(key);
    if (it != this->end()) {
        return it->second;
    }
    // create new FontAtlas
    float size = *reinterpret_cast<const float*>(&key.size);
    // compute an initial atlas width biased to be large (so we only grow vertically)
    uint32_t atlas_width  = std::max(4096u, static_cast<uint32_t>(size * 64.0f * 8.0f));
    uint32_t atlas_height = std::max(512u, static_cast<uint32_t>(size * 64.0f));
    auto [new_it, _]      = this->emplace(key, FontAtlas(font_face, atlas_width, atlas_height, size, key.anti_aliased));
    return new_it->second;
}

std::optional<std::reference_wrapper<FontAtlasSet>> FontAtlasSets::get_mut(const assets::AssetId<Font>& font_id) {
    auto it = find(font_id);
    if (it == end()) {
        return std::nullopt;
    }
    return std::ref(it->second);
}
std::optional<std::reference_wrapper<const FontAtlasSet>> FontAtlasSets::get(
    const assets::AssetId<Font>& font_id) const {
    auto it = find(font_id);
    if (it == end()) {
        return std::nullopt;
    }
    return std::cref(it->second);
}

void FontPlugin::apply_pending_font_atlas_updates(ResMut<FontAtlasSets> atlas_sets,
                                                  ResMut<assets::Assets<image::Image>> images) {
    for (auto& [font_id, atlas_set] : atlas_sets->iter_mut()) {
        for (auto& [key, atlas] : atlas_set.iter_mut()) {
            atlas.apply_pending(*images);
        }
    }
}
void FontPlugin::add_font_atlas_set(ResMut<FontAtlasSets> atlas_sets,
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
        auto library = reinterpret_cast<FT_Library>(font_lib->library);
        if (FT_New_Memory_Face(library, reinterpret_cast<const FT_Byte*>(font->data.get()),
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
void FontPlugin::build(App& app) {
    app.plugin_mut<assets::AssetPlugin>().register_asset<Font>().register_loader(FontLoader{});
    app.world_mut().init_resource<FontLibrary>();
    app.world_mut().init_resource<FontAtlasSets>();
    // apply pending may modify images, and we want extract asset to react to this change immediately
    app.configure_sets(sets(FontSystems::AddFontAtlasSet, FontSystems::ApplyPendingFontAtlasUpdates));
    app.add_systems(
        First, into(add_font_atlas_set).in_set(FontSystems::AddFontAtlasSet).after(assets::AssetSystems::WriteEvents));
    app.add_systems(PostUpdate, into(apply_pending_font_atlas_updates)
                                    .in_set(FontSystems::ApplyPendingFontAtlasUpdates)
                                    .before(assets::AssetSystems::WriteEvents));
}