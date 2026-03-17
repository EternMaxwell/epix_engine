module;

#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#include <spdlog/spdlog.h>

#include <fstream>

module epix.text;

import epix.image;
import epix.mesh;
import webgpu;
import std;

using namespace text::font;

namespace {
void apply_pending_font_atlas_updates(core::ResMut<FontAtlasSets> atlas_sets,
                                      core::ResMut<assets::Assets<image::Image>> images);
void add_font_atlas_set(core::ResMut<FontAtlasSets> atlas_sets,
                        core::Res<FontLibrary> font_lib,
                        core::EventReader<assets::AssetEvent<Font>> reader,
                        core::Res<assets::Assets<Font>> fonts);
}  // namespace

std::span<const char* const> FontLoader::extensions() {
    static const auto exts = std::array{".ttf", ".otf", ".woff", ".woff2"};
    return exts;
}

Font FontLoader::load(const std::filesystem::path& path, assets::LoadContext&) {
    auto file_size = std::filesystem::file_size(path);
    auto buffer    = std::make_unique<std::byte[]>(file_size);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open font file: " + path.string());
    }
    file.read(reinterpret_cast<char*>(buffer.get()), static_cast<std::streamsize>(file_size));
    return Font{std::move(buffer), static_cast<std::size_t>(file_size)};
}

FontLibrary::FontLibrary() {
    if (FT_Init_FreeType(reinterpret_cast<FT_Library*>(&library))) {
        throw std::runtime_error("Failed to initialize FreeType library");
    }
}

FontLibrary::~FontLibrary() { FT_Done_FreeType(reinterpret_cast<FT_Library>(library)); }

std::uint32_t FontAtlas::get_glyph_index(char32_t codepoint) {
    return static_cast<std::uint32_t>(FT_Get_Char_Index(reinterpret_cast<FT_Face>(font_face), codepoint));
}

void FontAtlas::apply_pending(assets::Assets<image::Image>& assets) {
    if (pending_glyphs.empty()) {
        return;
    }

    const auto pixel_size   = std::size_t{4};
    const auto layer_stride = static_cast<std::size_t>(atlas_width) * atlas_height * pixel_size;
    std::vector<std::byte> atlas_data(layer_stride * atlas_layers, std::byte{0});

    if (image) {
        if (auto atlas_image_ref = assets.try_get_mut(this->image.value()); atlas_image_ref) {
            auto& current      = atlas_image_ref.value().get();
            auto current_bytes = current.raw_view();
            auto copy_size     = std::min(atlas_data.size(), current_bytes.size());
            std::memcpy(atlas_data.data(), current_bytes.data(), copy_size);
        }
    }

    for (auto& pending : pending_glyphs) {
        auto it = atlas_locs.find(pending.glyph_index);
        if (it == atlas_locs.end()) {
            continue;
        }

        auto&& rect     = it->second;
        const auto* src = pending.bitmap_data.get();
        for (std::uint32_t row = 0; row < rect.height; ++row) {
            for (std::uint32_t col = 0; col < rect.width; ++col) {
                auto offset = (static_cast<std::size_t>(row) * rect.width + col) * 4;
                auto atlas_offset =
                    static_cast<std::size_t>(rect.layer) * layer_stride +
                    (static_cast<std::size_t>(rect.y + row) * atlas_width + (rect.x + col)) * pixel_size;
                std::memcpy(atlas_data.data() + atlas_offset, src + offset, pixel_size);
            }
        }
    }

    auto atlas_image =
        image::Image::create2d_array(atlas_width, atlas_height, atlas_layers, image::Format::RGBA8, atlas_data).value();
    atlas_image.set_usage(image::ImageUsage::Both);

    if (image) {
        if (auto atlas_image_ref = assets.try_get_mut(this->image.value()); atlas_image_ref) {
            atlas_image_ref.value().get() = std::move(atlas_image);
        } else {
            image = assets.emplace(std::move(atlas_image));
        }
    } else {
        image = assets.emplace(std::move(atlas_image));
    }

    pending_glyphs.clear();
}

AtlasRect FontAtlas::get_glyph_atlas_loc(std::uint32_t glyph_index) {
    if (auto it = atlas_locs.find(glyph_index); it != atlas_locs.end()) {
        return it->second;
    }
    cache_glyph_index(glyph_index);
    return atlas_locs.at(glyph_index);
}

const Glyph& FontAtlas::get_glyph(std::uint32_t glyph_index) {
    if (auto it = glyphs.find(glyph_index); it != glyphs.end()) {
        return it->second;
    }
    cache_glyph_index(glyph_index);
    return glyphs.at(glyph_index);
}

std::array<float, 5> FontAtlas::get_glyph_uv_rect(std::uint32_t glyph_index) {
    auto loc    = get_glyph_atlas_loc(glyph_index);
    float u0    = static_cast<float>(loc.x) / static_cast<float>(atlas_width);
    float v0    = static_cast<float>(loc.y) / static_cast<float>(atlas_height);
    float u1    = static_cast<float>(loc.x + loc.width) / static_cast<float>(atlas_width);
    float v1    = static_cast<float>(loc.y + loc.height) / static_cast<float>(atlas_height);
    float layer = static_cast<float>(loc.layer);
    return {u0, v0, u1, v1, layer};
}

std::expected<AtlasRect, FontAtlas::FontAtlasError> FontAtlas::place_char_bitmap_size(std::uint32_t width,
                                                                                      std::uint32_t height) {
    if (width == 0 || height == 0) {
        return AtlasRect::rect3d(0, 0, 0, 0, 0, 1);
    }

    constexpr std::uint32_t padding = 1;
    const std::uint32_t padded_w    = width + padding;
    const std::uint32_t padded_h    = height + padding;

    if (padded_w > atlas_width || padded_h > atlas_height) {
        return std::unexpected(
            FontAtlasError{FontAtlasError::Code::TooLargeForLayer, width, height, atlas_width, atlas_height});
    }

    for (std::uint32_t layer = 0; layer < atlas_layers; ++layer) {
        auto& state = layer_states[layer];
        if (state.x + padded_w <= atlas_width) {
            auto new_row_height = std::max(state.row_height, padded_h);
            if (state.y + new_row_height <= atlas_height) {
                auto placed_x = state.x;
                auto placed_y = state.y;
                state.x += padded_w;
                state.row_height = new_row_height;
                return AtlasRect::rect3d(placed_x, placed_y, layer, width, height, 1);
            }
        }

        if (state.y + state.row_height + padded_h <= atlas_height) {
            state.x = 0;
            state.y += state.row_height;
            state.row_height = padded_h;
            auto placed_x    = state.x;
            auto placed_y    = state.y;
            state.x += padded_w;
            return AtlasRect::rect3d(placed_x, placed_y, layer, width, height, 1);
        }
    }

    if (atlas_layers < max_texture_array_layers) {
        auto layer = atlas_layers;
        ++atlas_layers;
        layer_states.push_back(LayerState{.x = padded_w, .y = 0, .row_height = padded_h});
        return AtlasRect::rect3d(0, 0, layer, width, height, 1);
    }

    return std::unexpected(FontAtlasError{FontAtlasError::Code::AtlasFull, width, height, atlas_width, atlas_height});
}

image::Image FontAtlas::make_atlas_image() {
    auto img = image::Image::create2d_array(atlas_width, atlas_height, atlas_layers, image::Format::RGBA8);
    img.set_usage(image::ImageUsage::Both);
    return img;
}

bool FontAtlas::add_image_if_missing(assets::Assets<image::Image>& assets) {
    if (image) {
        return false;
    }
    auto atlas_image = make_atlas_image();
    image            = assets.emplace(std::move(atlas_image));
    return true;
}

std::optional<std::tuple<Glyph, std::unique_ptr<std::byte[]>, std::pair<std::uint32_t, std::uint32_t>>>
FontAtlas::generate_glyph(std::uint32_t glyph_index) {
    auto ft_face   = reinterpret_cast<FT_Face>(font_face);
    auto size_26_6 = static_cast<FT_F26Dot6>(font_size * 64.0f);
    FT_Set_Char_Size(ft_face, size_26_6, 0, 0, 0);
    if (FT_Load_Glyph(ft_face, glyph_index, FT_LOAD_RENDER)) {
        spdlog::error("[text] Failed to load glyph {}.", glyph_index);
        return std::nullopt;
    }

    FT_Glyph ft_glyph;
    if (FT_Get_Glyph(ft_face->glyph, &ft_glyph)) {
        spdlog::error("[text] Failed to get glyph {}.", glyph_index);
        return std::nullopt;
    }
    if (FT_Glyph_To_Bitmap(&ft_glyph, FT_RENDER_MODE_NORMAL, nullptr, 1)) {
        spdlog::error("[text] Failed to rasterize glyph {}.", glyph_index);
        return std::nullopt;
    }

    FT_Bitmap& bitmap    = ft_face->glyph->bitmap;
    std::uint32_t width  = static_cast<std::uint32_t>(bitmap.width);
    std::uint32_t height = static_cast<std::uint32_t>(bitmap.rows);
    auto bitmap_data     = std::make_unique<std::byte[]>(static_cast<std::size_t>(width) * height * 4);
    for (std::uint32_t row = 0; row < height; ++row) {
        const auto* src_row = reinterpret_cast<const std::uint8_t*>(bitmap.buffer) + (height - 1 - row) * bitmap.pitch;
        for (std::uint32_t col = 0; col < width; ++col) {
            auto alpha              = src_row[col];
            auto offset             = (static_cast<std::size_t>(row) * width + col) * 4;
            bitmap_data[offset + 0] = static_cast<std::byte>(255);
            bitmap_data[offset + 1] = static_cast<std::byte>(255);
            bitmap_data[offset + 2] = static_cast<std::byte>(255);
            bitmap_data[offset + 3] = static_cast<std::byte>(alpha);
        }
    }
    FT_Done_Glyph(ft_glyph);

    Glyph glyph;
    auto& metrics      = ft_face->glyph->metrics;
    glyph.width        = static_cast<float>(metrics.width) / 64.0f;
    glyph.height       = static_cast<float>(metrics.height) / 64.0f;
    glyph.horiBearingX = static_cast<float>(metrics.horiBearingX) / 64.0f;
    glyph.horiBearingY = static_cast<float>(metrics.horiBearingY) / 64.0f;
    glyph.horiAdvance  = static_cast<float>(metrics.horiAdvance) / 64.0f;
    glyph.vertBearingX = static_cast<float>(metrics.vertBearingX) / 64.0f;
    glyph.vertBearingY = static_cast<float>(metrics.vertBearingY) / 64.0f;
    glyph.vertAdvance  = static_cast<float>(metrics.vertAdvance) / 64.0f;

    return std::make_tuple(glyph, std::move(bitmap_data), std::make_pair(width, height));
}

void FontAtlas::cache_glyph_index(std::uint32_t glyph_index) {
    auto generated = generate_glyph(glyph_index);
    if (!generated) {
        atlas_locs[glyph_index] = AtlasRect::rect3d(0, 0, 0, 0, 0, 1);
        glyphs[glyph_index]     = Glyph{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        return;
    }

    auto& [glyph, bitmap_data, size] = *generated;
    auto placement                   = place_char_bitmap_size(size.first, size.second);
    if (!placement) {
        auto error = placement.error();
        spdlog::error("[text] Failed to place glyph {} into atlas {}x{}.", glyph_index, error.atlas_w, error.atlas_h);
        atlas_locs[glyph_index] = AtlasRect::rect3d(0, 0, 0, 0, 0, 1);
        glyphs[glyph_index]     = glyph;
        return;
    }

    atlas_locs[glyph_index] = *placement;
    glyphs[glyph_index]     = glyph;
    pending_glyphs.push_back(PendingGlyph{glyph_index, std::move(bitmap_data)});
}

std::optional<std::reference_wrapper<FontAtlas>> FontAtlasSet::get_mut(const FontAtlasKey& key) {
    if (auto it = storage.find(key); it != storage.end()) {
        return std::ref(it->second);
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<const FontAtlas>> FontAtlasSet::get(const FontAtlasKey& key) const {
    if (auto it = storage.find(key); it != storage.end()) {
        return std::cref(it->second);
    }
    return std::nullopt;
}

FontAtlas& FontAtlasSet::get_or_insert(const FontAtlasKey& key) {
    if (auto it = storage.find(key); it != storage.end()) {
        return it->second;
    }
    auto clamped_size = std::max(1.0f, key.size);
    auto atlas_width  = std::bit_ceil(
        std::clamp(static_cast<std::uint32_t>(std::ceil(clamped_size * 16.0f)), 1024u, max_texture_dimension_2d));
    auto atlas_height   = std::bit_ceil(std::clamp(static_cast<std::uint32_t>(std::ceil(clamped_size * 8.0f)), 512u,
                                                   std::min(2048u, max_texture_dimension_2d)));
    auto [it, inserted] = storage.emplace(
        key, FontAtlas(font_face, atlas_width, atlas_height, key.size, key.anti_aliased, max_texture_array_layers));
    return it->second;
}

std::optional<std::reference_wrapper<FontAtlasSet>> FontAtlasSets::get_mut(const assets::AssetId<Font>& font_id) {
    if (auto it = storage.find(font_id); it != storage.end()) {
        return std::ref(it->second);
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<const FontAtlasSet>> FontAtlasSets::get(
    const assets::AssetId<Font>& font_id) const {
    if (auto it = storage.find(font_id); it != storage.end()) {
        return std::cref(it->second);
    }
    return std::nullopt;
}

bool FontAtlasSets::contains(const assets::AssetId<Font>& font_id) const { return storage.contains(font_id); }

void FontAtlasSets::erase(const assets::AssetId<Font>& font_id) { storage.erase(font_id); }

void FontAtlasSets::add(const assets::AssetId<Font>& font_id, void* face) {
    storage.emplace(font_id, FontAtlasSet(face, max_texture_dimension_2d, max_texture_array_layers));
}

FontAtlasSets::FontAtlasSets(core::World& world) {
    if (auto limits = world.get_resource<wgpu::Limits>(); limits) {
        max_texture_dimension_2d = limits->get().maxTextureDimension2D;
        max_texture_array_layers = limits->get().maxTextureArrayLayers;
    }
}

namespace {
void apply_pending_font_atlas_updates(core::ResMut<FontAtlasSets> atlas_sets,
                                      core::ResMut<assets::Assets<image::Image>> images) {
    for (auto& [font_id, atlas_set] : atlas_sets->iter_mut()) {
        for (auto& [key, atlas] : atlas_set.iter_mut()) {
            atlas.apply_pending(*images);
        }
    }
}

void add_font_atlas_set(core::ResMut<FontAtlasSets> atlas_sets,
                        core::Res<FontLibrary> font_lib,
                        core::EventReader<assets::AssetEvent<Font>> reader,
                        core::Res<assets::Assets<Font>> fonts) {
    std::unordered_set<assets::AssetId<Font>> removed;
    std::unordered_set<assets::AssetId<Font>> modified;
    for (auto&& event : reader.read()) {
        if (event.is_unused()) {
            removed.insert(event.id);
        }
        if (event.is_added() || event.is_modified()) {
            modified.insert(event.id);
        }
    }

    for (const auto& id : removed) {
        atlas_sets->erase(id);
        modified.erase(id);
    }

    for (const auto& id : modified) {
        auto font_asset = fonts->get(id);
        if (!font_asset) {
            continue;
        }
        const auto& font = font_asset->get();

        FT_Face face;
        auto library = reinterpret_cast<FT_Library>(font_lib->library);
        if (FT_New_Memory_Face(library, reinterpret_cast<const FT_Byte*>(font.data.get()),
                               static_cast<FT_Long>(font.size), 0, &face)) {
            spdlog::error("[text] Failed to create FreeType face for font asset {}.", id.to_string_short());
            continue;
        }

        if (atlas_sets->contains(id)) {
            atlas_sets->erase(id);
        }
        atlas_sets->add(id, face);
    }
}
}  // namespace

void FontPlugin::build(core::App& app) {
    app.add_plugins(image::ImagePlugin{});
    app.plugin_scope(
        [](assets::AssetPlugin& asset_plugin) { asset_plugin.register_asset<Font>().register_loader<FontLoader>(); });
    app.world_mut().init_resource<FontLibrary>();
    app.world_mut().init_resource<FontAtlasSets>();
    app.configure_sets(core::sets(FontSystems::AddFontAtlasSet, FontSystems::ApplyPendingFontAtlasUpdates));
    app.add_systems(core::First, core::into(add_font_atlas_set)
                                     .in_set(FontSystems::AddFontAtlasSet)
                                     .after(assets::AssetSystems::WriteEvents)
                                     .set_name("add font atlas set"));
    app.add_systems(core::PostUpdate, core::into(apply_pending_font_atlas_updates)
                                          .in_set(FontSystems::ApplyPendingFontAtlasUpdates)
                                          .before(assets::AssetSystems::WriteEvents)
                                          .set_name("apply pending font atlas updates"));
}