#include "epix/render/pixel.h"
#include "shaders/pixel/fragment_shader.h"
#include "shaders/pixel/geometry_shader.h"
#include "shaders/pixel/vertex_shader.h"

using namespace epix::render::pixel::vulkan2;

EPIX_API PixelPipeline::PixelPipeline(Device& device)
    : epix::render::vulkan2::PipelineBase(device) {
    set_render_pass([](Device& device) {
        vk::AttachmentDescription color_attachment;
        color_attachment.setFormat(vk::Format::eR8G8B8A8Srgb);
        color_attachment.setSamples(vk::SampleCountFlagBits::e1);
        color_attachment.setLoadOp(vk::AttachmentLoadOp::eLoad);
        color_attachment.setStoreOp(vk::AttachmentStoreOp::eStore);
        color_attachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
        color_attachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
        color_attachment.setInitialLayout(
            vk::ImageLayout::eColorAttachmentOptimal
        );
        color_attachment.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal
        );
        vk::AttachmentReference color_attachment_ref;
        color_attachment_ref.setAttachment(0);
        color_attachment_ref.setLayout(vk::ImageLayout::eColorAttachmentOptimal
        );
        vk::SubpassDescription subpass;
        subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
        subpass.setColorAttachments(color_attachment_ref);
        vk::RenderPassCreateInfo render_pass_info;
        render_pass_info.setAttachments(color_attachment);
        render_pass_info.setSubpasses(subpass);
        return device.createRenderPass(render_pass_info);
    });
    set_vertex_bindings([]() {
        return std::vector<vk::VertexInputBindingDescription>{
            vk::VertexInputBindingDescription()
                .setBinding(0)
                .setStride(sizeof(PixelVertex))
                .setInputRate(vk::VertexInputRate::eVertex),
        };
    });
    add_shader(
        vk::ShaderStageFlagBits::eVertex, pixel_vk_vertex_spv,
        pixel_vk_vertex_spv + sizeof(pixel_vk_vertex_spv) / sizeof(uint32_t)
    );
    add_shader(
        vk::ShaderStageFlagBits::eFragment, pixel_vk_fragment_spv,
        pixel_vk_fragment_spv + sizeof(pixel_vk_fragment_spv) / sizeof(uint32_t)
    );
    add_shader(
        vk::ShaderStageFlagBits::eGeometry, pixel_vk_geometry_spv,
        pixel_vk_geometry_spv + sizeof(pixel_vk_geometry_spv) / sizeof(uint32_t)
    );
    set_descriptor_pool([](Device& device) {
        vk::DescriptorPoolSize pool_size =
            vk::DescriptorPoolSize()
                .setType(vk::DescriptorType::eUniformBuffer)
                .setDescriptorCount(256);
        return device.createDescriptorPool(
            vk::DescriptorPoolCreateInfo().setMaxSets(256).setPoolSizes(
                pool_size
            )
        );
    });
    set_default_topology(vk::PrimitiveTopology::ePointList);
}