#include "epix/render/debug.h"
#include "shaders/fragment_shader.h"
#include "shaders/vertex_shader.h"

using namespace epix::render::debug;

using namespace epix::render::debug::vulkan2;

EPIX_API DebugPipelines::DebugPipelines()
    : point_pipeline(), line_pipeline(), triangle_pipeline() {
    auto create_descriptor_pool_func = [](Device& device) {
        vk::DescriptorPoolSize pool_size =
            vk::DescriptorPoolSize()
                .setType(vk::DescriptorType::eUniformBuffer)
                .setDescriptorCount(256);
        return device.createDescriptorPool(
            vk::DescriptorPoolCreateInfo().setMaxSets(256).setPoolSizes(
                pool_size
            )
        );
    };
    point_pipeline.set_descriptor_pool(create_descriptor_pool_func);
    line_pipeline.set_descriptor_pool(create_descriptor_pool_func);
    triangle_pipeline.set_descriptor_pool(create_descriptor_pool_func);
    std::vector<uint32_t> vertex_spv(
        debug_vk_vertex_spv,
        debug_vk_vertex_spv + sizeof(debug_vk_vertex_spv) / sizeof(uint32_t)
    );
    std::vector<uint32_t> fragment_spv(
        debug_vk_fragment_spv,
        debug_vk_fragment_spv + sizeof(debug_vk_fragment_spv) / sizeof(uint32_t)
    );
    point_pipeline.set_vertex_bindings([]() {
        return std::vector<vk::VertexInputBindingDescription>{
            vk::VertexInputBindingDescription()
                .setBinding(0)
                .setStride(sizeof(DebugVertex))
                .setInputRate(vk::VertexInputRate::eVertex),
        };
    });
    point_pipeline.add_shader(vk::ShaderStageFlagBits::eVertex, vertex_spv);
    point_pipeline.add_shader(vk::ShaderStageFlagBits::eFragment, fragment_spv);
    line_pipeline.set_vertex_bindings([]() {
        return std::vector<vk::VertexInputBindingDescription>{
            vk::VertexInputBindingDescription()
                .setBinding(0)
                .setStride(sizeof(DebugVertex))
                .setInputRate(vk::VertexInputRate::eVertex),
        };
    });
    line_pipeline.add_shader(vk::ShaderStageFlagBits::eVertex, vertex_spv);
    line_pipeline.add_shader(vk::ShaderStageFlagBits::eFragment, fragment_spv);
    triangle_pipeline.set_vertex_bindings([]() {
        return std::vector<vk::VertexInputBindingDescription>{
            vk::VertexInputBindingDescription()
                .setBinding(0)
                .setStride(sizeof(DebugVertex))
                .setInputRate(vk::VertexInputRate::eVertex),
        };
    });
    triangle_pipeline.add_shader(vk::ShaderStageFlagBits::eVertex, vertex_spv);
    triangle_pipeline.add_shader(
        vk::ShaderStageFlagBits::eFragment, fragment_spv
    );
    point_pipeline.set_default_topology(vk::PrimitiveTopology::ePointList);
    line_pipeline.set_default_topology(vk::PrimitiveTopology::eLineList);
    triangle_pipeline.set_default_topology(vk::PrimitiveTopology::eTriangleList
    );
}