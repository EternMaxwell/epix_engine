#include "FiraSans_Regular.ttf.h"
#include "epix/font.h"

using namespace epix::font::vulkan2;

static std::shared_ptr<spdlog::logger> logger =
    spdlog::default_logger()->clone("font");

EPIX_API size_t FontAtlas::FontHash::operator()(const Font& font) const {
    return (
        std::hash<std::string>()(font.font_identifier) ^
        std::hash<int>()(font.pixels) ^ std::hash<bool>()(font.antialias)
    );
}

EPIX_API bool FontAtlas::FontEqual::operator()(const Font& lhs, const Font& rhs)
    const {
    return lhs.font_identifier == rhs.font_identifier &&
           lhs.pixels == rhs.pixels && lhs.antialias == rhs.antialias;
}

EPIX_API FontAtlas::CharAdd::CharAdd(
    const FT_Bitmap& bitmap, const glm::uvec3& pos, bool antialias
)
    : size(bitmap.width, bitmap.rows), pos(pos) {
    this->bitmap.resize(bitmap.width * bitmap.rows);
    for (int iy = 0; iy < bitmap.rows; iy++) {
        for (int ix = 0; ix < bitmap.width; ix++) {
            this->bitmap[ix + (bitmap.rows - iy - 1) * bitmap.width] =
                antialias ? bitmap.buffer[ix + iy * bitmap.pitch]
                          : ((bitmap.buffer[ix / 8 + iy * bitmap.pitch] >>
                              (7 - ix % 8)) &
                             1) *
                                255;
        }
    }
}

EPIX_API FontAtlas::FontAtlas(
    epix::render::vulkan2::backend::Device& device,
    epix::render::vulkan2::backend::CommandPool& command_pool,
    ResMut<VulkanResources>& res_manager
)
    : device(device) {
    cmd = device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo()
            .setCommandPool(command_pool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1)
    )[0];
    fence = device.createFence(
        vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
    );
    FT_Init_FreeType(&library);
    // need to add default font
    FT_Face face;
    FT_Error error = FT_New_Memory_Face(
        library, FiraSans_Regular_ttf, sizeof(FiraSans_Regular_ttf), 0, &face
    );
    if (error) {
        logger->error("Failed to load default font");
        return;
    }
    font_faces["default"] = face;
    font_add_cache.insert(Font{"default", 64, true});
    default_font_texture_index =
        res_manager->add_image_view("font::image_view::default", ImageView());
    font_add_cache.insert(Font{"default", 64, false});
    res_manager->add_sampler(
        "font::sampler::default",
        device.createSampler(
            vk::SamplerCreateInfo()
                .setMagFilter(vk::Filter::eLinear)
                .setMinFilter(vk::Filter::eLinear)
                .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
                .setAnisotropyEnable(false)
                .setMaxAnisotropy(1)
                .setBorderColor(vk::BorderColor::eFloatOpaqueBlack)
                .setUnnormalizedCoordinates(false)
                .setCompareEnable(false)
                .setCompareOp(vk::CompareOp::eAlways)
                .setMipmapMode(vk::SamplerMipmapMode::eLinear)
                .setMipLodBias(0)
                .setMinLod(0)
                .setMaxLod(0)
        )
    );
}

EPIX_API void FontAtlas::destroy() { device.destroyFence(fence); }

EPIX_API FT_Face FontAtlas::load_font(const std::string& file_path) {
    auto path = std::filesystem::absolute(file_path).string();
    if (font_faces.find(path) != font_faces.end()) {
        return font_faces[path];
    }
    FT_Face face;
    FT_Error error = FT_New_Face(library, path.c_str(), 0, &face);
    if (error) {
        logger->error("Failed to load font: {}, returning default", path);
        return font_faces["default"];
    }
    font_faces[path] = face;
    return face;
}

EPIX_API uint32_t FontAtlas::font_index(const Font& font) const {
    if (font_texture_index.find(font) == font_texture_index.end()) {
        logger->warn("Font not found, replacing it with default");
        std::unique_lock<std::mutex> lock(font_mutex);
        font_add_cache.insert(font);
        return default_font_texture_index;
    }
    return font_texture_index.at(font);
}

EPIX_API uint32_t FontAtlas::char_index(const Font& font, wchar_t c) {
    return FT_Get_Char_Index(font_faces[font.font_identifier], c);
}

EPIX_API std::optional<const Glyph> FontAtlas::get_glyph(
    const Font& font, wchar_t c
) const {
    if (!font_faces.contains(font.font_identifier)) {
        logger->warn(
            "Font {} not found, returning default. Font should always be "
            "loaded ahead.",
            font.font_identifier
        );
        return std::nullopt;
    }
    if (!glyph_maps.contains(font) || !char_loading_states.contains(font)) {
        std::unique_lock<std::mutex> lock(font_mutex);
        font_add_cache.insert(font);
        return std::nullopt;
    }
    uint32_t index  = FT_Get_Char_Index(font_faces.at(font.font_identifier), c);
    auto& glyph_map = glyph_maps.at(font);
    if (glyph_map.find(index) == glyph_map.end()) {
        std::unique_lock<std::mutex> lock(font_mutex);
        glyph_add_cache[font].push_back(index);
        return std::nullopt;
    }
    return glyph_map.at(index);
}

EPIX_API void FontAtlas::apply_cache(
    epix::render::vulkan2::backend::Queue& queue,
    epix::render::vulkan2::VulkanResources& res_manager
) {
    std::unique_lock<std::mutex> lock(font_mutex);
    using namespace epix::render::vulkan2::backend;
    if (font_add_cache.empty() && glyph_add_cache.empty() &&
        char_add_cache.empty())
        return;
    for (auto&& font : font_add_cache) {
        if (glyph_maps.contains(font)) {
            continue;
        }
        font_image_index[font] = res_manager.add_image(
            "font::image::" + font.font_identifier +
                std::to_string(font.pixels),
            Image()
        );
        font_texture_index[font] = res_manager.add_image_view(
            "font::image_view::" + font.font_identifier +
                std::to_string(font.pixels),
            ImageView()
        );
        texture_layers[font]      = 0;
        glyph_maps[font]          = {};
        char_loading_states[font] = {};
    }
    entt::dense_map<Font, uint32_t, FontHash, FontEqual> new_layers;
    std::vector<Image> old_images;
    std::vector<ImageView> old_image_views;
    for (auto&& [font, glyph_adds] : glyph_add_cache) {
        auto& glyph_map = glyph_maps[font];
        for (auto&& index : glyph_adds) {
            if (glyph_map.contains(index)) {
                continue;
            }
            auto face = font_faces[font.font_identifier];
            if (FT_Set_Char_Size(face, 0, font.pixels, 1024, 1024)) {
                logger->error("Failed to set char size, skipping character");
                continue;
            }
            if (FT_Set_Pixel_Sizes(face, 0, font.pixels)) {
                logger->error("Failed to set pixel size, skipping character");
                continue;
            }
            if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT)) {
                logger->error("Failed to load glyph, skipping character");
                continue;
            }
            if (FT_Render_Glyph(
                    face->glyph,
                    font.antialias ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO
                )) {
                logger->error("Failed to render glyph, skipping character");
                continue;
            }
            auto& glyph              = face->glyph;
            auto bitmap              = glyph->bitmap;
            auto& char_loading_state = char_loading_states[font];
            auto current_x           = char_loading_state.current_x;
            auto current_y           = char_loading_state.current_y;
            auto current_layer       = char_loading_state.current_layer;
            auto current_line_height = char_loading_state.current_line_height;
            if (current_x + bitmap.width + 1 >= font_texture_width) {
                current_x = 0;
                current_y += current_line_height;
                current_line_height = 0;
            }
            if (current_y + bitmap.rows >= font_texture_height) {
                current_x = 0;
                current_y = 0;
                current_layer += 1;
            }
            glm::uvec3 pos = {current_x, current_y, current_layer};
            if (current_layer >= font_texture_layers) {
                logger->warn("Font texture is full, skipping character");
                continue;
            }
            if (current_layer >= texture_layers[font]) {
                new_layers[font] = current_layer + 1;
            }
            current_x += bitmap.width + 1;
            current_line_height =
                std::max(current_line_height, bitmap.rows + 1);

            glm::uvec2 size = {bitmap.width, bitmap.rows};
            glm::vec2 uv1   = {
                (float)pos.x / font_texture_width,
                (float)pos.y / font_texture_height
            };
            glm::vec2 uv2 = {
                (float)(pos.x + bitmap.width) / font_texture_width,
                (float)(pos.y + bitmap.rows) / font_texture_height
            };
            Glyph new_glyph;
            new_glyph.size    = size;
            new_glyph.bearing = {
                glyph->metrics.horiBearingX >> 6,
                glyph->metrics.horiBearingY >> 6
            };
            new_glyph.advance = {glyph->advance.x >> 6, glyph->advance.y >> 6};
            new_glyph.array_index = current_layer;
            new_glyph.uv_1        = uv1;
            new_glyph.uv_2        = uv2;
            glyph_map.emplace(index, new_glyph);
            char_add_cache[font].emplace_back(bitmap, pos, font.antialias);
            char_loading_state = {
                current_x, current_y, current_layer, current_line_height
            };
        }
    }
    device.waitForFences(fence, VK_TRUE, UINT64_MAX);
    device.resetFences(fence);
    cmd.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
    cmd.begin(vk::CommandBufferBeginInfo());
    bool added_new_font = false;
    for (auto&& [font, layer] : new_layers) {
        added_new_font = true;
        if (texture_layers[font]) {
            // this means there is already a texture for this font so we need to
            // destroy it and copy the data to a new texture
            auto new_image = device.createImage(
                vk::ImageCreateInfo()
                    .setImageType(vk::ImageType::e2D)
                    .setFormat(vk::Format::eR8Uint)
                    .setExtent(
                        vk::Extent3D(font_texture_width, font_texture_height, 1)
                    )
                    .setMipLevels(1)
                    .setArrayLayers(layer)
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
            auto new_view = device.createImageView(
                vk::ImageViewCreateInfo()
                    .setImage(new_image)
                    .setViewType(vk::ImageViewType::e2D)
                    .setFormat(vk::Format::eR8Uint)
                    .setSubresourceRange(
                        vk::ImageSubresourceRange()
                            .setAspectMask(vk::ImageAspectFlagBits::eColor)
                            .setBaseMipLevel(0)
                            .setLevelCount(1)
                            .setBaseArrayLayer(0)
                            .setLayerCount(layer)
                    )
            );
            auto old_image = res_manager.replace_image(
                "font::image::" + font.font_identifier +
                    std::to_string(font.pixels),
                new_image
            );
            auto old_view = res_manager.replace_image_view(
                "font::image_view::" + font.font_identifier +
                    std::to_string(font.pixels),
                new_view
            );
            vk::ImageMemoryBarrier barrier;
            barrier.setOldLayout(vk::ImageLayout::eUndefined);
            barrier.setNewLayout(vk::ImageLayout::eTransferDstOptimal);
            barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
            barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
            barrier.setImage(new_image);
            barrier.setSubresourceRange(vk::ImageSubresourceRange(
                vk::ImageAspectFlagBits::eColor, 0, 1, 0, layer
            ));
            barrier.setSrcAccessMask(vk::AccessFlagBits::eMemoryRead);
            barrier.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier
            );
            barrier.setImage(old_image);
            barrier.setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
            barrier.setNewLayout(vk::ImageLayout::eTransferSrcOptimal);
            barrier.setSrcAccessMask(
                vk::AccessFlagBits::eShaderRead |
                vk::AccessFlagBits::eShaderWrite
            );
            barrier.setDstAccessMask(vk::AccessFlagBits::eTransferRead);
            barrier.setSubresourceRange(vk::ImageSubresourceRange(
                vk::ImageAspectFlagBits::eColor, 0, 1, 0, texture_layers[font]
            ));
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eFragmentShader,
                vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier
            );
            vk::ImageCopy copy_region;
            copy_region.setSrcSubresource(vk::ImageSubresourceLayers(
                vk::ImageAspectFlagBits::eColor, 0, 0, texture_layers[font]
            ));
            copy_region.setDstSubresource(vk::ImageSubresourceLayers(
                vk::ImageAspectFlagBits::eColor, 0, 0, texture_layers[font]
            ));
            copy_region.setExtent(vk::Extent3D(
                font_texture_width, font_texture_height, texture_layers[font]
            ));
            cmd.copyImage(
                old_image, vk::ImageLayout::eTransferSrcOptimal, new_image,
                vk::ImageLayout::eTransferDstOptimal, copy_region
            );
            barrier.setImage(new_image);
            barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal);
            barrier.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
            barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
            barrier.setDstAccessMask(
                vk::AccessFlagBits::eShaderRead |
                vk::AccessFlagBits::eShaderWrite
            );
            barrier.setSubresourceRange(vk::ImageSubresourceRange(
                vk::ImageAspectFlagBits::eColor, 0, 1, 0, layer
            ));
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier
            );
            old_images.push_back(old_image);
            old_image_views.push_back(old_view);
        } else {
            auto new_image = device.createImage(
                vk::ImageCreateInfo()
                    .setImageType(vk::ImageType::e2D)
                    .setFormat(vk::Format::eR8Uint)
                    .setExtent(
                        vk::Extent3D(font_texture_width, font_texture_height, 1)
                    )
                    .setMipLevels(1)
                    .setArrayLayers(layer)
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
            auto new_view = device.createImageView(
                vk::ImageViewCreateInfo()
                    .setImage(new_image)
                    .setViewType(vk::ImageViewType::e2D)
                    .setFormat(vk::Format::eR8Uint)
                    .setSubresourceRange(
                        vk::ImageSubresourceRange()
                            .setAspectMask(vk::ImageAspectFlagBits::eColor)
                            .setBaseMipLevel(0)
                            .setLevelCount(1)
                            .setBaseArrayLayer(0)
                            .setLayerCount(layer)
                    )
            );
            res_manager.replace_image(
                "font::image::" + font.font_identifier +
                    std::to_string(font.pixels),
                new_image
            );
            res_manager.replace_image_view(
                "font::image_view::" + font.font_identifier +
                    std::to_string(font.pixels),
                new_view
            );
            texture_layers[font] = layer;
        }
    }
    if (added_new_font) {
        cmd.end();
        queue.submit(vk::SubmitInfo().setCommandBuffers(cmd), fence);
        device.waitForFences(fence, VK_TRUE, UINT64_MAX);
        device.resetFences(fence);
        cmd.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
        cmd.begin(vk::CommandBufferBeginInfo());
    }
    std::vector<Buffer> staging_buffers;
    std::vector<std::tuple<Buffer, uint32_t, vk::BufferImageCopy>> copy_regions;
    for (auto&& [font, char_adds] : char_add_cache) {
        uint32_t staging_buffer_size = 0;
        for (auto&& char_add : char_adds) {
            staging_buffer_size += char_add.bitmap.size();
        }
        if (staging_buffer_size == 0) {
            continue;
        }
        auto staging_buffer = device.createBuffer(
            vk::BufferCreateInfo()
                .setSize(staging_buffer_size)
                .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
                .setSharingMode(vk::SharingMode::eExclusive),
            AllocationCreateInfo()
                .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
                .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                )
        );
        staging_buffers.push_back(staging_buffer);
        uint8_t* data   = (uint8_t*)staging_buffer.map();
        uint32_t offset = 0;
        for (auto&& char_add : char_adds) {
            std::memcpy(
                data + offset, char_add.bitmap.data(), char_add.bitmap.size()
            );
            if (char_add.size.x == 0 || char_add.size.y == 0) {
                continue;
            }
            vk::BufferImageCopy copy_region;
            copy_region.setBufferOffset(offset);
            copy_region.setBufferRowLength(0);
            copy_region.setBufferImageHeight(0);
            copy_region.setImageSubresource(
                vk::ImageSubresourceLayers()
                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                    .setMipLevel(0)
                    .setBaseArrayLayer(char_add.pos.z)
                    .setLayerCount(1)
            );
            copy_region.setImageOffset(
                {(int)char_add.pos.x, (int)char_add.pos.y, 0}
            );
            copy_region.setImageExtent({char_add.size.x, char_add.size.y, 1});
            copy_regions.push_back(std::make_tuple(
                staging_buffer, font_image_index[font], copy_region
            ));
            offset += char_add.bitmap.size();
        }
        staging_buffer.unmap();
    }
    for (auto&& [buffer, image_id, region] : copy_regions) {
        auto image = res_manager.get_image(image_id);
        vk::ImageMemoryBarrier barrier;
        barrier.setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
        barrier.setNewLayout(vk::ImageLayout::eTransferDstOptimal);
        barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setImage(image);
        barrier.setSubresourceRange(vk::ImageSubresourceRange(
            vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1
        ));
        barrier.setSrcAccessMask(
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
        );
        barrier.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier
        );
        cmd.copyBufferToImage(
            buffer, image, vk::ImageLayout::eTransferDstOptimal, region
        );
        barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal);
        barrier.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
        barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
        barrier.setDstAccessMask(
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
        );
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier
        );
    }
    cmd.end();
    queue.submit(vk::SubmitInfo().setCommandBuffers(cmd), fence);
    device.waitForFences(fence, VK_TRUE, UINT64_MAX);
    for (auto&& buffer : staging_buffers) {
        device.destroyBuffer(buffer);
    }
    for (auto&& image : old_images) {
        device.destroyImage(image);
    }
    for (auto&& view : old_image_views) {
        device.destroyImageView(view);
    }
    char_add_cache.clear();
    glyph_add_cache.clear();
    font_add_cache.clear();
}