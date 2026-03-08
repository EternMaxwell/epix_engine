module;

module epix.render;

import :image;

using namespace render;

wgpu::TextureFormat format_cast(image::Format format) {
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

GPUImage render::RenderAsset<image::Image>::process(image::Image&& asset, Param param) {
    auto& [device, queue, default_sampler] = param;
    wgpu::TextureDescriptor desc;
    desc.setUsage(wgpu::TextureUsage::eCopyDst | wgpu::TextureUsage::eTextureBinding)
        .setDimension(wgpu::TextureDimension::e2D)
        .setFormat(format_cast(asset.format()))
        .setSize({asset.width(), asset.height(), 1})
        .setMipLevelCount(1)
        .setSampleCount(1);

    if (desc.format == wgpu::TextureFormat::eUndefined) {
        throw std::runtime_error("Unsupported image format for GPU upload");
    }

    GPUImage gpu_image;
    gpu_image.texture = device->createTexture(desc);
    gpu_image.view    = gpu_image.texture.createView();
    gpu_image.sampler = default_sampler->sampler;

    auto view = asset.raw_view();

    queue->writeTexture(wgpu::TexelCopyTextureInfo()
                            .setTexture(gpu_image.texture)
                            .setOrigin({0, 0, 0})
                            .setAspect(wgpu::TextureAspect::eAll),
                        view.data(), view.size_bytes(),
                        wgpu::TexelCopyBufferLayout()
                            .setBytesPerRow(asset.width() * asset.format_info().pixelSize())
                            .setRowsPerImage(asset.height()),
                        wgpu::Extent3D{asset.width(), asset.height(), 1});

    return gpu_image;
}

render::RenderAssetUsage render::RenderAsset<image::Image>::usage(const image::Image& asset) {
    return static_cast<RenderAssetUsage>(asset.usage());
}