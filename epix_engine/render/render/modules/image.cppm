module;

export module epix.render:image;

import :assets;

import epix.image;
import epix.assets;
import webgpu;

namespace epix::render {
/** @brief Resource holding the default sampler used for image textures. */
export struct DefaultImageSampler {
    /** @brief The default GPU sampler. */
    wgpu::Sampler sampler;
};
/** @brief GPU-side representation of an image: texture, view, and
 * sampler. */
export struct GPUImage {
    /** @brief The GPU texture backing this image. */
    wgpu::Texture texture;
    /** @brief A texture view for binding this image in shaders. */
    wgpu::TextureView view;
    /** @brief The sampler used when sampling this image. */
    wgpu::Sampler sampler;
};
}  // namespace epix::render

template <>
struct epix::render::RenderAsset<epix::image::Image> {
    using Param          = std::tuple<Res<wgpu::Device>, Res<wgpu::Queue>, Res<render::DefaultImageSampler>>;
    using ProcessedAsset = GPUImage;

    ProcessedAsset process(image::Image&& asset, Param param);
    RenderAssetUsage usage(const image::Image& asset);
};