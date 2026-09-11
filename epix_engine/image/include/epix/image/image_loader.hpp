#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
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
EPIX_EXPORT std::string to_string(const FileTextureError& error);

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
EPIX_EXPORT std::string to_string(const ImageLoaderError& error);
EPIX_EXPORT std::exception_ptr to_exception_ptr(const ImageLoaderError& error);

/** @brief Asset loader for formats supported by Epix's selected decoder. */
EPIX_EXPORT struct ImageLoader {
    using Asset = Image;
    struct Settings {};
    using Error = ImageLoaderError;

    static std::span<std::string_view> extensions() noexcept;
    static STDEXEC::task<std::expected<Image, ImageLoaderError>> load(assets::Reader& reader,
                                                                      const Settings& settings,
                                                                      assets::LoadContext& context);
};

}  // namespace epix::image
