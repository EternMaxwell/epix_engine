#include <epix/image/image_loader.hpp>

#include <spdlog/spdlog.h>
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace epix::image {
namespace {

std::expected<Image, ImageLoadError> load_image_from_memory(const unsigned char* buffer, int size) {
    int w, h, channels;
    if (stbi_is_hdr_from_memory(buffer, size)) {
        float* pixels = stbi_loadf_from_memory(buffer, size, &w, &h, &channels, 0);
        if (!pixels) return std::unexpected(ImageLoadError::LoadFailed);

        const Format format = channels == 4 ? Format::RGBA32F : channels == 3 ? Format::RGB32F : Format::Grey32F;
        const auto byte_size = static_cast<std::size_t>(w) * h * channels * sizeof(float);
        std::vector<std::byte> output(byte_size);
        std::memcpy(output.data(), pixels, byte_size);
        stbi_image_free(pixels);
        return Image::create2d(w, h, format, output).value();
    }

    if (stbi_is_16_bit_from_memory(buffer, size)) {
        unsigned short* pixels = stbi_load_16_from_memory(buffer, size, &w, &h, &channels, 0);
        if (!pixels) return std::unexpected(ImageLoadError::LoadFailed);

        const Format format = channels == 4 ? Format::RGBA16 : channels == 3 ? Format::RGB16 : Format::Grey16;
        const auto byte_size = static_cast<std::size_t>(w) * h * channels * sizeof(unsigned short);
        std::vector<std::byte> output(byte_size);
        std::memcpy(output.data(), pixels, byte_size);
        stbi_image_free(pixels);
        return Image::create2d(w, h, format, output).value();
    }

    stbi_uc* pixels = stbi_load_from_memory(buffer, size, &w, &h, &channels, 0);
    if (!pixels) return std::unexpected(ImageLoadError::LoadFailed);

    const Format format = channels == 4   ? Format::RGBA8
                          : channels == 3 ? Format::RGB8
                          : channels == 2 ? Format::GreyAlpha8
                                          : Format::Grey8;
    const auto byte_size = static_cast<std::size_t>(w) * h * channels;
    std::vector<std::byte> output(byte_size);
    std::memcpy(output.data(), pixels, byte_size);
    stbi_image_free(pixels);
    return Image::create2d(w, h, format, output).value();
}

}  // namespace

std::exception_ptr to_exception_ptr(ImageLoadError error) {
    switch (error) {
        case ImageLoadError::FileNotFound:
            return std::make_exception_ptr(std::runtime_error("Image file not found"));
        case ImageLoadError::UnsupportedFormat:
            return std::make_exception_ptr(std::runtime_error("Unsupported image format"));
        case ImageLoadError::LoadFailed:
            return std::make_exception_ptr(std::runtime_error("Image load failed"));
        default:
            return std::make_exception_ptr(std::runtime_error("Unknown image load error"));
    }
}

std::expected<Image, ImageLoadError> Image::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return std::unexpected(ImageLoadError::FileNotFound);

    const auto path_string = path.string();
    int width, height, channels;
    if (stbi_is_hdr(path_string.c_str())) {
        float* pixels = stbi_loadf(path_string.c_str(), &width, &height, &channels, 0);
        if (!pixels) return std::unexpected(ImageLoadError::LoadFailed);
        const Format format = channels == 4 ? Format::RGBA32F : channels == 3 ? Format::RGB32F : Format::Grey32F;
        const auto byte_size = static_cast<std::size_t>(width) * height * channels * sizeof(float);
        std::vector<std::byte> bytes(byte_size);
        std::memcpy(bytes.data(), pixels, byte_size);
        stbi_image_free(pixels);
        return Image::create2d(width, height, format, bytes).value();
    }
    if (stbi_is_16_bit(path_string.c_str())) {
        unsigned short* pixels = stbi_load_16(path_string.c_str(), &width, &height, &channels, 0);
        if (!pixels) return std::unexpected(ImageLoadError::LoadFailed);
        const Format format = channels == 4 ? Format::RGBA16 : channels == 3 ? Format::RGB16 : Format::Grey16;
        const auto byte_size = static_cast<std::size_t>(width) * height * channels * sizeof(unsigned short);
        std::vector<std::byte> bytes(byte_size);
        std::memcpy(bytes.data(), pixels, byte_size);
        stbi_image_free(pixels);
        return Image::create2d(width, height, format, bytes).value();
    }

    stbi_uc* pixels = stbi_load(path_string.c_str(), &width, &height, &channels, 0);
    if (!pixels) return std::unexpected(ImageLoadError::LoadFailed);
    const Format format = channels == 4   ? Format::RGBA8
                          : channels == 3 ? Format::RGB8
                          : channels == 2 ? Format::GreyAlpha8
                                          : Format::Grey8;
    const auto byte_size = static_cast<std::size_t>(width) * height * channels;
    std::vector<std::byte> bytes(byte_size);
    std::memcpy(bytes.data(), pixels, byte_size);
    stbi_image_free(pixels);
    return Image::create2d(width, height, format, bytes).value();
}

std::expected<void, ImageSaveError> Image::save(const std::filesystem::path& path, const Image& image) {
    if (image.type() == ImageType::e2DArray || image.type() == ImageType::e3D) {
        return std::unexpected(ImageSaveError::SaveFailed);
    }

    const auto& info = image.format_info();
    auto extension   = path.extension().string();
    const auto path_string = path.string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) { return std::tolower(value); });

    bool success = false;
    if (info.isFloat) {
        success = stbi_write_hdr(path_string.c_str(), image.m_width, image.m_height, info.channels, image.raw<float>());
    } else {
        if (info.is16Bit && extension == ".png") {
            std::cerr << "Warning: Saving 16-bit directly might not be supported by all formats.\n";
        }
        if (extension == ".png") {
            success = stbi_write_png(path_string.c_str(), image.m_width, image.m_height, info.channels,
                                     image.data.data(), image.m_width * info.pixelSize());
        } else if (extension == ".jpg" || extension == ".jpeg") {
            success = stbi_write_jpg(path_string.c_str(), image.m_width, image.m_height, info.channels,
                                     image.data.data(), 90);
        } else if (extension == ".bmp") {
            success = stbi_write_bmp(path_string.c_str(), image.m_width, image.m_height, info.channels,
                                     image.data.data());
        }
    }
    if (!success) return std::unexpected(ImageSaveError::SaveFailed);
    return {};
}

std::span<std::string_view> ImageLoader::extensions() noexcept {
    static auto extensions = std::array{
        std::string_view{"png"}, std::string_view{"jpg"}, std::string_view{"jpeg"}, std::string_view{"bmp"},
        std::string_view{"tga"}, std::string_view{"hdr"}, std::string_view{"pic"},  std::string_view{"psd"},
        std::string_view{"gif"}, std::string_view{"ppm"}, std::string_view{"pgm"},  std::string_view{"pnm"},
    };
    return extensions;
}

STDEXEC::task<std::expected<Image, ImageLoadError>> ImageLoader::load(assets::Reader& reader,
                                                                      const Settings&,
                                                                      assets::LoadContext& context) {
    spdlog::trace("[image] Loading image from '{}'.", context.path().path.string());
    std::vector<std::uint8_t> bytes;
    const auto read_result = co_await reader.read_to_end(bytes);
    if (!read_result) co_return std::unexpected(ImageLoadError::LoadFailed);

    auto image = load_image_from_memory(bytes.data(), static_cast<int>(bytes.size()));
    if (!image) co_return std::unexpected(image.error());

    auto result = std::move(*image);
    switch (result.format()) {
        case Format::RGB8:
            result = result.convert(Format::RGBA8);
            break;
        case Format::RGB16:
            result = result.convert(Format::RGBA16);
            break;
        case Format::RGB32F:
            result = result.convert(Format::RGBA32F);
            break;
        default:
            break;
    }
    result.set_usage(ImageUsage::Render);
    co_return result;
}

}  // namespace epix::image
