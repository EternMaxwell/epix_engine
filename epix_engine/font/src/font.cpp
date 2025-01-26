#include "epix/font.h"

using namespace epix;
using namespace epix::font;
using namespace epix::font::components;
using namespace epix::font::resources::vulkan;

static std::shared_ptr<spdlog::logger> logger =
    spdlog::default_logger()->clone("font");

using namespace epix::font::resources::tools;

EPIX_API size_t std::hash<Font>::operator()(const Font& font) const {
    return (std::hash<int>()(font.pixels) ^ std::hash<bool>()(font.antialias)) *
           std::hash<FT_Face>()(font.font_face);
}

EPIX_API const Glyph& GlyphMap::get_glyph(uint32_t index) const {
    if (glyphs->find(index) == glyphs->end()) {
        throw std::runtime_error("Glyph not found");
    }
    return glyphs->at(index);
}

EPIX_API void GlyphMap::add_glyph(uint32_t index, const Glyph& glyph) {
    glyphs->insert({index, glyph});
}

EPIX_API bool GlyphMap::contains(uint32_t index) const {
    return glyphs->find(index) != glyphs->end();
}

EPIX_API FT_Face FT2Library::load_font(const std::string& file_path) {
    auto path = std::filesystem::absolute(file_path).string();
    if (font_faces.find(path) != font_faces.end()) {
        return font_faces[path];
    }
    FT_Face face;
    FT_Error error = FT_New_Face(library, path.c_str(), 0, &face);
    if (error) {
        throw std::runtime_error("Failed to load font: " + path);
    }
    font_faces[path] = face;
    return face;
}

EPIX_API std::tuple<Image, ImageView, GlyphMap>& FT2Library::get_font_texture(
    const Font& font, Device& device, CommandPool& command_pool, Queue& queue
) {
    if (font_textures.find(font) != font_textures.end()) {
        return font_textures[font];
    }

    Image image = Image::create(
        device,
        vk::ImageCreateInfo()
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eR8Uint)
            .setExtent(vk::Extent3D(font_texture_width, font_texture_height, 1))
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(
                vk::ImageUsageFlagBits::eSampled |
                vk::ImageUsageFlagBits::eTransferDst |
                vk::ImageUsageFlagBits::eTransferSrc
            )
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)
    );

    auto cmd = CommandBuffer::allocate_primary(device, command_pool);
    cmd->begin(vk::CommandBufferBeginInfo{});
    vk::ImageMemoryBarrier barrier;
    barrier.setOldLayout(vk::ImageLayout::eUndefined);
    barrier.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setImage(image);
    barrier.setSubresourceRange(
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
    );
    barrier.setSrcAccessMask({});
    barrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    cmd->pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier
    );
    cmd->end();
    vk::SubmitInfo submit_info;
    submit_info.setCommandBuffers(*cmd);
    queue->submit(submit_info, nullptr);
    queue->waitIdle();
    cmd.free(device, command_pool);

    vk::ImageViewCreateInfo image_view_info;
    image_view_info.setImage(image);
    image_view_info.setViewType(vk::ImageViewType::e2DArray);
    image_view_info.setFormat(vk::Format::eR8Uint);
    image_view_info.setSubresourceRange(
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
    );
    ImageView image_view = ImageView::create(device, image_view_info);

    GlyphMap glyph_map;

    font_textures[font] = {image, image_view, glyph_map};
    vk::DescriptorImageInfo image_info;
    image_info.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    image_info.setImageView(image_view);
    vk::WriteDescriptorSet descriptor_write;
    descriptor_write.setDstSet(*font_texture_descriptor_set);
    descriptor_write.setDstBinding(0);
    descriptor_write.setDstArrayElement(font_texture_count);
    descriptor_write.setDescriptorType(vk::DescriptorType::eSampledImage);
    descriptor_write.setDescriptorCount(1);
    descriptor_write.setPImageInfo(&image_info);
    auto descriptor_writes = {descriptor_write};
    device->updateDescriptorSets(descriptor_writes, nullptr);
    logger->info("Created font texture with index {}", font_texture_count);
    font_texture_index.emplace(font, font_texture_count);
    font_texture_count += 1;
    return font_textures[font];
}

EPIX_API uint32_t FT2Library::font_index(const Font& font) {
    if (font_texture_index.find(font) == font_texture_index.end()) {
        logger->error("Font not found, replacing it with 0");
        return 0;
    }
    return font_texture_index[font];
}

EPIX_API void FT2Library::add_char_to_texture(
    const Font& font,
    wchar_t c,
    Device& device,
    CommandPool& command_pool,
    Queue& queue
) {
    auto&& [image, image_view, glyph_map] = font_textures[font];

    FT_Face face   = font.font_face;
    uint32_t index = FT_Get_Char_Index(face, c);

    if (glyph_map.contains(index)) {
        return;
    }
    auto [current_x, current_y, current_layer, current_line_height] =
        char_loading_states[font];

    if (FT_Set_Char_Size(face, 0, font.pixels, 1024, 1024)) {
        logger->error("Failed to set char size, skipping character");
        return;
    }
    if (FT_Set_Pixel_Sizes(face, 0, font.pixels)) {
        logger->error("Failed to set pixel size, skipping character");
        return;
    }
    if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT)) {
        logger->error("Failed to load glyph, skipping character");
        return;
    }
    if (FT_Render_Glyph(
            face->glyph,
            font.antialias ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO
        )) {
        logger->error("Failed to render glyph, skipping character");
        return;
    }

    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap bitmap  = slot->bitmap;

    if (current_x + bitmap.width >= font_texture_width) {
        current_x = 0;
        current_y += current_line_height;
        current_line_height = 0;
    }

    if (current_y + bitmap.rows >= font_texture_height) {
        current_y = 0;
        current_layer += 1;
        logger->debug("Resizing font texture to {} layers", current_layer + 1);
        // resize the font texture, e.g. increase the number of layers
        vk::ImageCreateInfo image_info;
        image_info.setImageType(vk::ImageType::e2D);
        image_info.setFormat(vk::Format::eR8Uint);
        image_info.setExtent(
            vk::Extent3D(font_texture_width, font_texture_height, 1)
        );
        image_info.setMipLevels(1);
        image_info.setArrayLayers(current_layer + 1);
        image_info.setSamples(vk::SampleCountFlagBits::e1);
        image_info.setTiling(vk::ImageTiling::eOptimal);
        image_info.setUsage(
            vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eTransferDst |
            vk::ImageUsageFlagBits::eTransferSrc
        );
        image_info.setSharingMode(vk::SharingMode::eExclusive);
        image_info.setInitialLayout(vk::ImageLayout::eUndefined);
        AllocationCreateInfo alloc_info;
        alloc_info.setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        alloc_info.setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        Image new_image = Image::create(device, image_info, alloc_info);
        vk::ImageViewCreateInfo image_view_info;
        image_view_info.setImage(new_image);
        image_view_info.setViewType(vk::ImageViewType::e2DArray);
        image_view_info.setFormat(vk::Format::eR8Uint);
        image_view_info.setSubresourceRange(vk::ImageSubresourceRange(
            vk::ImageAspectFlagBits::eColor, 0, 1, 0, current_layer + 1
        ));
        auto new_image_view = ImageView::create(device, image_view_info);
        // transition
        auto cmd = CommandBuffer::allocate_primary(device, command_pool);
        cmd->begin(vk::CommandBufferBeginInfo{});
        vk::ImageMemoryBarrier barrier;
        barrier.setOldLayout(vk::ImageLayout::eUndefined);
        barrier.setNewLayout(vk::ImageLayout::eTransferDstOptimal);
        barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setImage(new_image);
        barrier.setSubresourceRange(vk::ImageSubresourceRange(
            vk::ImageAspectFlagBits::eColor, 0, 1, 0, current_layer + 1
        ));
        barrier.setSrcAccessMask({});
        barrier.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
        cmd->pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier
        );
        barrier.setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
        barrier.setNewLayout(vk::ImageLayout::eTransferSrcOptimal);
        barrier.setSrcAccessMask(vk::AccessFlagBits::eShaderRead);
        barrier.setDstAccessMask(vk::AccessFlagBits::eTransferRead);
        barrier.setSubresourceRange(vk::ImageSubresourceRange(
            vk::ImageAspectFlagBits::eColor, 0, 1, 0, current_layer
        ));
        barrier.setImage(image);
        cmd->pipelineBarrier(
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier
        );
        vk::ImageCopy region;
        region.setSrcSubresource(vk::ImageSubresourceLayers(
            vk::ImageAspectFlagBits::eColor, 0, 0, current_layer
        ));
        region.setDstSubresource(vk::ImageSubresourceLayers(
            vk::ImageAspectFlagBits::eColor, 0, 0, current_layer
        ));
        region.setExtent(
            vk::Extent3D(font_texture_width, font_texture_height, 1)
        );
        cmd->copyImage(
            image, vk::ImageLayout::eTransferSrcOptimal, new_image,
            vk::ImageLayout::eTransferDstOptimal, region
        );
        barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal);
        barrier.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
        barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
        barrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
        barrier.setSubresourceRange(vk::ImageSubresourceRange(
            vk::ImageAspectFlagBits::eColor, 0, 1, 0, current_layer + 1
        ));
        barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setImage(new_image);
        cmd->pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier
        );
        cmd->end();
        vk::SubmitInfo submit_info;
        submit_info.setCommandBuffers(*cmd);
        queue->submit(submit_info, nullptr);
        queue->waitIdle();
        cmd.free(device, command_pool);
        image.destroy(device);
        image_view.destroy(device);
        std::get<0>(font_textures[font]) = new_image;
        std::get<1>(font_textures[font]) = new_image_view;
        vk::WriteDescriptorSet descriptor_write;
        vk::DescriptorImageInfo image_desc_info;
        image_desc_info.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
        image_desc_info.setImageView(new_image_view);
        descriptor_write.setDstSet(*font_texture_descriptor_set);
        descriptor_write.setDstBinding(0);
        descriptor_write.setDstArrayElement(font_texture_index[font]);
        descriptor_write.setDescriptorType(vk::DescriptorType::eSampledImage);
        descriptor_write.setDescriptorCount(1);
        descriptor_write.setPImageInfo(&image_desc_info);
        auto descriptor_writes = {descriptor_write};
        device->updateDescriptorSets(descriptor_writes, nullptr);
    }

    if (current_layer >= font_texture_layers) {
        logger->warn("Font texture is full, skipping character");
        return;
    }

    if (bitmap.rows > current_line_height) {
        current_line_height = bitmap.rows;
    }

    if (bitmap.width > 0 && bitmap.rows > 0) {
        logger->debug(
            "Adding character with bitmap size {},{} to font texture at {}, "
            "{}, layer {}",
            bitmap.width, bitmap.rows, current_x, current_y, current_layer
        );

        Buffer staging_buffer = Buffer::create_host(
            device, bitmap.width * bitmap.rows,
            vk::BufferUsageFlagBits::eTransferSrc
        );

        uint8_t* buffer = (uint8_t*)staging_buffer.map(device);
        for (int iy = 0; iy < bitmap.rows; iy++) {
            for (int ix = 0; ix < bitmap.width; ix++) {
                buffer[ix + (bitmap.rows - iy - 1) * bitmap.width] =
                    font.antialias
                        ? bitmap.buffer[ix + iy * bitmap.pitch]
                        : ((bitmap.buffer[ix / 8 + iy * bitmap.pitch] >>
                            (7 - ix % 8)) &
                           1) *
                              255;
            }
        }
        staging_buffer.unmap(device);

        auto command_buffer =
            CommandBuffer::allocate_primary(device, command_pool);
        command_buffer->begin(vk::CommandBufferBeginInfo{});
        vk::ImageMemoryBarrier barrier1;
        barrier1.setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
        barrier1.setNewLayout(vk::ImageLayout::eTransferDstOptimal);
        barrier1.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier1.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier1.setImage(image);
        barrier1.setSubresourceRange(vk::ImageSubresourceRange(
            vk::ImageAspectFlagBits::eColor, 0, 1, current_layer, 1
        ));
        barrier1.setSrcAccessMask(vk::AccessFlagBits::eShaderRead);
        barrier1.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
        command_buffer->pipelineBarrier(
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier1
        );
        vk::BufferImageCopy region;
        region.setBufferOffset(0);
        region.setBufferRowLength(bitmap.width);
        region.setBufferImageHeight(bitmap.rows);
        region.setImageSubresource(vk::ImageSubresourceLayers(
            vk::ImageAspectFlagBits::eColor, 0, current_layer, 1
        ));
        region.setImageOffset({(int)current_x, (int)current_y, 0});
        region.setImageExtent({bitmap.width, bitmap.rows, 1});
        command_buffer->copyBufferToImage(
            staging_buffer, image, vk::ImageLayout::eTransferDstOptimal, region
        );
        vk::ImageMemoryBarrier barrier;
        barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal);
        barrier.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
        barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setImage(image);
        barrier.setSubresourceRange(vk::ImageSubresourceRange(
            vk::ImageAspectFlagBits::eColor, 0, 1, current_layer, 1
        ));
        barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
        barrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
        command_buffer->pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier
        );
        command_buffer->end();
        vk::SubmitInfo submit_info;
        submit_info.setCommandBuffers(*command_buffer);
        queue->submit(submit_info, nullptr);
        queue->waitIdle();
        command_buffer.free(device, command_pool);
        staging_buffer.destroy(device);
    }

    Glyph glyph;
    glyph.advance = {
        static_cast<int>(slot->advance.x >> 6),
        static_cast<int>(slot->advance.y >> 6)
    };
    glyph.bearing = {
        static_cast<int>(slot->metrics.horiBearingX >> 6),
        static_cast<int>(slot->metrics.horiBearingY >> 6)
    };
    glyph.size        = {bitmap.width, bitmap.rows};
    glyph.image_index = current_layer;
    glyph.uv_1        = {
        current_x / static_cast<float>(font_texture_width),
        current_y / static_cast<float>(font_texture_height)
    };
    glyph.uv_2 = {
        (current_x + bitmap.width) / static_cast<float>(font_texture_width),
        (current_y + bitmap.rows) / static_cast<float>(font_texture_height)
    };

    glyph_map.add_glyph(index, glyph);

    current_x += bitmap.width;
    char_loading_states[font] = {
        current_x, current_y, current_layer, current_line_height
    };
}

EPIX_API void FT2Library::add_chars_to_texture(
    const Font& font,
    const std::wstring& text,
    Device& device,
    CommandPool& command_pool,
    Queue& queue
) {
    for (auto c : text) {
        add_char_to_texture(font, c, device, command_pool, queue);
    }
}

EPIX_API std::optional<const Glyph> FT2Library::get_glyph(
    const Font& font, wchar_t c
) {
    if (font_textures.find(font) == font_textures.end()) {
        return std::nullopt;
    }
    auto [image, image_view, glyph_map] = font_textures[font];
    uint32_t index                      = FT_Get_Char_Index(font.font_face, c);
    if (!glyph_map.contains(index)) {
        return std::nullopt;
    }
    return glyph_map.get_glyph(index);
}

EPIX_API std::optional<const Glyph> FT2Library::get_glyph_add(
    const Font& font,
    wchar_t c,
    Device& device,
    CommandPool& command_pool,
    Queue& queue
) {
    if (font_textures.find(font) == font_textures.end()) {
        return std::nullopt;
    }
    auto [image, image_view, glyph_map] = font_textures[font];
    uint32_t index                      = FT_Get_Char_Index(font.font_face, c);
    if (!glyph_map.contains(index)) {
        add_char_to_texture(font, c, device, command_pool, queue);
    }
    return get_glyph(font, c);
}

EPIX_API void FT2Library::clear_font_textures(Device& device) {
    for (auto&& [font, texture] : font_textures) {
        auto [image, image_view, glyph_map] = texture;
        image.destroy(device);
        image_view.destroy(device);
    }
    font_textures.clear();
}

EPIX_API void FT2Library::destroy() {
    for (auto&& [_, face] : font_faces) {
        FT_Done_Face(face);
    }
    FT_Done_FreeType(library);
}

EPIX_API void FontPlugin::build(App& app) {
    if (app.get_plugin<render_vk::RenderVKPlugin>()) {
        app.add_system(PreStartup, systems::vulkan::insert_ft2_library)
            .after(render_vk::systems::create_context);
        app.add_system(Startup, systems::vulkan::create_renderer);
        app.add_system(Render, systems::vulkan::draw_text);
        app.add_system(Exit, systems::vulkan::destroy_renderer);
    } else if (app.get_plugin<epix::render::vulkan2::VulkanPlugin>()) {
        app.add_system(
               PreStartup, epix::font::vulkan2::systems::insert_font_atlas
        )
            .after(
                epix::render::vulkan2::systems::create_context,
                epix::render::vulkan2::systems::create_res_manager
            );
        app.add_system(
            PreExtract, epix::font::vulkan2::systems::extract_font_atlas
        );
        app.add_system(
               PostExit, epix::font::vulkan2::systems::destroy_font_atlas
        )
            .before(epix::render::vulkan2::systems::destroy_context);
    }
}