/**
 * @file epix.sprite.cppm
 * @brief C++20 module interface for the sprite rendering system.
 *
 * This module provides sprite rendering functionality.
 */
module;

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

// Third-party headers
#include <nvrhi/nvrhi.h>
#include <glm/glm.hpp>

export module epix.sprite;

export import epix.core;
export import epix.assets;
export import epix.render;
export import epix.transform;
export import epix.image;

export namespace epix::sprite {

/**
 * @brief Sprite component with rendering parameters.
 */
struct Sprite {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    bool flip_x = false;
    bool flip_y = false;
    std::optional<glm::vec4> uv_rect;  // (u_min, v_min, u_max, v_max), if not set, use full image
    std::optional<glm::vec2> size;     // if not set, use image size
    glm::vec2 anchor{0.f, 0.f};        // (0,0) is center, (-0.5,-0.5) is bottom-left, (0.5,0.5) is top-right
};

/**
 * @brief Extracted sprite data for rendering.
 */
struct ExtractedSprite {
    Sprite sprite;
    epix::transform::GlobalTransform transform;
    epix::assets::AssetId<epix::image::Image> texture;
};

/**
 * @brief Sprite pipeline resources.
 */
struct SpritePipeline {
    epix::render::RenderPipelineId pipeline_id;
    nvrhi::BindingLayoutHandle image_layout;
    nvrhi::BindingLayoutHandle uniform_layout;

    static SpritePipeline from_world(epix::core::World& world);
};

/**
 * @brief Plugin for sprite shader setup.
 */
struct SpriteShadersPlugin {
    void build(epix::core::App& app);
};

/**
 * @brief View uniform buffer.
 */
struct ViewUniform {
    nvrhi::BufferHandle view_buffer;
};

/**
 * @brief Cache for view uniforms.
 */
struct ViewUniformCache {
    std::deque<ViewUniform> cache;
};

/**
 * @brief Vertex buffer resources for sprites.
 */
struct VertexBuffers {
    nvrhi::BufferHandle position_buffer;
    nvrhi::BufferHandle texcoord_buffer;
    nvrhi::BufferHandle index_buffer;

    static VertexBuffers from_world(epix::core::World& world);
};

/**
 * @brief Instance data for sprite rendering.
 */
struct SpriteInstanceData {
    glm::mat4 model;
    glm::vec4 uv_offset_scale;   // (u_offset, v_offset, u_scale, v_scale)
    glm::vec4 color;
    glm::vec4 pos_offset_scale;  // (x_offset, y_offset, x_scale, y_scale)
};

/**
 * @brief Instance buffer for sprite batching.
 */
struct SpriteInstanceBuffer {
   private:
    std::vector<SpriteInstanceData> data;
    nvrhi::BufferHandle buffer;

   public:
    void clear() { data.clear(); }
    size_t push(const SpriteInstanceData& instance) {
        data.push_back(instance);
        return data.size() - 1;
    }
    size_t size() const { return data.size(); }
    nvrhi::BufferHandle handle() const { return buffer; }
    void upload(nvrhi::DeviceHandle device, nvrhi::CommandListHandle cmd_list);
    void upload(nvrhi::DeviceHandle device);
};

/**
 * @brief Sprite batch for rendering.
 */
struct SpriteBatch {
    nvrhi::BindingSetHandle binding_set;
    uint32_t instance_start;
};

/**
 * @brief Default sampler settings.
 */
struct DefaultSampler {
    nvrhi::SamplerHandle handle;
    nvrhi::SamplerDesc desc;
};

/**
 * @brief Plugin for default sampler.
 */
struct DefaultSamplerPlugin {
    static bool desc_equal(const nvrhi::SamplerDesc& a, const nvrhi::SamplerDesc& b);
    void finish(epix::core::App& app);
};

/**
 * @brief Plugin for the sprite system.
 */
struct SpritePlugin {
    void build(epix::core::App& app);
    void finish(epix::core::App& app);
};

/**
 * @brief Bundle for creating sprite entities.
 */
struct SpriteBundle {
    Sprite sprite;
    epix::transform::Transform transform;
    epix::assets::Handle<epix::image::Image> texture;
};

}  // namespace epix::sprite
