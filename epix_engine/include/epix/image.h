#pragma once

#include <epix/app.h>
#include <epix/assets.h>
#include <epix/vulkan.h>

namespace epix::image {
struct Image {
    // whether this image needs to be uploaded to GPU
    bool gpu_image = true;
    // The raw data of the image
    std::vector<uint8_t> data;
    // The vulkan ImageCreateInfo
    // Currently fields in info that are pointer types are not safe to use
    vk::ImageCreateInfo info;
};

struct ImageView {
    // This handle should be set to weak handle after extracted to render world
    // If the image asset's gpu_image is false, it will be set to true.
    assets::Handle<Image> image;
    // The vulkan ImageViewCreateInfo
    // Currently fields in info that are pointer types are not safe to use
    vk::ImageViewCreateInfo info;

    // Handle is considered equal if the asset_id of the handle is the same,
    // regardless of whether it is strong or weak.
    EPIX_API bool operator==(const ImageView& other) const;
};

struct ImagePlugin : Plugin {
    EPIX_API void build(App& app) override;
};

struct StbImageLoader {
    static constexpr auto exts =
        std::array{".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr",
                   ".pic", ".psd", ".gif",  ".ppm", ".pgm", ".pnm"};
    EPIX_API static const decltype(exts)& extensions() noexcept;
    EPIX_API static Image load(
        const std::filesystem::path& path, assets::LoadContext& context
    );
};
};  // namespace epix::image

template <>
struct std::hash<epix::image::ImageView> {
    EPIX_API size_t operator()(const epix::image::ImageView& image_view
    ) const noexcept;
};