#include <stb_image.h>

#include "epix/image.hpp"
#include "nvrhi/nvrhi.h"

using namespace epix::image;

Image Image::srgba8norm(uint32_t width, uint32_t height) {
    Image image;
    image.info.format    = nvrhi::Format::SRGBA8_UNORM;
    image.info.dimension = nvrhi::TextureDimension::Texture2D;
    image.info.width     = width;
    image.info.height    = height;
    image.data.resize(width * height * 4);
    image.main_world   = 1;
    image.render_world = 1;
    return image;
}

Image Image::srgba8unorm_render(uint32_t width, uint32_t height) {
    Image image        = srgba8norm(width, height);
    image.main_world   = 0;
    image.render_world = 1;
    return image;
}

Image Image::srgba8unorm_main(uint32_t width, uint32_t height) {
    Image image        = srgba8norm(width, height);
    image.main_world   = 1;
    image.render_world = 0;
    return image;
}

Image Image::rgba32float(uint32_t width, uint32_t height) {
    Image image;
    image.info.format    = nvrhi::Format::RGBA32_FLOAT;
    image.info.dimension = nvrhi::TextureDimension::Texture2D;
    image.info.width     = width;
    image.info.height    = height;
    image.data.resize(width * height * 16);  // 4 floats, 4 bytes each

    image.main_world   = 1;
    image.render_world = 1;
    return image;
}

Image Image::rgba32float_render(uint32_t width, uint32_t height) {
    Image image        = rgba32float(width, height);
    image.main_world   = 0;
    image.render_world = 1;
    return image;
}

Image Image::rgba32float_main(uint32_t width, uint32_t height) {
    Image image        = rgba32float(width, height);
    image.main_world   = 1;
    image.render_world = 0;
    return image;
}

std::optional<Image> Image::with_desc(const nvrhi::TextureDesc& desc) {
    Image image;
    image.info        = desc;
    size_t pixel_size = nvrhi::getFormatInfo(desc.format).bytesPerBlock;
    if (nvrhi::getFormatInfo(desc.format).blockSize != 1) {
        return std::nullopt;
    }
    image.data.resize(desc.width * desc.height * desc.depth * desc.arraySize * pixel_size, 0);
    return image;
}

std::expected<void, ImageDataError> Image::set_data_internal(Rect rect, const void* data, size_t data_size) {
    if (nvrhi::getFormatInfo(info.format).blockSize != 1) {
        return std::unexpected(CompressedImageNotSupported{info.format});
    }
    if (rect.x + rect.width > info.width || rect.y + rect.height > info.height || rect.z + rect.depth > info.depth ||
        rect.layer + rect.layers > info.arraySize) {
        return std::unexpected(ExtendOutOfBoundsError{});
    }
    size_t bytes_per_pixel = nvrhi::getFormatInfo(info.format).bytesPerBlock;
    if (bytes_per_pixel * rect.width * rect.height * rect.depth != data_size) {
        return std::unexpected(SizeMismatchError{.expected_size = nvrhi::getFormatInfo(info.format).bytesPerBlock,
                                                 .given_size    = data_size});
    }
    for (size_t layer = 0; layer < rect.layers; layer++) {
        for (size_t z = 0; z < rect.depth; z++) {
            for (size_t y = 0; y < rect.height; y++) {
                size_t dest_offset = ((rect.layer + layer) * info.width * info.height * info.depth +
                                      (rect.z + z) * info.width * info.height + (rect.y + y) * info.width + rect.x) *
                                     bytes_per_pixel;
                size_t src_offset =
                    (layer * rect.depth * rect.height + z * rect.height + y) * rect.width * bytes_per_pixel;
                std::memcpy(&this->data[dest_offset], reinterpret_cast<const uint8_t*>(data) + src_offset,
                            rect.width * bytes_per_pixel);
            }
        }
    }
    return {};
}

void Image::flip_vertical() {
    if (nvrhi::getFormatInfo(info.format).blockSize != 1) {
        return;
    }
    size_t bytes_per_pixel = nvrhi::getFormatInfo(info.format).bytesPerBlock;
    size_t row_bytes       = info.width * bytes_per_pixel;
    std::vector<uint8_t> temp(row_bytes);
    for (size_t row = 0; row < info.height / 2; row++) {
        size_t top_start    = row * row_bytes;
        size_t bottom_start = (info.height - row - 1) * row_bytes;
        std::memcpy(temp.data(), &data[top_start], row_bytes);
        std::memcpy(&data[top_start], &data[bottom_start], row_bytes);
        std::memcpy(&data[bottom_start], temp.data(), row_bytes);
    }
}

std::span<const char* const> StbImageLoader::extensions() noexcept {
    static constexpr auto exts =
        std::array{".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".pic", ".psd", ".gif", ".ppm", ".pgm", ".pnm"};
    return exts;
}
Image StbImageLoader::load(const std::filesystem::path& path, assets::LoadContext& context) {
    Image image;
    int width, height, channels;
    auto path_str = path.string();
    bool hdr      = stbi_is_hdr(path_str.c_str());
    bool bit16    = stbi_is_16_bit(path_str.c_str());

    // default infos for stbi loaded images
    image.info.dimension        = nvrhi::TextureDimension::Texture2D;
    image.info.initialState     = nvrhi::ResourceStates::ShaderResource;
    image.info.keepInitialState = true;

    if (hdr) {
        // this is an HDR image, use float for storage
        auto loaded = stbi_loadf(path_str.c_str(), &width, &height, &channels, 4);
        if (!loaded) {
            throw std::runtime_error("Fail to load HDR image: " + path_str);
        }
        image.data.resize(width * height * 4 * sizeof(float));
        std::memcpy(image.data.data(), loaded, image.data.size());
        stbi_image_free(loaded);
        image.info.format = nvrhi::Format::RGBA32_FLOAT;
    } else if (bit16) {
        // this is a 16-bit image, use uint16_t for storage
        auto loaded = stbi_load_16(path_str.c_str(), &width, &height, &channels, 4);
        if (!loaded) {
            throw std::runtime_error("Fail to load 16-bit image: " + path_str);
        }
        image.data.resize(width * height * 4 * sizeof(uint16_t));
        std::memcpy(image.data.data(), loaded, image.data.size());
        stbi_image_free(loaded);
        image.info.format = nvrhi::Format::RGBA16_UINT;
    } else {
        // this is an 8-bit image, use uint8_t for storage
        auto loaded = stbi_load(path_str.c_str(), &width, &height, &channels, 4);
        if (!loaded) {
            throw std::runtime_error("Fail to load 8-bit image: " + path_str);
        }
        image.data.resize(width * height * 4);
        std::memcpy(image.data.data(), loaded, image.data.size());
        stbi_image_free(loaded);
        image.info.format = nvrhi::Format::RGBA8_UINT;
    }

    // size assigned here after all possible branches
    image.info.width     = width;
    image.info.height    = height;
    image.info.depth     = 1;
    image.info.arraySize = 1;
    image.info.mipLevels = 1;

    image.render_world = 1;

    return std::move(image);
}

void ImagePlugin::build(epix::App& app) {
    app.add_plugins(epix::assets::AssetPlugin{});
    app.plugin_scope([](epix::assets::AssetPlugin& asset_plugin) {
        asset_plugin.register_asset<Image>().register_loader<StbImageLoader>();
    });
}
