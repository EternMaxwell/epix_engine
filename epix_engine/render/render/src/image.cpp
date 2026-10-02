
#include <spdlog/spdlog.h>

#include <epix/render.hpp>
#include <epix/render/image.hpp>

namespace epix::render {

void TexturePlugin::attach(app::App& app) {
    // Bevy texture::TexturePlugin owns image render-asset extraction and the
    // manually managed texture-view resource.
    app.add_plugins(RenderAssetPlugin<texture::GpuImage>{});
    app.world_mut().init_resource<texture::ManualTextureViews>();
    app.add_plugins(ExtractResourcePlugin<texture::ManualTextureViews>{});

    // Bevy TexturePlugin::finish obtains this configuration from the separate
    // bevy_image::ImagePlugin.  Keep that ownership boundary: RenderPlugin
    // must not silently install ImagePlugin.
    const auto image_plugin = app.get_plugin<image::ImagePlugin>();
    if (!image_plugin) {
        throw std::runtime_error(
            "render::TexturePlugin requires image::ImagePlugin to be added before render::RenderPlugin");
    }
    const image::ImageSamplerDescriptor default_sampler_descriptor = image_plugin->get().default_sampler;

    app.sub_app_mut(Render).then([default_sampler_descriptor](app::App& render_app) {
        auto& world = render_app.world_mut();
        world.init_resource<render_resource::TextureCache>();
        render_app.add_systems(Render, into(render_resource::update_texture_cache_system)
                                           .in_set(RenderSystems::Cleanup)
                                           .set_name("update texture cache"));

        const wgpu::Device device = world.resource<wgpu::Device>().clone();
        // Bevy TexturePlugin::finish creates the sampler and fallback images
        // after render resources are available. Epix initializes them during
        // attachment because RenderPlugin creates its device synchronously
        // before child plugins are attached.
        auto sampler_descriptor = default_sampler_descriptor.as_wgpu();
        sampler_descriptor.setLabel("DefaultImageSampler");
        wgpu::Sampler sampler = device.createSampler(sampler_descriptor);
        world.insert_resource(DefaultImageSampler{.sampler = std::move(sampler)});
        auto fallback = texture::FallbackImage::from_world(world);
        world.insert_resource(std::move(fallback));
        auto fallback_zero = texture::FallbackImageZero::from_world(world);
        world.insert_resource(std::move(fallback_zero));
        auto fallback_cubemap = texture::FallbackImageCubemap::from_world(world);
        world.insert_resource(std::move(fallback_cubemap));
        world.init_resource<texture::FallbackImageFormatMsaaCache>();
    });
}

std::expected<texture::GpuImage, PrepareAssetError<image::Image>> RenderAsset<texture::GpuImage>::prepare_asset(
    image::Image&& asset, assets::AssetId<image::Image> id, Param param, const texture::GpuImage* previous) {
    (void)id;
    auto& [device, queue, default_sampler] = param;
    spdlog::trace("[render.image] Processing image to GPU: {}x{}x{} format={}.", asset.width(), asset.height(),
                  asset.depth_or_layers(), static_cast<int>(asset.texture_format()));
    // The image owns its texture descriptor (Bevy `Image::texture_descriptor`), so
    // preparation consumes the declared descriptor instead of reconstructing the
    // dimension and format from image intrinsics.
    wgpu::TextureDescriptor desc = asset.texture_descriptor;

    if (desc.format == wgpu::TextureFormat::eUndefined) {
        return std::unexpected(GpuAssetCreationError::CreateTexture);
    }

    texture::GpuImage gpu_image;
    gpu_image.texture = device->createTexture(desc);
    if (!gpu_image.texture) {
        return std::unexpected(GpuAssetCreationError::CreateTexture);
    }
    // Bevy builds the view from the image's optional `texture_view_descriptor`
    // and otherwise uses the default descriptor (gpu_image.rs:113-118), letting
    // wgpu derive the format, dimension, mip levels and layers from the texture
    // itself. An explicit override therefore wins.
    gpu_image.texture_view =
        gpu_image.texture.createView(asset.texture_view_descriptor.value_or(wgpu::TextureViewDescriptor{}));
    if (!gpu_image.texture_view) {
        return std::unexpected(GpuAssetCreationError::CreateTextureView);
    }
    // Bevy gpu_image.rs:119-122: ImageSampler::Default uses the global
    // DefaultImageSampler; ImageSampler::Descriptor creates a custom sampler.
    if (const auto* sampler_descriptor = asset.sampler().descriptor()) {
        gpu_image.sampler = device->createSampler(sampler_descriptor->as_wgpu());
        if (!gpu_image.sampler) {
            return std::unexpected(GpuAssetCreationError::CreateSampler);
        }
    } else {
        gpu_image.sampler = default_sampler->sampler;
    }
    // Bevy takes the view format from the image's own view descriptor
    // (gpu_image.rs:130) and the mip count from the texture descriptor (:133).
    gpu_image.texture_format = desc.format;
    if (asset.texture_view_descriptor && asset.texture_view_descriptor->format != wgpu::TextureFormat::eUndefined) {
        gpu_image.texture_view_format = asset.texture_view_descriptor->format;
    }
    gpu_image.size            = wgpu::Extent3D{asset.width(), asset.height(), asset.depth_or_layers()};
    gpu_image.mip_level_count = desc.mipLevelCount;

    auto view          = asset.raw_view();
    gpu_image.had_data = asset.has_data();

    // Bevy copy_on_resize (gpu_image.rs:80-108): when the upload has no data
    // and resizing should preserve content, copy min(old, new) extents from the
    // previous GPU image; with no previous image Bevy warns instead.
    if (!asset.has_data() && asset.copy_on_resize()) {
        if (previous && previous->texture) {
            const wgpu::Extent3D copy_size{std::min(asset.width(), previous->size.width),
                                           std::min(asset.height(), previous->size.height),
                                           std::min(asset.depth_or_layers(), previous->size.depthOrArrayLayers)};
            wgpu::CommandEncoder encoder = device->createCommandEncoder();
            wgpu::TexelCopyTextureInfo source;
            source.setTexture(previous->texture).setMipLevel(0).setOrigin(wgpu::Origin3D{0, 0, 0});
            wgpu::TexelCopyTextureInfo destination;
            destination.setTexture(gpu_image.texture).setMipLevel(0).setOrigin(wgpu::Origin3D{0, 0, 0});
            encoder.copyTextureToTexture(source, destination, copy_size);
            queue->submit(encoder.finish());
        } else {
            spdlog::warn("No previous asset to copy from for image: {}x{}x{}", asset.width(), asset.height(),
                         asset.depth_or_layers());
        }
    }

    // Bevy hands the data to create_texture_with_data, which walks mip levels
    // and array layers in `data_order` order and pads every row to
    // COPY_BYTES_PER_ROW_ALIGNMENT (256) (render_device.rs:219-230). wgpu
    // validation requires bytesPerRow % 256 == 0 for multi-row uploads.
    if (asset.has_data()) {
        const std::uint32_t pixel_size =
            static_cast<std::uint32_t>(image::texture_format_pixel_size(asset.texture_format()).value_or(0));
        const std::uint32_t width     = asset.width();
        const std::uint32_t height    = asset.height();
        const std::uint32_t mip_count = asset.texture_descriptor.mipLevelCount;
        // Only a 2D texture carries array layers; the depth of a 1D or 3D
        // texture is part of each mip's extent instead.
        const bool arrayed         = desc.dimension == wgpu::TextureDimension::e2D;
        const std::uint32_t layers = arrayed ? asset.depth_or_layers() : 1;
        // LayerMajor walks layers as the outer loop, MipMajor mips.
        const bool mip_major      = asset.data_order == image::TextureDataOrder::MipMajor;
        const std::uint32_t outer = mip_major ? mip_count : layers;
        const std::uint32_t inner = mip_major ? layers : mip_count;
        std::size_t offset        = 0;
        for (std::uint32_t a = 0; a < outer; ++a) {
            for (std::uint32_t b = 0; b < inner; ++b) {
                const std::uint32_t layer      = mip_major ? b : a;
                const std::uint32_t mip        = mip_major ? a : b;
                const std::uint32_t mip_width  = std::max(1u, width >> mip);
                const std::uint32_t mip_height = std::max(1u, height >> mip);
                const std::uint32_t mip_depth  = arrayed ? 1u : std::max(1u, asset.depth_or_layers() >> mip);
                const std::size_t block = static_cast<std::size_t>(mip_width) * mip_height * mip_depth * pixel_size;
                const std::uint32_t raw_row     = mip_width * pixel_size;
                const std::uint32_t aligned_row = epix::render::readback::align_byte_size(raw_row);
                wgpu::TexelCopyBufferLayout layout;
                layout.setBytesPerRow(aligned_row).setRowsPerImage(mip_height);
                const wgpu::Extent3D copy_size{mip_width, mip_height, mip_depth};
                if (aligned_row == raw_row) {
                    queue->writeTexture(wgpu::TexelCopyTextureInfo()
                                            .setTexture(gpu_image.texture)
                                            .setMipLevel(mip)
                                            .setOrigin({0, 0, layer})
                                            .setAspect(wgpu::TextureAspect::eAll),
                                        view.data() + offset, block, layout, copy_size);
                } else {
                    // stage a row-padded copy so the upload passes validation;
                    // every row of every depth slice must be padded
                    std::vector<std::uint8_t> padded(static_cast<std::size_t>(aligned_row) * mip_height * mip_depth);
                    for (std::uint32_t z = 0; z < mip_depth; ++z) {
                        for (std::uint32_t y = 0; y < mip_height; ++y) {
                            const std::size_t row = static_cast<std::size_t>(z) * mip_height + y;
                            std::memcpy(padded.data() + row * aligned_row, view.data() + offset + row * raw_row,
                                        raw_row);
                        }
                    }
                    queue->writeTexture(wgpu::TexelCopyTextureInfo()
                                            .setTexture(gpu_image.texture)
                                            .setMipLevel(mip)
                                            .setOrigin({0, 0, layer})
                                            .setAspect(wgpu::TextureAspect::eAll),
                                        padded.data(), padded.size(), layout, copy_size);
                }
                offset += block;
            }
        }
    }

    return gpu_image;
}

epix::assets::RenderAssetUsages RenderAsset<texture::GpuImage>::asset_usage(const image::Image& asset) noexcept {
    return static_cast<assets::RenderAssetUsages>(asset.usage());
}

}  // namespace epix::render
