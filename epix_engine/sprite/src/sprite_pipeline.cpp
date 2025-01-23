#include "epix/sprite.h"
#include "shaders/fragment_shader.h"
#include "shaders/vertex_shader.h"


using namespace epix::sprite::vulkan2;

EPIX_API SpritePipeline::SpritePipeline(Device& device) : PipelineBase(device) {
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
    add_shader(
        vk::ShaderStageFlagBits::eVertex, sprite_vk_vertex_spv,
        sprite_vk_vertex_spv + sizeof(sprite_vk_vertex_spv) / sizeof(uint32_t)
    );
    add_shader(
        vk::ShaderStageFlagBits::eFragment, sprite_vk_fragment_spv,
        sprite_vk_fragment_spv + sizeof(sprite_vk_fragment_spv) / sizeof(uint32_t)
    );
    set_vertex_bindings([]() {
        return std::vector<vk::VertexInputBindingDescription>{
            vk::VertexInputBindingDescription()
                .setBinding(0)
                .setStride(sizeof(SpriteVertex))
                .setInputRate(vk::VertexInputRate::eVertex),
        };
    });
    set_default_topology(vk::PrimitiveTopology::eTriangleList);
}