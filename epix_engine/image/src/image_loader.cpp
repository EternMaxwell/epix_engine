#include <spdlog/spdlog.h>
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <epix/image/image_loader.hpp>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace epix::image {
namespace {

/** @brief The decoder's failure text, or a stable fallback when it has none. */
std::string decode_failure_message() {
    const char* reason = stbi_failure_reason();
    return reason ? std::string(reason) : std::string("unknown decoder failure");
}

/** @brief Lowercased file extension of `path`, without the leading dot. */
std::string lowercased_extension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    if (!extension.empty() && extension.front() == '.') extension.erase(extension.begin());
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

/** @brief Whether `extension` is one of the extensions this loader advertises. */
bool is_supported_extension(std::string_view extension) {
    const auto supported = ImageLoader::extensions();
    return std::ranges::find(supported, extension) != supported.end();
}

/**
 * @brief Bevy's `ImageFormat` debug name for an extension carrying a format
 * Epix's `stb_image` decoder cannot decode.
 *
 * Bevy reports these through `TextureError::UnsupportedTextureFormat` when the
 * `image` crate has no converter for the format (or the format's feature is
 * disabled). Epix has no `image` crate, so the same condition is "the loader
 * knows the extension but the decoder cannot read it", and the payload keeps
 * Bevy's format name so the reported text matches.
 */
std::optional<std::string_view> undecodable_format_name(std::string_view extension) {
    if (extension == "basis") return "Basis";
    if (extension == "dds") return "Dds";
    if (extension == "exr") return "OpenExr";
    if (extension == "ff" || extension == "farbfeld") return "Farbfeld";
    if (extension == "ico") return "Ico";
    if (extension == "ktx2") return "Ktx2";
    if (extension == "qoi") return "Qoi";
    if (extension == "tif" || extension == "tiff") return "Tiff";
    if (extension == "webp") return "WebP";
    return std::nullopt;
}

/** @brief Rust `Debug` text of a `DataFormat` (Bevy derives `Debug`). */
std::string_view data_format_debug_name(DataFormat format) noexcept {
    switch (format) {
        case DataFormat::Rgb:
            return "Rgb";
        case DataFormat::Rgba:
            return "Rgba";
        case DataFormat::Rrr:
            return "Rrr";
        case DataFormat::Rrrg:
            return "Rrrg";
        case DataFormat::Rg:
            return "Rg";
    }
    return "Rgb";
}

/** @brief Rust `Debug` text of a `TranscodeFormat`, used by
 * `TextureError::FormatRequiresTranscodingError`'s message. */
std::string transcode_format_debug_name(const TranscodeFormat& format) {
    return std::visit(
        [](const auto& value) -> std::string {
            using Alternative = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Alternative, TranscodeFormat::Etc1s>) {
                return "Etc1s";
            } else if constexpr (std::same_as<Alternative, TranscodeFormat::Uastc>) {
                return std::format("Uastc({})", data_format_debug_name(value.format));
            } else if constexpr (std::same_as<Alternative, TranscodeFormat::R8UnormSrgb>) {
                return "R8UnormSrgb";
            } else if constexpr (std::same_as<Alternative, TranscodeFormat::Rg8UnormSrgb>) {
                return "Rg8UnormSrgb";
            } else {
                return "Rgb8";
            }
        },
        format.value);
}

/**
 * @brief Classify a file extension before decoding, exactly where Bevy calls
 * `ImageFormat::from_extension` and then `as_image_crate_format`.
 *
 * @return A `TextureError` when the extension cannot be decoded, or
 * `std::nullopt` when decoding should be attempted.
 */
std::optional<TextureError> extension_error(std::string_view extension) {
    if (const auto format = undecodable_format_name(extension)) {
        return TextureError{TextureError::UnsupportedTextureFormat{std::string(*format)}};
    }
    if (extension.empty() || !is_supported_extension(extension)) {
        return TextureError{TextureError::InvalidImageExtension{std::string(extension)}};
    }
    return std::nullopt;
}

/** @brief Read a whole file into memory, reporting failures as `ImageError`. */
std::expected<std::vector<std::byte>, TextureError> read_file_bytes(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        // `image`'s equivalent of a missing file is an io error surfaced through
        // `TextureError::ImageError`.
        const auto message = error ? error.message() : std::string("No such file or directory");
        return std::unexpected(TextureError{TextureError::ImageError{std::format("{}: {}", path.string(), message)}});
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::unexpected(
            TextureError{TextureError::ImageError{std::format("{}: could not open the file", path.string())}});
    }
    // `std::byte` has no implicit conversion from `char`, so the file is read as
    // characters and reinterpreted once.
    const std::vector<char> buffer{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad()) {
        return std::unexpected(TextureError{TextureError::ImageError{std::format("{}: read failed", path.string())}});
    }
    std::vector<std::byte> bytes(buffer.size());
    std::memcpy(bytes.data(), buffer.data(), buffer.size());
    return bytes;
}

/**
 * @brief Widen three-channel pixels to four channels with an opaque alpha.
 *
 * WebGPU has no three-channel texture format, so Bevy's `Image::from_dynamic`
 * converts `Rgb8` to `Rgba8` (and the 16-bit/float equivalents) while building
 * the image, and Epix's `Format` therefore has no three-channel alternative
 * either. `stb_image` still returns three channels for a colour-type-2 PNG or a
 * Radiance HDR, so the decoded bytes are widened here, before the `Image`
 * exists, rather than through a conversion of an already-created image.
 */
template <typename Element>
std::vector<std::byte> widen_rgb_to_rgba(const Element* pixels, std::size_t pixel_count, Element opaque) {
    std::vector<std::byte> output(pixel_count * 4 * sizeof(Element));
    auto* target = reinterpret_cast<Element*>(output.data());
    for (std::size_t index = 0; index < pixel_count; ++index) {
        target[index * 4 + 0] = pixels[index * 3 + 0];
        target[index * 4 + 1] = pixels[index * 3 + 1];
        target[index * 4 + 2] = pixels[index * 3 + 2];
        target[index * 4 + 3] = opaque;
    }
    return output;
}

/** @brief Copy raw decoded pixels into the byte payload of an `Image`. */
template <typename Element>
std::vector<std::byte> copy_pixels(const Element* pixels, std::size_t count) {
    std::vector<std::byte> output(count * sizeof(Element));
    std::memcpy(output.data(), pixels, output.size());
    return output;
}

std::expected<Image, TextureError> make_decoded_image(int width,
                                                      int height,
                                                      const std::vector<std::byte>& payload,
                                                      Format format) {
    // `create2d`'s payload overload deduces `std::span` from its argument, which
    // only accepts an lvalue for a non-const element type, so the payload is
    // passed by const reference rather than moved.
    auto image =
        Image::create2d(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), format, payload);
    if (!image) {
        return std::unexpected(TextureError{TextureError::InvalidData{std::format(
            "decoded {}x{} {} image does not match its payload", width, height, static_cast<int>(format))}});
    }
    return std::move(*image);
}

std::expected<Image, TextureError> load_image_from_memory(const unsigned char* buffer, int size) {
    int w, h, channels;
    if (stbi_is_hdr_from_memory(buffer, size)) {
        float* pixels = stbi_loadf_from_memory(buffer, size, &w, &h, &channels, 0);
        if (!pixels) return std::unexpected(TextureError{TextureError::ImageError{decode_failure_message()}});
        const auto pixel_count = static_cast<std::size_t>(w) * h;
        // `stb_image` returns two channels only for grey+alpha data, which has no
        // three-channel counterpart, so the existing Grey32F mapping is kept.
        const Format format = channels >= 3 ? Format::RGBA32F : Format::Grey32F;
        auto payload        = channels == 3 ? widen_rgb_to_rgba(pixels, pixel_count, 1.0f)
                                            : copy_pixels(pixels, pixel_count * static_cast<std::size_t>(channels));
        stbi_image_free(pixels);
        return make_decoded_image(w, h, payload, format);
    }

    if (stbi_is_16_bit_from_memory(buffer, size)) {
        unsigned short* pixels = stbi_load_16_from_memory(buffer, size, &w, &h, &channels, 0);
        if (!pixels) return std::unexpected(TextureError{TextureError::ImageError{decode_failure_message()}});
        const auto pixel_count = static_cast<std::size_t>(w) * h;
        // `Format` has no 16-bit grey+alpha alternative, so two-channel data keeps
        // the pre-existing Grey16 mapping and three-channel data is widened.
        const Format format = channels >= 3 ? Format::RGBA16 : Format::Grey16;
        auto payload = channels == 3 ? widen_rgb_to_rgba(pixels, pixel_count, static_cast<unsigned short>(0xFFFF))
                                     : copy_pixels(pixels, pixel_count * static_cast<std::size_t>(channels));
        stbi_image_free(pixels);
        return make_decoded_image(w, h, payload, format);
    }

    stbi_uc* pixels = stbi_load_from_memory(buffer, size, &w, &h, &channels, 0);
    if (!pixels) return std::unexpected(TextureError{TextureError::ImageError{decode_failure_message()}});
    const auto pixel_count = static_cast<std::size_t>(w) * h;
    const Format format    = channels == 4   ? Format::RGBA8
                             : channels == 3 ? Format::RGBA8
                             : channels == 2 ? Format::GreyAlpha8
                                             : Format::Grey8;
    auto payload           = channels == 3 ? widen_rgb_to_rgba(pixels, pixel_count, static_cast<stbi_uc>(0xFF))
                                           : copy_pixels(pixels, pixel_count * static_cast<std::size_t>(channels));
    stbi_image_free(pixels);
    return make_decoded_image(w, h, payload, format);
}

/** @brief Retain the decoded image for the render world only, as before this
 * batch. `ImageLoaderSettings::asset_usage` is tracked separately (G22). */
Image finalize_loaded_image(Image result) {
    result.set_usage(assets::RenderAssetUsages::RENDER_WORLD);
    return result;
}

}  // namespace

std::string to_string(const TextureReinterpretationError& error) {
    return std::visit(
        [](const auto& value) -> std::string {
            using Alternative = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Alternative, TextureReinterpretationError::IncompatibleSizes>) {
                // Rust `Debug` of `Extent3d`, which Bevy's message embeds.
                const auto debug_extent = [](const wgpu::Extent3D& extent) {
                    return std::format("Extent3d {{ width: {}, height: {}, depth_or_array_layers: {} }}", extent.width,
                                       extent.height, extent.depthOrArrayLayers);
                };
                return std::format("incompatible sizes: old = {} new = {}", debug_extent(value.old),
                                   debug_extent(value.new_size));
            } else if constexpr (std::same_as<Alternative, TextureReinterpretationError::WrongDimension>) {
                return std::string("must be a 2d image");
            } else if constexpr (std::same_as<Alternative, TextureReinterpretationError::InvalidLayerCount>) {
                return std::string("must not already be a layered image");
            } else {
                return std::format("can not evenly divide height = {} by layers = {}", value.height, value.layer_count);
            }
        },
        error.value);
}

std::string to_string(const TextureError& error) {
    return std::visit(
        [](const auto& value) -> std::string {
            using Alternative = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Alternative, TextureError::InvalidImageMimeType>) {
                return std::format("invalid image mime type: {}", value.mime_type);
            } else if constexpr (std::same_as<Alternative, TextureError::InvalidImageExtension>) {
                return std::format("invalid image extension: {}", value.extension);
            } else if constexpr (std::same_as<Alternative, TextureError::ImageError>) {
                return std::format("failed to load an image: {}", value.message);
            } else if constexpr (std::same_as<Alternative, TextureError::UnsupportedTextureFormat>) {
                return std::format("unsupported texture format: {}", value.format);
            } else if constexpr (std::same_as<Alternative, TextureError::SuperCompressionNotSupported>) {
                return std::format("supercompression not supported: {}", value.format);
            } else if constexpr (std::same_as<Alternative, TextureError::SuperDecompressionError>) {
                return std::format("failed to decompress an image: {}", value.message);
            } else if constexpr (std::same_as<Alternative, TextureError::InvalidData>) {
                return std::format("invalid data: {}", value.message);
            } else if constexpr (std::same_as<Alternative, TextureError::TranscodeError>) {
                return std::format("transcode error: {}", value.message);
            } else if constexpr (std::same_as<Alternative, TextureError::FormatRequiresTranscodingError>) {
                return std::format("format requires transcoding: {}", transcode_format_debug_name(value.format));
            } else {
                return std::string("only cubemaps with six faces are supported");
            }
        },
        error.value);
}

std::exception_ptr to_exception_ptr(const TextureError& error) {
    return std::make_exception_ptr(std::runtime_error(to_string(error)));
}

std::string to_string(const FileTextureError& error) {
    return std::format("Error reading image file {}: {}.", error.path, to_string(error.error));
}

std::string to_string(const ImageLoaderError& error) {
    return std::visit(
        [](const auto& value) -> std::string {
            using Alternative = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Alternative, ImageLoaderError::Io>) {
                return std::format("Failed to load image bytes: {}", value.code.message());
            } else if constexpr (std::same_as<Alternative, ImageLoaderError::FileTexture>) {
                return std::format("Could not load texture file: {}", to_string(value.error));
            } else {
                return std::format("Invalid array layout: {}", to_string(value.error));
            }
        },
        error.value);
}

std::exception_ptr to_exception_ptr(const ImageLoaderError& error) {
    return std::make_exception_ptr(std::runtime_error(to_string(error)));
}

std::expected<Image, TextureError> Image::load(const std::filesystem::path& path) {
    // `Image::load` is the documented Epix filesystem helper, so it stays
    // decoder-driven exactly as before: it does not gate on the extension.
    // Bevy's extension classification (`ImageFormat::from_extension` followed
    // by `as_image_crate_format`) lives in `ImageLoader::load`, which is the
    // asset-server path Bevy models.
    auto bytes = read_file_bytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    if (bytes->empty()) {
        return std::unexpected(
            TextureError{TextureError::ImageError{std::format("{}: the file is empty", path.string())}});
    }
    // Both entry points share one decode path, so a filesystem load and an
    // asset-server load cannot disagree about formats or payload layout.
    return load_image_from_memory(reinterpret_cast<const unsigned char*>(bytes->data()),
                                  static_cast<int>(bytes->size()));
}

std::expected<void, ImageSaveError> Image::save(const std::filesystem::path& path, const Image& image) {
    if (image.type() == ImageType::e2DArray || image.type() == ImageType::e3D) {
        return std::unexpected(ImageSaveError::SaveFailed);
    }

    const auto& info       = image.format_info();
    auto extension         = path.extension().string();
    const auto path_string = path.string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) { return std::tolower(value); });

    // A `Format` with no `wgpu::TextureFormat` counterpart (the three-channel
    // alternatives) reports `Format::Unknown`, and `getFormatInfo` then reports
    // zero channels, which would make the encoders write a corrupt file. Refuse
    // instead of writing invalid data.
    if (image.format() == Format::Unknown) {
        spdlog::error("[image] Refusing to save '{}': the image's format has no channel description.", path_string);
        return std::unexpected(ImageSaveError::SaveFailed);
    }

    bool success = false;
    if (info.isFloat) {
        success = stbi_write_hdr(path_string.c_str(), image.width(), image.height(), info.channels, image.raw<float>());
    } else {
        if (info.is16Bit && extension == ".png") {
            std::cerr << "Warning: Saving 16-bit directly might not be supported by all formats.\n";
        }
        if (extension == ".png") {
            success = stbi_write_png(path_string.c_str(), image.width(), image.height(), info.channels,
                                     image.data.data(), image.width() * info.pixelSize());
        } else if (extension == ".jpg" || extension == ".jpeg") {
            success = stbi_write_jpg(path_string.c_str(), image.width(), image.height(), info.channels,
                                     image.data.data(), 90);
        } else if (extension == ".bmp") {
            success =
                stbi_write_bmp(path_string.c_str(), image.width(), image.height(), info.channels, image.data.data());
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

STDEXEC::task<std::expected<Image, ImageLoaderError>> ImageLoader::load(assets::Reader& reader,
                                                                        const Settings&,
                                                                        assets::LoadContext& context) {
    const auto& path = context.path().path;
    spdlog::trace("[image] Loading image from '{}'.", path.string());
    const auto path_string = path.string();
    // Bevy's ImageLoader defaults to `ImageFormatSetting::FromExtension` and
    // reports the format errors before decoding.
    if (const auto error = extension_error(lowercased_extension(path))) {
        co_return std::unexpected(
            ImageLoaderError{ImageLoaderError::FileTexture{FileTextureError{*error, path_string}}});
    }

    std::vector<std::uint8_t> bytes;
    const auto read_result = co_await reader.read_to_end(bytes);
    if (!read_result) {
        co_return std::unexpected(ImageLoaderError{ImageLoaderError::Io{read_result.error()}});
    }

    auto image = load_image_from_memory(bytes.data(), static_cast<int>(bytes.size()));
    if (!image) {
        co_return std::unexpected(
            ImageLoaderError{ImageLoaderError::FileTexture{FileTextureError{image.error(), path_string}}});
    }
    co_return finalize_loaded_image(std::move(*image));
}

}  // namespace epix::image
