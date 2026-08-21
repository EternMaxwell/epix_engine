
#include <spdlog/spdlog.h>

#include <epix/render.hpp>
#include <epix/render/image.hpp>

namespace epix::render {

wgpu::TextureFormat format_cast(image::Format format) noexcept {
    switch (format) {
        case image::Format::Grey8:
            return wgpu::TextureFormat::eR8Unorm;
        case image::Format::GreyAlpha8:
            return wgpu::TextureFormat::eRG8Unorm;
        case image::Format::RGB8:
            return wgpu::TextureFormat::eUndefined;  // no direct mapping, need to convert to RGBA8
        case image::Format::RGBA8:
            return wgpu::TextureFormat::eRGBA8Unorm;
        case image::Format::Grey16:
            return wgpu::TextureFormat::eR16Uint;
        case image::Format::RGB16:
            return wgpu::TextureFormat::eUndefined;  // no direct mapping, need to convert to RGBA16
        case image::Format::RGBA16:
            return wgpu::TextureFormat::eRGBA16Uint;
        case image::Format::Grey32F:
            return wgpu::TextureFormat::eR32Float;
        case image::Format::RGB32F:
            return wgpu::TextureFormat::eUndefined;  // no direct mapping, need to convert to RGBA32F
        case image::Format::RGBA32F:
            return wgpu::TextureFormat::eRGBA32Float;
        default:
            return wgpu::TextureFormat::eUndefined;
    }
}

wgpu::TextureDimension dimension_cast(image::ImageType type) noexcept {
    switch (type) {
        case image::ImageType::e1D:
            return wgpu::TextureDimension::e1D;
        case image::ImageType::e2D:
        case image::ImageType::e2DArray:
            return wgpu::TextureDimension::e2D;
        case image::ImageType::e3D:
            return wgpu::TextureDimension::e3D;
        default:
            return wgpu::TextureDimension::e2D;
    }
}

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

namespace {
wgpu::AddressMode address_mode_cast(image::ImageAddressMode mode) noexcept {
    switch (mode) {
        case image::ImageAddressMode::ClampToEdge:
            return wgpu::AddressMode::eClampToEdge;
        case image::ImageAddressMode::Repeat:
            return wgpu::AddressMode::eRepeat;
        case image::ImageAddressMode::MirrorRepeat:
            return wgpu::AddressMode::eMirrorRepeat;
        case image::ImageAddressMode::ClampToBorder:
            // The bundled wgpu-native (25.0.2.2) has no
            // WGPUAddressMode_ClampToBorder; fall back to ClampToEdge
            // (documented wrapper gap).
            return wgpu::AddressMode::eClampToEdge;
    }
    return wgpu::AddressMode::eClampToEdge;
}
wgpu::FilterMode filter_mode_cast(image::ImageFilterMode mode) noexcept {
    return mode == image::ImageFilterMode::Linear ? wgpu::FilterMode::eLinear : wgpu::FilterMode::eNearest;
}
wgpu::MipmapFilterMode mipmap_filter_mode_cast(image::ImageFilterMode mode) noexcept {
    return mode == image::ImageFilterMode::Linear ? wgpu::MipmapFilterMode::eLinear
                                                  : wgpu::MipmapFilterMode::eNearest;
}
wgpu::CompareFunction compare_function_cast(image::ImageCompareFunction fn) noexcept {
    switch (fn) {
        case image::ImageCompareFunction::Never:
            return wgpu::CompareFunction::eNever;
        case image::ImageCompareFunction::Less:
            return wgpu::CompareFunction::eLess;
        case image::ImageCompareFunction::Equal:
            return wgpu::CompareFunction::eEqual;
        case image::ImageCompareFunction::LessEqual:
            return wgpu::CompareFunction::eLessEqual;
        case image::ImageCompareFunction::Greater:
            return wgpu::CompareFunction::eGreater;
        case image::ImageCompareFunction::NotEqual:
            return wgpu::CompareFunction::eNotEqual;
        case image::ImageCompareFunction::GreaterEqual:
            return wgpu::CompareFunction::eGreaterEqual;
        case image::ImageCompareFunction::Always:
            return wgpu::CompareFunction::eAlways;
    }
    return wgpu::CompareFunction::eAlways;
}
/** @brief Map an epix ImageSamplerDescriptor to a wgpu SamplerDescriptor
 * (Bevy ImageSamplerDescriptor::as_wgpu). Note: the generated webgpu wrapper
 * omits SamplerDescriptor.borderColor, so ClampToBorder with a border color
 * cannot be expressed (wgpu-native FFI gap, documented in the tracker). */
wgpu::SamplerDescriptor to_wgpu_sampler_descriptor(const image::ImageSamplerDescriptor& descriptor) {
    wgpu::SamplerDescriptor out;
    if (!descriptor.label.empty()) out.setLabel(wgpu::StringView(descriptor.label));
    out.setAddressModeU(address_mode_cast(descriptor.address_mode_u))
        .setAddressModeV(address_mode_cast(descriptor.address_mode_v))
        .setAddressModeW(address_mode_cast(descriptor.address_mode_w))
        .setMagFilter(filter_mode_cast(descriptor.mag_filter))
        .setMinFilter(filter_mode_cast(descriptor.min_filter))
        .setMipmapFilter(mipmap_filter_mode_cast(descriptor.mipmap_filter))
        .setLodMinClamp(descriptor.lod_min_clamp)
        .setLodMaxClamp(descriptor.lod_max_clamp)
        .setMaxAnisotropy(descriptor.anisotropy_clamp);
    if (descriptor.compare.has_value()) {
        out.setCompare(compare_function_cast(*descriptor.compare));
    }
    return out;
}
}  // namespace

texture::GpuImage RenderAsset<image::Image>::prepare_asset(image::Image&& asset, Param param, const texture::GpuImage* previous) {
    auto& [device, queue, default_sampler] = param;
    spdlog::trace("[render.image] Processing image to GPU: {}x{}x{} format={}.", asset.width(), asset.height(),
                  asset.depth_or_layers(), static_cast<int>(asset.format()));
    wgpu::TextureDescriptor desc;
    desc.setUsage(wgpu::TextureUsage::eCopyDst | wgpu::TextureUsage::eTextureBinding)
        .setDimension(dimension_cast(asset.type()))
        .setFormat(format_cast(asset.format()))
        .setSize({asset.width(), asset.height(), asset.depth_or_layers()})
        .setMipLevelCount(1)
        .setSampleCount(1);

    if (desc.format == wgpu::TextureFormat::eUndefined) {
        throw std::runtime_error("Unsupported image format for GPU upload");
    }

    texture::GpuImage gpu_image;
    gpu_image.texture = device->createTexture(desc);
    auto view_desc    = wgpu::TextureViewDescriptor()
                            .setDimension(view_dimension_cast(asset.type()))
                            .setMipLevelCount(1)
                            .setBaseMipLevel(0)
                            .setFormat(desc.format)
                            .setBaseArrayLayer(0)
                            .setArrayLayerCount(asset.layers());
    gpu_image.texture_view    = gpu_image.texture.createView(view_desc);
    // Bevy gpu_image.rs:119-122: ImageSampler::Default uses the global
    // DefaultImageSampler; ImageSampler::Descriptor creates a custom sampler.
    if (asset.sampler() == image::ImageSampler::Descriptor) {
        gpu_image.sampler = device->createSampler(to_wgpu_sampler_descriptor(asset.sampler_descriptor()));
    } else {
        gpu_image.sampler = default_sampler->sampler;
    }
    gpu_image.texture_format  = desc.format;
    gpu_image.size            = wgpu::Extent3D{asset.width(), asset.height(), asset.depth_or_layers()};
    gpu_image.mip_level_count = 1;

    auto view = asset.raw_view();
    gpu_image.had_data        = view.size_bytes() > 0;

    // Bevy copy_on_resize (gpu_image.rs:80-108): when the upload has no data
    // and the previous GPU image exists, copy min(old, new) extents from it
    // so a resize preserves the previous frame's content.
    if (view.size_bytes() == 0 && asset.copy_on_resize() && previous && previous->texture) {
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
    const std::uint32_t pixel_size  = asset.format_info().pixelSize();
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
        // stage a row-padded copy so the upload passes validation
        std::vector<std::uint8_t> padded(aligned_row * height);
        for (std::uint32_t y = 0; y < height; ++y) {
            std::memcpy(padded.data() + static_cast<std::size_t>(y) * aligned_row,
                        view.data() + static_cast<std::size_t>(y) * raw_row, raw_row);
        }
        queue->writeTexture(wgpu::TexelCopyTextureInfo()
                                .setTexture(gpu_image.texture)
                                .setOrigin({0, 0, 0})
                                .setAspect(wgpu::TextureAspect::eAll),
                            padded.data(), padded.size(), layout,
                            wgpu::Extent3D{width, height, asset.depth_or_layers()});
    }

    return gpu_image;
}

RenderAssetUsages RenderAsset<image::Image>::usage(const image::Image& asset) noexcept {
    return static_cast<RenderAssetUsages>(asset.usage());
}

}  // namespace epix::render
