#include "epix/render/debug.h"
#include "shaders/fragment_shader.h"
#include "shaders/vertex_shader.h"

using namespace epix::render::debug;

using namespace epix::render::debug::vulkan2;

EPIX_API DebugPipelines::DebugPipelines(Device device)
    : point_pipeline(device), line_pipeline(device), triangle_pipeline(device) {
    auto create_render_pass_func = [](Device& device) {
        return device.createRenderPass(
            vk::RenderPassCreateInfo()
                .setAttachments(
                    vk::AttachmentDescription()
                        .setFormat(vk::Format::eR8G8B8A8Srgb)
                        .setSamples(vk::SampleCountFlagBits::e1)
                        .setLoadOp(vk::AttachmentLoadOp::eLoad)
                        .setStoreOp(vk::AttachmentStoreOp::eStore)
                        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                        .setInitialLayout(
                            vk::ImageLayout::eColorAttachmentOptimal
                        )
                        .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal
                        )
                )
                .setSubpasses(
                    vk::SubpassDescription()
                        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
                        .setColorAttachmentCount(1)
                        .setPColorAttachments(
                            &vk::AttachmentReference()
                                 .setAttachment(0)
                                 .setLayout(
                                     vk::ImageLayout::eColorAttachmentOptimal
                                 )
                        )
                )
        );
    };
    point_pipeline.set_render_pass(create_render_pass_func);
    line_pipeline.set_render_pass(create_render_pass_func);
    triangle_pipeline.set_render_pass(create_render_pass_func);
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

EPIX_API void DebugPipelines::create() {
    point_pipeline.create();
    line_pipeline.create();
    triangle_pipeline.create();
}

EPIX_API void DebugPipelines::destroy() {
    point_pipeline.destroy();
    line_pipeline.destroy();
    triangle_pipeline.destroy();
}