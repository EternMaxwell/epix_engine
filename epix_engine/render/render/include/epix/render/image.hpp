#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <tuple>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/render/assets.hpp>

namespace epix::render {
/** @brief Resource holding the default sampler used for image textures. */
EPIX_EXPORT struct DefaultImageSampler {
    /** @brief The default GPU sampler. */
    wgpu::Sampler sampler;
};
/** @brief GPU-side representation of an image: texture, view, and
 * sampler. */
EPIX_EXPORT struct GPUImage {
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
    using Param          = std::tuple<epix::core::Res<wgpu::Device>,
                                      epix::core::Res<wgpu::Queue>,
                                      epix::core::Res<render::DefaultImageSampler>>;
    using ProcessedAsset = GPUImage;

    ProcessedAsset process(image::Image&& asset, Param param);
    RenderAssetUsage usage(const image::Image& asset) noexcept;
};