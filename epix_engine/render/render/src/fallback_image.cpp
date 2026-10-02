
#include <epix/render/fallback_image.hpp>

namespace epix::render::texture {
namespace {
/** @brief The texture dimension an image of the given view dimension is
 * created with (wgpu TextureViewDimension::compatible_texture_dimension, used
 * by Bevy fallback_image.rs:90). */
wgpu::TextureDimension compatible_texture_dimension(wgpu::TextureViewDimension dimension) noexcept {
    switch (dimension) {
        case wgpu::TextureViewDimension::e1D:
            return wgpu::TextureDimension::e1D;
        case wgpu::TextureViewDimension::e3D:
            return wgpu::TextureDimension::e3D;
        default:
            return wgpu::TextureDimension::e2D;
    }
}

/** @brief Whether a format is a depth or stencil format (wgpu
 * TextureFormat::is_depth_stencil_format, used by Bevy fallback_image.rs:88). */
bool is_depth_stencil_format(wgpu::TextureFormat format) noexcept {
    switch (format) {
        case wgpu::TextureFormat::eStencil8:
        case wgpu::TextureFormat::eDepth16Unorm:
        case wgpu::TextureFormat::eDepth24Plus:
        case wgpu::TextureFormat::eDepth24PlusStencil8:
        case wgpu::TextureFormat::eDepth32Float:
        case wgpu::TextureFormat::eDepth32FloatStencil8:
            return true;
        default:
            return false;
    }
}

/** @brief Create a 1x1 solid-color fallback texture (Bevy
 * fallback_image_new, fallback_image.rs:67-144). `value` is the per-channel
 * byte value (255 = white, 0 = transparent black). */
GpuImage create_fallback_image(epix::ecs::World& world,
                               wgpu::TextureFormat format,
                               wgpu::TextureViewDimension dimension,
                               std::uint32_t samples,
                               std::uint8_t value) {
    auto device  = world.get_resource<wgpu::Device>();
    auto queue   = world.get_resource<wgpu::Queue>();
    auto sampler = world.get_resource<render::DefaultImageSampler>();
    if (!device || !queue || !sampler) {
        return {};
    }
    const wgpu::Extent3D size{1, 1, (dimension == wgpu::TextureViewDimension::eCube ||
                                     dimension == wgpu::TextureViewDimension::eCubeArray)
                                        ? 6u
                                        : 1u};
    const bool create_texture_with_data = !is_depth_stencil_format(format) && samples == 1;
    const wgpu::TextureDimension image_dimension = compatible_texture_dimension(dimension);
    wgpu::TextureUsage usage = wgpu::TextureUsage::eTextureBinding | wgpu::TextureUsage::eCopyDst |
                               wgpu::TextureUsage::eCopySrc;
    if (image_dimension == wgpu::TextureDimension::e2D) {
        usage = usage | wgpu::TextureUsage::eRenderAttachment;
    }
    wgpu::Texture texture = device->get().createTexture(wgpu::TextureDescriptor()
                                                            .setSize(size)
                                                            .setFormat(format)
                                                            .setUsage(usage)
                                                            .setDimension(image_dimension)
                                                            .setSampleCount(samples)
                                                            .setMipLevelCount(1)
                                                            .setLabel("fallback_image"));
    if (create_texture_with_data) {
        const std::size_t pixel_size   = image::texture_format_pixel_size(format).value_or(0);
        const std::vector<std::uint8_t> data(pixel_size, value);
        for (std::uint32_t layer = 0; layer < size.depthOrArrayLayers; ++layer) {
            wgpu::TexelCopyTextureInfo dest;
            dest.setTexture(texture).setMipLevel(0).setOrigin(wgpu::Origin3D{0, 0, layer});
            queue->get().writeTexture(dest, data.data(), data.size(),
                                      wgpu::TexelCopyBufferLayout()
                                          .setBytesPerRow(static_cast<std::uint32_t>(pixel_size))
                                          .setRowsPerImage(1),
                                      wgpu::Extent3D{1, 1, 1});
        }
    }
    GpuImage image;
    image.texture = texture;
    // Bevy derives the view from the requested dimension and the created layer
    // count, leaving the remaining ranges unset (fallback_image.rs:123-127).
    wgpu::TextureViewDescriptor view_descriptor{};
    view_descriptor.dimension       = dimension;
    view_descriptor.arrayLayerCount = size.depthOrArrayLayers;
    image.texture_view              = texture.createView(view_descriptor);
    image.texture_format            = format;
    image.sampler                   = sampler->get().sampler;
    image.size                      = size;
    image.mip_level_count           = 1;
    image.had_data                  = true;
    return image;
}
}  // namespace

FallbackImage FallbackImage::from_world(epix::ecs::World& world) {
    const wgpu::TextureFormat format = image::bevy_default<wgpu::TextureFormat>();
    FallbackImage image;
    image.d1         = create_fallback_image(world, format, wgpu::TextureViewDimension::e1D, 1, 255);
    image.d2         = create_fallback_image(world, format, wgpu::TextureViewDimension::e2D, 1, 255);
    image.d2_array   = create_fallback_image(world, format, wgpu::TextureViewDimension::e2DArray, 1, 255);
    image.cube       = create_fallback_image(world, format, wgpu::TextureViewDimension::eCube, 1, 255);
    image.cube_array = create_fallback_image(world, format, wgpu::TextureViewDimension::eCubeArray, 1, 255);
    image.d3         = create_fallback_image(world, format, wgpu::TextureViewDimension::e3D, 1, 255);
    return image;
}

FallbackImageZero FallbackImageZero::from_world(epix::ecs::World& world) {
    return FallbackImageZero{create_fallback_image(world, image::bevy_default<wgpu::TextureFormat>(),
                                                   wgpu::TextureViewDimension::e2D, 1, 0)};
}

FallbackImageCubemap FallbackImageCubemap::from_world(epix::ecs::World& world) {
    return FallbackImageCubemap{create_fallback_image(world, image::bevy_default<wgpu::TextureFormat>(),
                                                      wgpu::TextureViewDimension::eCube, 1, 255)};
}

}  // namespace epix::render::texture
