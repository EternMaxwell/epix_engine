#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstdint>
#include <epix/ecs.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <unordered_map>
#include <utility>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/render/image.hpp>

namespace epix::render::texture {
/**
 * @brief The GPU representation of an image: texture, view, format, sampler
 * and metadata (Bevy 0.18 `GpuImage`).
 */
EPIX_EXPORT struct GpuImage {
    /** @brief The GPU texture. */
    wgpu::Texture texture;
    /** @brief A texture view for binding in shaders. */
    wgpu::TextureView texture_view;
    /** @brief Format of the texture. */
    wgpu::TextureFormat texture_format = wgpu::TextureFormat::eUndefined;
    /** @brief Optional view format override. */
    std::optional<wgpu::TextureFormat> texture_view_format;
    /** @brief Sampler used when sampling this image. */
    wgpu::Sampler sampler;
    /** @brief Texture size. */
    wgpu::Extent3D size{};
    /** @brief Number of mip levels. */
    std::uint32_t mip_level_count = 1;
    /** @brief True if the source image provided data on the last upload. */
    bool had_data = false;

    /** @brief Aspect ratio (width / height) of a 2D image (Bevy
     * GpuImage::aspect_ratio, gpu_image.rs:142-146). epix returns a float
     * ratio; Bevy returns an AspectRatio type. */
    float aspect_ratio() const { return static_cast<float>(size.width) / static_cast<float>(size.height); }
    /** @brief Size of a 2D image as (width, height) (Bevy
     * GpuImage::size_2d, gpu_image.rs:150-152). */
    glm::uvec2 size_2d() const { return glm::uvec2(size.width, size.height); }
};

/**
 * @brief Resource holding the default (1x1 white) fallback images used when a
 * texture asset is missing, one per texture view dimension (Bevy
 * `FallbackImage`, fallback_image.rs:23-47).
 */
namespace detail {
/** @brief Create a 1x1 solid-color fallback texture (Bevy
 * fallback_image_new, fallback_image.rs:76-120). `value` is the per-channel
 * byte value (255 = white, 0 = transparent black). */
inline GpuImage create_fallback_image(epix::ecs::World& world,
                                      wgpu::TextureDimension dimension,
                                      wgpu::TextureViewDimension view_dimension,
                                      std::uint32_t layers,
                                      std::uint8_t value) {
    auto device  = world.get_resource<wgpu::Device>();
    auto queue   = world.get_resource<wgpu::Queue>();
    auto sampler = world.get_resource<render::DefaultImageSampler>();
    if (!device || !queue || !sampler) {
        return {};
    }
    wgpu::Texture texture =
        device->get().createTexture(wgpu::TextureDescriptor()
                                        .setSize({1, 1, layers})
                                        .setFormat(wgpu::TextureFormat::eRGBA8Unorm)
                                        .setUsage(wgpu::TextureUsage::eTextureBinding | wgpu::TextureUsage::eCopyDst)
                                        .setDimension(dimension)
                                        .setSampleCount(1)
                                        .setMipLevelCount(1)
                                        .setLabel("fallback_image"));
    const std::array<std::uint8_t, 4> color{value, value, value, value};
    for (std::uint32_t layer = 0; layer < layers; ++layer) {
        wgpu::TexelCopyTextureInfo dest;
        dest.setTexture(texture).setMipLevel(0).setOrigin(wgpu::Origin3D{0, 0, layer});
        queue->get().writeTexture(dest, color.data(), color.size(),
                                  wgpu::TexelCopyBufferLayout().setBytesPerRow(4).setRowsPerImage(1),
                                  wgpu::Extent3D{1, 1, 1});
    }
    GpuImage image;
    image.texture = texture;
    // wgpu-native requires the view's explicit ranges to be non-zero.  This
    // is Bevy's default view range: its one mip level and every created layer.
    wgpu::TextureViewDescriptor view_descriptor{};
    view_descriptor.dimension       = view_dimension;
    view_descriptor.baseMipLevel    = 0;
    view_descriptor.mipLevelCount   = 1;
    view_descriptor.baseArrayLayer  = 0;
    view_descriptor.arrayLayerCount = layers;
    image.texture_view              = texture.createView(view_descriptor);
    image.texture_format            = wgpu::TextureFormat::eRGBA8Unorm;
    image.sampler                   = sampler->get().sampler;
    image.size                      = wgpu::Extent3D{1, 1, layers};
    image.mip_level_count           = 1;
    image.had_data                  = true;
    return image;
}
}  // namespace detail

EPIX_EXPORT struct FallbackImage {
    /** @brief Fallback for TextureViewDimension::eD1. */
    GpuImage d1;
    /** @brief Fallback for TextureViewDimension::e2D. */
    GpuImage d2;
    /** @brief Fallback for TextureViewDimension::e2DArray. */
    GpuImage d2_array;
    /** @brief Fallback for TextureViewDimension::eCube. */
    GpuImage cube;
    /** @brief Fallback for TextureViewDimension::eCubeArray. */
    GpuImage cube_array;
    /** @brief Fallback for TextureViewDimension::e3D. */
    GpuImage d3;

    /** @brief Create the 1x1 white fallback images (Bevy fallback_image.rs
     * FromWorld, created by TexturePlugin::finish). */
    static FallbackImage from_world(epix::ecs::World& world) {
        FallbackImage image;
        image.d1 =
            detail::create_fallback_image(world, wgpu::TextureDimension::e1D, wgpu::TextureViewDimension::e1D, 1, 255);
        image.d2 =
            detail::create_fallback_image(world, wgpu::TextureDimension::e2D, wgpu::TextureViewDimension::e2D, 1, 255);
        image.d2_array   = detail::create_fallback_image(world, wgpu::TextureDimension::e2D,
                                                         wgpu::TextureViewDimension::e2DArray, 1, 255);
        image.cube       = detail::create_fallback_image(world, wgpu::TextureDimension::e2D,
                                                         wgpu::TextureViewDimension::eCube, 6, 255);
        image.cube_array = detail::create_fallback_image(world, wgpu::TextureDimension::e2D,
                                                         wgpu::TextureViewDimension::eCubeArray, 6, 255);
        image.d3 =
            detail::create_fallback_image(world, wgpu::TextureDimension::e3D, wgpu::TextureViewDimension::e3D, 1, 255);
        return image;
    }

    /** @brief The fallback image for the given dimension, or nullptr for
     * unknown dimensions (Bevy FallbackImage::get). */
    const GpuImage* get(wgpu::TextureViewDimension dimension) const noexcept {
        switch (dimension) {
            case wgpu::TextureViewDimension::e1D:
                return &d1;
            case wgpu::TextureViewDimension::e2D:
                return &d2;
            case wgpu::TextureViewDimension::e2DArray:
                return &d2_array;
            case wgpu::TextureViewDimension::eCube:
                return &cube;
            case wgpu::TextureViewDimension::eCubeArray:
                return &cube_array;
            case wgpu::TextureViewDimension::e3D:
                return &d3;
            default:
                return nullptr;
        }
    }
};

/** @brief Fallback image whose texture is filled with zeroes (Bevy `FallbackImageZero`). */
EPIX_EXPORT struct FallbackImageZero {
    /** @brief The zero-filled GPU image. */
    GpuImage image;

    /** @brief Create the 1x1 transparent-black fallback (Bevy
     * fallback_image.rs FromWorld). */
    static FallbackImageZero from_world(epix::ecs::World& world) {
        return FallbackImageZero{
            detail::create_fallback_image(world, wgpu::TextureDimension::e2D, wgpu::TextureViewDimension::e2D, 1, 0)};
    }
};

/** @brief Fallback cubemap image (Bevy `FallbackImageCubemap`). */
EPIX_EXPORT struct FallbackImageCubemap {
    /** @brief The cubemap GPU image. */
    GpuImage image;

    /** @brief Create the 1x1 white cubemap fallback (Bevy
     * fallback_image.rs FromWorld). */
    static FallbackImageCubemap from_world(epix::ecs::World& world) {
        return FallbackImageCubemap{detail::create_fallback_image(world, wgpu::TextureDimension::e2D,
                                                                  wgpu::TextureViewDimension::eCube, 6, 255)};
    }
};

/** @brief Hash for (samples, format) keys. */
EPIX_EXPORT struct MsaaCacheKeyHash {
    std::size_t operator()(const std::pair<std::uint32_t, wgpu::TextureFormat>& key) const noexcept {
        std::size_t h = std::hash<std::uint32_t>{}(key.first);
        h ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(key.second)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

/** @brief Lazy per-(samples, format) MSAA fallback image cache (Bevy
 * `FallbackImageFormatMsaaCache`). */
EPIX_EXPORT struct FallbackImageFormatMsaaCache {
    /** @brief Key: (sample count, texture format). */
    using Key = std::pair<std::uint32_t, wgpu::TextureFormat>;

    /** @brief Get or create the MSAA fallback image for the given sample
     * count and format. `create` builds the image on cache miss (Bevy
     * FallbackImageMsaa::get). */
    GpuImage* get_or_create(const Key& key, std::function<GpuImage()> create) {
        if (auto it = images.find(key); it != images.end()) {
            return &it->second;
        }
        return &images.emplace(key, create()).first->second;
    }
    /** @brief True when the cache is empty. */
    bool is_empty() const noexcept { return images.empty(); }

   private:
    std::unordered_map<Key, GpuImage, MsaaCacheKeyHash> images;
};

}  // namespace epix::render::texture

template <>
struct epix::render::RenderAsset<epix::image::Image> {
    using Param          = std::tuple<epix::ecs::Res<wgpu::Device>,
                                      epix::ecs::Res<wgpu::Queue>,
                                      epix::ecs::Res<epix::render::DefaultImageSampler>>;
    using ProcessedAsset = epix::render::texture::GpuImage;
    using ExtractedAsset = epix::image::Image;

    std::expected<ProcessedAsset, epix::render::PrepareAssetError<epix::image::Image>> prepare_asset(
        epix::image::Image&& asset,
        epix::assets::AssetId<epix::image::Image> id,
        Param param,
        const epix::render::texture::GpuImage* previous);
    epix::render::RenderAssetUsages usage(const epix::image::Image& asset) noexcept;
    std::optional<std::size_t> byte_len(const epix::image::Image& asset) const noexcept {
        return asset.has_data() ? std::optional{asset.raw_view().size_bytes()} : std::nullopt;
    }
    /** @brief Move render-only pixel data out of the stored image while
     * retaining its metadata in the main-world asset (Bevy
     * `GpuImage::take_gpu_data`). */
    std::expected<epix::image::Image, epix::render::AssetExtractionError> take_gpu_data(
        epix::image::Image& source,
        const epix::render::texture::GpuImage* previous_gpu_asset) const {
        const bool valid_upload = source.has_data() || !previous_gpu_asset || !previous_gpu_asset->had_data;
        if (!valid_upload) {
            return std::unexpected(epix::render::AssetExtractionError::AlreadyExtracted);
        }
        return source.take_data();
    }
};

// This conversion contract needs the complete image RenderAsset specialization,
// while AsBindGroup itself intentionally remains independent of render assets.
#include <epix/render/as_bind_group_shader_type.hpp>
