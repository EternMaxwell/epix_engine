#include "epix/font.h"
#include "shaders/fragment_shader.h"
#include "shaders/geometry_shader.h"
#include "shaders/vertex_shader.h"

using namespace epix::font::vulkan2;

EPIX_API TextPipeline::TextPipeline() : PipelineBase() {
    set_descriptor_pool([](Device& device) {
        std::vector<vk::DescriptorPoolSize> pool_sizes = {
            vk::DescriptorPoolSize()
                .setType(vk::DescriptorType::eUniformBuffer)
                .setDescriptorCount(256)
        };
        vk::DescriptorPoolCreateInfo pool_info;
        pool_info.setPoolSizes(pool_sizes);
        pool_info.setMaxSets(256);
        pool_info.setFlags(
            vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet |
            vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind
        );
        return device.createDescriptorPool(pool_info);
    });
    set_vertex_bindings([]() {
        return std::vector<vk::VertexInputBindingDescription>{
            vk::VertexInputBindingDescription()
                .setBinding(0)
                .setStride(sizeof(TextVertex))
                .setInputRate(vk::VertexInputRate::eVertex),
        };
    });
    add_shader(
        vk::ShaderStageFlagBits::eVertex, font_vk_vertex_spv,
        font_vk_vertex_spv + sizeof(font_vk_vertex_spv) / sizeof(uint32_t)
    );
    add_shader(
        vk::ShaderStageFlagBits::eFragment, font_vk_fragment_spv,
        font_vk_fragment_spv + sizeof(font_vk_fragment_spv) / sizeof(uint32_t)
    );
    add_shader(
        vk::ShaderStageFlagBits::eGeometry, font_vk_geometry_spv,
        font_vk_geometry_spv + sizeof(font_vk_geometry_spv) / sizeof(uint32_t)
    );
    set_default_topology(vk::PrimitiveTopology::ePointList);
}