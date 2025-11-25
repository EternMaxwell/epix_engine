#pragma once

#include <freetype/freetype.h>

#include <array>
#include <epix/assets.hpp>
#include <epix/core.hpp>
#include <epix/image.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>

namespace epix::text::font {
struct Font {
    std::unique_ptr<std::byte[]> data;
};
struct FontLoader {
    auto& extensions() const {
        static const auto exts = std::array{".ttf", ".otf", ".woff", ".woff2"};
        return exts;
    }
    Font load(const std::filesystem::path& path, epix::assets::LoadContext& context) const {
        auto file_size = std::filesystem::file_size(path);
        auto buffer    = std::make_unique<std::byte[]>(file_size);
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open font file: " + path.string());
        }
        file.read(reinterpret_cast<char*>(buffer.get()), file_size);
        return Font{std::move(buffer)};
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
struct FontAtlas {
    /// The image where the font glyphs are stored
    assets::Handle<image::Image> image;
    /// Mapping from character code to its position in the atlas
    std::unordered_map<char32_t, image::Rect> char_map;
};
struct FontAtlasKey {
    uint32_t size;  // binary representation of font size(float)
    bool anti_aliased;
};
struct TextFont {
    assets::Handle<Font> font;
    float size               = 16.0f;
    float line_height        = 1.2f;
    bool relative_height : 1 = true;
    bool anti_aliased : 1    = true;

    operator FontAtlasKey() const { return FontAtlasKey{*reinterpret_cast<const uint32_t*>(&size), anti_aliased}; }
};
/// Map from Font detail to its FontAtlas, since same font face can still have different sizes/styles
struct FontAtlasSet : std::unordered_map<FontAtlasKey, FontAtlas> {};
/// Map from Font asset to its FontAtlasSet
struct FontAtlasSets : std::unordered_map<assets::AssetId<Font>, FontAtlasSet> {};
struct FontPlugin {
    void build(App& app) {
        app.plugin_mut<assets::AssetPlugin>().register_asset<Font>().register_loader(FontLoader{});
        app.world_mut().init_resource<FontLibrary>();
    }
};
}  // namespace epix::text::font