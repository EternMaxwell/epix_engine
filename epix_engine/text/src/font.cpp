#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <asio/awaitable.hpp>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <epix/meta.hpp>
#include <epix/text.hpp>
#include <exception>
#include <expected>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>

#include "font_array.hpp"

using namespace epix;
using namespace epix::text::font;

namespace {
void add_font_atlas_set(ecs::ResMut<FontAtlasSets> atlas_sets,
                        ecs::Res<FontLibrary> font_lib,
                        ecs::EventReader<assets::AssetEvent<Font>> reader,
                        ecs::Res<assets::Assets<Font>> fonts);
void register_default_embedded_font(app::App& app);

/** @brief The smallest power of two strictly greater than `value`, floored at
 * one (Bevy `1u32 << (32 - value.leading_zeros())`, `font_atlas.rs:160`). */
std::uint32_t next_power_of_two_above(std::uint32_t value) noexcept {
    if (value >= (1u << 31u)) {
        return value;
    }
    return 1u << (32u - static_cast<std::uint32_t>(std::countl_zero(value)));
}

/** @brief Bevy's `FontSmoothing::None` threshold: alpha above 50% becomes
 * fully opaque, everything else fully transparent (`font_atlas.rs:212-225`). */
void apply_alpha_threshold(image::Image& glyph_texture) {
    auto bytes = glyph_texture.raw_view_mut();
    for (std::size_t index = 3; index < bytes.size(); index += 4) {
        bytes[index] = static_cast<std::byte>(std::to_integer<std::uint8_t>(bytes[index]) > 127 ? 255 : 0);
    }
}

Font make_default_embedded_font() {
    Font font{std::make_unique<std::byte[]>(font_data_array_size), static_cast<std::size_t>(font_data_array_size)};
    std::memcpy(font.data.get(), font_data_array, font.size);
    return font;
}
}  // namespace

std::span<std::string_view> FontLoader::extensions() noexcept {
    static auto exts = std::array{std::string_view{"ttf"}, std::string_view{"otf"}, std::string_view{"woff"},
                                  std::string_view{"woff2"}};
    return std::span<std::string_view>(exts.data(), exts.size());
}

STDEXEC::task<std::expected<Font, FontLoader::Error>> FontLoader::load(assets::Reader& reader,
                                                                       const Settings&,
                                                                       assets::LoadContext&) {
    try {
        std::vector<uint8_t> bytes;
        auto read_result = co_await reader.read_to_end(bytes);
        if (!read_result)
            co_return std::unexpected(std::make_exception_ptr(std::runtime_error("Failed to read font data")));
        auto buffer = std::make_unique<std::byte[]>(bytes.size());
        std::memcpy(buffer.get(), bytes.data(), bytes.size());
        co_return Font{std::move(buffer), bytes.size()};
    } catch (...) {
        co_return std::unexpected(std::current_exception());
    }
}

FontLibrary::FontLibrary() {
    if (FT_Init_FreeType(reinterpret_cast<FT_Library*>(&library))) {
        throw std::runtime_error("Failed to initialize FreeType library");
    }
}

FontLibrary::FontLibrary(FontLibrary&& other) noexcept : library(other.library) { other.library = nullptr; }

FontLibrary& FontLibrary::operator=(FontLibrary&& other) noexcept {
    if (this != &other) {
        if (library != nullptr) {
            FT_Done_FreeType(reinterpret_cast<FT_Library>(library));
        }
        library       = other.library;
        other.library = nullptr;
    }
    return *this;
}

FontLibrary::~FontLibrary() {
    if (library != nullptr) {
        FT_Done_FreeType(reinterpret_cast<FT_Library>(library));
    }
}

std::string TextError::to_string() const {
    switch (code) {
        case TextErrorCode::FailedToAddGlyph:
            return std::format("FailedToAddGlyph({})", glyph_id);
        case TextErrorCode::FailedToGetGlyphImage:
            return std::format("FailedToGetGlyphImage({})", glyph_id);
        case TextErrorCode::MissingAtlasLayout:
            return "MissingAtlasLayout";
        case TextErrorCode::MissingAtlasTexture:
            return "MissingAtlasTexture";
        case TextErrorCode::InconsistentAtlasState:
            return "InconsistentAtlasState";
    }
    return "UnknownTextError";
}

FontAtlas FontAtlas::create(assets::Assets<image::Image>& textures,
                            assets::Assets<image::TextureAtlasLayout>& atlas_layouts,
                            glm::uvec2 size,
                            bool font_smoothing) {
    // Bevy: `Image::new_fill(size, D2, &[0, 0, 0, 0], Rgba8UnormSrgb, MAIN_WORLD | RENDER_WORLD)`
    // (font_atlas.rs:40-47). The main-world usage flag is required because the
    // atlas is updated in place as more glyphs arrive.
    const std::size_t byte_size = static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y) * 4u;
    image::Image image{
        wgpu::Extent3D{size.x, size.y, 1u},
        wgpu::TextureDimension::e2D,
        std::vector<std::byte>(byte_size, std::byte{0}),
        wgpu::TextureFormat::eRGBA8UnormSrgb,
        static_cast<assets::RenderAssetUsages>(assets::RenderAssetUsages::MAIN_WORLD |
                                              assets::RenderAssetUsages::RENDER_WORLD)};
    if (!font_smoothing) {
        image.set_sampler(image::ImageSampler::nearest());
    }
    auto texture       = textures.add(std::move(image));
    auto texture_atlas = atlas_layouts.add(image::TextureAtlasLayout::new_empty(size));
    return FontAtlas{image::DynamicTextureAtlasBuilder(size, 1), std::move(texture_atlas), std::move(texture)};
}

std::expected<void, TextError> FontAtlas::add_glyph(assets::Assets<image::Image>& textures,
                                                    assets::Assets<image::TextureAtlasLayout>& atlas_layouts,
                                                    std::uint32_t key,
                                                    const image::Image& glyph_texture,
                                                    glm::ivec2 offset) {
    auto atlas_layout = atlas_layouts.get_mut(this->texture_atlas.id());
    if (!atlas_layout) {
        return std::unexpected(TextError{TextErrorCode::MissingAtlasLayout, key});
    }
    auto atlas_texture = textures.get_mut(this->texture.id());
    if (!atlas_texture) {
        return std::unexpected(TextError{TextErrorCode::MissingAtlasTexture, key});
    }

    auto glyph_index = dynamic_texture_atlas_builder.add_texture(atlas_layout->get(), glyph_texture, atlas_texture->get());
    if (!glyph_index) {
        return std::unexpected(TextError{TextErrorCode::FailedToAddGlyph, key});
    }

    glyph_to_atlas_index.insert_or_assign(key, GlyphAtlasLocation{*glyph_index, offset});
    return {};
}

std::optional<std::tuple<Glyph, image::Image, glm::ivec2>> FontAtlasSet::generate_glyph(float font_size,
                                                                                        std::uint32_t glyph_index) {
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

    auto* bitmap_glyph = reinterpret_cast<FT_BitmapGlyph>(ft_glyph);
    FT_Bitmap& bitmap  = bitmap_glyph->bitmap;
    const std::uint32_t width  = static_cast<std::uint32_t>(bitmap.width);
    const std::uint32_t height = static_cast<std::uint32_t>(bitmap.rows);
    const std::ptrdiff_t pitch = bitmap.pitch;
    const auto* buffer         = reinterpret_cast<const std::uint8_t*>(bitmap.buffer);

    // Bevy's glyph images are white RGB with the rasterized coverage in alpha,
    // stored top row first (font_atlas.rs:210-232), which is also FreeType's
    // native bitmap row order.
    std::vector<std::byte> data(static_cast<std::size_t>(width) * height * 4u);
    for (std::uint32_t row = 0; row < height; ++row) {
        const auto* src_row = pitch >= 0 ? buffer + static_cast<std::ptrdiff_t>(row) * pitch
                                         : buffer + static_cast<std::ptrdiff_t>(height - 1u - row) * -pitch;
        for (std::uint32_t col = 0; col < width; ++col) {
            auto alpha              = src_row[col];
            auto offset             = (static_cast<std::size_t>(row) * width + col) * 4;
            data[offset + 0]        = static_cast<std::byte>(255);
            data[offset + 1]        = static_cast<std::byte>(255);
            data[offset + 2]        = static_cast<std::byte>(255);
            data[offset + 3]        = static_cast<std::byte>(alpha);
        }
    }

    image::Image glyph_texture{
        wgpu::Extent3D{width, height, 1u},
        wgpu::TextureDimension::e2D,
        std::move(data),
        wgpu::TextureFormat::eRGBA8UnormSrgb,
        static_cast<assets::RenderAssetUsages>(assets::RenderAssetUsages::MAIN_WORLD)};

    // Swash (and therefore Bevy) reports the bitmap placement relative to the
    // glyph origin in a +Y-down space; FreeType's `FT_BitmapGlyph` carries the
    // same placement.
    const glm::ivec2 offset{bitmap_glyph->left, bitmap_glyph->top};

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

    FT_Done_Glyph(ft_glyph);
    return std::make_tuple(glyph, std::move(glyph_texture), offset);
}

const Glyph& FontAtlasSet::get_glyph(const FontAtlasKey& key, std::uint32_t glyph_index) {
    auto& entries = glyphs[key];
    if (auto it = entries.find(glyph_index); it != entries.end()) {
        return it->second.metrics;
    }

    auto& entry = entries[glyph_index];
    if (auto generated = generate_glyph(key.size, glyph_index)) {
        entry.metrics = std::get<0>(*generated);
    } else {
        entry.metrics = Glyph{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    }
    return entry.metrics;
}

std::expected<GlyphAtlasInfo, TextError> FontAtlasSet::add_glyph_to_atlas(
    assets::Assets<image::Image>& textures,
    assets::Assets<image::TextureAtlasLayout>& atlas_layouts,
    const FontAtlasKey& key,
    std::uint32_t glyph_index) {
    auto& entries = glyphs[key];
    if (auto it = entries.find(glyph_index); it != entries.end() && it->second.placed) {
        return it->second.atlas_info;
    }

    auto generated = generate_glyph(key.size, glyph_index);
    if (!generated) {
        return std::unexpected(TextError{TextErrorCode::FailedToGetGlyphImage, glyph_index});
    }
    auto& [metrics, glyph_texture, offset] = *generated;
    if (!key.anti_aliased) {
        apply_alpha_threshold(glyph_texture);
    }

    // Bevy tries every atlas in order and only creates a new one when none of
    // them has room (font_atlas.rs:149-178).
    auto& atlases = storage[key];
    for (auto& atlas : atlases) {
        if (atlas.add_glyph(textures, atlas_layouts, glyph_index, glyph_texture, offset)) {
            auto& entry      = entries[glyph_index];
            entry.metrics    = metrics;
            entry.atlas_info = GlyphAtlasInfo{atlas.texture.id(), atlas.texture_atlas.id(),
                                              *atlas.get_glyph_index(glyph_index)};
            entry.placed     = true;
            return entry.atlas_info;
        }
    }

    // Pick the higher of 512 or the smallest power of two greater than the
    // glyph's largest dimension (font_atlas.rs:153-167).
    const std::uint32_t glyph_max_size = std::max(glyph_texture.height(), glyph_texture.width());
    const std::uint32_t containing     = std::max(next_power_of_two_above(glyph_max_size), 512u);
    FontAtlas new_atlas = FontAtlas::create(textures, atlas_layouts, glm::uvec2{containing}, key.anti_aliased);
    if (auto added = new_atlas.add_glyph(textures, atlas_layouts, glyph_index, glyph_texture, offset); !added) {
        return std::unexpected(added.error());
    }

    auto& entry      = entries[glyph_index];
    entry.metrics    = metrics;
    entry.atlas_info = GlyphAtlasInfo{new_atlas.texture.id(), new_atlas.texture_atlas.id(),
                                      *new_atlas.get_glyph_index(glyph_index)};
    entry.placed     = true;
    const auto atlas_info = entry.atlas_info;
    atlases.push_back(std::move(new_atlas));
    return atlas_info;
}

std::optional<std::reference_wrapper<std::vector<FontAtlas>>> FontAtlasSet::get_mut(const FontAtlasKey& key) {
    if (auto it = storage.find(key); it != storage.end()) {
        return std::ref(it->second);
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<const std::vector<FontAtlas>>> FontAtlasSet::get(
    const FontAtlasKey& key) const {
    if (auto it = storage.find(key); it != storage.end()) {
        return std::cref(it->second);
    }
    return std::nullopt;
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
    storage.emplace(font_id, FontAtlasSet(face, max_texture_dimension_2d));
}

FontAtlasSets::FontAtlasSets(ecs::World& world) {
    if (auto limits = world.get_resource<wgpu::Limits>(); limits) {
        max_texture_dimension_2d = limits->get().maxTextureDimension2D;
    }
}

namespace epix::text::font {
struct DefaultFontHandle {
    assets::Handle<Font> handle;
};
}  // namespace epix::text::font

namespace {
void add_font_atlas_set(ecs::ResMut<FontAtlasSets> atlas_sets,
                        ecs::Res<FontLibrary> font_lib,
                        ecs::EventReader<assets::AssetEvent<Font>> reader,
                        ecs::Res<assets::Assets<Font>> fonts) {
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
        spdlog::debug("[text] Font atlas set created for font '{}'.", id.to_string_short());
    }
}

void register_default_embedded_font(app::App& app) {
    app.world_mut().resource_mut<assets::EmbeddedAssetRegistry>().insert_asset_static(
        "fonts/default.ttf",
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(font_data_array), font_data_array_size));
}
}  // namespace

void FontPlugin::attach(app::App& app) {
    spdlog::debug("[text] Attaching FontPlugin.");
    assets::app_register_asset<Font>(app);
    assets::app_register_loader<FontLoader>(app);
    app.world_mut().init_resource<FontLibrary>();
    app.world_mut().init_resource<FontAtlasSets>();
    app.configure_sets(ecs::sets(FontSystems::AddFontAtlasSet));
    app.add_systems(app::First, ecs::into(add_font_atlas_set)
                                    .in_set(FontSystems::AddFontAtlasSet)
                                    .after(assets::AssetSystems::WriteEvents)
                                    .set_name("add font atlas set"));
}

void FontPlugin::ready(app::App& app) {
    spdlog::debug("[text] Registering default embedded font.");
    register_default_embedded_font(app);
}
