#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#include <epix/image.hpp>
#include <epix/render/fallback_image.hpp>
#include <epix/render/render_resource.hpp>
#include <optional>
#include <utility>
#include <webgpu/webgpu.hpp>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief The texture view and sampler a material's optional texture should be
 *  bound with: the image's own when its handle is present and uploaded, and the
 *  1x1 white fallback otherwise (Bevy `get_image_texture`,
 *  `bevy_pbr/src/render/mesh.rs:1964-1978`).
 *
 *  Returns null pointers only when a handle is present but its image has not
 *  been uploaded, which is Bevy's `gpu_images.get(handle)?`. */
EPIX_EXPORT std::optional<std::pair<const wgpu::TextureView*, const wgpu::Sampler*>> get_image_texture(
    const render::texture::GpuImage& dummy_white_gpu_image,
    const render::RenderAssets<render::texture::GpuImage>& gpu_images,
    const std::optional<assets::Handle<image::Image>>& handle_option);

}  // namespace epix::pbr
