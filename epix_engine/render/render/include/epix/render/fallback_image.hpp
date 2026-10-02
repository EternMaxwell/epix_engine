#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>
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
     * ratio; Bevy returns an AspectRatio type. A zero dimension is rejected
     * in both. */
    float aspect_ratio() const {
        if (size.width == 0 || size.height == 0) {
            throw std::invalid_argument(
                "Failed to calculate aspect ratio: Image dimensions must be positive, non-zero values");
        }
        return static_cast<float>(size.width) / static_cast<float>(size.height);
    }
    /** @brief Size of a 2D image as (width, height) (Bevy
     * GpuImage::size_2d, gpu_image.rs:150-152). */
    glm::uvec2 size_2d() const { return glm::uvec2(size.width, size.height); }
};

/**
 * @brief Resource holding the default (1x1 white) fallback images used when a
 * texture asset is missing, one per texture view dimension (Bevy
 * `FallbackImage`, fallback_image.rs:23-47).
 */
EPIX_EXPORT struct EPIX_API FallbackImage {
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
    static FallbackImage from_world(epix::ecs::World& world);

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
EPIX_EXPORT struct EPIX_API FallbackImageZero {
    /** @brief The zero-filled GPU image. */
    GpuImage image;

    /** @brief Create the 1x1 transparent-black fallback (Bevy
     * fallback_image.rs FromWorld). */
    static FallbackImageZero from_world(epix::ecs::World& world);
};

/** @brief Fallback cubemap image (Bevy `FallbackImageCubemap`). */
EPIX_EXPORT struct EPIX_API FallbackImageCubemap {
    /** @brief The cubemap GPU image. */
    GpuImage image;

    /** @brief Create the 1x1 white cubemap fallback (Bevy
     * fallback_image.rs FromWorld). */
    static FallbackImageCubemap from_world(epix::ecs::World& world);
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
struct epix::render::RenderAsset<epix::render::texture::GpuImage> {
    using Param          = std::tuple<epix::ecs::Res<wgpu::Device>,
                                      epix::ecs::Res<wgpu::Queue>,
                                      epix::ecs::Res<epix::render::DefaultImageSampler>>;
    using SourceAsset    = epix::image::Image;
    using ExtractedAsset = epix::image::Image;

    EPIX_API std::expected<epix::render::texture::GpuImage, epix::render::PrepareAssetError<epix::image::Image>>
    prepare_asset(epix::image::Image&& asset,
                  epix::assets::AssetId<epix::image::Image> id,
                  Param param,
                  const epix::render::texture::GpuImage* previous);
    EPIX_API epix::assets::RenderAssetUsages asset_usage(const epix::image::Image& asset) noexcept;
    std::optional<std::size_t> byte_len(const epix::image::Image& asset) const noexcept {
        return asset.has_data() ? std::optional{asset.raw_view().size_bytes()} : std::nullopt;
    }
    /** @brief Move render-only pixel data out of the stored image while
     * retaining its metadata in the main-world asset (Bevy
     * `GpuImage::take_gpu_data`). */
    std::expected<epix::image::Image, epix::render::AssetExtractionError> take_gpu_data(
        epix::image::Image& source, const epix::render::texture::GpuImage* previous_gpu_asset) const {
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
