#pragma once

#include <epix/rdvk.h>

#include "sprite/components.h"
#include "sprite/resources.h"
#include "sprite/systems.h"

namespace epix {
namespace sprite {
using namespace prelude;
using namespace render_vk::components;
using namespace sprite::components;
using namespace sprite::resources;
using namespace sprite::systems;

struct SpritePluginVK : Plugin {
    EPIX_API void build(App& app) override;
};

namespace vulkan2 {
using namespace epix::render::vulkan2::backend;
using namespace epix::render::vulkan2;

struct Sprite {
    std::string image_name;
    glm::vec2 size;
    glm::vec2 center = {0.5f, 0.5f};
};

struct SpriteVertex {
    glm::vec3 pos;
    glm::vec2 tex_coord;
    glm::vec4 color;
    glm::mat4 model;
    int image_index;
    int sampler_index;
};

struct SpriteMesh : public Mesh<SpriteVertex> {
    int smpler_id = -1;
    EPIX_API SpriteMesh() : Mesh<SpriteVertex>(false) { set_16bit_indices(); }
    EPIX_API void set_sampler(
        const VulkanResources* res_manager, const std::string& sampler_name
    ) {
        smpler_id = res_manager->sampler_index(sampler_name);
    }
    EPIX_API void draw_sprite(
        const Sprite& sprite,
        const glm::vec3& pos,
        const glm::vec4& color,
        const glm::mat4& model,
        const VulkanResources* res_manager
    ) {
        int image_index = res_manager->image_view_index(sprite.image_name);
        if (image_index == -1) {
            spdlog::warn("Texture not found, skipping this sprite");
            return;
        }
        if (smpler_id == -1) {
            spdlog::warn("Sampler not set, skipping this sprite");
            return;
        }
        int sampler_index = smpler_id;
        emplace_vertex(
            glm::vec3(-sprite.size * sprite.center, 0.0f) + glm::vec3(pos),
            glm::vec2{0.0f, 0.0f}, color, model, image_index, sampler_index
        );
        emplace_vertex(
            glm::vec3(
                sprite.size.x * (1.0f - sprite.center.x),
                -sprite.size.y * sprite.center.y, 0.0f
            ) + glm::vec3(pos),
            glm::vec2{1.0f, 0.0f}, color, model, image_index, sampler_index
        );
        emplace_vertex(
            glm::vec3(
                sprite.size.x * (1.0f - sprite.center.x),
                sprite.size.y * (1.0f - sprite.center.y), 0.0f
            ) + glm::vec3(pos),
            glm::vec2{1.0f, 1.0f}, color, model, image_index, sampler_index
        );
        emplace_vertex(
            glm::vec3(
                -sprite.size.x * sprite.center.x,
                sprite.size.y * (1.0f - sprite.center.y), 0.0f
            ) + glm::vec3(pos),
            glm::vec2{0.0f, 1.0f}, color, model, image_index, sampler_index
        );
        uint32_t current_index = vertex_count().value() - 1;
        emplace_index(current_index - 3);
        emplace_index(current_index - 2);
        emplace_index(current_index - 1);
        emplace_index(current_index - 1);
        emplace_index(current_index);
        emplace_index(current_index - 3);
    }
    EPIX_API void draw_sprite(
        const Sprite& sprite,
        const glm::vec3& pos,
        const glm::vec4& color,
        const glm::mat4& model,
        Res<VulkanResources> res_manager
    ) {
        draw_sprite(sprite, pos, color, model, res_manager.get());
    }
};

using SpriteStagingMesh = StagingMesh<Mesh<SpriteVertex>>;
using SpriteGPUMesh     = GPUMesh<SpriteStagingMesh>;

struct SpritePipeline {
    EPIX_API static PipelineBase* create();
};
}  // namespace vulkan2
}  // namespace sprite
}  // namespace epix