#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <expected>
#include <optional>
#include <span>
#include <stdexec/execution.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#endif

#include <epix/image/image.hpp>

namespace epix::image {

// `ImageFormatSetting`, `ImageArrayLayout`, and `ImageLoaderSettings` are
// declared in `image.hpp`, beside `Image::from_buffer`, because the decode core
// consumes them. Bevy declares them in `bevy_image::image_loader`; the types and
// defaults are identical, only the declaration site differs so that the header
// dependency stays acyclic.

/** @brief Error returned when reading a texture file, wrapping the underlying
 * `TextureError` with the path that failed (Bevy `FileTextureError`). */
EPIX_EXPORT struct FileTextureError {
    /** @brief The underlying texture failure. */
    TextureError error;
    /** @brief Display path of the file being read. */
    std::string path;

    bool operator==(const FileTextureError&) const = default;
};

/** @brief Bevy `FileTextureError`'s `Display` text. */
EPIX_EXPORT EPIX_API std::string to_string(const FileTextureError& error);

/** @brief Error returned when loading an image through `ImageLoader` (Bevy
 * `ImageLoaderError`). Bevy marks the enum `#[non_exhaustive]`; the C++
 * counterpart is a closed variant of the three alternatives Bevy defines. */
EPIX_EXPORT struct ImageLoaderError {
    /** @brief Reading the image bytes failed (Bevy `ImageLoaderError::Io`).
     * Bevy wraps `std::io::Error`; Epix's `assets::Reader::read_to_end` reports
     * `std::error_code`, so that is the carried payload. */
    struct Io {
        std::error_code code;
        bool operator==(const Io&) const = default;
    };
    /** @brief Decoding the image bytes failed (Bevy
     * `ImageLoaderError::FileTexture`). */
    struct FileTexture {
        FileTextureError error;
        bool operator==(const FileTexture&) const = default;
    };
    /** @brief The bytes could not be interpreted as an array texture (Bevy
     * `ImageLoaderError::ArrayLayout`). */
    struct ArrayLayout {
        TextureReinterpretationError error;
        bool operator==(const ArrayLayout&) const = default;
    };

    std::variant<Io, FileTexture, ArrayLayout> value{Io{}};

    bool operator==(const ImageLoaderError&) const = default;
};

/** @brief Bevy `ImageLoaderError`'s `Display` text. */
EPIX_EXPORT EPIX_API std::string to_string(const ImageLoaderError& error);
EPIX_EXPORT EPIX_API std::exception_ptr to_exception_ptr(const ImageLoaderError& error);

/** @brief Asset loader for formats supported by Epix's selected decoder. */
EPIX_EXPORT struct EPIX_API ImageLoader {
    using Asset = Image;
    /** @brief Bevy names this `ImageLoaderSettings`; Epix's nested alias keeps
     * `ImageLoader::Settings` working as the loader seam expects. */
    using Settings = ImageLoaderSettings;
    using Error    = ImageLoaderError;

    /** @brief The container formats this loader decodes (Bevy
     * `ImageLoader::SUPPORTED_FORMATS`).
     *
     * Bevy derives the list from the `image` crate features the build enables.
     * Epix's decoder is `stb_image`, so the list is the set of Bevy formats that
     * decoder can actually read, plus the two Epix-only containers (`Pic`,
     * `Psd`) it also decodes. `ImageFormat` alternatives this decoder cannot read
     * are absent rather than listed and then rejected. */
    static std::span<const ImageFormat> SUPPORTED_FORMATS() noexcept;

    /** @brief The file extensions this loader advertises (Bevy
     * `ImageLoader::SUPPORTED_FILE_EXTENSIONS`).
     *
     * Derived from `SUPPORTED_FORMATS` through `to_file_extensions`, exactly as
     * Bevy derives its list, so the two cannot drift apart. */
    static std::span<std::string_view> extensions() noexcept;

    static STDEXEC::task<std::expected<Image, ImageLoaderError>> load(assets::Reader& reader,
                                                                      const Settings& settings,
                                                                      assets::LoadContext& context);
};

}  // namespace epix::image
