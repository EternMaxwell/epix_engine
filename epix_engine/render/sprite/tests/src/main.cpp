#include <epix/input.h>
#include <epix/sprite.h>
#include <epix/window.h>

using namespace epix;
using namespace epix::sprite::vulkan2;

void create_test_texture(
    ResMut<epix::render::vulkan2::RenderContext> context,
    ResMut<epix::render::vulkan2::VulkanResources> res_manager
) {
    using namespace epix::render::vulkan2::backend;

    auto& device       = context->device;
    auto& command_pool = context->command_pool;
    auto& queue        = context->queue;
    auto image         = device.createImage(
        vk::ImageCreateInfo()
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eR8G8B8A8Srgb)
            .setExtent(vk::Extent3D(1, 1, 1))
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(
                vk::ImageUsageFlagBits::eTransferDst |
                vk::ImageUsageFlagBits::eSampled
            )
            .setSharingMode(vk::SharingMode::eExclusive),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)
    );
    auto view = device.createImageView(
        vk::ImageViewCreateInfo()
            .setImage(image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR8G8B8A8Srgb)
            .setSubresourceRange(
                vk::ImageSubresourceRange()
                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                    .setBaseMipLevel(0)
                    .setLevelCount(1)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1)
            )
    );
    auto cmd = device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo()
            .setCommandPool(command_pool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1)
    )[0];
    auto staging_buffer = device.createBuffer(
        vk::BufferCreateInfo().setSize(4).setUsage(
            vk::BufferUsageFlagBits::eTransferSrc
        ),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
            .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
    );
    auto data = (uint8_t*)staging_buffer.map();
    data[0]   = 255;
    data[1]   = 0;
    data[2]   = 0;
    data[3]   = 255;
    staging_buffer.unmap();
    cmd.begin(vk::CommandBufferBeginInfo());
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags(), {}, {},
        vk::ImageMemoryBarrier()
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcAccessMask({})
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(image)
            .setSubresourceRange(
                vk::ImageSubresourceRange()
                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                    .setBaseMipLevel(0)
                    .setLevelCount(1)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1)
            )
    );
    cmd.copyBufferToImage(
        staging_buffer, image, vk::ImageLayout::eTransferDstOptimal,
        vk::BufferImageCopy()
            .setBufferOffset(0)
            .setBufferRowLength(0)
            .setBufferImageHeight(0)
            .setImageSubresource(
                vk::ImageSubresourceLayers()
                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                    .setMipLevel(0)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1)
            )
            .setImageOffset({0, 0, 0})
            .setImageExtent({1, 1, 1})
    );
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags(), {},
        {},
        vk::ImageMemoryBarrier()
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(image)
            .setSubresourceRange(
                vk::ImageSubresourceRange()
                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                    .setBaseMipLevel(0)
                    .setLevelCount(1)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1)
            )
    );
    cmd.end();
    auto fence = device.createFence(vk::FenceCreateInfo());
    queue.submit(vk::SubmitInfo().setCommandBuffers(cmd), fence);
    device.waitForFences(fence, true, UINT64_MAX);
    device.destroyFence(fence);
    device.freeCommandBuffers(command_pool, cmd);
    staging_buffer.destroy();
    res_manager->add_image("test_image", image);
    res_manager->add_image_view("test", view);
}

void create_sampler(ResMut<epix::render::vulkan2::VulkanResources> res_manager
) {
    auto& device = res_manager->device();
    auto sampler = device.createSampler(
        vk::SamplerCreateInfo()
            .setMagFilter(vk::Filter::eLinear)
            .setMinFilter(vk::Filter::eLinear)
            .setAddressModeU(vk::SamplerAddressMode::eRepeat)
            .setAddressModeV(vk::SamplerAddressMode::eRepeat)
            .setAddressModeW(vk::SamplerAddressMode::eRepeat)
            .setAnisotropyEnable(false)
            .setMaxAnisotropy(1.0f)
            .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
            .setUnnormalizedCoordinates(false)
            .setCompareEnable(false)
            .setCompareOp(vk::CompareOp::eAlways)
            .setMipmapMode(vk::SamplerMipmapMode::eLinear)
            .setMipLodBias(0.0f)
            .setMinLod(0.0f)
            .setMaxLod(0.0f)
    );
    res_manager->add_sampler("test", sampler);
}

struct TestPassBase : PassBase {
   protected:
    TestPassBase(Device& device) : PassBase(device) {
        set_attachments(
            vk::AttachmentDescription()
                .setFormat(vk::Format::eR8G8B8A8Srgb)
                .setSamples(vk::SampleCountFlagBits::e1)
                .setLoadOp(vk::AttachmentLoadOp::eLoad)
                .setStoreOp(vk::AttachmentStoreOp::eStore)
                .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
                .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
                .setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal)
        );
        subpass_info(0)
            .set_colors(vk::AttachmentReference().setAttachment(0).setLayout(
                vk::ImageLayout::eColorAttachmentOptimal
            ))
            .set_bind_point(vk::PipelineBindPoint::eGraphics);
        create();
        add_pipeline(0, "test::sprite::pipeline", SpritePipeline::create());
    }

   public:
    static TestPassBase* create_new(Device& device) {
        return new TestPassBase(device);
    }
};

struct TestPass : Pass {
   protected:
    TestPass(const PassBase* base, CommandPool& command_pool)
        : Pass(base, command_pool, [](Pass& pass, const PassBase& base) {
              using namespace epix::render::vulkan2::backend;
              pass.add_subpass(0);
              auto create_desc_set =
                  [](Device& device, const DescriptorPool& pool,
                     const std::vector<DescriptorSetLayout>& layouts) {
                      return device.allocateDescriptorSets(
                          vk::DescriptorSetAllocateInfo()
                              .setDescriptorPool(pool)
                              .setSetLayouts(layouts[0])
                      );
                  };
              auto destroy_desc_set = [](Device& device,
                                         const DescriptorPool& pool,
                                         std::vector<DescriptorSet>& sets) {
                  device.freeDescriptorSets(pool, sets[0]);
              };
              pass.subpass_add_pipeline(
                  0, "test::sprite::pipeline", create_desc_set, destroy_desc_set
              );
          }) {}

   public:
    static TestPass* create_new(
        const TestPassBase* base, CommandPool& command_pool
    ) {
        return new TestPass(base, command_pool);
    }
};

void create_pass_base(Command cmd, Res<RenderContext> context) {
    if (!context) return;
    cmd.add_resource(TestPassBase::create_new(context->device));
}

void create_pass(
    Command cmd, ResMut<RenderContext> context, Res<TestPassBase> base
) {
    if (!context || !base) return;
    cmd.add_resource(TestPass::create_new(base.get(), context->command_pool));
}

void destroy_pass(ResMut<TestPassBase> base, ResMut<TestPass> pass) {
    if (!base || !pass) return;
    pass->destroy();
    base->destroy();
}

void create_meshes(Command cmd, Res<RenderContext> context) {
    if (!context) return;
    auto& device = context->device;
    SpriteStagingMesh mesh(device);
    cmd.insert_resource(mesh);
    SpriteGPUMesh mesh2(device);
    cmd.insert_resource(mesh2);
}

void destroy_meshes(
    ResMut<SpriteStagingMesh> staging_mesh, ResMut<SpriteGPUMesh> mesh
) {
    if (!mesh || !staging_mesh) return;
    mesh->destroy();
    staging_mesh->destroy();
}

void extract_pass(Extract<ResMut<TestPass>> pass, Command cmd) {
    if (!pass) return;
    cmd.share_resource(pass);
}

void extract_meshes(
    Extract<ResMut<SpriteStagingMesh>> smesh,
    Extract<ResMut<SpriteGPUMesh>> mesh,
    Command cmd
) {
    if (!smesh || !mesh) return;
    cmd.share_resource(mesh);
    cmd.share_resource(smesh);
}

void prepare_mesh(
    Command cmd,
    Extract<ResMut<SpriteStagingMesh>> mesh,
    Extract<Res<VulkanResources>> res_manager
) {
    Sprite sprite;
    sprite.image_name = "test";
    sprite.size       = glm::vec2(1.0f, 1.0f);
    sprite.center     = glm::vec2(0.5f, 0.5f);
    SpriteMesh ms;
    ms.set_sampler(res_manager.get(), "test");
    ms.draw_sprite(
        sprite, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
        glm::mat4(1.0f), res_manager.get()
    );
    mesh->update(ms);
}

void draw_mesh(
    Res<SpriteStagingMesh> staging_mesh,
    ResMut<SpriteGPUMesh> gpu_mesh,
    ResMut<TestPass> pass,
    ResMut<RenderContext> context,
    Res<VulkanResources> res_manager
) {
    if (!staging_mesh || !gpu_mesh || !pass || !context || !res_manager) return;
    auto& queue = context->queue;
    pass->begin(
        [&](auto& device, auto& render_pass) {
            vk::FramebufferCreateInfo framebuffer_info;
            framebuffer_info.setRenderPass(render_pass);
            framebuffer_info.setAttachments(
                context->primary_swapchain.current_image_view()
            );
            framebuffer_info.setWidth(context->primary_swapchain.extent().width
            );
            framebuffer_info.setHeight(
                context->primary_swapchain.extent().height
            );
            framebuffer_info.setLayers(1);
            return device.createFramebuffer(framebuffer_info);
        },
        context->primary_swapchain.extent()
    );
    pass->update_mesh(*gpu_mesh, *staging_mesh);
    auto& subpass = pass->next_subpass();
    subpass.activate_pipeline(
        0,
        [&](auto& viewports, auto& scissors) {
            viewports.resize(1);
            viewports[0].setWidth(context->primary_swapchain.extent().width);
            viewports[0].setHeight(context->primary_swapchain.extent().height);
            viewports[0].setMinDepth(0.0f);
            viewports[0].setMaxDepth(1.0f);
            scissors.resize(1);
            scissors[0].setExtent(context->primary_swapchain.extent());
            scissors[0].setOffset({0, 0});
        },
        [&](auto& device, auto& descriptor_sets) {
            descriptor_sets.resize(2);
            vk::DescriptorBufferInfo buffer_info;
            buffer_info.setBuffer(res_manager->get_buffer("uniform_buffer"));
            buffer_info.setOffset(0);
            buffer_info.setRange(sizeof(glm::mat4) * 2);
            vk::WriteDescriptorSet descriptor_writes =
                vk::WriteDescriptorSet()
                    .setDstSet(descriptor_sets[0])
                    .setDstBinding(0)
                    .setDstArrayElement(0)
                    .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                    .setDescriptorCount(1)
                    .setPBufferInfo(&buffer_info);
            device.updateDescriptorSets(descriptor_writes, {});
            descriptor_sets[1] = res_manager->get_descriptor_set();
        }
    );
    subpass.draw(*gpu_mesh);
    pass->end();
    pass->submit(queue);
}

void create_uniform_buffer(
    ResMut<epix::render::vulkan2::RenderContext> context,
    ResMut<epix::render::vulkan2::VulkanResources> res_manager
) {
    auto& device = context->device;
    auto buffer  = device.createBuffer(
        vk::BufferCreateInfo()
            .setSize(sizeof(glm::mat4) * 2)
            .setUsage(vk::BufferUsageFlagBits::eUniformBuffer),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
    );
    auto data = (glm::mat4*)buffer.map();
    data[0]   = glm::mat4(1.0f);
    data[1]   = glm::mat4(1.0f);
    buffer.unmap();
    res_manager->add_buffer("uniform_buffer", buffer);
}

int main() {
    App app = App::create2();
    app.add_plugin(epix::window::WindowPlugin{});
    app.get_plugin<epix::window::WindowPlugin>()->primary_desc().set_vsync(false
    );
    app.add_plugin(epix::render::vulkan2::VulkanPlugin{}.set_debug_callback(true
    ));
    app.add_plugin(epix::input::InputPlugin{});
    app.add_system(
        Startup,
        into(create_test_texture, create_sampler, create_uniform_buffer)
    );
    app.add_system(
        Startup, into(create_pass_base, create_pass, create_meshes).chain()
    );
    app.add_system(
        Extraction, into(prepare_mesh, extract_pass, extract_meshes)
    );
    app.add_system(Render, into(draw_mesh));
    app.add_system(Exit, into(destroy_pass, destroy_meshes).chain());
    app.run();
}