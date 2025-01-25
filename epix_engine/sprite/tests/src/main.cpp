#include <epix/input.h>
#include <epix/sprite.h>
#include <epix/window.h>

using namespace epix;
using namespace epix::sprite::vulkan2;

void create_batch_and_mesh(
    Command cmd,
    ResMut<SpritePipeline> pipeline,
    ResMut<epix::render::vulkan2::RenderContext> context
) {
    SpriteBatch batch(*pipeline, context->command_pool);
    SpriteStagingMesh mesh(context->device);
    cmd.insert_resource(std::move(batch));
    cmd.insert_resource(std::move(mesh));
}

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

void prepare_mesh(
    Command cmd,
    ResMut<SpriteStagingMesh> mesh,
    ResMut<SpriteBatch> batch,
    Res<VulkanResources> res_manager
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

void draw_mesh(
    Command cmd,
    ResMut<SpriteBatch> batch,
    ResMut<SpriteStagingMesh> mesh,
    Res<epix::render::vulkan2::VulkanResources> res_manager,
    ResMut<epix::render::vulkan2::RenderContext> context
) {
    batch->begin(
        [&](auto& device, auto& render_pass) {
            return device.createFramebuffer(
                vk::FramebufferCreateInfo()
                    .setRenderPass(render_pass)
                    .setAttachments(
                        context->primary_swapchain.current_image_view()
                    )
                    .setWidth(context->primary_swapchain.extent().width)
                    .setHeight(context->primary_swapchain.extent().height)
                    .setLayers(1)
            );
        },
        context->primary_swapchain.extent(),
        res_manager->get_buffer("uniform_buffer"), res_manager.get()
    );
    batch->draw(*mesh);
    batch->end(context->queue);
}

void extract_mesh_batch(
    ResMut<SpriteBatch> batch, ResMut<SpriteStagingMesh> mesh, Command cmd
) {
    cmd.share_resource(batch);
    cmd.share_resource(mesh);
}

void destroy_batch_mesh(
    ResMut<epix::render::vulkan2::RenderContext> context,
    ResMut<SpriteBatch> batch,
    ResMut<SpriteStagingMesh> mesh
) {
    batch->destroy();
    mesh->destroy();
}

int main() {
    App app = App::create2();
    app.add_plugin(epix::window::WindowPlugin{});
    app.get_plugin<epix::window::WindowPlugin>()->primary_desc().set_vsync(false
    );
    app.add_plugin(epix::render::vulkan2::VulkanPlugin{}.set_debug_callback(true
    ));
    app.add_plugin(epix::input::InputPlugin{});
    app.add_plugin(epix::sprite::SpritePluginVK{});
    app.add_system(
        Startup, create_batch_and_mesh, create_test_texture, create_sampler,
        create_uniform_buffer
    );
    app.add_system(Extraction, prepare_mesh, extract_mesh_batch);
    app.add_system(Render, draw_mesh);
    app.add_system(Exit, destroy_batch_mesh);
    app.run();
}