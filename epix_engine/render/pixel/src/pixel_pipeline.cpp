#include "epix/render/pixel.h"
#include "shaders/pixel/fragment_shader.h"
#include "shaders/pixel/geometry_shader.h"
#include "shaders/pixel/vertex_shader.h"

using namespace epix::render::pixel::vulkan2;

EPIX_API epix::render::vulkan2::PipelineBase* PixelPipeline::create() {
    auto* pipeline = new epix::render::vulkan2::PipelineBase();
    pipeline->set_vertex_bindings([]() {
        return std::vector<vk::VertexInputBindingDescription>{
            vk::VertexInputBindingDescription()
                .setBinding(0)
                .setStride(sizeof(PixelVertex))
                .setInputRate(vk::VertexInputRate::eVertex),
        };
    });
    pipeline->add_shader(
        vk::ShaderStageFlagBits::eVertex, pixel_vk_vertex_spv,
        pixel_vk_vertex_spv + sizeof(pixel_vk_vertex_spv) / sizeof(uint32_t)
    );
    pipeline->add_shader(
        vk::ShaderStageFlagBits::eFragment, pixel_vk_fragment_spv,
        pixel_vk_fragment_spv + sizeof(pixel_vk_fragment_spv) / sizeof(uint32_t)
    );
    pipeline->add_shader(
        vk::ShaderStageFlagBits::eGeometry, pixel_vk_geometry_spv,
        pixel_vk_geometry_spv + sizeof(pixel_vk_geometry_spv) / sizeof(uint32_t)
    );
    pipeline->set_descriptor_pool([](Device& device) {
        vk::DescriptorPoolSize pool_size =
            vk::DescriptorPoolSize()
                .setType(vk::DescriptorType::eUniformBuffer)
                .setDescriptorCount(256);
        return device.createDescriptorPool(
            vk::DescriptorPoolCreateInfo()
                .setMaxSets(256)
                .setPoolSizes(pool_size)
                .setFlags(
                    vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet |
                    vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind
                )
        );
    });
    pipeline->set_default_topology(vk::PrimitiveTopology::ePointList);
    return pipeline;
}