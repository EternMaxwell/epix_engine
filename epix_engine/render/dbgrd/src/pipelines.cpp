#include "epix/render/debug.h"
#include "shaders/fragment_shader.h"
#include "shaders/vertex_shader.h"

using namespace epix::render::debug;

using namespace epix::render::debug::vulkan2;

epix::render::vulkan2::PipelineBase* debug_base() {
    auto pipeline = new epix::render::vulkan2::PipelineBase();
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
                    vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind |
                    vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet
                )
        );
    });
    pipeline->set_vertex_bindings([]() {
        return std::vector<vk::VertexInputBindingDescription>{
            vk::VertexInputBindingDescription()
                .setBinding(0)
                .setStride(sizeof(DebugVertex))
                .setInputRate(vk::VertexInputRate::eVertex),
        };
    });
    std::vector<uint32_t> vertex_spv(
        debug_vk_vertex_spv,
        debug_vk_vertex_spv + sizeof(debug_vk_vertex_spv) / sizeof(uint32_t)
    );
    std::vector<uint32_t> fragment_spv(
        debug_vk_fragment_spv,
        debug_vk_fragment_spv + sizeof(debug_vk_fragment_spv) / sizeof(uint32_t)
    );
    pipeline->add_shader(vk::ShaderStageFlagBits::eVertex, vertex_spv);
    pipeline->add_shader(vk::ShaderStageFlagBits::eFragment, fragment_spv);
    return pipeline;
}

EPIX_API epix::render::vulkan2::PipelineBase* DebugPipelines::point_pipeline() {
    auto pipeline = debug_base();
    pipeline->set_default_topology(vk::PrimitiveTopology::ePointList);
    return pipeline;
}

EPIX_API epix::render::vulkan2::PipelineBase* DebugPipelines::line_pipeline() {
    auto pipeline = debug_base();
    pipeline->set_default_topology(vk::PrimitiveTopology::eLineList);
    return pipeline;
}

EPIX_API epix::render::vulkan2::PipelineBase* DebugPipelines::triangle_pipeline(
) {
    auto pipeline = debug_base();
    pipeline->set_default_topology(vk::PrimitiveTopology::eTriangleList);
    return pipeline;
}