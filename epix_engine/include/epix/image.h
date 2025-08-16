#pragma once

#include <epix/app.h>
#include <epix/assets.h>
#include <epix/render/assets.h>
#include <epix/vulkan.h>

namespace epix {
namespace image {

struct SizeMismatchError {
    size_t expected_size;
    size_t given_size;
};
struct CompressedImageNotSupported {
    nvrhi::Format image_format;
};
struct ExtendOutOfBoundsError {};
struct ImageDataError : std::variant<SizeMismatchError,
                                     CompressedImageNotSupported,
                                     ExtendOutOfBoundsError> {
    using variant::variant;
};
struct Image {
    render::assets::RenderAssetUsage usage =
        render::assets::RenderAssetUsageBits::RENDER_WORLD;
    // The raw data of the image
    std::vector<uint8_t> data;
    nvrhi::TextureDesc info =
        nvrhi::TextureDesc()
            .setDepth(1)
            .setArraySize(1)
            .setMipLevels(1)
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true);

    static Image srgba8norm(uint32_t width, uint32_t height) {
        Image image;
        image.info.format    = nvrhi::Format::SRGBA8_UNORM;
        image.info.dimension = nvrhi::TextureDimension::Texture2D;
        image.info.width     = width;
        image.info.height    = height;
        image.data.resize(width * height * 4);

        image.usage = render::assets::RenderAssetUsageBits::MAIN_WORLD |
                      render::assets::RenderAssetUsageBits::RENDER_WORLD;
        return image;
    }
    static Image srgba8unorm_render(uint32_t width, uint32_t height) {
        Image image = srgba8norm(width, height);
        image.usage = render::assets::RenderAssetUsageBits::RENDER_WORLD;
        return image;
    }
    static Image srgba8unorm_main(uint32_t width, uint32_t height) {
        Image image = srgba8norm(width, height);
        image.usage = render::assets::RenderAssetUsageBits::MAIN_WORLD;
        return image;
    }
    static Image rgba32float(uint32_t width, uint32_t height) {
        Image image;
        image.info.format    = nvrhi::Format::RGBA32_FLOAT;
        image.info.dimension = nvrhi::TextureDimension::Texture2D;
        image.info.width     = width;
        image.info.height    = height;
        image.data.resize(width * height * 16);  // 4 floats, 4 bytes each

        image.usage = render::assets::RenderAssetUsageBits::MAIN_WORLD |
                      render::assets::RenderAssetUsageBits::RENDER_WORLD;
        return image;
    }
    static Image rgba32float_render(uint32_t width, uint32_t height) {
        Image image = rgba32float(width, height);
        image.usage = render::assets::RenderAssetUsageBits::RENDER_WORLD;
        return image;
    }
    static Image rgba32float_main(uint32_t width, uint32_t height) {
        Image image = rgba32float(width, height);
        image.usage = render::assets::RenderAssetUsageBits::MAIN_WORLD;
        return image;
    }
    template <typename T>
    std::expected<void, ImageDataError> set_data(size_t x,
                                                 size_t y,
                                                 size_t width,
                                                 size_t height,
                                                 std::span<const T> data) {
        if (nvrhi::getFormatInfo(info.format).blockSize != 1) {
            return std::unexpected(CompressedImageNotSupported{info.format});
        }
        if (x + width > info.width || y + height > info.height) {
            return std::unexpected(ExtendOutOfBoundsError{});
        }
        size_t bytes_per_pixel =
            nvrhi::getFormatInfo(info.format).bytesPerBlock;
        if (bytes_per_pixel * width * height != sizeof(T) * data.size()) {
            return std::unexpected(SizeMismatchError{
                .expected_size =
                    nvrhi::getFormatInfo(info.format).bytesPerBlock,
                .given_size = sizeof(T) * data.size()});
        }
        if (width == info.width) {
            // width is the same, we can do a single copy
            std::memcpy(&this->data[y * info.width * bytes_per_pixel +
                                    x * bytes_per_pixel],
                        data.data(), width * height * bytes_per_pixel);
            return {};
        }
        for (size_t row = 0; row < height; row++) {
            size_t dest_start =
                (y + row) * info.width * bytes_per_pixel + x * bytes_per_pixel;
            size_t src_start = row * width * bytes_per_pixel;
            std::memcpy(&this->data[dest_start], &data[src_start],
                        width * bytes_per_pixel);
        }
        return {};
    }
};

struct ImagePlugin {
    EPIX_API void build(App& app);
};

struct StbImageLoader {
    EPIX_API static std::span<const char* const> extensions() noexcept;
    EPIX_API static Image load(const std::filesystem::path& path,
                               assets::LoadContext& context);
};
}  // namespace image
template <>
struct epix::render::assets::RenderAsset<image::Image> {
    using Param          = ParamSet<Res<nvrhi::DeviceHandle>>;
    using ProcessedAsset = nvrhi::TextureHandle;

    EPIX_API ProcessedAsset process(image::Image&& asset, Param& param);
    EPIX_API RenderAssetUsage usage(const image::Image& asset);
};
}  // namespace epix
