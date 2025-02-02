#pragma once

#include <epix/app.h>
#include <epix/render_vk.h>

#include <glm/glm.hpp>

namespace epix {
namespace sprite {
namespace components {
using namespace prelude;
using namespace render_vk::components;

struct Sprite {
    Entity image;
    Entity sampler;
    glm::vec2 size;
    glm::vec2 center = {0.5f, 0.5f};
    glm::vec4 color  = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct SpritePos2D {
    glm::vec3 pos;
    float rotation  = 0.0f;
    glm::vec2 scale = {1.0f, 1.0f};
};

struct SpriteVertex {
    glm::vec3 pos;
    glm::vec2 tex_coord;
    glm::vec4 color;
    int model_index;
    int image_index;
    int sampler_index;
};

struct ImageIndex {
    int index;
};

struct ImageLoading {
    std::string path;
};

struct ImageSize {
    int width;
    int height;
};

struct SamplerIndex {
    int index;
};

struct ImageBindingUpdate {
    Entity image_view;
};

struct SamplerBindingUpdate {
    Entity sampler;
};

struct SamplerCreating {
    vk::SamplerCreateInfo create_info;
    std::string name;
};

struct SpriteRenderer {
    DescriptorSetLayout sprite_descriptor_set_layout;
    DescriptorPool sprite_descriptor_pool;
    DescriptorSet sprite_descriptor_set;

    RenderPass sprite_render_pass;
    PipelineLayout sprite_pipeline_layout;
    Pipeline sprite_pipeline;

    Buffer sprite_uniform_buffer;
    Buffer sprite_vertex_buffer;
    Buffer sprite_index_buffer;
    Buffer sprite_model_buffer;

    Fence fence;
    CommandBuffer command_buffer;
    Framebuffer frame_buffer;
};

struct SpriteDepth {};

struct SpriteDepthExtent {
    uint32_t width;
    uint32_t height;
};
}  // namespace components
}  // namespace sprite
}  // namespace epix