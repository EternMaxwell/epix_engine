/**
 * @file epix.sprite.cppm
 * @brief Sprite module for 2D sprite rendering
 */

export module epix.sprite;

#include <epix/core.hpp>
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>
#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

export namespace epix::sprite {
    // Sprite component
    struct Sprite {
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        bool flip_x = false;
        bool flip_y = false;
        std::optional<glm::vec4> uv_rect;
        std::optional<glm::vec2> size;
        glm::vec2 anchor{0.f, 0.f};
    };
    
    // Extracted sprite for rendering
    struct ExtractedSprite {
        Sprite sprite;
        epix::transform::GlobalTransform transform;
        epix::assets::AssetId<epix::image::Image> texture;
    };
    
    // Sprite pipeline
    struct SpritePipeline {
        epix::render::RenderPipelineId pipeline_id;
        nvrhi::BindingLayoutHandle image_layout;
        nvrhi::BindingLayoutHandle uniform_layout;

        static SpritePipeline from_world(epix::World& world);
    };
    
    // View uniform
    struct ViewUniform {
        nvrhi::BufferHandle view_buffer;
    };
    
    struct ViewUniformCache {
        std::deque<ViewUniform> cache;
    };
    
    // Vertex buffers
    struct VertexBuffers {
        nvrhi::BufferHandle position_buffer;
        nvrhi::BufferHandle texcoord_buffer;
        nvrhi::BufferHandle index_buffer;

        static VertexBuffers from_world(epix::World& world);
    };
    
    // Sprite instance data
    struct SpriteInstanceData {
        glm::mat4 model;
        glm::vec4 uv_offset_scale;
        glm::vec4 color;
        glm::vec4 pos_offset_scale;
    };
    
    struct SpriteInstanceBuffer {
       private:
        std::vector<SpriteInstanceData> data;
        nvrhi::BufferHandle buffer;

       public:
        void clear();
        size_t push(const SpriteInstanceData& instance);
        size_t size() const;
        nvrhi::BufferHandle handle() const;
        void upload(nvrhi::DeviceHandle device, nvrhi::CommandListHandle cmd_list);
        void upload(nvrhi::DeviceHandle device);
    };
    
    // Sprite batch
    struct SpriteBatch {
        nvrhi::BindingSetHandle binding_set;
        uint32_t instance_start;
    };
    
    // Default sampler
    struct DefaultSampler {
        nvrhi::SamplerHandle handle;
        nvrhi::SamplerDesc desc;
    };
    
    // Sprite plugins
    struct SpriteShadersPlugin {
        void build(epix::App& app);
    };
    
}  // namespace epix::sprite
