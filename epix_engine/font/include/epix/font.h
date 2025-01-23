#pragma once

#include "font/components.h"
#include "font/resources.h"
#include "font/systems.h"

// vulkan2
#include <epix/rdvk.h>

namespace epix {
namespace font {
using namespace prelude;
struct FontPlugin : Plugin {
    uint32_t canvas_width  = 4096;
    uint32_t canvas_height = 1024;
    EPIX_API void build(App& app) override;
};
namespace vulkan2 {
using namespace epix::render::vulkan2::backend;
using namespace epix::render::vulkan2;

struct TextVertex {
    glm::vec2 pos;
    glm::vec4 color;
    glm::vec2 uv1;
    glm::vec2 uv2;
    glm::vec2 size;
    int image_index;
    int texture_index;
    int sampler_index;
};

struct Font {
    std::string font_identifier = "default";
    int pixels                  = 64;
    bool antialias              = true;
};
struct Glyph {
    glm::uvec2 size;
    glm::ivec2 bearing;
    glm::ivec2 advance;
    int array_index;
    glm::vec2 uv_1;
    glm::vec2 uv_2;
};
static std::shared_ptr<spdlog::logger> logger =
    spdlog::default_logger()->clone("font");
struct FontAtlas {
    struct FontHash {
        EPIX_API size_t operator()(const Font& font) const;
    };
    struct FontEqual {
        EPIX_API bool operator()(const Font& lhs, const Font& rhs) const;
    };
    struct CharLoadingState {
        uint32_t current_x           = 0;
        uint32_t current_y           = 0;
        uint32_t current_layer       = 0;
        uint32_t current_line_height = 0;
    };
    struct CharAdd {
        std::vector<uint8_t> bitmap;
        glm::uvec2 size;
        glm::uvec3 pos;

        EPIX_API CharAdd(
            const FT_Bitmap& bitmap, const glm::uvec3& pos, bool antialias
        );
    };

    const uint32_t font_texture_width  = 2048;
    const uint32_t font_texture_height = 2048;
    const uint32_t font_texture_layers = 256;
    FT_Library library;
    epix::render::vulkan2::backend::Device device;
    epix::render::vulkan2::backend::CommandBuffer cmd;
    epix::render::vulkan2::backend::Fence fence;

    entt::dense_map<Font, uint32_t, FontHash, FontEqual> font_texture_index;
    entt::dense_map<Font, uint32_t, FontHash, FontEqual> font_image_index;
    entt::dense_map<Font, uint32_t, FontHash, FontEqual> texture_layers;
    uint32_t default_font_texture_index;
    entt::dense_map<Font, entt::dense_map<uint32_t, Glyph>, FontHash, FontEqual>
        glyph_maps;
    entt::dense_map<std::string, FT_Face> font_faces;
    entt::dense_map<Font, CharLoadingState, FontHash, FontEqual>
        char_loading_states;

    entt::dense_map<Font, std::vector<CharAdd>, FontHash, FontEqual>
        char_add_cache;

    mutable entt::dense_set<Font, FontHash, FontEqual> font_add_cache;
    mutable entt::dense_map<Font, std::vector<uint32_t>, FontHash, FontEqual>
        glyph_add_cache;
    mutable std::mutex font_mutex;

    EPIX_API FontAtlas(
        epix::render::vulkan2::backend::Device& device,
        epix::render::vulkan2::backend::CommandPool& command_pool,
        ResMut<VulkanResources>& res_manager
    );
    EPIX_API void destroy();
    EPIX_API FT_Face load_font(const std::string& file_path);
    EPIX_API uint32_t font_index(const Font& font) const;
    EPIX_API uint32_t char_index(const Font& font, wchar_t c);
    EPIX_API std::optional<const Glyph> get_glyph(const Font& font, wchar_t c)
        const;
    EPIX_API void apply_cache(
        epix::render::vulkan2::backend::Queue& queue,
        epix::render::vulkan2::VulkanResources& res_manager
    );
};

struct Text {
    Font font;
    std::wstring text;
    float height     = 0;
    glm::vec4 color  = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2 center = {0.0f, 0.0f};
};

struct TextMesh : public Mesh<TextVertex> {
   public:
    EPIX_API TextMesh();
    EPIX_API void draw_text(
        const Text& text,
        const glm::vec2& pos,
        const FontAtlas* font_atlas,
        const VulkanResources* res_manager
    );
    EPIX_API void draw_text(
        const Text& text,
        const glm::vec2& pos,
        Res<FontAtlas> font_atlas,
        Res<VulkanResources> res_manager
    );
};

using TextStagingMesh   = StagingMesh<Mesh<TextVertex>>;
using TextGPUMesh       = GPUMesh<Mesh<TextVertex>>;
using TextDedicatedMesh = GPUMesh<StagingMesh<Mesh<TextVertex>>>;

struct TextBatch : Batch<Mesh<TextVertex>, glm::mat4> {
    EPIX_API TextBatch(PipelineBase& pipeline, vk::CommandPool& pool);
};

struct TextPipeline : public PipelineBase {
    EPIX_API TextPipeline(Device& device);
};

namespace systems {
EPIX_API void create_pipeline(Command cmd, Res<RenderContext> context);
EPIX_API void insert_font_atlas(
    Command cmd,
    ResMut<RenderContext> context,
    ResMut<VulkanResources> res_manager
);
EPIX_API void extract_font_atlas(
    ResMut<FontAtlas> font_atlas,
    Command cmd,
    ResMut<RenderContext> context,
    ResMut<VulkanResources> res_manager
);
EPIX_API void extract_pipeline(ResMut<TextPipeline> context, Command cmd);
EPIX_API void destroy_pipeline(Command cmd, ResMut<TextPipeline> pipeline);
EPIX_API void destroy_font_atlas(ResMut<FontAtlas> font_atlas);
}  // namespace systems

}  // namespace vulkan2
}  // namespace font
}  // namespace epix