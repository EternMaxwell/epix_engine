
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

namespace {
/** @brief The texture view dimension for an image type (Bevy derives this from
 * `texture_descriptor.dimension` plus the layer count). */
wgpu::TextureViewDimension view_dimension_cast(image::ImageType type) noexcept {
    switch (type) {
        case image::ImageType::e1D:
            return wgpu::TextureViewDimension::e1D;
        case image::ImageType::e2D:
            return wgpu::TextureViewDimension::e2D;
        case image::ImageType::e2DArray:
            return wgpu::TextureViewDimension::e2DArray;
        case image::ImageType::e3D:
            return wgpu::TextureViewDimension::e3D;
        default:
            return wgpu::TextureViewDimension::e2D;
    }
}
}  // namespace

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
    // Bevy builds the view from the image's optional `texture_view_descriptor`,
    // falling back to a descriptor derived from the texture itself. An explicit
    // override therefore wins, which is how a distinct view format is requested.
    wgpu::TextureViewDescriptor view_desc =
        asset.texture_view_descriptor.value_or(wgpu::TextureViewDescriptor()
                                                   .setDimension(view_dimension_cast(asset.type()))
                                                   .setMipLevelCount(1)
                                                   .setBaseMipLevel(0)
                                                   .setFormat(desc.format)
                                                   .setBaseArrayLayer(0)
                                                   .setArrayLayerCount(asset.layers()));
    gpu_image.texture_view = gpu_image.texture.createView(view_desc);
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
    gpu_image.texture_format  = desc.format;
    gpu_image.size            = wgpu::Extent3D{asset.width(), asset.height(), asset.depth_or_layers()};
    gpu_image.mip_level_count = 1;

    auto view          = asset.raw_view();
    gpu_image.had_data = asset.has_data();

    // Bevy copy_on_resize (gpu_image.rs:80-108): when the upload has no data
    // and the previous GPU image exists, copy min(old, new) extents from it
    // so a resize preserves the previous frame's content.
    if (!asset.has_data() && asset.copy_on_resize() && previous && previous->texture) {
        const wgpu::Extent3D copy_size{std::min(asset.width(), previous->size.width),
                                       std::min(asset.height(), previous->size.height),
                                       std::min(asset.depth_or_layers(), previous->size.depthOrArrayLayers)};
        if (copy_size.width > 0 && copy_size.height > 0 && copy_size.depthOrArrayLayers > 0) {
            wgpu::CommandEncoder encoder = device->createCommandEncoder();
            wgpu::TexelCopyTextureInfo source;
            source.setTexture(previous->texture).setMipLevel(0).setOrigin(wgpu::Origin3D{0, 0, 0});
            wgpu::TexelCopyTextureInfo destination;
            destination.setTexture(gpu_image.texture).setMipLevel(0).setOrigin(wgpu::Origin3D{0, 0, 0});
            encoder.copyTextureToTexture(source, destination, copy_size);
            queue->submit(encoder.finish());
        } else {
            spdlog::warn("No previous image to copy on resize for image {}x{}x{}", asset.width(), asset.height(),
                         asset.depth_or_layers());
        }
    }

    // Bevy pads every row to COPY_BYTES_PER_ROW_ALIGNMENT (256) via
    // create_texture_with_data (render_device.rs:219-230); wgpu validation
    // requires bytesPerRow % 256 == 0 for multi-row copies.
    //
    // Bevy passes `image.data_order` to `create_texture_with_data`, which uses it
    // to decide whether the mip or the layer index is the outer loop of the upload
    // (LayerMajor vs MipMajor). Epix images are always created with
    // `mipLevelCount == 1` and only mip 0 is written here, so with a single mip
    // level the two orders produce the identical byte sequence and the field has
    // no observable effect. The layered path below therefore walks layers as the
    // outer loop, which is LayerMajor, and the invariant is asserted rather than
    // silently assumed so that adding mip support cannot forget this.
    if (asset.has_data()) {
        assert(asset.texture_descriptor.mipLevelCount == 1 &&
               "layered upload assumes a single mip level; data_order must be honoured once mips exist");
        const std::uint32_t pixel_size =
            static_cast<std::uint32_t>(image::texture_format_pixel_size(asset.texture_format()).value_or(0));
        const std::uint32_t width       = asset.width();
        const std::uint32_t height      = asset.height();
        const std::uint32_t raw_row     = width * pixel_size;
        const std::uint32_t aligned_row = epix::render::readback::align_byte_size(raw_row);
        wgpu::TexelCopyBufferLayout layout;
        layout.setBytesPerRow(aligned_row).setRowsPerImage(height);
        if (aligned_row == raw_row) {
            queue->writeTexture(wgpu::TexelCopyTextureInfo()
                                    .setTexture(gpu_image.texture)
                                    .setOrigin({0, 0, 0})
                                    .setAspect(wgpu::TextureAspect::eAll),
                                view.data(), view.size_bytes(), layout,
                                wgpu::Extent3D{width, height, asset.depth_or_layers()});
        } else {
            // stage a row-padded copy so the upload passes validation; the copy
            // extent covers depth_or_layers slices, so every row of every layer
            // must be padded (a single-slice buffer would overflow the copy).
            const std::uint32_t layers = asset.depth_or_layers();
            std::vector<std::uint8_t> padded(static_cast<std::size_t>(aligned_row) * height * layers);
            for (std::uint32_t layer = 0; layer < layers; ++layer) {
                for (std::uint32_t y = 0; y < height; ++y) {
                    std::size_t row = static_cast<std::size_t>(layer) * height + y;
                    std::memcpy(padded.data() + row * aligned_row, view.data() + row * raw_row, raw_row);
                }
            }
            queue->writeTexture(wgpu::TexelCopyTextureInfo()
                                    .setTexture(gpu_image.texture)
                                    .setOrigin({0, 0, 0})
                                    .setAspect(wgpu::TextureAspect::eAll),
                                padded.data(), padded.size(), layout, wgpu::Extent3D{width, height, layers});
        }
    }

    return gpu_image;
}

epix::assets::RenderAssetUsages RenderAsset<texture::GpuImage>::asset_usage(const image::Image& asset) noexcept {
    return static_cast<assets::RenderAssetUsages>(asset.usage());
}

}  // namespace epix::render
