module;

export module epix.render:image;

import :assets;

import epix.image;
import epix.assets;
import webgpu;

namespace render {
export struct DefaultImageSampler {
    wgpu::Sampler sampler;
};
export struct GPUImage {
    wgpu::Texture texture;
    wgpu::TextureView view;
    wgpu::Sampler sampler;
};
}  // namespace render

template <>
struct render::RenderAsset<image::Image> {
    using Param          = std::tuple<Res<wgpu::Device>, Res<wgpu::Queue>, Res<render::DefaultImageSampler>>;
    using ProcessedAsset = GPUImage;

    ProcessedAsset process(image::Image&& asset, Param param);
    RenderAssetUsage usage(const image::Image& asset);
};