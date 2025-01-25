#include "epix/sprite.h"
#include "shaders/fragment_shader.h"
#include "shaders/vertex_shader.h"

using namespace epix::sprite::vulkan2;

EPIX_API SpritePipeline::SpritePipeline(backend::Device& device)
    : PipelineBase(device) {
    set_render_pass([](backend::Device& device) {
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
        sprite_vk_fragment_spv +
            sizeof(sprite_vk_fragment_spv) / sizeof(uint32_t)
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
    set_descriptor_pool([](backend::Device& device) {
        std::vector<vk::DescriptorPoolSize> pool_sizes{
            vk::DescriptorPoolSize()
                .setType(vk::DescriptorType::eUniformBuffer)
                .setDescriptorCount(256)
        };
        return device.createDescriptorPool(
            vk::DescriptorPoolCreateInfo().setMaxSets(256).setPoolSizes(
                pool_sizes
            )
        );
    });
}

EPIX_API SpriteBatch::SpriteBatch(
    PipelineBase& pipeline, backend::CommandPool& pool
)
    : Batch(
          pipeline,
          pool,
          [](auto& device, auto& pool, auto& layouts) {
              return std::vector<backend::DescriptorSet>{
                  device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo()
                                                    .setDescriptorPool(pool)
                                                    .setSetLayouts(layouts))
              };
          },
          [](auto& device, auto& pool, auto& sets) {
              device.freeDescriptorSets(pool, sets[0]);
          }
      ) {}
EPIX_API void SpriteBatch::begin(
    std::function<vk::Framebuffer(backend::Device&, backend::RenderPass&)> func,
    vk::Extent2D extent,
    backend::Buffer uniform_buffer,
    const VulkanResources* res_manager
) {
    Batch::begin(func, extent, [&](auto& device, auto& sets) {
        sets.resize(2);
        vk::DescriptorBufferInfo buffer_info;
        buffer_info.setBuffer(uniform_buffer);
        buffer_info.setOffset(0);
        buffer_info.setRange(sizeof(glm::mat4) * 2);
        vk::WriteDescriptorSet descriptor_write;
        descriptor_write.setDstSet(sets[0]);
        descriptor_write.setDstBinding(0);
        descriptor_write.setDstArrayElement(0);
        descriptor_write.setDescriptorType(vk::DescriptorType::eUniformBuffer);
        descriptor_write.setDescriptorCount(1);
        descriptor_write.setPBufferInfo(&buffer_info);
        auto descriptor_writes = {descriptor_write};
        device.updateDescriptorSets(descriptor_writes, {});
        sets[1] = res_manager->get_descriptor_set();
    });
}
EPIX_API void SpriteBatch::begin(
    std::function<vk::Framebuffer(backend::Device&, backend::RenderPass&)> func,
    vk::Extent2D extent,
    backend::Buffer uniform_buffer,
    Res<VulkanResources> res_manager
) {
    begin(func, extent, uniform_buffer, res_manager.get());
}