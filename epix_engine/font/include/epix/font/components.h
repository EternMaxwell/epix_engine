#pragma once

#include <epix/app.h>
#include <epix/render_ogl.h>
#include <epix/render_vk.h>
#include <freetype/freetype.h>
#include <ft2build.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <unordered_map>

#include "resources.h"

namespace epix {
namespace font {
namespace components {
using namespace prelude;
using namespace render_vk::components;
using Font = resources::Font;

struct TextUniformBuffer {
    glm::mat4 view;
    glm::mat4 proj;
};

struct Text {
    Font font;
    std::wstring text;
    float height    = 0;
    float color[4]  = {1.0f, 1.0f, 1.0f, 1.0f};
    float center[2] = {0.0f, 0.0f};
};

struct TextPos {
    int x;
    int y;
};

struct TextVertex {
    float pos[2];
    float uvs[4];
    float size[2];
    int image_index;
    int model_id;
};

struct TextModelData {
    glm::mat4 model;
    glm::vec4 color;
    alignas(16) int texture_index;
};

struct TextRenderer {
    DescriptorSetLayout text_descriptor_set_layout;
    DescriptorPool text_descriptor_pool;
    DescriptorSet text_descriptor_set;

    RenderPass text_render_pass;
    PipelineLayout text_pipeline_layout;
    Pipeline text_pipeline;

    Buffer text_uniform_buffer;
    Buffer text_model_buffer;
    Buffer text_vertex_buffer;

    Sampler text_texture_sampler;

    Fence fence;
    CommandBuffer command_buffer;
    Framebuffer framebuffer;

    struct Context {
        Device* device;
        Queue* queue;
        Swapchain* swapchain;
        CommandPool* command_pool;
        ResMut<resources::vulkan::FT2Library> ft2_library;
        TextVertex* vertices;
        TextModelData* models;
        uint32_t vertex_count = 0;
        int model_count       = 0;
    };

    std::optional<Context> ctx;

    EPIX_API void begin(
        Device* device,
        Swapchain* swapchain,
        Queue* queue,
        CommandPool* command_pool,
        ResMut<resources::vulkan::FT2Library> ft2_library
    );
    EPIX_API void reset_cmd();
    EPIX_API void flush();
    EPIX_API void setModel(const TextModelData& model);
    EPIX_API void setModel(const TextPos& pos, int texture_id);
    EPIX_API void draw(const Text& text, const TextPos& pos);
    EPIX_API void end();
};

struct TextRendererGl {
    uint32_t text_program;
    uint32_t text_vao;
    uint32_t text_vbo;
    uint32_t text_ibo;
    uint32_t text_ubo;
};
}  // namespace components
}  // namespace font
}  // namespace epix
