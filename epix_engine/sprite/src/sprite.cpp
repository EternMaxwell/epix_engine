#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <filesystem>

#include "epix/sprite.h"
#include "shaders/fragment_shader.h"
#include "shaders/vertex_shader.h"

using namespace epix::sprite;
using namespace epix::sprite::components;
using namespace epix::sprite::resources;
using namespace epix::sprite::systems;
using namespace epix::prelude;
using namespace epix::render_vk::components;

static std::shared_ptr<spdlog::logger> sprite_logger =
    spdlog::default_logger()->clone("sprite");

EPIX_API void systems::insert_sprite_server_vk(Command cmd) {
    cmd.insert_resource(SpriteServerVK{});
}

EPIX_API Entity
SpriteServerVK::load_image(Command& cmd, const std::string& _path) {
    std::string path = std::filesystem::absolute(_path).string();
    auto image       = images.find(path);
    if (image != images.end()) {
        return image->second;
    } else {
        auto image_view = cmd.spawn(ImageLoading{path}).id();
        images[path]    = image_view;
        return image_view;
    }
}

EPIX_API Entity SpriteServerVK::create_sampler(
    Command& cmd, vk::SamplerCreateInfo create_info, const std::string& name
) {
    auto sampler = samplers.find(name);
    if (sampler != samplers.end()) {
        return sampler->second;
    } else {
        auto sampler_handle =
            cmd.spawn(SamplerCreating{create_info, name}).id();
        samplers[name] = sampler_handle;
        return sampler_handle;
    }
}

EPIX_API Entity SpriteServerVK::get_sampler(const std::string& name) {
    auto sampler = samplers.find(name);
    if (sampler != samplers.end()) {
        return sampler->second;
    } else {
        return {};
    }
}

EPIX_API void systems::loading_actual_image(
    Command cmd,
    Query<Get<Entity, ImageLoading>> query,
    Query<Get<Device, CommandPool, Queue>, With<RenderContext>> ctx_query,
    ResMut<SpriteServerVK> sprite_server
) {
    if (!query) return;
    if (!ctx_query) return;
    auto [device, command_pool, queue] = ctx_query.single();
    for (auto [entity, image_loading] : query.iter()) {
        sprite_logger->debug("loading image at path: {}", image_loading.path);
        int width, height, channels;
        stbi_set_flip_vertically_on_load(true);
        uint8_t* pixels = stbi_load(
            image_loading.path.c_str(), &width, &height, &channels,
            STBI_rgb_alpha
        );
        if (!pixels) {
            sprite_logger->error(
                "Failed to load image at path: {}. This may lead to sprite "
                "being "
                "ignored or using other images if an new image was loaded to "
                "the "
                "entity the handle points to.",
                image_loading.path
            );
            cmd.entity(entity).despawn();
            return;
        }
        Buffer staging_buffer = Buffer::create_host(
            device, width * height * 4, vk::BufferUsageFlagBits::eTransferSrc
        );
        void* data = staging_buffer.map(device);
        memcpy(data, pixels, width * height * 4);
        staging_buffer.unmap(device);
        Image image = Image::create_2d(
            device, width, height, 1, 1, vk::Format::eR8G8B8A8Srgb,
            vk::ImageUsageFlagBits::eTransferDst |
                vk::ImageUsageFlagBits::eSampled
        );
        ImageView image_view = ImageView::create_2d(
            device, image, vk::Format::eR8G8B8A8Srgb,
            vk::ImageAspectFlagBits::eColor
        );
        auto command_buffer =
            CommandBuffer::allocate_primary(device, command_pool);
        command_buffer->begin(vk::CommandBufferBeginInfo());
        vk::ImageMemoryBarrier image_transition_undefined_to_transfer_dst;
        image_transition_undefined_to_transfer_dst
            .setSrcAccessMask(vk::AccessFlagBits::eNoneKHR)
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
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
            );
        command_buffer->pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags(), {}, {},
            {image_transition_undefined_to_transfer_dst}
        );
        vk::BufferImageCopy buffer_image_copy;
        buffer_image_copy.setBufferOffset(0)
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
            .setImageExtent({(uint32_t)width, (uint32_t)height, 1});
        command_buffer->copyBufferToImage(
            staging_buffer.buffer, image.image,
            vk::ImageLayout::eTransferDstOptimal, {buffer_image_copy}
        );
        vk::ImageMemoryBarrier image_transition_transfer_dst_to_shader_read;
        image_transition_transfer_dst_to_shader_read
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
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
            );
        command_buffer->pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags(),
            {}, {}, {image_transition_transfer_dst_to_shader_read}
        );
        command_buffer->end();
        vk::SubmitInfo submit_info;
        submit_info.setCommandBufferCount(1).setPCommandBuffers(
            &command_buffer.command_buffer
        );
        queue->submit(submit_info, nullptr);
        queue->waitIdle();
        staging_buffer.destroy(device);
        cmd.entity(entity).erase<ImageLoading>();
        ImageIndex image_index;
        if (sprite_server->free_image_indices.empty()) {
            image_index.index = sprite_server->next_image_index;
            sprite_server->next_image_index++;
        } else {
            image_index.index = sprite_server->free_image_indices.front();
            sprite_server->free_image_indices.pop_front();
        }
        ImageSize image_size{width, height};
        cmd.entity(entity).emplace(image, image_view, image_index, image_size);
        auto id = cmd.spawn(ImageBindingUpdate{.image_view{entity}});
        cmd.entity(id).despawn();  // despawn the update entity this frame, so
                                   // that the image is not updated again and
                                   // user will not need to despawn it manually
        sprite_logger->debug("image loaded at path: {}", image_loading.path);
    }
}

EPIX_API void systems::creating_actual_sampler(
    Command cmd,
    Query<Get<Entity, SamplerCreating>> query,
    Query<Get<Device>, With<RenderContext>> ctx_query,
    ResMut<SpriteServerVK> sprite_server
) {
    if (!ctx_query) return;
    auto [device] = ctx_query.single();
    for (auto [entity, sampler_creating] : query.iter()) {
        sprite_logger->debug(
            "creating sampler with name: {}", sampler_creating.name
        );
        auto sampler = Sampler::create(device, sampler_creating.create_info);
        SamplerIndex sampler_index;
        if (sprite_server->free_sampler_indices.empty()) {
            sampler_index.index = sprite_server->next_sampler_index;
            sprite_server->next_sampler_index++;
        } else {
            sampler_index.index = sprite_server->free_sampler_indices.front();
            sprite_server->free_sampler_indices.pop_front();
        }
        cmd.entity(entity).emplace(sampler, sampler_index);
        cmd.entity(entity).erase<SamplerCreating>();
        auto id = cmd.spawn(SamplerBindingUpdate{.sampler{entity}});
        cmd.entity(id).despawn();  // despawn the update entity this frame, so
                                   // that the sampler is not updated again and
                                   // user will not need to despawn it manually
        sprite_logger->debug(
            "sampler created with name: {}", sampler_creating.name
        );
    }
}

EPIX_API void systems::destroy_sprite_server_vk_images(
    ResMut<SpriteServerVK> sprite_server,
    Query<Get<Image, ImageView>, With<ImageIndex>> query,
    Query<Get<Device, CommandPool, Queue>, With<RenderContext>> ctx_query
) {
    if (!ctx_query) return;
    auto [device, command_pool, queue] = ctx_query.single();
    for (auto&& [path, entity_id] : sprite_server->images) {
        auto [image, image_view] = query.get(entity_id);
        image.destroy(device);
        image_view.destroy(device);
    }
}

EPIX_API void systems::destroy_sprite_server_vk_samplers(
    ResMut<SpriteServerVK> sprite_server,
    Query<Get<Sampler>, With<SamplerIndex>> query,
    Query<Get<Device>, With<RenderContext>> ctx_query
) {
    if (!ctx_query) return;
    auto [device] = ctx_query.single();
    for (auto&& [name, entity_id] : sprite_server->samplers) {
        auto [sampler] = query.get(entity_id);
        sampler.destroy(device);
    }
}

EPIX_API void SpritePluginVK::build(App& app) {
    app.add_system(PreStartup, insert_sprite_server_vk);
    app.add_system(Startup, create_sprite_renderer_vk);
    app.add_system(PostStartup, create_sprite_depth_vk);
    app.add_system(Prepare, loading_actual_image, creating_actual_sampler);
    app.add_system(Prepare, update_image_bindings).after(loading_actual_image);
    app.add_system(Prepare, update_sampler_bindings)
        .after(creating_actual_sampler);
    app.add_system(PreRender, update_sprite_depth_vk);
    app.add_system(Render, draw_sprite_2d_vk);
    app.add_system(
        Exit, destroy_sprite_server_vk_images,
        destroy_sprite_server_vk_samplers, destroy_sprite_depth_vk,
        destroy_sprite_renderer_vk
    );
}