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

/** @brief Classify a resolved container format exactly where Bevy calls
 * `ImageFormat::as_image_crate_format`.
 *
 * Bevy's `ImageType::to_image_format` resolves the extension or the explicit
 * format, and `from_buffer` then rejects a format the `image` crate cannot
 * decode with `TextureError::UnsupportedTextureFormat(format!("{format:?}"))`.
 * Epix's decoder is `stb_image`, so `is_decodable` takes the same decision and
 * the error keeps the same format name.
 *
 * @return A `TextureError` when the format cannot be decoded, or
 * `std::nullopt` when decoding should be attempted. */
std::optional<TextureError> format_error(ImageFormat format) {
    if (!is_decodable(format)) {
        return TextureError{TextureError::UnsupportedTextureFormat(to_string(format))};
    }
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
 * @brief Classify a file extension before decoding, exactly where Bevy resolves
 * `ImageFormatSetting::FromExtension` into `ImageType::Extension`.
 *
 * @return A `TextureError` when the extension cannot be decoded, or
 * `std::nullopt` when decoding should be attempted.
 */
std::optional<TextureError> extension_error(std::string_view extension) {
    const auto format = from_extension(extension);
    if (!format) {
        // Bevy's `ImageType::to_image_format` for `Extension` reports
        // `InvalidImageExtension` when `from_extension` returns `None`.
        return TextureError{TextureError::InvalidImageExtension{std::string(extension)}};
    }
    return format_error(*format);
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

/**
 * @brief Apply Bevy's `is_srgb` decision to a decoded image.
 *
 * Bevy's `Image::from_dynamic` chooses the sRGB texture format for 8-bit data
 * when `is_srgb` is true, so an sRGB PNG is uploaded as `Rgba8UnormSrgb` and the
 * GPU linearizes it on sample. Epix's `Format` names the pixel layout, not the
 * transfer function, so the sRGB variant is selected here on the descriptor's
 * texture format. 16-bit and float formats have no sRGB counterpart in either
 * engine and are left alone.
 */
void apply_srgb(Image& image, bool is_srgb) {
    if (!is_srgb) return;
    image.texture_descriptor.format = srgb_texture_format(image.texture_descriptor.format);
}

/** @brief The channel count `stb_image` reported for a decoded image. */
Format decoded_format(int channels, bool is_float, bool is_16_bit) noexcept {
    if (is_float) return channels >= 3 ? Format::RGBA32F : Format::Grey32F;
    if (is_16_bit) return channels >= 3 ? Format::RGBA16 : Format::Grey16;
    // `stb_image` declares the channel count it returns, so four, two, and one
    // are the only values left; anything else is treated as four.
    if (channels == 1) return Format::Grey8;
    if (channels == 2) return Format::GreyAlpha8;
    return Format::RGBA8;
}

std::expected<Image, TextureError> decode_image(const unsigned char* buffer, int size) {
    int w, h, channels;
    if (stbi_is_hdr_from_memory(buffer, size)) {
        float* pixels = stbi_loadf_from_memory(buffer, size, &w, &h, &channels, 0);
        if (!pixels) return std::unexpected(TextureError{TextureError::ImageError{decode_failure_message()}});
        const auto pixel_count = static_cast<std::size_t>(w) * h;
        // `stb_image` returns two channels only for grey+alpha data, which has no
        // three-channel counterpart, so the existing Grey32F mapping is kept.
        const Format format = decoded_format(channels, true, false);
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
        const Format format = decoded_format(channels, false, true);
        auto payload = channels == 3 ? widen_rgb_to_rgba(pixels, pixel_count, static_cast<unsigned short>(0xFFFF))
                                     : copy_pixels(pixels, pixel_count * static_cast<std::size_t>(channels));
        stbi_image_free(pixels);
        return make_decoded_image(w, h, payload, format);
    }

    stbi_uc* pixels = stbi_load_from_memory(buffer, size, &w, &h, &channels, 0);
    if (!pixels) return std::unexpected(TextureError{TextureError::ImageError{decode_failure_message()}});
    const auto pixel_count = static_cast<std::size_t>(w) * h;
    const Format format    = decoded_format(channels, false, false);
    auto payload           = channels == 3 ? widen_rgb_to_rgba(pixels, pixel_count, static_cast<stbi_uc>(0xFF))
                                           : copy_pixels(pixels, pixel_count * static_cast<std::size_t>(channels));
    stbi_image_free(pixels);
    return make_decoded_image(w, h, payload, format);
}

/**
 * @brief Interpret a decoded image as an array texture exactly where Bevy's
 * `ImageLoader` applies `ImageLoaderSettings::array_layout`.
 *
 * `RowCount` names the layer count directly; `RowHeight` divides the decoded
 * height by the height of one image, which is Bevy's own arithmetic (including
 * its truncating integer division). `RowHeight { pixels: 0 }` has no Bevy
 * counterpart (it would divide by zero and panic) and is reported as
 * `HeightNotDivisibleByLayers`, matching the rejection
 * `reinterpret_stacked_2d_as_array` applies to a zero layer count.
 */
std::expected<std::uint32_t, TextureReinterpretationError> array_layer_count(const Image& image,
                                                                            const ImageArrayLayout& layout) {
    return std::visit(
        [&image](const auto& alternative) -> std::expected<std::uint32_t, TextureReinterpretationError> {
            using Alternative = std::remove_cvref_t<decltype(alternative)>;
            if constexpr (std::same_as<Alternative, ImageArrayLayout::RowCount>) {
                return alternative.rows;
            } else {
                if (alternative.pixels == 0) {
                    return std::unexpected(TextureReinterpretationError{
                        TextureReinterpretationError::HeightNotDivisibleByLayers{image.height(), 0}});
                }
                return image.height() / alternative.pixels;
            }
        },
        layout.alternatives);
}

}  // namespace

std::string to_string(ImageFormat format) {
    // Bevy's `UnsupportedTextureFormat` payload is `format!("{format:?}")`, i.e.
    // the variant name, so the Epix name has to match it exactly.
    switch (format) {
        case ImageFormat::Basis:
            return "Basis";
        case ImageFormat::Bmp:
            return "Bmp";
        case ImageFormat::Dds:
            return "Dds";
        case ImageFormat::Farbfeld:
            return "Farbfeld";
        case ImageFormat::Gif:
            return "Gif";
        case ImageFormat::OpenExr:
            return "OpenExr";
        case ImageFormat::Hdr:
            return "Hdr";
        case ImageFormat::Ico:
            return "Ico";
        case ImageFormat::Jpeg:
            return "Jpeg";
        case ImageFormat::Ktx2:
            return "Ktx2";
        case ImageFormat::Png:
            return "Png";
        case ImageFormat::Pnm:
            return "Pnm";
        case ImageFormat::Qoi:
            return "Qoi";
        case ImageFormat::Tga:
            return "Tga";
        case ImageFormat::Tiff:
            return "Tiff";
        case ImageFormat::WebP:
            return "WebP";
        case ImageFormat::Pic:
            return "Pic";
        case ImageFormat::Psd:
            return "Psd";
    }
    return "Unknown";
}

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
    // Epix's filesystem helper has no path-derived format, so it decodes through
    // the bytes with Bevy's defaults, exactly like `ImageFormatSetting::Guess`.
    // Bevy's extension classification (`ImageFormat::from_extension` followed by
    // `as_image_crate_format`) lives in `ImageLoader::load`, the asset-server path
    // Bevy models.
    return from_buffer(*bytes);
}

std::expected<Image, TextureError> Image::from_buffer(std::span<const std::byte> bytes) {
    return from_buffer(bytes, ImageLoaderSettings{});
}

std::expected<Image, TextureError> Image::from_buffer(std::span<const std::byte> bytes,
                                                      const ImageLoaderSettings& settings) {
    if (bytes.empty()) {
        return std::unexpected(TextureError{TextureError::ImageError{"the image payload is empty"}});
    }
    // Bevy's `image::guess_format` reports an unrecognised container before the
    // decoder runs; `stbi_info` is `stb_image`'s equivalent probe.
    int probe_width = 0, probe_height = 0, probe_channels = 0;
    if (!stbi_info_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()),
                               &probe_width, &probe_height, &probe_channels)) {
        return std::unexpected(TextureError{TextureError::ImageError{decode_failure_message()}});
    }

    auto decoded = decode_image(reinterpret_cast<const unsigned char*>(bytes.data()),
                                static_cast<int>(bytes.size()));
    if (!decoded) return std::unexpected(decoded.error());

    Image image = std::move(*decoded);
    // Bevy's `ImageLoader::load` order: `from_buffer` applies `is_srgb` and the
    // sampler, then `texture_format` overrides the descriptor, then `array_layout`
    // reinterprets the result (in `ImageLoader::load`, where a failure can be
    // reported as `ImageLoaderError::ArrayLayout`).
    apply_srgb(image, settings.is_srgb);
    image.set_sampler(settings.sampler);
    image.set_usage(settings.asset_usage);

    if (settings.texture_format) {
        image.texture_descriptor.format = *settings.texture_format;
    }

    // `settings.array_layout` is deliberately *not* applied here. Bevy raises a
    // failed reinterpretation as the typed `ImageLoaderError::ArrayLayout`, and
    // `TextureError` has no alternative to carry it, so `ImageLoader::load` owns
    // that step; this function is the decode core plus the descriptor-level
    // settings.
    return image;
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

std::span<const ImageFormat> ImageLoader::SUPPORTED_FORMATS() noexcept {
    // Bevy's list is `ImageFormat` alternatives whose `image`-crate feature the
    // build enables, minus the two the special-cased loaders (`basis`,
    // `ktx2`) would otherwise claim. Epix has one `stb_image` decoder, so the
    // list is exactly the formats that decoder reads, plus the two Epix-only
    // containers it also reads (`Pic`, `Psd`).
    static constexpr auto formats = std::array{
        ImageFormat::Bmp,  ImageFormat::Gif, ImageFormat::Hdr, ImageFormat::Jpeg, ImageFormat::Png,
        ImageFormat::Pnm,  ImageFormat::Tga, ImageFormat::Pic, ImageFormat::Psd,
    };
    return formats;
}

std::span<std::string_view> ImageLoader::extensions() noexcept {
    // Bevy derives `SUPPORTED_FILE_EXTENSIONS` from `SUPPORTED_FORMATS` through
    // `ImageFormat::to_file_extensions`, so the two lists cannot drift apart.
    static auto extensions = [] {
        std::vector<std::string_view> result;
        for (const auto format : SUPPORTED_FORMATS()) {
            for (const auto extension : to_file_extensions(format)) {
                result.push_back(extension);
            }
        }
        return result;
    }();
    return extensions;
}

STDEXEC::task<std::expected<Image, ImageLoaderError>> ImageLoader::load(assets::Reader& reader,
                                                                        const Settings& settings,
                                                                        assets::LoadContext& context) {
    const auto& path = context.path().path;
    spdlog::trace("[image] Loading image from '{}'.", path.string());
    const auto path_string = path.string();

    std::vector<std::uint8_t> bytes;
    const auto read_result = co_await reader.read_to_end(bytes);
    if (!read_result) {
        co_return std::unexpected(ImageLoaderError{ImageLoaderError::Io{read_result.error()}});
    }

    // Bevy resolves `settings.format` into an `ImageType` and lets
    // `Image::from_buffer` report the format errors (`InvalidImageExtension`,
    // `UnsupportedTextureFormat`, `ImageError`) through `FileTextureError`, which
    // is why the classification happens here rather than before the read.
    //
    // The three arms are mutually exclusive, so this is a `visit` rather than a
    // chain of `get_if` pointer tests: `FromExtension` uses the path extension,
    // `Format` names the container outright, and `Guess` defers to the bytes.
    const auto classification = std::visit(
        [&path](const auto& alternative) -> std::optional<TextureError> {
            using Alternative = std::remove_cvref_t<decltype(alternative)>;
            if constexpr (std::same_as<Alternative, ImageFormatSetting::FromExtension>) {
                // Bevy unwraps the path extension here (`extension().unwrap()`);
                // Epix reports an empty extension instead of panicking on a path
                // without one.
                return extension_error(lowercased_extension(path));
            } else if constexpr (std::same_as<Alternative, ImageFormatSetting::Format>) {
                return format_error(alternative.format);
            } else {
                // `Guess` needs no classification: `from_buffer` detects the
                // container from the bytes and reports an undetectable payload as
                // an `ImageError`, which is Bevy's `image::guess_format` failure
                // path.
                return std::nullopt;
            }
        },
        settings.format.alternatives);
    if (classification) {
        co_return std::unexpected(
            ImageLoaderError{ImageLoaderError::FileTexture{FileTextureError{*classification, path_string}}});
    }

    auto image = Image::from_buffer(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                                               bytes.size()),
                                    settings);
    if (!image) {
        co_return std::unexpected(
            ImageLoaderError{ImageLoaderError::FileTexture{FileTextureError{image.error(), path_string}}});
    }

    // Bevy applies `array_layout` after the texture-format override, converting a
    // failed reinterpretation into `ImageLoaderError::ArrayLayout` with `?`
    // rather than wrapping it in `FileTextureError`.
    if (settings.array_layout) {
        const auto layers = array_layer_count(*image, *settings.array_layout);
        if (!layers) {
            co_return std::unexpected(ImageLoaderError{ImageLoaderError::ArrayLayout{layers.error()}});
        }
        auto reinterpreted = image->reinterpret_stacked_2d_as_array(*layers);
        if (!reinterpreted) {
            co_return std::unexpected(ImageLoaderError{ImageLoaderError::ArrayLayout{reinterpreted.error()}});
        }
    }
    co_return std::move(*image);
}

}  // namespace epix::image
