#pragma once

#ifndef EPIX_CXX_MODULE
#include <zpp_bits.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::image {

/** @brief Floating-point rectangle used for image and sprite coordinates. */
EPIX_EXPORT struct Rect {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};

    static constexpr Rect from_corners(glm::vec2 min, glm::vec2 max) noexcept { return Rect{min, max}; }
    static constexpr Rect from_center_size(glm::vec2 center, glm::vec2 size) noexcept {
        return Rect{center - size * 0.5f, center + size * 0.5f};
    }
    constexpr glm::vec2 size() const noexcept { return max - min; }
    constexpr glm::vec2 center() const noexcept { return (min + max) * 0.5f; }
    constexpr bool contains(glm::vec2 point) const noexcept {
        return point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y;
    }
    bool operator==(const Rect&) const = default;
};

/** @brief Unsigned integer rectangle used by texture atlas layouts. */
EPIX_EXPORT struct URect {
    glm::uvec2 min{0u};
    glm::uvec2 max{0u};

    static constexpr URect from_corners(glm::uvec2 min, glm::uvec2 max) noexcept { return URect{min, max}; }
    constexpr glm::uvec2 size() const noexcept { return max - min; }
    constexpr Rect as_rect() const noexcept { return Rect{glm::vec2(min), glm::vec2(max)}; }
    bool operator==(const URect&) const = default;
};

/** @brief The container format of encoded image data (Bevy `ImageFormat`).
 *
 * Bevy feature-gates every alternative; Epix has a single `stb_image` decoder, so
 * all of them are always declared and `is_decodable()` reports the ones that
 * decoder can actually read. `Pic` and `Psd` are Epix additions: `stb_image`
 * decodes both and `ImageLoader::extensions()` has always advertised them, so
 * `from_extension` must recognise them or the loader could not accept the
 * extensions it registers. */
EPIX_EXPORT enum class ImageFormat : std::uint8_t {
    Basis,
    Bmp,
    Dds,
    Farbfeld,
    Gif,
    OpenExr,
    Hdr,
    Ico,
    Jpeg,
    Ktx2,
    Png,
    Pnm,
    Qoi,
    Tga,
    Tiff,
    WebP,
    /** @brief Epix extension: `stb_image` decodes Softimage PIC. */
    Pic,
    /** @brief Epix extension: `stbi_image` decodes Photoshop PSD. */
    Psd,
};

/** @brief File extensions for a container format (Bevy
 * `ImageFormat::to_file_extensions`). C++ enums cannot have inherent methods, so
 * each is the namespace-scoped equivalent. */
EPIX_EXPORT EPIX_API std::span<const std::string_view> to_file_extensions(ImageFormat format) noexcept;
/** @brief MIME types for a container format (Bevy
 * `ImageFormat::to_mime_types`); empty when the format has no dedicated type. */
EPIX_EXPORT EPIX_API std::span<const std::string_view> to_mime_types(ImageFormat format) noexcept;
/** @brief Resolve a MIME type, case-insensitively (Bevy
 * `ImageFormat::from_mime_type`). */
EPIX_EXPORT EPIX_API std::optional<ImageFormat> from_mime_type(std::string_view mime_type) noexcept;
/** @brief Resolve a file extension, case-insensitively (Bevy
 * `ImageFormat::from_extension`). */
EPIX_EXPORT EPIX_API std::optional<ImageFormat> from_extension(std::string_view extension) noexcept;
/** @brief Whether Epix's decoder can read this format.
 *
 * Bevy's counterpart is `ImageFormat::as_image_crate_format()`, which returns
 * `None` exactly when the `image` crate has no converter for the format; the
 * loader turns that into `TextureError::UnsupportedTextureFormat` naming the
 * format. Epix's decoder is `stb_image`, so the same decision is taken here and
 * the loader reports the same error with the same format name. */
EPIX_EXPORT EPIX_API bool is_decodable(ImageFormat format) noexcept;

/** @brief The variant name of a container format, matching Rust's `Debug`
 * output that Bevy embeds in `TextureError::UnsupportedTextureFormat`. */
EPIX_EXPORT EPIX_API std::string to_string(ImageFormat format);

/** @brief The sRGB counterpart of a texture format, or the format itself when
 * WebGPU has no sRGB variant for it.
 *
 * Bevy applies its `is_srgb` loader setting inside `Image::from_dynamic`, which
 * selects the sRGB texture format for 8-bit data. The generated Epix bindings
 * only expose `eRGBA8UnormSrgb`/`eBGRA8UnormSrgb`, so single- and dual-channel
 * formats keep their linear format and the gap is recorded in the tracker. */
EPIX_EXPORT EPIX_API wgpu::TextureFormat srgb_texture_format(wgpu::TextureFormat format) noexcept;

/** @brief Error codes returned when saving an image to disk. */
EPIX_EXPORT enum class ImageSaveError { SaveFailed };
/** @brief Error codes returned when sampling a pixel out of bounds. */
EPIX_EXPORT enum class ImageSampleError { OutOfBounds };
/** @brief Error codes returned when writing pixel data to an image. */
EPIX_EXPORT enum class ImageWriteError { OutOfBounds, DataSizeMismatch };

/** @brief Error returned when a texture cannot be reinterpreted as requested
 * (Bevy `TextureReinterpretationError`). */
EPIX_EXPORT struct EPIX_API TextureReinterpretationError {
    /** @brief C++ counterpart to `TextureReinterpretationError::IncompatibleSizes`. */
    struct IncompatibleSizes {
        wgpu::Extent3D old{};
        wgpu::Extent3D new_size{};
        /** @brief Bevy's `IncompatibleSizes` is `PartialEq`; the wgpu extent has
         * no `operator==`, so the three fields are compared directly. */
        bool operator==(const IncompatibleSizes& other) const noexcept {
            const auto same_extent = [](const wgpu::Extent3D& lhs, const wgpu::Extent3D& rhs) {
                return lhs.width == rhs.width && lhs.height == rhs.height &&
                       lhs.depthOrArrayLayers == rhs.depthOrArrayLayers;
            };
            return same_extent(old, other.old) && same_extent(new_size, other.new_size);
        }
    };
    /** @brief C++ counterpart to `TextureReinterpretationError::WrongDimension`. */
    struct WrongDimension {
        bool operator==(const WrongDimension&) const = default;
    };
    /** @brief C++ counterpart to `TextureReinterpretationError::InvalidLayerCount`. */
    struct InvalidLayerCount {
        bool operator==(const InvalidLayerCount&) const = default;
    };
    /** @brief C++ counterpart to
     * `TextureReinterpretationError::HeightNotDivisibleByLayers`. */
    struct HeightNotDivisibleByLayers {
        std::uint32_t height                                     = 0;
        std::uint32_t layer_count                                = 0;
        bool operator==(const HeightNotDivisibleByLayers&) const = default;
    };

    std::variant<IncompatibleSizes, WrongDimension, InvalidLayerCount, HeightNotDivisibleByLayers> value{
        WrongDimension{}};

    bool operator==(const TextureReinterpretationError&) const = default;
};

/** @brief Bevy `TextureReinterpretationError`'s `Display` text. */
EPIX_EXPORT EPIX_API std::string to_string(const TextureReinterpretationError& error);

/** @brief Error returned when accessing a specific pixel in a texture (Bevy
 * `TextureAccessError`). */
EPIX_EXPORT struct TextureAccessError {
    /** @brief C++ counterpart to `TextureAccessError::OutOfBounds { x, y, z }`. */
    struct OutOfBounds {
        std::uint32_t x                           = 0;
        std::uint32_t y                           = 0;
        std::uint32_t z                           = 0;
        bool operator==(const OutOfBounds&) const = default;
    };
    /** @brief C++ counterpart to
     * `TextureAccessError::UnsupportedTextureFormat(TextureFormat)`. */
    struct UnsupportedTextureFormat {
        wgpu::TextureFormat format                             = wgpu::TextureFormat::eUndefined;
        bool operator==(const UnsupportedTextureFormat&) const = default;
    };
    /** @brief C++ counterpart to `TextureAccessError::WrongDimension`. */
    struct WrongDimension {
        bool operator==(const WrongDimension&) const = default;
    };

    std::variant<OutOfBounds, UnsupportedTextureFormat, WrongDimension> value{OutOfBounds{}};

    bool operator==(const TextureAccessError&) const = default;
};

/** @brief Bevy `TextureAccessError`'s `Display` text (`image.rs:1963-1972`).
 *
 * The `UnsupportedTextureFormat` arm renders the format through the WebGPU wrapper's `to_string`
 * rather than through a Rust-`Debug` table; see G26 in the image mismatch tracker. */
EPIX_EXPORT EPIX_API std::string to_string(const TextureAccessError& error);

/** @brief The number of bytes one pixel of `format` occupies, or Bevy's
 * `TextureAccessError::UnsupportedTextureFormat` for a block-compressed format
 * (Bevy `TextureFormatPixelInfo::pixel_size`).
 *
 * Bevy implements this on `wgpu::TextureFormat` through `block_dimensions()` and
 * `block_copy_size(None)`: any format whose block dimensions are not `(1, 1)` is
 * rejected, which is every BC/ETC2/ASTC format, and the rest report their
 * per-pixel byte size. Three formats that WebGPU defines without a copy size
 * (`Depth24Plus`, `Depth24PlusStencil8` and `Depth32FloatStencil8`) hit Bevy's
 * `Option::unwrap()` on `None` and panic; Epix throws `std::runtime_error` with
 * the same message. */
EPIX_EXPORT EPIX_API std::expected<std::size_t, TextureAccessError> texture_format_pixel_size(
    wgpu::TextureFormat format);

/** @brief The number of components one texel of `format` carries (wgpu-types
 * `TextureFormat::components`).
 *
 * The `aspect`-dependent formats (`Depth24PlusStencil8`, `Depth32FloatStencil8`) report the
 * `TextureAspect::All` count, which is what wgpu-types' `components()` forwards to
 * `components_with_aspect`. Formats WebGPU names but `wgpu-types` does not (`eUndefined`,
 * `eForce32`) report 0. */
EPIX_EXPORT EPIX_API std::uint8_t texture_format_components(wgpu::TextureFormat format) noexcept;

/** @brief True for the 32-bit float texture formats Epix's CPU-side extensions handle
 * (`Image::blur`, `Image::resize`, `Image::save`, `Image::sample`).
 *
 * Bevy classifies a format through `wgpu::TextureFormat::sample_type`; these extensions only ever
 * see the formats `Image::from_dynamic` and the `create*` factories produce, so the float set is
 * the two 32-bit float spellings. */
EPIX_EXPORT EPIX_API bool is_float_texture_format(wgpu::TextureFormat format) noexcept;

/** @brief The data format of a supercompressed image (Bevy `DataFormat`). */
EPIX_EXPORT enum class DataFormat : std::uint8_t { Rgb, Rgba, Rrr, Rrrg, Rg };

/** @brief A texture format that must be transcoded before wgpu can use it
 * (Bevy `TranscodeFormat`). */
EPIX_EXPORT struct TranscodeFormat {
    /** @brief C++ counterpart to `TranscodeFormat::Etc1s`. */
    struct Etc1s {
        bool operator==(const Etc1s&) const = default;
    };
    /** @brief C++ counterpart to `TranscodeFormat::Uastc(DataFormat)`. */
    struct Uastc {
        DataFormat format                   = DataFormat::Rgb;
        bool operator==(const Uastc&) const = default;
    };
    /** @brief Transcode from `R8UnormSrgb` to `R8Unorm` for wgpu. */
    struct R8UnormSrgb {
        bool operator==(const R8UnormSrgb&) const = default;
    };
    /** @brief Transcode from `Rg8UnormSrgb` to `R8G8Unorm` for wgpu. */
    struct Rg8UnormSrgb {
        bool operator==(const Rg8UnormSrgb&) const = default;
    };
    /** @brief Transcode from `Rgb8` to `Rgba8` for wgpu. */
    struct Rgb8 {
        bool operator==(const Rgb8&) const = default;
    };

    std::variant<Etc1s, Uastc, R8UnormSrgb, Rg8UnormSrgb, Rgb8> value{Etc1s{}};

    bool operator==(const TranscodeFormat&) const = default;
};

/** @brief Error returned when loading a texture (Bevy `TextureError`).
 *
 * The alternative set, the payloads, and the `to_string` text all follow
 * Bevy's `TextureError` so the failures a loader reports stay comparable.
 * `ImageError` carries the decoder's message rather than a Rust
 * `image::ImageError` because Epix decodes through `stb_image` (recorded as an
 * adaptation in the image tracker). */
EPIX_EXPORT struct TextureError {
    /** @brief C++ counterpart to `TextureError::InvalidImageMimeType(String)`. */
    struct InvalidImageMimeType {
        std::string mime_type;
        bool operator==(const InvalidImageMimeType&) const = default;
    };
    /** @brief C++ counterpart to `TextureError::InvalidImageExtension(String)`. */
    struct InvalidImageExtension {
        std::string extension;
        bool operator==(const InvalidImageExtension&) const = default;
    };
    /** @brief C++ counterpart to `TextureError::ImageError(image::ImageError)`.
     * Epix carries the decoder's message. */
    struct ImageError {
        std::string message;
        bool operator==(const ImageError&) const = default;
    };
    /** @brief C++ counterpart to
     * `TextureError::UnsupportedTextureFormat(String)`. */
    struct UnsupportedTextureFormat {
        std::string format;
        bool operator==(const UnsupportedTextureFormat&) const = default;
    };
    /** @brief C++ counterpart to
     * `TextureError::SuperCompressionNotSupported(String)`. */
    struct SuperCompressionNotSupported {
        std::string format;
        bool operator==(const SuperCompressionNotSupported&) const = default;
    };
    /** @brief C++ counterpart to
     * `TextureError::SuperDecompressionError(String)`. */
    struct SuperDecompressionError {
        std::string message;
        bool operator==(const SuperDecompressionError&) const = default;
    };
    /** @brief C++ counterpart to `TextureError::InvalidData(String)`. */
    struct InvalidData {
        std::string message;
        bool operator==(const InvalidData&) const = default;
    };
    /** @brief C++ counterpart to `TextureError::TranscodeError(String)`. */
    struct TranscodeError {
        std::string message;
        bool operator==(const TranscodeError&) const = default;
    };
    /** @brief C++ counterpart to
     * `TextureError::FormatRequiresTranscodingError(TranscodeFormat)`. */
    struct FormatRequiresTranscodingError {
        TranscodeFormat format;
        bool operator==(const FormatRequiresTranscodingError&) const = default;
    };
    /** @brief C++ counterpart to `TextureError::IncompleteCubemap`. */
    struct IncompleteCubemap {
        bool operator==(const IncompleteCubemap&) const = default;
    };

    std::variant<InvalidImageMimeType,
                 InvalidImageExtension,
                 ImageError,
                 UnsupportedTextureFormat,
                 SuperCompressionNotSupported,
                 SuperDecompressionError,
                 InvalidData,
                 TranscodeError,
                 FormatRequiresTranscodingError,
                 IncompleteCubemap>
        value{InvalidData{}};

    bool operator==(const TextureError&) const = default;
};

/** @brief Bevy `TextureError`'s `Display` text. */
EPIX_EXPORT EPIX_API std::string to_string(const TextureError& error);
EPIX_EXPORT EPIX_API std::exception_ptr to_exception_ptr(const TextureError& error);

/** @brief Customization point for Bevy's `BevyDefault` trait
 * (`bevy_image/src/image.rs`). */
template <typename T>
struct BevyDefaultImpl;

/** @brief Satisfied when `BevyDefaultImpl<T>` provides Bevy's `bevy_default`. */
template <typename T>
concept BevyDefault = requires {
    { BevyDefaultImpl<T>::bevy_default() } -> std::same_as<T>;
};

/** @brief The default value for a type (Bevy `BevyDefault::bevy_default`). */
template <BevyDefault T>
constexpr T bevy_default() noexcept {
    return BevyDefaultImpl<T>::bevy_default();
}

/** @brief Bevy's default texture format, `Rgba8UnormSrgb`, which is the format a
 * camera target uses without HDR. */
template <>
struct BevyDefaultImpl<wgpu::TextureFormat> {
    static constexpr wgpu::TextureFormat bevy_default() noexcept { return wgpu::TextureFormat::eRGBA8UnormSrgb; }
};

static_assert(BevyDefault<wgpu::TextureFormat>);
static_assert(bevy_default<wgpu::TextureFormat>() == wgpu::TextureFormat::eRGBA8UnormSrgb);

/** @brief Returns the sRGB view formats of a texture format (Bevy
 * `TextureSrgbViewFormats::srgb_view_formats`).
 *
 * Covers the same set as `TextureFormat::add_srgb_suffix`; a format without an
 * sRGB variant yields an empty span. */
EPIX_EXPORT EPIX_API std::span<const wgpu::TextureFormat> srgb_view_formats(wgpu::TextureFormat format) noexcept;

template <typename T>
struct span_type {
    using span = decltype(std::span(std::declval<T>()));
    using type = typename span::value_type;
};

/** @brief Bitmask specifying how an image is used (Bevy `RenderAssetUsages`).
 *
 * Bevy's `Image` carries the same `RenderAssetUsages` type as every other render
 * asset, so image no longer defines its own `Main`/`Render`/`Both` enum. */
using ImageUsage = assets::RenderAssetUsages;

/** @brief How wgpu interprets the bytes of an image with layers and mips
 * (Bevy `TextureDataOrder`). */
EPIX_EXPORT enum class TextureDataOrder {
    /** @brief Data is laid out as texture layers, then mips. */
    LayerMajor,
    /** @brief Data is laid out as mips, then texture layers. */
    MipMajor,
};

/** @brief Image dimension type. */
EPIX_EXPORT enum class ImageType {
    /** @brief One-dimensional image. */
    e1D,
    /** @brief Two-dimensional image. */
    e2D,
    /** @brief Array of 2D image layers. */
    e2DArray,
    /** @brief Three-dimensional (volumetric) image. */
    e3D,
};

/** @brief Texel addressing mode for sampler coordinates (Bevy
 * `ImageAddressMode`, mirrors wgpu AddressMode). */
EPIX_EXPORT enum class ImageAddressMode {
    /** @brief Clamp the value to the edge of the texture. */
    ClampToEdge,
    /** @brief Repeat the texture in a tiling fashion. */
    Repeat,
    /** @brief Repeat the texture, mirroring it every repeat. */
    MirrorRepeat,
    /** @brief Clamp the value to the border of the texture (requires the
     * ADDRESS_MODE_CLAMP_TO_BORDER feature). */
    ClampToBorder,
};

/** @brief Texel mixing mode when sampling between texels (Bevy
 * `ImageFilterMode`, mirrors wgpu FilterMode). */
EPIX_EXPORT enum class ImageFilterMode {
    /** @brief Nearest neighbor sampling. */
    Nearest,
    /** @brief Linear interpolation. */
    Linear,
};

/** @brief Comparison function used for depth and stencil operations (Bevy
 * `ImageCompareFunction`, mirrors wgpu CompareFunction). */
EPIX_EXPORT enum class ImageCompareFunction {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

/** @brief Border color used with ImageAddressMode::ClampToBorder (Bevy
 * `ImageSamplerBorderColor`, mirrors wgpu SamplerBorderColor). */
EPIX_EXPORT enum class ImageSamplerBorderColor {
    /** @brief RGBA color [0, 0, 0, 0]. */
    TransparentBlack,
    /** @brief RGBA color [0, 0, 0, 1]. */
    OpaqueBlack,
    /** @brief RGBA color [1, 1, 1, 1]. */
    OpaqueWhite,
};

/** @brief Sampler configuration for an image (Bevy `ImageSamplerDescriptor`,
 * mirrors wgpu SamplerDescriptor). The default matches wgpu's default:
 * clamp-to-edge addressing, nearest filters, lod clamp 0..32. */
EPIX_EXPORT struct EPIX_API ImageSamplerDescriptor {
   private:
    friend zpp::bits::access;
    // `std::optional` and the wgpu descriptor types are not reflectable through
    // structured bindings, so the member list is declared explicitly.
    using serialize = zpp::bits::members<12>;

   public:
    /** @brief Optional debug label. */
    std::string label;
    /** @brief Address mode for the u (x) direction. */
    ImageAddressMode address_mode_u = ImageAddressMode::ClampToEdge;
    /** @brief Address mode for the v (y) direction. */
    ImageAddressMode address_mode_v = ImageAddressMode::ClampToEdge;
    /** @brief Address mode for the w (z) direction. */
    ImageAddressMode address_mode_w = ImageAddressMode::ClampToEdge;
    /** @brief Filter when the texture is magnified (made larger). */
    ImageFilterMode mag_filter = ImageFilterMode::Nearest;
    /** @brief Filter when the texture is minified (made smaller). */
    ImageFilterMode min_filter = ImageFilterMode::Nearest;
    /** @brief Filter between mip map levels. */
    ImageFilterMode mipmap_filter = ImageFilterMode::Nearest;
    /** @brief Minimum level of detail (mip level). */
    float lod_min_clamp = 0.0f;
    /** @brief Maximum level of detail (mip level). */
    float lod_max_clamp = 32.0f;
    /** @brief Comparison sampler function, if enabled. */
    std::optional<ImageCompareFunction> compare = std::nullopt;
    /** @brief Must be at least 1; if > 1 all filters must be linear. */
    std::uint16_t anisotropy_clamp = 1;
    /** @brief Border color for ClampToBorder addressing. */
    std::optional<ImageSamplerBorderColor> border_color = std::nullopt;

    bool operator==(const ImageSamplerDescriptor&) const = default;

    /** @brief Linear min/mag/mipmap filters (Bevy
     * ImageSamplerDescriptor::linear). */
    static ImageSamplerDescriptor linear() {
        ImageSamplerDescriptor descriptor;
        descriptor.mag_filter    = ImageFilterMode::Linear;
        descriptor.min_filter    = ImageFilterMode::Linear;
        descriptor.mipmap_filter = ImageFilterMode::Linear;
        return descriptor;
    }
    /** @brief Nearest min/mag/mipmap filters (Bevy
     * ImageSamplerDescriptor::nearest). */
    static ImageSamplerDescriptor nearest() {
        ImageSamplerDescriptor descriptor;
        descriptor.mag_filter    = ImageFilterMode::Nearest;
        descriptor.min_filter    = ImageFilterMode::Nearest;
        descriptor.mipmap_filter = ImageFilterMode::Nearest;
        return descriptor;
    }
    /** @brief Set the min/mag/mipmap filters (Bevy
     * ImageSamplerDescriptor::set_filter). */
    ImageSamplerDescriptor& set_filter(ImageFilterMode filter) noexcept {
        mag_filter    = filter;
        min_filter    = filter;
        mipmap_filter = filter;
        return *this;
    }
    /** @brief Set the u/v/w address modes (Bevy
     * ImageSamplerDescriptor::set_address_mode). */
    ImageSamplerDescriptor& set_address_mode(ImageAddressMode mode) noexcept {
        address_mode_u = mode;
        address_mode_v = mode;
        address_mode_w = mode;
        return *this;
    }
    /** @brief Set the anisotropy clamp and force linear filters, which anisotropy
     * requires (Bevy `ImageSamplerDescriptor::set_anisotropic_filter`). */
    ImageSamplerDescriptor& set_anisotropic_filter(std::uint16_t anisotropy_clamp) noexcept {
        mag_filter             = ImageFilterMode::Linear;
        min_filter             = ImageFilterMode::Linear;
        mipmap_filter          = ImageFilterMode::Linear;
        this->anisotropy_clamp = anisotropy_clamp;
        return *this;
    }
    /** @brief Convert to the WebGPU sampler descriptor (Bevy
     * `ImageSamplerDescriptor::as_wgpu`). */
    wgpu::SamplerDescriptor as_wgpu() const noexcept;
};

/** @brief The sampler to use during rendering (Bevy `ImageSampler`).
 *
 * As in Bevy, the `Descriptor` alternative *carries* its descriptor, so a custom
 * descriptor cannot exist without the matching alternative and an image using the
 * global default carries no descriptor at all. */
EPIX_EXPORT struct EPIX_API ImageSampler {
   private:
    friend zpp::bits::access;
    // `std::variant` is not reflectable through structured bindings, so the
    // single member is declared explicitly (see `ImageLoaderSettings`).
    using serialize = zpp::bits::members<1>;

   public:
    /** @brief C++ counterpart to the payloadless `ImageSampler::Default`. */
    struct DefaultAlternative {
        bool operator==(const DefaultAlternative&) const = default;
    };
    /** @brief C++ counterpart to `ImageSampler::Descriptor(ImageSamplerDescriptor)`. */
    struct DescriptorAlternative {
        ImageSamplerDescriptor value;
        bool operator==(const DescriptorAlternative&) const = default;
    };

    std::variant<DefaultAlternative, DescriptorAlternative> alternatives{DefaultAlternative{}};

    constexpr ImageSampler() = default;
    constexpr ImageSampler(DefaultAlternative value) : alternatives(value) {}
    constexpr ImageSampler(DescriptorAlternative value) : alternatives(std::move(value)) {}
    constexpr ImageSampler(ImageSamplerDescriptor descriptor)
        : alternatives(DescriptorAlternative{std::move(descriptor)}) {}

    /** @brief A sampler using linear min/mag filters (Bevy `ImageSampler::linear`). */
    static ImageSampler linear() { return ImageSampler{ImageSamplerDescriptor::linear()}; }
    /** @brief A sampler using nearest min/mag filters (Bevy `ImageSampler::nearest`). */
    static ImageSampler nearest() { return ImageSampler{ImageSamplerDescriptor::nearest()}; }

    /** @brief True when this is the payloadless `Default` alternative. */
    bool is_default() const noexcept { return std::holds_alternative<DefaultAlternative>(alternatives); }
    /** @brief The carried descriptor, or `nullptr` when this is `Default` (Bevy's
     * `ImageSampler::Descriptor(..)` pattern match). */
    const ImageSamplerDescriptor* descriptor() const noexcept {
        const auto* alternative = std::get_if<DescriptorAlternative>(&alternatives);
        return alternative ? &alternative->value : nullptr;
    }
    /** @brief Initialize the descriptor if it is not already initialized (Bevy
     * `ImageSampler::get_or_init_descriptor`). */
    ImageSamplerDescriptor& get_or_init_descriptor() {
        if (!std::holds_alternative<DescriptorAlternative>(alternatives)) {
            alternatives = DescriptorAlternative{};
        }
        return std::get<DescriptorAlternative>(alternatives).value;
    }

    bool operator==(const ImageSampler&) const = default;
};

/** @brief How to determine an image's container format when loading (Bevy
 * `ImageFormatSetting`). `FromExtension` is the default, exactly as in Bevy. */
EPIX_EXPORT struct ImageFormatSetting {
    /** @brief C++ counterpart to the payloadless
     * `ImageFormatSetting::FromExtension`. */
    struct FromExtension {
        bool operator==(const FromExtension&) const = default;
    };
    /** @brief C++ counterpart to `ImageFormatSetting::Format(ImageFormat)`. */
    struct Format {
        ImageFormat format                   = ImageFormat::Png;
        bool operator==(const Format&) const = default;
    };
    /** @brief C++ counterpart to the payloadless `ImageFormatSetting::Guess`. */
    struct Guess {
        bool operator==(const Guess&) const = default;
    };

    std::variant<FromExtension, Format, Guess> alternatives{FromExtension{}};

    bool operator==(const ImageFormatSetting&) const = default;
};

/** @brief How to interpret an image as an array of textures (Bevy
 * `ImageArrayLayout`), primarily for `texture2DArray` uniforms. */
EPIX_EXPORT struct ImageArrayLayout {
    /** @brief Interpret the image as a vertical stack of `rows` images. */
    struct RowCount {
        std::uint32_t rows                     = 1;
        bool operator==(const RowCount&) const = default;
    };
    /** @brief Interpret the image as a vertical stack of images, each `pixels`
     * pixels tall. */
    struct RowHeight {
        std::uint32_t pixels                    = 1;
        bool operator==(const RowHeight&) const = default;
    };

    std::variant<RowCount, RowHeight> alternatives{RowCount{}};

    bool operator==(const ImageArrayLayout&) const = default;
};

/** @brief Settings for loading an `Image` (Bevy `ImageLoaderSettings`), with
 * Bevy's defaults: format from the extension, no texture-format override,
 * `is_srgb` true, the default sampler, usage in both worlds, and no array
 * layout.
 *
 * Bevy's own `ImageLoaderSettings` is a plain value type with no serialization
 * derives, which is why `.meta` files cannot carry image settings there. Epix's
 * asset architecture needs every loader settings type in `is_settings`, so the
 * two `std::optional` members are declared through zpp_bits' explicit member
 * list: reflection cannot enumerate them, and `std::optional` of a large wgpu
 * enum exceeds the reflector's member limit. */
EPIX_EXPORT struct ImageLoaderSettings {
   private:
    friend zpp::bits::access;
    using serialize = zpp::bits::members<6>;

   public:
    /** @brief How to determine the container format. */
    ImageFormatSetting format{};
    /** @brief Forcibly use a specific texture format, which controls how the
     * data is sampled rather than how it is stored (Bevy `texture_format`). */
    std::optional<wgpu::TextureFormat> texture_format{};
    /** @brief Whether the data is in sRGB space when the format does not already
     * determine that. */
    bool is_srgb = true;
    /** @brief Sampler used when rendering; it does not affect the image data. */
    ImageSampler sampler{};
    /** @brief Worlds that retain the asset (Bevy
     * `RenderAssetUsages::default()`, i.e. main world and render world). */
    assets::RenderAssetUsages asset_usage =
        static_cast<assets::RenderAssetUsages>(assets::MAIN_WORLD | assets::RENDER_WORLD);
    /** @brief Optional interpretation of the image as an array of textures. */
    std::optional<ImageArrayLayout> array_layout{};
};

/** @brief A rectangular pixel buffer with a fixed channel layout (Bevy `image::ImageBuffer`).
 *
 * `Channels` counts the interleaved samples per pixel and `Element` is the sample type, so the
 * aliases below are the `image` crate's `Luma`/`LumaA`/`Rgb`/`Rgba` buffers at the three
 * supported sample widths. */
template <typename Element, std::uint32_t Channels>
struct PixelBuffer {
    /** @brief Width in pixels. */
    std::uint32_t width = 0;
    /** @brief Height in pixels. */
    std::uint32_t height = 0;
    /** @brief Row-major, interleaved samples. */
    std::vector<Element> data;

    /** @brief The number of interleaved samples per pixel. */
    static constexpr std::uint32_t channels = Channels;
    /** @brief The number of pixels the buffer holds. */
    std::size_t pixel_count() const noexcept { return static_cast<std::size_t>(width) * height; }
};

using Luma8Image   = PixelBuffer<std::uint8_t, 1>;
using LumaA8Image  = PixelBuffer<std::uint8_t, 2>;
using Rgb8Image    = PixelBuffer<std::uint8_t, 3>;
using Rgba8Image   = PixelBuffer<std::uint8_t, 4>;
using Luma16Image  = PixelBuffer<std::uint16_t, 1>;
using LumaA16Image = PixelBuffer<std::uint16_t, 2>;
using Rgb16Image   = PixelBuffer<std::uint16_t, 3>;
using Rgba16Image  = PixelBuffer<std::uint16_t, 4>;
using Rgb32FImage  = PixelBuffer<float, 3>;
using Rgba32FImage = PixelBuffer<float, 4>;

/** @brief A CPU-side decoded image (Bevy `image::DynamicImage`).
 *
 * Bevy converts through the `image` crate's `DynamicImage`: `Image::from_dynamic` builds an
 * `Image` from one and `Image::try_into_dynamic` builds one from an `Image`. Epix has no `image`
 * crate, so this value type carries the same variant set plus the conversions those two functions
 * call, using the `image` crate's own channel arithmetic. */
EPIX_EXPORT struct EPIX_API DynamicImage : std::variant<Luma8Image,
                                                        LumaA8Image,
                                                        Rgb8Image,
                                                        Rgba8Image,
                                                        Luma16Image,
                                                        LumaA16Image,
                                                        Rgb16Image,
                                                        Rgba16Image,
                                                        Rgb32FImage,
                                                        Rgba32FImage> {
    using Base = std::variant<Luma8Image,
                              LumaA8Image,
                              Rgb8Image,
                              Rgba8Image,
                              Luma16Image,
                              LumaA16Image,
                              Rgb16Image,
                              Rgba16Image,
                              Rgb32FImage,
                              Rgba32FImage>;
    using Base::Base;

    /** @brief The image width in pixels (Bevy `DynamicImage::width`). */
    std::uint32_t width() const noexcept;
    /** @brief The image height in pixels (Bevy `DynamicImage::height`). */
    std::uint32_t height() const noexcept;

    /** @brief Convert to an 8-bit RGBA buffer (Bevy `DynamicImage::into_rgba8`). */
    Rgba8Image into_rgba8() const;
    /** @brief Convert to a 16-bit RGBA buffer (Bevy `DynamicImage::into_rgba16`). */
    Rgba16Image into_rgba16() const;
    /** @brief Convert to an 8-bit luma buffer (Bevy `DynamicImage::into_luma8`). */
    Luma8Image into_luma8() const;
    /** @brief Convert to an 8-bit luma+alpha buffer (Bevy `DynamicImage::into_luma_alpha8`). */
    LumaA8Image into_luma_alpha8() const;
};

/** @brief C++ counterparts to `IntoDynamicImageError`'s alternatives
 * (`image_texture_conversion.rs:210-225`). */
EPIX_EXPORT namespace into_dynamic_image_error {
    /** @brief Conversion into a dynamic image is not supported for the source format. */
    struct UnsupportedFormat {
        wgpu::TextureFormat format                      = wgpu::TextureFormat::eUndefined;
        bool operator==(const UnsupportedFormat&) const = default;
    };
    /** @brief Encountered an unknown error during conversion. */
    struct UnknownConversionError {
        wgpu::TextureFormat format                           = wgpu::TextureFormat::eUndefined;
        bool operator==(const UnknownConversionError&) const = default;
    };
    /** @brief Tried to convert an image that has no texture data. */
    struct UninitializedImage {
        bool operator==(const UninitializedImage&) const = default;
    };
}  // namespace into_dynamic_image_error

/** @brief Errors that occur while converting an `Image` into a `DynamicImage`
 * (Bevy `IntoDynamicImageError`). */
EPIX_EXPORT struct EPIX_API IntoDynamicImageError : std::variant<into_dynamic_image_error::UnsupportedFormat,
                                                                 into_dynamic_image_error::UnknownConversionError,
                                                                 into_dynamic_image_error::UninitializedImage> {
    using Base = std::variant<into_dynamic_image_error::UnsupportedFormat,
                              into_dynamic_image_error::UnknownConversionError,
                              into_dynamic_image_error::UninitializedImage>;
    using Base::Base;

    /** @brief Bevy `IntoDynamicImageError`'s `Display` text. */
    std::string to_string() const;
};

/** @brief Multi-dimensional image with runtime pixel format.
 *
 * Supports 1D, 2D, 2D array, and 3D images. Provides factory methods,
 * loading/saving, sampling, writing, and conversion utilities.
 */
EPIX_EXPORT class EPIX_API Image {
   private:
    // Bevy derives the image's size, dimension, and texture format from
    // `texture_descriptor` instead of storing them as separate intrinsics, so the
    // descriptor above is the source of truth and no duplicate fields are kept.
    assets::RenderAssetUsages m_usage = static_cast<assets::RenderAssetUsages>(assets::RenderAssetUsages::MAIN_WORLD |
                                                                               assets::RenderAssetUsages::RENDER_WORLD);
    bool m_copy_on_resize             = false;
    // Bevy stores the sampler as a payload-carrying alternative, so no separate
    // descriptor member exists here.
    ImageSampler m_sampler = ImageSampler{};

    std::vector<std::byte> data;
    // Bevy stores Image::data as Option<Vec<u8>>. Keep the existing C++
    // vector representation while preserving the same present/absent state,
    // including the distinction between an empty payload and one that was
    // already transferred to the render world.
    bool m_has_data = true;

    template <typename T>
    T* raw() noexcept {
        return reinterpret_cast<T*>(data.data());
    }
    template <typename T>
    const T* raw() const noexcept {
        return reinterpret_cast<const T*>(data.data());
    }

   public:
    /** @brief Create Bevy's default opaque-white 1x1 RGBA image. */
    Image();

    /** @brief Create an image from raw binary data and the corresponding metadata
     * (Bevy `Image::new`).
     *
     * Panics (throws `std::invalid_argument`) if the length of `data`, the volume of `size`
     * and the size of `format` do not match. */
    Image(wgpu::Extent3D size,
          wgpu::TextureDimension dimension,
          std::vector<std::byte> data,
          wgpu::TextureFormat format,
          assets::RenderAssetUsages asset_usage);

    /** @brief Exactly the same as `Image::new`, but doesn't initialize the image
     * (Bevy `Image::new_uninit`). */
    Image(wgpu::Extent3D size,
          wgpu::TextureDimension dimension,
          wgpu::TextureFormat format,
          assets::RenderAssetUsages asset_usage);

    /** @brief Create a transparent-white 1x1 RGBA image (Bevy
     * `Image::transparent`). */
    static Image transparent();

    /** @brief Create a zero-initialized image with the given type and
     * dimensions.
     * @param type Image dimension type.
     * @param w Width in pixels.
     * @param h Height in pixels.
     * @param depth_or_layers Depth (3D) or layer count (2D array).
     * @param format Pixel format. */
    static Image create(
        ImageType type, std::uint32_t w, std::uint32_t h, std::uint32_t depth_or_layers, wgpu::TextureFormat format);
    /** @brief Create a zero-initialized 1D image.
     * @param w Width in pixels.
     * @param format Pixel format. */
    static Image create1d(std::uint32_t w, wgpu::TextureFormat format);
    /** @brief Create a zero-initialized 2D image.
     * @param w Width in pixels.
     * @param h Height in pixels.
     * @param format Pixel format. */
    static Image create2d(std::uint32_t w, std::uint32_t h, wgpu::TextureFormat format);
    /** @brief Create a zero-initialized 2D array image.
     * @param w Width in pixels.
     * @param h Height in pixels.
     * @param layers Number of array layers.
     * @param format Pixel format. */
    static Image create2d_array(std::uint32_t w, std::uint32_t h, std::uint32_t layers, wgpu::TextureFormat format);
    /** @brief Create a zero-initialized 3D image.
     * @param w Width in pixels.
     * @param h Height in pixels.
     * @param depth Depth in pixels.
     * @param format Pixel format. */
    static Image create3d(std::uint32_t w, std::uint32_t h, std::uint32_t depth, wgpu::TextureFormat format);
    /** @brief Create an image initialized with the provided data.
     * @tparam T Contiguous range of trivially-copyable elements.
     * @param type Image dimension type.
     * @param w Width in pixels.
     * @param h Height in pixels.
     * @param depth_or_layers Depth (3D) or layer count (2D array).
     * @param format Pixel format.
     * @param initData Initial pixel data; must match the expected byte size.
     * @return The image, or std::nullopt if the data size is wrong. */
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create(ImageType type,
                                       std::uint32_t w,
                                       std::uint32_t h,
                                       std::uint32_t depth_or_layers,
                                       wgpu::TextureFormat format,
                                       T&& initData);
    /** @brief Create a 1D image initialized with the provided data.
     * @tparam T Contiguous range of trivially-copyable elements. */
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create1d(std::uint32_t w, wgpu::TextureFormat format, T&& initData);
    /** @brief Create a 2D image initialized with the provided data.
     * @tparam T Contiguous range of trivially-copyable elements. */
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create2d(std::uint32_t w, std::uint32_t h, wgpu::TextureFormat format, T&& initData);
    /** @brief Create a 2D array image initialized with the provided data.
     * @tparam T Contiguous range of trivially-copyable elements. */
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create2d_array(
        std::uint32_t w, std::uint32_t h, std::uint32_t layers, wgpu::TextureFormat format, T&& initData);
    /** @brief Create a 3D image initialized with the provided data.
     * @tparam T Contiguous range of trivially-copyable elements. */
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create3d(
        std::uint32_t w, std::uint32_t h, std::uint32_t depth, wgpu::TextureFormat format, T&& initData);

    /** @brief Load an image from the filesystem.
     *
     * An Epix convenience over `from_buffer`: the container is detected from the
     * bytes, so no extension is required. Bevy has no filesystem helper; its
     * `AssetServer` path goes through `ImageLoader::load`.
     * @param path Path to the image file.
     * @return The loaded Image or a TextureError. */
    static std::expected<Image, TextureError> load(const std::filesystem::path& path);

    /** @brief Decode encoded image bytes into an `Image` (Bevy
     * `Image::from_buffer`).
     *
     * This is the shared decode core: `Image::load` reads a file and calls it,
     * and `ImageLoader::load` resolves its `ImageLoaderSettings` and calls it, so
     * a filesystem load and an asset-server load cannot disagree about the
     * decoded pixels or the texture format. It applies `is_srgb`, `sampler`,
     * `asset_usage`, and `texture_format`; `array_layout` is applied by
     * `ImageLoader::load`, because a failed reinterpretation is reported there as
     * the typed `ImageLoaderError::ArrayLayout`.
     *
     * @param bytes Encoded container bytes.
     * @param settings Loader settings.
     * @return The decoded image or a `TextureError`. */
    static std::expected<Image, TextureError> from_buffer(std::span<const std::byte> bytes,
                                                          const ImageLoaderSettings& settings);
    /** @brief Decode encoded image bytes, detecting the container from the bytes
     * (Bevy's `ImageFormatSetting::Guess` path). */
    static std::expected<Image, TextureError> from_buffer(std::span<const std::byte> bytes);
    /** @brief Save an image to the filesystem.
     * @param path Destination file path.
     * @param image The image to write.
     * @return Void on success or an ImageSaveError. */
    static std::expected<void, ImageSaveError> save(const std::filesystem::path& path, const Image& image);

    /** @brief The texture descriptor that describes this image's GPU texture
     * (Bevy `Image::texture_descriptor`).
     *
     * This is the single source of truth for the image's size, dimension, and
     * texture format, exactly as in Bevy. */
    wgpu::TextureDescriptor texture_descriptor{};
    /** @brief For texture data with layers and mips, controls how wgpu
     * interprets the buffer layout (Bevy `Image::data_order`). */
    TextureDataOrder data_order = TextureDataOrder::LayerMajor;
    /** @brief Optional view descriptor override (Bevy
     * `Image::texture_view_descriptor`). */
    std::optional<wgpu::TextureViewDescriptor> texture_view_descriptor{};

    /** @brief Get the image width in pixels (Bevy `Image::width`). */
    std::uint32_t width() const noexcept { return texture_descriptor.size.width; }
    /** @brief Get the image height in pixels (Bevy `Image::height`). */
    std::uint32_t height() const noexcept { return texture_descriptor.size.height; }
    /** @brief Get the raw depth-or-layers value (Bevy `Image::depth_or_array_layers`). */
    std::uint32_t depth_or_layers() const noexcept { return texture_descriptor.size.depthOrArrayLayers; }
    /** @brief Get the depth (1 if not a 3D image). */
    std::uint32_t depth() const noexcept {
        return type() == ImageType::e3D ? texture_descriptor.size.depthOrArrayLayers : 1;
    }
    /** @brief Get the layer count (1 if not a 2D array image). */
    std::uint32_t layers() const noexcept {
        return type() == ImageType::e2DArray ? texture_descriptor.size.depthOrArrayLayers : 1;
    }
    /** @brief Get the image dimension type, derived from the descriptor. */
    ImageType type() const noexcept;

    /** @brief Get the WebGPU texture format (Bevy
     * `Image::texture_descriptor.format`). */
    wgpu::TextureFormat texture_format() const noexcept { return texture_descriptor.format; }
    /** @brief Get the format metadata for this image's pixel format. */

    /** @brief Get the usage flags for this image (Bevy `Image::asset_usage`). */
    assets::RenderAssetUsages usage() const noexcept { return m_usage; }
    /** @brief Set the usage flags (Bevy `Image::asset_usage`).
     * @param usage New usage bitmask. */
    void set_usage(assets::RenderAssetUsages usage) noexcept { m_usage = usage; }
    /** @brief True when a GPU upload without data copies min(old, new)
     * extents from the previous GPU image (Bevy Image::copy_on_resize). */
    bool copy_on_resize() const noexcept { return m_copy_on_resize; }
    /** @brief Set whether the GPU upload copies the previous image when
     * resizing without data. */
    void set_copy_on_resize(bool value) noexcept { m_copy_on_resize = value; }
    /** @brief The sampler to use during rendering (Bevy Image::sampler). */
    const ImageSampler& sampler() const noexcept { return m_sampler; }
    /** @brief Set the sampler alternative (Bevy Image::sampler). */
    void set_sampler(ImageSampler sampler) noexcept { m_sampler = std::move(sampler); }
    /** @brief The carried custom sampler descriptor, or `nullptr` when the sampler
     * is the payloadless default (Bevy `ImageSampler::Descriptor`). */
    const ImageSamplerDescriptor* sampler_descriptor() const noexcept { return m_sampler.descriptor(); }
    /** @brief Initialize and return the custom sampler descriptor (Bevy
     * `ImageSampler::get_or_init_descriptor`). */
    ImageSamplerDescriptor& get_or_init_sampler_descriptor() { return m_sampler.get_or_init_descriptor(); }
    /** @brief Set a custom sampler descriptor and select the `Descriptor`
     * alternative (Bevy `ImageSampler::Descriptor`). */
    void set_sampler_descriptor(ImageSamplerDescriptor descriptor) noexcept {
        m_sampler = ImageSampler{std::move(descriptor)};
    }

    /** @brief Get a read-only byte span of the raw pixel data. */
    std::span<const std::byte> raw_view() const noexcept { return std::as_bytes(std::span(data)); }
    std::span<std::byte> raw_view_mut() noexcept { return std::as_writable_bytes(std::span(data)); }
    /** @brief Whether this image still owns a CPU pixel payload (Bevy
     * `Image::data.is_some()`). An empty-but-present payload is distinct from
     * data transferred by `take_data`. */
    bool has_data() const noexcept { return m_has_data; }
    /** @brief Move the CPU pixel payload and metadata into a new image while
     * retaining the metadata in this image (Bevy `Image::data.take()` used by
     * `GpuImage::take_gpu_data`). */
    Image take_data();

    /** @brief Convert a `DynamicImage` to an `Image` (Bevy `Image::from_dynamic`).
     *
     * The per-variant table is Bevy's: 8-bit luma, luma+alpha, RGB and RGBA all become
     * `Rgba8Unorm`/`Rgba8UnormSrgb` (the three-channel and luma buffers are widened to RGBA),
     * `Luma16` becomes `R16Uint`, `LumaA16` becomes `Rg16Uint`, `Rgb16`/`Rgba16` become
     * `Rgba16Unorm`, and `Rgb32F`/`Rgba32F` become `Rgba32Float`. */
    static Image from_dynamic(DynamicImage dyn_img, bool is_srgb, assets::RenderAssetUsages asset_usage);

    /** @brief Convert this `Image` to a `DynamicImage` (Bevy `Image::try_into_dynamic`).
     *
     * Only `R8Unorm`, `Rg8Unorm`, `Rgba8UnormSrgb`, `Bgra8Unorm` and `Bgra8UnormSrgb` are
     * covered, the BGRA spellings are swapped to RGBA, and a payload whose length does not match
     * the dimensions reports `UnknownConversionError` exactly as `ImageBuffer::from_raw` does. */
    std::expected<DynamicImage, IntoDynamicImageError> try_into_dynamic() const;

    /**
     * @brief Sample a pixel at (x, y), returning an array of 4 floats, each representing a channel. Missing channels
     * are set to 0.
     */
    std::expected<std::array<float, 4>, ImageSampleError> sample(std::uint32_t x) const;
    std::expected<std::array<float, 4>, ImageSampleError> sample(std::uint32_t x, std::uint32_t y) const;
    std::expected<std::array<float, 4>, ImageSampleError> sample(std::uint32_t x,
                                                                 std::uint32_t y,
                                                                 std::uint32_t z) const;
    /**
     * @brief Write pixel data at (x, y). The data size must match the pixel size of the image format.
     */
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    std::expected<void, ImageWriteError> write_raw(std::uint32_t x, T&& data);
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    std::expected<void, ImageWriteError> write_raw(std::uint32_t x, std::uint32_t y, T&& data);
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    std::expected<void, ImageWriteError> write_raw(std::uint32_t x, std::uint32_t y, std::uint32_t z, T&& data);
    /**
     * @brief Write pixel data at (x, y). The values are normalized floats (0.0 - 1.0) for each channel, unless hdr.
     */
    std::expected<void, ImageWriteError> write(std::uint32_t x, std::span<const float> values);
    std::expected<void, ImageWriteError> write(std::uint32_t x, std::uint32_t y, std::span<const float> values);
    std::expected<void, ImageWriteError> write(std::uint32_t x,
                                               std::uint32_t y,
                                               std::uint32_t z,
                                               std::span<const float> values);

    /**
     * @brief Convert the image to `new_format`, or `std::nullopt` when the conversion is not
     * supported (Bevy `Image::convert`).
     *
     * Bevy builds an `image::DynamicImage` from the image and converts that, so it can only
     * convert the formats `Image::try_into_dynamic` understands (`R8Unorm`, `Rg8Unorm`,
     * `Rgba8UnormSrgb`, `Bgra8UnormSrgb` and `Bgra8Unorm`) and only into `R8Unorm`, `Rg8Unorm`
     * or `Rgba8UnormSrgb`; anything else answers `None`, which is what the atlas builder logs
     * and skips. The same gates and `DynamicImage` round-trip apply here, including
     * `from_dynamic`'s RGBA widening. */
    std::optional<Image> convert(wgpu::TextureFormat new_format) const;
    /**
     * @brief Resample the image to a new width and height and return it
     * (Epix-only extension; Bevy's `Image::resize` is the in-place
     * `resize(Extent3D)` below).
     */
    Image resampled(std::uint32_t newW, std::uint32_t newH) const;
    /**
     * @brief Apply Gaussian blur to the image and return the blurred image.
     */
    Image blur(std::uint32_t radius) const;

    // ─── Bevy `Image` size and reinterpretation surface (I2) ───────────────────────────────────

    /** @brief Returns the size of a 2D image (Bevy `Image::size`). */
    glm::uvec2 size() const noexcept { return {width(), height()}; }
    /** @brief Returns the size of a 2D image as f32 (Bevy `Image::size_f32`). */
    glm::vec2 size_f32() const noexcept { return {static_cast<float>(width()), static_cast<float>(height())}; }
    /** @brief Returns the aspect ratio (width / height) of a 2D image (Bevy
     * `Image::aspect_ratio`).
     *
     * Bevy returns an `AspectRatio` value type; Epix has no color/aspect value
     * types, so the ratio is returned as the equivalent `f32`. */
    float aspect_ratio() const;

    /** @brief Resizes the image to the new size, removing data or appending zeros.
     * Does not scale the contents (Bevy `Image::resize`).
     *
     * Use `resize_in_place` to keep pixel data intact. */
    void resize(const wgpu::Extent3D& new_size);

    /** @brief Changes the size if the total pixel count stays the same, otherwise
     * returns `TextureReinterpretationError::IncompatibleSizes` (Bevy
     * `Image::reinterpret_size`). */
    std::expected<void, TextureReinterpretationError> reinterpret_size(const wgpu::Extent3D& new_size);

    /** @brief Resizes the image keeping pixel data anchored at the top-left,
     * growing with zeros or clipping (Bevy `Image::resize_in_place`). */
    void resize_in_place(const wgpu::Extent3D& new_size);

    /** @brief Reinterprets a vertically stacked 2D image as a 2D array texture
     * (Bevy `Image::reinterpret_stacked_2d_as_array`). */
    std::expected<void, TextureReinterpretationError> reinterpret_stacked_2d_as_array(std::uint32_t layers);

    /** @brief True when the image's texture format is compressed (Bevy
     * `Image::is_compressed`). */
    bool is_compressed() const noexcept;

    /** @brief The byte offset of the given pixel, or `std::nullopt` when the
     * coordinates are outside the image or the format has no known pixel size
     * (Bevy `Image::pixel_data_offset`). */
    std::optional<std::size_t> pixel_data_offset(const glm::uvec3& coords) const noexcept;

    // ─── Bevy `Image` pixel access surface (I3) ────────────────────────────────────────────────

    /** @brief The raw bytes of one pixel, or `std::nullopt` when the coordinates
     * are outside the image or the format has no known pixel size (Bevy
     * `Image::pixel_bytes`). */
    std::optional<std::span<const std::byte>> pixel_bytes(const glm::uvec3& coords) const noexcept;
    /** @brief Mutable raw bytes of one pixel (Bevy `Image::pixel_bytes_mut`). */
    std::optional<std::span<std::byte>> pixel_bytes_mut(const glm::uvec3& coords) noexcept;

    /** @brief Overwrite every pixel with one complete format-sized `pixel`
     * (Bevy `Image::clear`). */
    void clear(std::span<const std::byte> pixel);

    /** @brief Read a pixel as linear RGBA (Bevy `Image::get_color_at`, which
     * returns a `Color`).
     *
     * Epix has no `Color` value type and uses `glm::vec4` for colors throughout,
     * so the components are returned directly as linear RGBA. */
    std::expected<glm::vec4, TextureAccessError> get_color_at(std::uint32_t x, std::uint32_t y) const;
    /** @brief Read a pixel from a layered 2D or 3D texture (Bevy
     * `Image::get_color_at_3d`). */
    std::expected<glm::vec4, TextureAccessError> get_color_at_3d(std::uint32_t x,
                                                                 std::uint32_t y,
                                                                 std::uint32_t z) const;
    /** @brief Read a pixel from a 1D texture (Bevy `Image::get_color_at_1d`). */
    std::expected<glm::vec4, TextureAccessError> get_color_at_1d(std::uint32_t x) const;

    /** @brief Write linear RGBA into a pixel, encoded into the image's texture
     * format (Bevy `Image::set_color_at`). */
    std::expected<void, TextureAccessError> set_color_at(std::uint32_t x, std::uint32_t y, const glm::vec4& color);
    /** @brief Write linear RGBA into a pixel of a layered 2D or 3D texture (Bevy
     * `Image::set_color_at_3d`). */
    std::expected<void, TextureAccessError> set_color_at_3d(std::uint32_t x,
                                                            std::uint32_t y,
                                                            std::uint32_t z,
                                                            const glm::vec4& color);
    /** @brief Write linear RGBA into a pixel of a 1D texture (Bevy
     * `Image::set_color_at_1d`). */
    std::expected<void, TextureAccessError> set_color_at_1d(std::uint32_t x, const glm::vec4& color);
};

/** @brief The persistent opaque-white fallback image installed by
 * `ImagePlugin`, corresponding to Bevy's
 * `Handle<Image>::default()`. */
inline const assets::Handle<Image> DEFAULT_IMAGE_HANDLE{uuids::uuid{}};

/** @brief The persistent transparent-white fallback image installed by
 * `ImagePlugin`, matching Bevy's
 * `TRANSPARENT_IMAGE_HANDLE`. */
inline const assets::Handle<Image> TRANSPARENT_IMAGE_HANDLE{
    uuids::uuid::from_string("d18ad97e-a322-4981-9505-44c59a4b5e46").value()};

/** @brief Plugin that registers the image asset loader and related
 * systems (Bevy `ImagePlugin`).
 *
 * Render-side
 * texture preparation reads @c default_sampler from this
 * application plugin, so add this plugin before @c
 * render::RenderPlugin when
 * using default-sampled images. */
EPIX_EXPORT struct EPIX_API ImagePlugin {
    /** @brief The sampler used by images whose sampler is @c Default. */
    ImageSamplerDescriptor default_sampler = ImageSamplerDescriptor::linear();

    /** @brief Make an image plugin with linear sampling by default (Bevy
     * `ImagePlugin::default_linear`). */
    static ImagePlugin default_linear() { return ImagePlugin{.default_sampler = ImageSamplerDescriptor::linear()}; }

    /** @brief Make an image plugin with nearest sampling by default (Bevy
     * `ImagePlugin::default_nearest`). */
    static ImagePlugin default_nearest() { return ImagePlugin{.default_sampler = ImageSamplerDescriptor::nearest()}; }

    void attach(epix::app::App& app);
};

template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::optional<Image> Image::create(ImageType type,
                                   std::uint32_t w,
                                   std::uint32_t h,
                                   std::uint32_t depth_or_layers,
                                   wgpu::TextureFormat format,
                                   T&& initData) {
    std::optional<Image> img;
    std::size_t expectedSize =
        static_cast<std::size_t>(w) * h * depth_or_layers * texture_format_pixel_size(format).value_or(0);
    auto init_bytes = std::as_bytes(std::span(std::forward<T>(initData)));
    if (init_bytes.size() != expectedSize) return img;
    img.emplace(create(type, w, h, depth_or_layers, format));
    std::memcpy(img->data.data(), init_bytes.data(), expectedSize);
    return img;
}
template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::optional<Image> Image::create1d(std::uint32_t w, wgpu::TextureFormat format, T&& initData) {
    std::optional<Image> img;
    std::size_t expectedSize = static_cast<std::size_t>(w) * texture_format_pixel_size(format).value_or(0);
    auto init_bytes          = std::as_bytes(std::span(std::forward<T>(initData)));
    if (init_bytes.size() != expectedSize) return img;
    img.emplace(create1d(w, format));
    std::memcpy(img->data.data(), init_bytes.data(), expectedSize);
    return img;
}
template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::optional<Image> Image::create2d(std::uint32_t w, std::uint32_t h, wgpu::TextureFormat format, T&& initData) {
    std::optional<Image> img;
    std::size_t expectedSize = static_cast<std::size_t>(w) * h * texture_format_pixel_size(format).value_or(0);
    auto init_bytes          = std::as_bytes(std::span(std::forward<T>(initData)));
    if (init_bytes.size() != expectedSize) return img;
    img.emplace(create2d(w, h, format));
    std::memcpy(img->data.data(), init_bytes.data(), expectedSize);
    return img;
}
template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::optional<Image> Image::create2d_array(
    std::uint32_t w, std::uint32_t h, std::uint32_t layers, wgpu::TextureFormat format, T&& initData) {
    std::optional<Image> img;
    std::size_t expectedSize = static_cast<std::size_t>(w) * h * layers * texture_format_pixel_size(format).value_or(0);
    auto init_bytes          = std::as_bytes(std::span(std::forward<T>(initData)));
    if (init_bytes.size() != expectedSize) return img;
    img.emplace(create2d_array(w, h, layers, format));
    std::memcpy(img->data.data(), init_bytes.data(), expectedSize);
    return img;
}
template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::optional<Image> Image::create3d(
    std::uint32_t w, std::uint32_t h, std::uint32_t depth, wgpu::TextureFormat format, T&& initData) {
    std::optional<Image> img;
    std::size_t expectedSize =
        static_cast<std::size_t>(w) * h * std::max(depth, 1u) * texture_format_pixel_size(format).value_or(0);
    auto init_bytes = std::as_bytes(std::span(std::forward<T>(initData)));
    if (init_bytes.size() != expectedSize) return img;
    img.emplace(create3d(w, h, depth, format));
    std::memcpy(img->data.data(), init_bytes.data(), expectedSize);
    return img;
}
template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::expected<void, ImageWriteError> Image::write_raw(std::uint32_t x, T&& data) {
    return write_raw(x, 0, 0, std::forward<T>(data));
}
template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::expected<void, ImageWriteError> Image::write_raw(std::uint32_t x, std::uint32_t y, T&& data) {
    return write_raw(x, y, 0, std::forward<T>(data));
}
template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::expected<void, ImageWriteError> Image::write_raw(std::uint32_t x, std::uint32_t y, std::uint32_t z, T&& data) {
    const std::size_t pixel_size = texture_format_pixel_size(texture_descriptor.format).value_or(0);
    auto bytes                   = std::as_bytes(std::span(std::forward<T>(data)));
    if (x >= width() || y >= height() || z >= depth_or_layers()) return std::unexpected(ImageWriteError::OutOfBounds);
    if (bytes.size() != pixel_size) return std::unexpected(ImageWriteError::DataSizeMismatch);

    auto slice_stride = static_cast<std::size_t>(width()) * height() * pixel_size;
    std::byte* pixelPtr =
        this->data.data() + z * slice_stride + (static_cast<std::size_t>(y) * width() + x) * pixel_size;
    std::memcpy(pixelPtr, bytes.data(), pixel_size);
    return {};
}
}  // namespace epix::image

template <>
struct std::hash<epix::assets::AssetId<epix::image::Image>> {
    std::size_t operator()(const epix::assets::AssetId<epix::image::Image>& id) const {
        return std::visit([]<typename U>(const U& index) { return std::hash<U>()(index); }, id);
    }
};
