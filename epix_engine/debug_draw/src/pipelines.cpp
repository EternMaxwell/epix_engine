#include "epix/render/debug.h"

using namespace epix::render::debug;

EPIX_API void vulkan2::PointPipeline::create() {
    create_descriptor_set_layout();
    create_pipeline_layout();
    create_descriptor_pool();
    create_render_pass();
    create_pipeline(vk::PrimitiveTopology::ePointList);
}

EPIX_API void vulkan2::PointPipeline::destroy() {
    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipeline_layout);
    device.destroyDescriptorSetLayout(descriptor_set_layout);
    device.destroyDescriptorPool(descriptor_pool);
    device.destroyRenderPass(render_pass);
}

EPIX_API void vulkan2::LinePipeline::create() {
    create_descriptor_set_layout();
    create_pipeline_layout();
    create_descriptor_pool();
    create_render_pass();
    create_pipeline(vk::PrimitiveTopology::eLineList);
}

EPIX_API void vulkan2::LinePipeline::destroy() {
    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipeline_layout);
    device.destroyDescriptorSetLayout(descriptor_set_layout);
    device.destroyDescriptorPool(descriptor_pool);
    device.destroyRenderPass(render_pass);
}

EPIX_API void vulkan2::TrianglePipeline::create() {
    create_descriptor_set_layout();
    create_pipeline_layout();
    create_descriptor_pool();
    create_render_pass();
    create_pipeline(vk::PrimitiveTopology::eTriangleList);
}

EPIX_API void vulkan2::TrianglePipeline::destroy() {
    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipeline_layout);
    device.destroyDescriptorSetLayout(descriptor_set_layout);
    device.destroyDescriptorPool(descriptor_pool);
    device.destroyRenderPass(render_pass);
}