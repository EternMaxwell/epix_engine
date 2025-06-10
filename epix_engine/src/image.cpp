#include <stb_image.h>

#include "epix/image.h"

using namespace epix::image;

bool ImageView::operator==(const ImageView& other) const {
    return image.id() == other.image.id() && info == other.info;
}

EPIX_API size_t std::hash<ImageView>::operator()(const ImageView& image_view
) const noexcept {
    size_t seed =
        std::hash<epix::assets::AssetId<Image>>{}(image_view.image.id());
    // combine the hash with each field of the info
    // flags
    seed ^= std::hash<VkImageViewCreateFlags>{}((VkImageViewCreateFlags
            )image_view.info.flags) +
            0x9e3779b9 + (seed << 6) + (seed >> 2);
    // image
    seed ^= std::hash<VkImage>{}(image_view.info.image) + 0x9e3779b9 +
            (seed << 6) + (seed >> 2);
    // viewType
    seed ^= std::hash<VkImageViewType>{}((VkImageViewType
            )image_view.info.viewType) +
            0x9e3779b9 + (seed << 6) + (seed >> 2);
    // format
    seed ^= std::hash<VkFormat>{}((VkFormat)image_view.info.format) +
            0x9e3779b9 + (seed << 6) + (seed >> 2);
    // components
    seed ^= std::hash<VkComponentSwizzle>{}((VkComponentSwizzle
            )image_view.info.components.r) +
            0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<VkComponentSwizzle>{}((VkComponentSwizzle
            )image_view.info.components.g) +
            0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<VkComponentSwizzle>{}((VkComponentSwizzle
            )image_view.info.components.b) +
            0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<VkComponentSwizzle>{}((VkComponentSwizzle
            )image_view.info.components.a) +
            0x9e3779b9 + (seed << 6) + (seed >> 2);
    // subresourceRange
    seed ^= std::hash<VkImageAspectFlags>{}((VkImageAspectFlags
            )image_view.info.subresourceRange.aspectMask) +
            0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^=
        std::hash<uint32_t>{}(image_view.info.subresourceRange.baseMipLevel) +
        0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<uint32_t>{}(image_view.info.subresourceRange.levelCount) +
            0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^=
        std::hash<uint32_t>{}(image_view.info.subresourceRange.baseArrayLayer) +
        0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<uint32_t>{}(image_view.info.subresourceRange.layerCount) +
            0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}

EPIX_API const decltype(StbImageLoader::exts)& StbImageLoader::extensions(
) noexcept {
    return exts;
}
EPIX_API Image StbImageLoader::load(
    const std::filesystem::path& path, assets::LoadContext& context
) {
    Image image;
    int width, height, channels;
    auto path_str = path.string();
    bool hdr      = stbi_is_hdr(path_str.c_str());
    bool bit16    = stbi_is_16_bit(path_str.c_str());

    // default infos for stbi loaded images
    image.info.imageType   = vk::ImageType::e2D;
    image.info.mipLevels   = 1;
    image.info.arrayLayers = 1;
    image.info.samples     = vk::SampleCountFlagBits::e1;
    image.info.tiling      = vk::ImageTiling::eOptimal;
    image.info.usage =
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
    image.info.sharingMode           = vk::SharingMode::eExclusive;
    image.info.initialLayout         = vk::ImageLayout::eUndefined;
    image.info.queueFamilyIndexCount = 0;
    image.info.pQueueFamilyIndices   = nullptr;

    if (hdr) {
        // this is an HDR image, use float for storage
        auto loaded =
            stbi_loadf(path_str.c_str(), &width, &height, &channels, 4);
        if (!loaded) {
            throw std::runtime_error("Fail to load HDR image: " + path_str);
        }
        image.data.resize(width * height * 4 * sizeof(float));
        std::memcpy(image.data.data(), loaded, image.data.size());
        stbi_image_free(loaded);
        image.info.format = vk::Format::eR32G32B32A32Sfloat;
    } else if (bit16) {
        // this is a 16-bit image, use half-float for storage
        auto loaded =
            stbi_load_16(path_str.c_str(), &width, &height, &channels, 4);
        if (!loaded) {
            throw std::runtime_error("Fail to load 16-bit image: " + path_str);
        }
        image.data.resize(width * height * 4 * sizeof(uint16_t));
        std::memcpy(image.data.data(), loaded, image.data.size());
        stbi_image_free(loaded);
        image.info.format = vk::Format::eR16G16B16A16Sfloat;
    } else {
        // this is an 8-bit image, use uint8_t for storage
        auto loaded =
            stbi_load(path_str.c_str(), &width, &height, &channels, 4);
        if (!loaded) {
            throw std::runtime_error("Fail to load 8-bit image: " + path_str);
        }
        image.data.resize(width * height * 4);
        std::memcpy(image.data.data(), loaded, image.data.size());
        stbi_image_free(loaded);
        image.info.format = vk::Format::eR8G8B8A8Srgb;
    }

    // size assigned here after all possible branches
    image.info.extent = vk::Extent3D{
        static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1
    };

    return std::move(image);
}

EPIX_API void ImagePlugin::build(epix::App& app) {
    app.plugin_scope([](epix::assets::AssetPlugin& asset_plugin) {
        asset_plugin.register_asset<Image>().register_loader<StbImageLoader>();
    });
}