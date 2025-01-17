#pragma once

#include <epix/app.h>
#include <epix/render_vk.h>
#include <freetype/freetype.h>
#include <ft2build.h>

#include <filesystem>

namespace epix::font::resources {
struct Font;
namespace tools {
struct Glyph;
struct GlyphMap;
}  // namespace tools
namespace res_ogl {
struct FT2LibGL;
}
namespace vulkan {
struct FT2Library;
}
}  // namespace epix::font::resources

template <>
struct std::hash<epix::font::resources::Font> {
    EPIX_API size_t operator()(const epix::font::resources::Font& font) const;
};

namespace epix {
namespace font {
namespace resources {
using namespace prelude;
using namespace render_vk::components;
struct Font {
    bool antialias = true;
    int pixels     = 64;
    FT_Face font_face;

    EPIX_API bool operator==(const Font& other) const {
        return font_face == other.font_face && pixels == other.pixels &&
               antialias == other.antialias;
    }
};
namespace tools {
struct Glyph {
    struct ivec2 {
        int32_t x;
        int32_t y;
    };
    struct uvec2 {
        uint32_t x;
        uint32_t y;
    };
    struct vec2 {
        float x;
        float y;
    };
    uvec2 size;
    ivec2 bearing;
    ivec2 advance;
    int image_index;
    vec2 uv_1;
    vec2 uv_2;
};
struct GlyphMap {
    EPIX_API const Glyph& get_glyph(uint32_t index) const;
    EPIX_API void add_glyph(uint32_t index, const Glyph& glyph);
    EPIX_API bool contains(uint32_t index) const;

   private:
    std::shared_ptr<std::unordered_map<uint32_t, Glyph>> glyphs =
        std::make_shared<std::unordered_map<uint32_t, Glyph>>();
};
}  // namespace tools
using namespace tools;
namespace res_ogl {
struct FT2LibGL {
    using texture_handle = uint64_t;
    using texture_id     = uint32_t;
    using buffer_id      = uint32_t;
    struct CharLoadingState {
        uint32_t current_x           = 0;
        uint32_t current_y           = 0;
        uint32_t current_layer       = 0;
        uint32_t current_line_height = 0;
    };
    EPIX_API FT_Face load_font(const std::string& file_path);
    EPIX_API void init();
    EPIX_API void destroy();
    const uint32_t font_texture_width  = 2048;
    const uint32_t font_texture_height = 2048;
    const uint32_t font_texture_layers = 256;
    buffer_id font_texture_buffer;
    entt::dense_map<std::string, FT_Face> font_faces;
    entt::dense_map<Font, texture_handle> font_texture_handles;
    entt::dense_map<Font, texture_id> font_texture_ids;
    entt::dense_map<Font, CharLoadingState> char_loading_states;
};
}  // namespace res_ogl
namespace vulkan {
struct FT2Library {
    struct CharLoadingState {
        uint32_t current_x           = 0;
        uint32_t current_y           = 0;
        uint32_t current_layer       = 0;
        uint32_t current_line_height = 0;
    };
    EPIX_API FT_Face load_font(const std::string& file_path);
    EPIX_API std::tuple<Image, ImageView, GlyphMap>& get_font_texture(
        const Font& font,
        Device& device,
        CommandPool& command_pool,
        Queue& queue
    );
    EPIX_API uint32_t font_index(const Font& font);
    EPIX_API void add_char_to_texture(
        const Font& font,
        wchar_t c,
        Device& device,
        CommandPool& command_pool,
        Queue& queue
    );
    EPIX_API void add_chars_to_texture(
        const Font& font,
        const std::wstring& text,
        Device& device,
        CommandPool& command_pool,
        Queue& queue
    );
    EPIX_API std::optional<const Glyph> get_glyph(const Font& font, wchar_t c);
    EPIX_API std::optional<const Glyph> get_glyph_add(
        const Font& font,
        wchar_t c,
        Device& device,
        CommandPool& command_pool,
        Queue& queue
    );

    EPIX_API void clear_font_textures(Device& device);
    EPIX_API void destroy();

    const uint32_t font_texture_width  = 2048;
    const uint32_t font_texture_height = 2048;
    const uint32_t font_texture_layers = 256;
    FT_Library library;
    entt::dense_map<std::string, FT_Face> font_faces;
    entt::dense_map<Font, uint32_t> font_texture_index;
    entt::dense_map<Font, std::tuple<Image, ImageView, GlyphMap>> font_textures;
    entt::dense_map<Font, CharLoadingState> char_loading_states;
    DescriptorSetLayout font_texture_descriptor_set_layout;
    DescriptorPool font_texture_descriptor_pool;
    DescriptorSet font_texture_descriptor_set;
    uint32_t font_texture_count = 0;
};
}  // namespace vulkan
}  // namespace resources
}  // namespace font
}  // namespace epix