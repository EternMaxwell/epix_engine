#include "epix/sprite.h"
#include "shaders/fragment_shader.h"
#include "shaders/vertex_shader.h"

using namespace epix::sprite::vulkan2;

EPIX_API PipelineBase* SpritePipeline::create() {
    auto* pipeline = new PipelineBase();
    pipeline->add_shader(
        vk::ShaderStageFlagBits::eVertex, sprite_vk_vertex_spv,
        sprite_vk_vertex_spv + sizeof(sprite_vk_vertex_spv) / sizeof(uint32_t)
    );
    pipeline->add_shader(
        vk::ShaderStageFlagBits::eFragment, sprite_vk_fragment_spv,
        sprite_vk_fragment_spv +
            sizeof(sprite_vk_fragment_spv) / sizeof(uint32_t)
    );
    pipeline->set_vertex_bindings([]() {
        return std::vector<vk::VertexInputBindingDescription>{
            vk::VertexInputBindingDescription()
                .setBinding(0)
                .setStride(sizeof(SpriteVertex))
                .setInputRate(vk::VertexInputRate::eVertex),
        };
    });
    pipeline->set_default_topology(vk::PrimitiveTopology::eTriangleList);
    pipeline->set_descriptor_pool([](backend::Device& device) {
        std::vector<vk::DescriptorPoolSize> pool_sizes{
            vk::DescriptorPoolSize()
                .setType(vk::DescriptorType::eUniformBuffer)
                .setDescriptorCount(256)
        };
        return device.createDescriptorPool(
            vk::DescriptorPoolCreateInfo()
                .setMaxSets(256)
                .setPoolSizes(pool_sizes)
                .setFlags(
                    vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet |
                    vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind
                )
        );
    });
    return pipeline;
}