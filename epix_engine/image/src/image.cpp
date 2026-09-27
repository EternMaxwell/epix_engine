#include <spdlog/spdlog.h>
#include <stb_image_resize2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <epix/image/image.hpp>
#include <epix/image/image_loader.hpp>
#include <expected>
#include <glm/gtc/packing.hpp>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace epix::image {
namespace {

/** @brief Lowercased copy of an ASCII name, for case-insensitive lookup. */
std::string lowercased(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

// Bevy's `ImageFormat::to_file_extensions` tables. `Pnm` also accepts `pnm` and
// the two Epix formats cover `pic`/`psd`, because `stb_image` decodes them and
// `ImageLoader::extensions()` advertises those three; without them the loader
// would reject the extensions it registers.
constexpr std::array<std::string_view, 1> kExtensionsBasis{"basis"};
constexpr std::array<std::string_view, 1> kExtensionsBmp{"bmp"};
constexpr std::array<std::string_view, 1> kExtensionsDds{"dds"};
constexpr std::array<std::string_view, 2> kExtensionsFarbfeld{"ff", "farbfeld"};
constexpr std::array<std::string_view, 1> kExtensionsGif{"gif"};
constexpr std::array<std::string_view, 1> kExtensionsOpenExr{"exr"};
constexpr std::array<std::string_view, 1> kExtensionsHdr{"hdr"};
constexpr std::array<std::string_view, 1> kExtensionsIco{"ico"};
constexpr std::array<std::string_view, 2> kExtensionsJpeg{"jpg", "jpeg"};
constexpr std::array<std::string_view, 1> kExtensionsKtx2{"ktx2"};
constexpr std::array<std::string_view, 1> kExtensionsPng{"png"};
constexpr std::array<std::string_view, 5> kExtensionsPnm{"pam", "pbm", "pgm", "ppm", "pnm"};
constexpr std::array<std::string_view, 1> kExtensionsQoi{"qoi"};
constexpr std::array<std::string_view, 1> kExtensionsTga{"tga"};
constexpr std::array<std::string_view, 2> kExtensionsTiff{"tif", "tiff"};
constexpr std::array<std::string_view, 1> kExtensionsWebP{"webp"};
constexpr std::array<std::string_view, 1> kExtensionsPic{"pic"};
constexpr std::array<std::string_view, 1> kExtensionsPsd{"psd"};

// Bevy's `ImageFormat::to_mime_types` tables. Formats with no dedicated type
// keep an empty list, exactly as in Bevy.
constexpr std::array<std::string_view, 2> kMimeBasis{"image/basis", "image/x-basis"};
constexpr std::array<std::string_view, 2> kMimeBmp{"image/bmp", "image/x-bmp"};
constexpr std::array<std::string_view, 1> kMimeDds{"image/vnd-ms.dds"};
constexpr std::array<std::string_view, 1> kMimeGif{"image/gif"};
constexpr std::array<std::string_view, 1> kMimeOpenExr{"image/x-exr"};
constexpr std::array<std::string_view, 1> kMimeHdr{"image/vnd.radiance"};
constexpr std::array<std::string_view, 1> kMimeIco{"image/x-icon"};
constexpr std::array<std::string_view, 1> kMimeJpeg{"image/jpeg"};
constexpr std::array<std::string_view, 1> kMimeKtx2{"image/ktx2"};
constexpr std::array<std::string_view, 1> kMimePng{"image/png"};
constexpr std::array<std::string_view, 4> kMimePnm{"image/x-portable-bitmap", "image/x-portable-graymap",
                                                   "image/x-portable-pixmap", "image/x-portable-anymap"};
constexpr std::array<std::string_view, 2> kMimeQoi{"image/qoi", "image/x-qoi"};
constexpr std::array<std::string_view, 2> kMimeTga{"image/x-targa", "image/x-tga"};
constexpr std::array<std::string_view, 1> kMimeTiff{"image/tiff"};
constexpr std::array<std::string_view, 1> kMimeWebP{"image/webp"};

/** @brief True when `format` is a block-compressed format (Bevy
 * `Image::is_compressed`).
 *
 * The generated WebGPU bindings spell these with an uppercase block family name
 * (`eBC1RGBAUnorm`), unlike Bevy's `wgpu` re-export (`Bc1RgbaUnorm`). */
bool is_compressed_texture_format(wgpu::TextureFormat format) noexcept {
    switch (format) {
        case wgpu::TextureFormat::eBC1RGBAUnorm:
        case wgpu::TextureFormat::eBC1RGBAUnormSrgb:
        case wgpu::TextureFormat::eBC2RGBAUnorm:
        case wgpu::TextureFormat::eBC2RGBAUnormSrgb:
        case wgpu::TextureFormat::eBC3RGBAUnorm:
        case wgpu::TextureFormat::eBC3RGBAUnormSrgb:
        case wgpu::TextureFormat::eBC4RUnorm:
        case wgpu::TextureFormat::eBC4RSnorm:
        case wgpu::TextureFormat::eBC5RGUnorm:
        case wgpu::TextureFormat::eBC5RGSnorm:
        case wgpu::TextureFormat::eBC6HRGBUfloat:
        case wgpu::TextureFormat::eBC6HRGBFloat:
        case wgpu::TextureFormat::eBC7RGBAUnorm:
        case wgpu::TextureFormat::eBC7RGBAUnormSrgb:
        case wgpu::TextureFormat::eETC2RGB8Unorm:
        case wgpu::TextureFormat::eETC2RGB8UnormSrgb:
        case wgpu::TextureFormat::eETC2RGB8A1Unorm:
        case wgpu::TextureFormat::eETC2RGB8A1UnormSrgb:
        case wgpu::TextureFormat::eETC2RGBA8Unorm:
        case wgpu::TextureFormat::eETC2RGBA8UnormSrgb:
        case wgpu::TextureFormat::eEACR11Unorm:
        case wgpu::TextureFormat::eEACR11Snorm:
        case wgpu::TextureFormat::eEACRG11Unorm:
        case wgpu::TextureFormat::eEACRG11Snorm:
        case wgpu::TextureFormat::eASTC4x4Unorm:
        case wgpu::TextureFormat::eASTC4x4UnormSrgb:
        case wgpu::TextureFormat::eASTC5x4Unorm:
        case wgpu::TextureFormat::eASTC5x4UnormSrgb:
        case wgpu::TextureFormat::eASTC5x5Unorm:
        case wgpu::TextureFormat::eASTC5x5UnormSrgb:
        case wgpu::TextureFormat::eASTC6x5Unorm:
        case wgpu::TextureFormat::eASTC6x5UnormSrgb:
        case wgpu::TextureFormat::eASTC6x6Unorm:
        case wgpu::TextureFormat::eASTC6x6UnormSrgb:
        case wgpu::TextureFormat::eASTC8x5Unorm:
        case wgpu::TextureFormat::eASTC8x5UnormSrgb:
        case wgpu::TextureFormat::eASTC8x6Unorm:
        case wgpu::TextureFormat::eASTC8x6UnormSrgb:
        case wgpu::TextureFormat::eASTC8x8Unorm:
        case wgpu::TextureFormat::eASTC8x8UnormSrgb:
        case wgpu::TextureFormat::eASTC10x5Unorm:
        case wgpu::TextureFormat::eASTC10x5UnormSrgb:
        case wgpu::TextureFormat::eASTC10x6Unorm:
        case wgpu::TextureFormat::eASTC10x6UnormSrgb:
        case wgpu::TextureFormat::eASTC10x8Unorm:
        case wgpu::TextureFormat::eASTC10x8UnormSrgb:
        case wgpu::TextureFormat::eASTC10x10Unorm:
        case wgpu::TextureFormat::eASTC10x10UnormSrgb:
        case wgpu::TextureFormat::eASTC12x10Unorm:
        case wgpu::TextureFormat::eASTC12x10UnormSrgb:
        case wgpu::TextureFormat::eASTC12x12Unorm:
        case wgpu::TextureFormat::eASTC12x12UnormSrgb:
            return true;
        default:
            return false;
    }
}

/** @brief The WebGPU texture dimension for an image type. */
wgpu::TextureDimension dimension_cast(ImageType type) noexcept {
    switch (type) {
        case ImageType::e1D:
            return wgpu::TextureDimension::e1D;
        case ImageType::e3D:
            return wgpu::TextureDimension::e3D;
        case ImageType::e2D:
        case ImageType::e2DArray:
        default:
            return wgpu::TextureDimension::e2D;
    }
}

/** @brief Fill in the Bevy-default texture descriptor for an image. */
void set_descriptor(Image& image,
                    ImageType type,
                    std::uint32_t width,
                    std::uint32_t height,
                    std::uint32_t depth_or_layers,
                    wgpu::TextureFormat format) {
    image.texture_descriptor.size.width              = width;
    image.texture_descriptor.size.height             = height;
    image.texture_descriptor.size.depthOrArrayLayers = depth_or_layers;
    image.texture_descriptor.dimension               = dimension_cast(type);
    image.texture_descriptor.format                  = format;
    image.texture_descriptor.mipLevelCount           = 1;
    image.texture_descriptor.sampleCount             = 1;
    // Bevy's `Image::new_uninit` sets TEXTURE_BINDING | COPY_DST | COPY_SRC.
    image.texture_descriptor.usage = static_cast<wgpu::TextureUsage>(
        wgpu::TextureUsage::eTextureBinding | wgpu::TextureUsage::eCopyDst | wgpu::TextureUsage::eCopySrc);
}

wgpu::AddressMode address_mode_cast(ImageAddressMode mode) noexcept {
    switch (mode) {
        case ImageAddressMode::ClampToEdge:
            return wgpu::AddressMode::eClampToEdge;
        case ImageAddressMode::Repeat:
            return wgpu::AddressMode::eRepeat;
        case ImageAddressMode::MirrorRepeat:
            return wgpu::AddressMode::eMirrorRepeat;
        case ImageAddressMode::ClampToBorder:
        default:
            // wgpu-native's C API has no `AddressMode::ClampToBorder` (Bevy's wgpu
            // does), so the address mode falls back to edge clamping. Recorded as
            // an intentional wgpu-native FFI gap in the tracker.
            return wgpu::AddressMode::eClampToEdge;
    }
}

wgpu::FilterMode filter_mode_cast(ImageFilterMode mode) noexcept {
    return mode == ImageFilterMode::Linear ? wgpu::FilterMode::eLinear : wgpu::FilterMode::eNearest;
}

wgpu::MipmapFilterMode mipmap_filter_mode_cast(ImageFilterMode mode) noexcept {
    return mode == ImageFilterMode::Linear ? wgpu::MipmapFilterMode::eLinear : wgpu::MipmapFilterMode::eNearest;
}

wgpu::CompareFunction compare_function_cast(ImageCompareFunction function) noexcept {
    switch (function) {
        case ImageCompareFunction::Never:
            return wgpu::CompareFunction::eNever;
        case ImageCompareFunction::Less:
            return wgpu::CompareFunction::eLess;
        case ImageCompareFunction::Equal:
            return wgpu::CompareFunction::eEqual;
        case ImageCompareFunction::LessEqual:
            return wgpu::CompareFunction::eLessEqual;
        case ImageCompareFunction::Greater:
            return wgpu::CompareFunction::eGreater;
        case ImageCompareFunction::NotEqual:
            return wgpu::CompareFunction::eNotEqual;
        case ImageCompareFunction::GreaterEqual:
            return wgpu::CompareFunction::eGreaterEqual;
        case ImageCompareFunction::Always:
        default:
            return wgpu::CompareFunction::eAlways;
    }
}

Image create_like(const Image& image, wgpu::TextureFormat format) {
    switch (image.type()) {
        case ImageType::e1D: {
            auto result = Image::create1d(image.width(), format);
            result.set_usage(image.usage());
            return result;
        }
        case ImageType::e2D: {
            auto result = Image::create2d(image.width(), image.height(), format);
            result.set_usage(image.usage());
            return result;
        }
        case ImageType::e2DArray: {
            auto result = Image::create2d_array(image.width(), image.height(), image.layers(), format);
            result.set_usage(image.usage());
            return result;
        }
        case ImageType::e3D: {
            auto result = Image::create3d(image.width(), image.height(), image.depth(), format);
            result.set_usage(image.usage());
            return result;
        }
        default: {
            auto result = Image::create2d(image.width(), image.height(), format);
            result.set_usage(image.usage());
            return result;
        }
    }
}

Image create_resized_like(const Image& image, std::uint32_t width, std::uint32_t height, wgpu::TextureFormat format) {
    switch (image.type()) {
        case ImageType::e1D: {
            auto result = Image::create1d(width, format);
            result.set_usage(image.usage());
            return result;
        }
        case ImageType::e2D: {
            auto result = Image::create2d(width, height, format);
            result.set_usage(image.usage());
            return result;
        }
        case ImageType::e2DArray: {
            auto result = Image::create2d_array(width, height, image.layers(), format);
            result.set_usage(image.usage());
            return result;
        }
        case ImageType::e3D: {
            auto result = Image::create3d(width, height, image.depth(), format);
            result.set_usage(image.usage());
            return result;
        }
        default: {
            auto result = Image::create2d(width, height, format);
            result.set_usage(image.usage());
            return result;
        }
    }
}
}  // namespace

bool is_float_texture_format(wgpu::TextureFormat format) noexcept {
    return format == wgpu::TextureFormat::eR32Float || format == wgpu::TextureFormat::eRGBA32Float;
}

std::uint8_t texture_format_components(wgpu::TextureFormat format) noexcept {
    switch (format) {
        case wgpu::TextureFormat::eR8Unorm:
        case wgpu::TextureFormat::eR8Snorm:
        case wgpu::TextureFormat::eR8Uint:
        case wgpu::TextureFormat::eR8Sint:
        case wgpu::TextureFormat::eR16Unorm:
        case wgpu::TextureFormat::eR16Uint:
        case wgpu::TextureFormat::eR16Sint:
        case wgpu::TextureFormat::eR16Float:
        case wgpu::TextureFormat::eR32Uint:
        case wgpu::TextureFormat::eR32Sint:
        case wgpu::TextureFormat::eR32Float:
        case wgpu::TextureFormat::eStencil8:
        case wgpu::TextureFormat::eDepth16Unorm:
        case wgpu::TextureFormat::eDepth24Plus:
        case wgpu::TextureFormat::eDepth32Float:
        case wgpu::TextureFormat::eBC4RUnorm:
        case wgpu::TextureFormat::eBC4RSnorm:
        case wgpu::TextureFormat::eEACR11Unorm:
        case wgpu::TextureFormat::eEACR11Snorm:
            return 1;
        case wgpu::TextureFormat::eRG8Unorm:
        case wgpu::TextureFormat::eRG8Snorm:
        case wgpu::TextureFormat::eRG8Uint:
        case wgpu::TextureFormat::eRG8Sint:
        case wgpu::TextureFormat::eRG16Unorm:
        case wgpu::TextureFormat::eRG16Uint:
        case wgpu::TextureFormat::eRG16Sint:
        case wgpu::TextureFormat::eRG16Float:
        case wgpu::TextureFormat::eRG32Uint:
        case wgpu::TextureFormat::eRG32Sint:
        case wgpu::TextureFormat::eRG32Float:
        case wgpu::TextureFormat::eDepth24PlusStencil8:
        case wgpu::TextureFormat::eDepth32FloatStencil8:
        case wgpu::TextureFormat::eBC5RGUnorm:
        case wgpu::TextureFormat::eBC5RGSnorm:
        case wgpu::TextureFormat::eEACRG11Unorm:
        case wgpu::TextureFormat::eEACRG11Snorm:
            return 2;
        case wgpu::TextureFormat::eRGB9E5Ufloat:
        case wgpu::TextureFormat::eRG11B10Ufloat:
        case wgpu::TextureFormat::eBC6HRGBUfloat:
        case wgpu::TextureFormat::eBC6HRGBFloat:
        case wgpu::TextureFormat::eETC2RGB8Unorm:
        case wgpu::TextureFormat::eETC2RGB8UnormSrgb:
            return 3;
        case wgpu::TextureFormat::eRGBA8Unorm:
        case wgpu::TextureFormat::eRGBA8UnormSrgb:
        case wgpu::TextureFormat::eRGBA8Snorm:
        case wgpu::TextureFormat::eRGBA8Uint:
        case wgpu::TextureFormat::eRGBA8Sint:
        case wgpu::TextureFormat::eBGRA8Unorm:
        case wgpu::TextureFormat::eBGRA8UnormSrgb:
        case wgpu::TextureFormat::eRGBA16Unorm:
        case wgpu::TextureFormat::eRGBA16Uint:
        case wgpu::TextureFormat::eRGBA16Sint:
        case wgpu::TextureFormat::eRGBA16Float:
        case wgpu::TextureFormat::eRGBA32Uint:
        case wgpu::TextureFormat::eRGBA32Sint:
        case wgpu::TextureFormat::eRGBA32Float:
        case wgpu::TextureFormat::eRGB10A2Uint:
        case wgpu::TextureFormat::eRGB10A2Unorm:
        case wgpu::TextureFormat::eBC1RGBAUnorm:
        case wgpu::TextureFormat::eBC1RGBAUnormSrgb:
        case wgpu::TextureFormat::eBC2RGBAUnorm:
        case wgpu::TextureFormat::eBC2RGBAUnormSrgb:
        case wgpu::TextureFormat::eBC3RGBAUnorm:
        case wgpu::TextureFormat::eBC3RGBAUnormSrgb:
        case wgpu::TextureFormat::eBC7RGBAUnorm:
        case wgpu::TextureFormat::eBC7RGBAUnormSrgb:
        case wgpu::TextureFormat::eETC2RGB8A1Unorm:
        case wgpu::TextureFormat::eETC2RGB8A1UnormSrgb:
        case wgpu::TextureFormat::eETC2RGBA8Unorm:
        case wgpu::TextureFormat::eETC2RGBA8UnormSrgb:
        case wgpu::TextureFormat::eASTC4x4Unorm:
        case wgpu::TextureFormat::eASTC4x4UnormSrgb:
        case wgpu::TextureFormat::eASTC5x4Unorm:
        case wgpu::TextureFormat::eASTC5x4UnormSrgb:
        case wgpu::TextureFormat::eASTC5x5Unorm:
        case wgpu::TextureFormat::eASTC5x5UnormSrgb:
        case wgpu::TextureFormat::eASTC6x5Unorm:
        case wgpu::TextureFormat::eASTC6x5UnormSrgb:
        case wgpu::TextureFormat::eASTC6x6Unorm:
        case wgpu::TextureFormat::eASTC6x6UnormSrgb:
        case wgpu::TextureFormat::eASTC8x5Unorm:
        case wgpu::TextureFormat::eASTC8x5UnormSrgb:
        case wgpu::TextureFormat::eASTC8x6Unorm:
        case wgpu::TextureFormat::eASTC8x6UnormSrgb:
        case wgpu::TextureFormat::eASTC8x8Unorm:
        case wgpu::TextureFormat::eASTC8x8UnormSrgb:
        case wgpu::TextureFormat::eASTC10x5Unorm:
        case wgpu::TextureFormat::eASTC10x5UnormSrgb:
        case wgpu::TextureFormat::eASTC10x6Unorm:
        case wgpu::TextureFormat::eASTC10x6UnormSrgb:
        case wgpu::TextureFormat::eASTC10x8Unorm:
        case wgpu::TextureFormat::eASTC10x8UnormSrgb:
        case wgpu::TextureFormat::eASTC10x10Unorm:
        case wgpu::TextureFormat::eASTC10x10UnormSrgb:
        case wgpu::TextureFormat::eASTC12x10Unorm:
        case wgpu::TextureFormat::eASTC12x10UnormSrgb:
        case wgpu::TextureFormat::eASTC12x12Unorm:
        case wgpu::TextureFormat::eASTC12x12UnormSrgb:
            return 4;
        default:
            // `eUndefined`/`eForce32` have no `wgpu-types` counterpart.
            return 0;
    }
}

std::expected<std::size_t, TextureAccessError> texture_format_pixel_size(wgpu::TextureFormat format) {
    // Bevy rejects every format whose block dimensions are not `(1, 1)`.
    if (is_compressed_texture_format(format)) {
        return std::unexpected(TextureAccessError{TextureAccessError::UnsupportedTextureFormat{format}});
    }

    switch (format) {
        // `block_copy_size(None)` for the single-texel-block formats the generated bindings name.
        case wgpu::TextureFormat::eR8Unorm:
        case wgpu::TextureFormat::eR8Snorm:
        case wgpu::TextureFormat::eR8Uint:
        case wgpu::TextureFormat::eR8Sint:
        case wgpu::TextureFormat::eStencil8:
            return 1;
        case wgpu::TextureFormat::eRG8Unorm:
        case wgpu::TextureFormat::eRG8Snorm:
        case wgpu::TextureFormat::eRG8Uint:
        case wgpu::TextureFormat::eRG8Sint:
        case wgpu::TextureFormat::eR16Unorm:
        case wgpu::TextureFormat::eR16Uint:
        case wgpu::TextureFormat::eR16Sint:
        case wgpu::TextureFormat::eR16Float:
        case wgpu::TextureFormat::eDepth16Unorm:
            return 2;
        case wgpu::TextureFormat::eRGBA8Unorm:
        case wgpu::TextureFormat::eRGBA8UnormSrgb:
        case wgpu::TextureFormat::eRGBA8Snorm:
        case wgpu::TextureFormat::eRGBA8Uint:
        case wgpu::TextureFormat::eRGBA8Sint:
        case wgpu::TextureFormat::eBGRA8Unorm:
        case wgpu::TextureFormat::eBGRA8UnormSrgb:
        case wgpu::TextureFormat::eRG16Unorm:
        case wgpu::TextureFormat::eRG16Uint:
        case wgpu::TextureFormat::eRG16Sint:
        case wgpu::TextureFormat::eRG16Float:
        case wgpu::TextureFormat::eR32Uint:
        case wgpu::TextureFormat::eR32Sint:
        case wgpu::TextureFormat::eR32Float:
        case wgpu::TextureFormat::eRGB9E5Ufloat:
        case wgpu::TextureFormat::eRGB10A2Uint:
        case wgpu::TextureFormat::eRGB10A2Unorm:
        case wgpu::TextureFormat::eRG11B10Ufloat:
        case wgpu::TextureFormat::eDepth32Float:
            return 4;
        case wgpu::TextureFormat::eRGBA16Unorm:
        case wgpu::TextureFormat::eRGBA16Uint:
        case wgpu::TextureFormat::eRGBA16Sint:
        case wgpu::TextureFormat::eRGBA16Float:
        case wgpu::TextureFormat::eRG32Uint:
        case wgpu::TextureFormat::eRG32Sint:
        case wgpu::TextureFormat::eRG32Float:
            return 8;
        case wgpu::TextureFormat::eRGBA32Uint:
        case wgpu::TextureFormat::eRGBA32Sint:
        case wgpu::TextureFormat::eRGBA32Float:
            return 16;

        // WebGPU defines no copy size for these, so Bevy's `block_copy_size(None).unwrap()`
        // panics; Epix throws Rust's panic message.
        case wgpu::TextureFormat::eDepth24Plus:
        case wgpu::TextureFormat::eDepth24PlusStencil8:
        case wgpu::TextureFormat::eDepth32FloatStencil8:
            throw std::runtime_error("called `Option::unwrap()` on a `None` value");

        default:
            // `eUndefined`/`eForce32` have no `wgpu-types` counterpart, so Bevy cannot name them.
            return std::unexpected(TextureAccessError{TextureAccessError::UnsupportedTextureFormat{format}});
    }
}

Image Image::create(
    ImageType type, std::uint32_t w, std::uint32_t h, std::uint32_t depth_or_layers, wgpu::TextureFormat format) {
    switch (type) {
        case ImageType::e1D:
            return create1d(w, format);
        case ImageType::e2D:
            return create2d(w, h, format);
        case ImageType::e2DArray:
            return create2d_array(w, h, depth_or_layers, format);
        case ImageType::e3D:
            return create3d(w, h, depth_or_layers, format);
        default:
            return create2d(w, h, format);
    }
}

std::span<const std::string_view> to_file_extensions(ImageFormat format) noexcept {
    // Bevy `ImageFormat::to_file_extensions`; `Pic` and `Psd` are the two Epix
    // additions that `stb_image` can decode and `ImageLoader` advertises.
    switch (format) {
        case ImageFormat::Basis:
            return kExtensionsBasis;
        case ImageFormat::Bmp:
            return kExtensionsBmp;
        case ImageFormat::Dds:
            return kExtensionsDds;
        case ImageFormat::Farbfeld:
            return kExtensionsFarbfeld;
        case ImageFormat::Gif:
            return kExtensionsGif;
        case ImageFormat::OpenExr:
            return kExtensionsOpenExr;
        case ImageFormat::Hdr:
            return kExtensionsHdr;
        case ImageFormat::Ico:
            return kExtensionsIco;
        case ImageFormat::Jpeg:
            return kExtensionsJpeg;
        case ImageFormat::Ktx2:
            return kExtensionsKtx2;
        case ImageFormat::Png:
            return kExtensionsPng;
        case ImageFormat::Pnm:
            return kExtensionsPnm;
        case ImageFormat::Qoi:
            return kExtensionsQoi;
        case ImageFormat::Tga:
            return kExtensionsTga;
        case ImageFormat::Tiff:
            return kExtensionsTiff;
        case ImageFormat::WebP:
            return kExtensionsWebP;
        case ImageFormat::Pic:
            return kExtensionsPic;
        case ImageFormat::Psd:
            return kExtensionsPsd;
    }
    return {};
}

std::span<const std::string_view> to_mime_types(ImageFormat format) noexcept {
    switch (format) {
        case ImageFormat::Basis:
            return kMimeBasis;
        case ImageFormat::Bmp:
            return kMimeBmp;
        case ImageFormat::Dds:
            return kMimeDds;
        case ImageFormat::Gif:
            return kMimeGif;
        case ImageFormat::OpenExr:
            return kMimeOpenExr;
        case ImageFormat::Hdr:
            return kMimeHdr;
        case ImageFormat::Ico:
            return kMimeIco;
        case ImageFormat::Jpeg:
            return kMimeJpeg;
        case ImageFormat::Ktx2:
            return kMimeKtx2;
        case ImageFormat::Png:
            return kMimePng;
        case ImageFormat::Pnm:
            return kMimePnm;
        case ImageFormat::Qoi:
            return kMimeQoi;
        case ImageFormat::Tga:
            return kMimeTga;
        case ImageFormat::Tiff:
            return kMimeTiff;
        case ImageFormat::WebP:
            return kMimeWebP;
        default:
            // Bevy returns an empty list for a format without dedicated MIME
            // types (Farbfeld, and the Epix-only PIC/PSD).
            return {};
    }
}

std::optional<ImageFormat> from_mime_type(std::string_view mime_type) noexcept {
    const auto lower = lowercased(mime_type);
    // Bevy resolves against the same tables `to_mime_types` publishes, so the
    // two cannot drift apart.
    for (std::uint8_t index = 0; index <= static_cast<std::uint8_t>(ImageFormat::Psd); ++index) {
        const auto format = static_cast<ImageFormat>(index);
        for (const auto candidate : to_mime_types(format)) {
            if (lower == candidate) return format;
        }
    }
    return std::nullopt;
}

std::optional<ImageFormat> from_extension(std::string_view extension) noexcept {
    const auto lower = lowercased(extension);
    for (std::uint8_t index = 0; index <= static_cast<std::uint8_t>(ImageFormat::Psd); ++index) {
        const auto format = static_cast<ImageFormat>(index);
        for (const auto candidate : to_file_extensions(format)) {
            if (lower == candidate) return format;
        }
    }
    return std::nullopt;
}

bool is_decodable(ImageFormat format) noexcept {
    // Bevy's `as_image_crate_format()` returns `None` exactly for the formats
    // whose transcoding needs a feature the build lacks; the loader reports
    // `TextureError::UnsupportedTextureFormat` with the format's name. Epix
    // decodes through `stb_image`, which cannot read these containers.
    switch (format) {
        case ImageFormat::Basis:
        case ImageFormat::Dds:
        case ImageFormat::OpenExr:
        case ImageFormat::Ktx2:
        case ImageFormat::Qoi:
        case ImageFormat::Tiff:
        case ImageFormat::WebP:
        case ImageFormat::Ico:
        case ImageFormat::Farbfeld:
            return false;
        default:
            return true;
    }
}

wgpu::TextureFormat srgb_texture_format(wgpu::TextureFormat format) noexcept {
    switch (format) {
        case wgpu::TextureFormat::eRGBA8Unorm:
            return wgpu::TextureFormat::eRGBA8UnormSrgb;
        case wgpu::TextureFormat::eBGRA8Unorm:
            return wgpu::TextureFormat::eBGRA8UnormSrgb;
        default:
            // `eR8UnormSrgb`/`eRG8UnormSrgb` are not exposed by the generated
            // Epix bindings, so single- and dual-channel data keeps its linear
            // format. Recorded as an FFI gap in the image tracker.
            return format;
    }
}

Image::Image() : data(4, std::byte{0xFF}) {
    // Bevy's default image is an opaque white 1x1 RGBA8 image whose texture format is
    // `TextureFormat::bevy_default()`, i.e. `Rgba8UnormSrgb`.
    set_descriptor(*this, ImageType::e2D, 1, 1, 1, wgpu::TextureFormat::eRGBA8UnormSrgb);
}

Image::Image(wgpu::Extent3D size,
             wgpu::TextureDimension dimension,
             std::vector<std::byte> image_data,
             wgpu::TextureFormat format,
             assets::RenderAssetUsages asset_usage) {
    // Bevy's `Image::new` only debug-asserts the payload length, and only for formats that
    // report a pixel size: the check is compiled out of release builds entirely, which is
    // what `debug_assert_eq!` means. A plain `assert` comes back on in a release build that
    // leaves `NDEBUG` unset, so the guard is written the way the rest of the port writes a
    // debug-only check.
    if (const auto pixel_size = texture_format_pixel_size(format)) {
        const std::size_t volume = static_cast<std::size_t>(size.width) * size.height * size.depthOrArrayLayers;
#ifndef NDEBUG
        if (volume * *pixel_size != image_data.size()) {
            throw std::invalid_argument("Pixel data, size and format have to match");
        }
#endif
        (void)volume;
    }

    texture_descriptor.size          = size;
    texture_descriptor.dimension     = dimension;
    texture_descriptor.format        = format;
    texture_descriptor.mipLevelCount = 1;
    texture_descriptor.sampleCount   = 1;
    texture_descriptor.usage         = static_cast<wgpu::TextureUsage>(
        wgpu::TextureUsage::eTextureBinding | wgpu::TextureUsage::eCopyDst | wgpu::TextureUsage::eCopySrc);
    m_usage    = asset_usage;
    data       = std::move(image_data);
    m_has_data = true;
}

Image::Image(wgpu::Extent3D size,
             wgpu::TextureDimension dimension,
             wgpu::TextureFormat format,
             assets::RenderAssetUsages asset_usage) {
    texture_descriptor.size          = size;
    texture_descriptor.dimension     = dimension;
    texture_descriptor.format        = format;
    texture_descriptor.mipLevelCount = 1;
    texture_descriptor.sampleCount   = 1;
    texture_descriptor.usage         = static_cast<wgpu::TextureUsage>(
        wgpu::TextureUsage::eTextureBinding | wgpu::TextureUsage::eCopyDst | wgpu::TextureUsage::eCopySrc);
    m_usage = asset_usage;
    data.clear();
    m_has_data = false;
}

Image Image::transparent() {
    auto image    = Image{};
    image.data[3] = std::byte{0};
    return image;
}

Image Image::take_data() {
    Image extracted;
    extracted.texture_descriptor      = texture_descriptor;
    extracted.data_order              = data_order;
    extracted.texture_view_descriptor = texture_view_descriptor;
    extracted.m_usage                 = m_usage;
    extracted.m_copy_on_resize        = m_copy_on_resize;
    extracted.m_sampler               = m_sampler;
    extracted.data                    = std::move(data);
    extracted.m_has_data              = m_has_data;

    data.clear();
    m_has_data = false;
    return extracted;
}

Image Image::create1d(std::uint32_t w, wgpu::TextureFormat format) {
    Image img;
    set_descriptor(img, ImageType::e1D, w, 1, 1, format);
    img.data.assign(static_cast<std::size_t>(w) * texture_format_pixel_size(format).value_or(0), std::byte(0));
    return img;
}

wgpu::SamplerDescriptor ImageSamplerDescriptor::as_wgpu() const noexcept {
    wgpu::SamplerDescriptor out;
    if (!label.empty()) out.setLabel(wgpu::StringView(label));
    out.setAddressModeU(address_mode_cast(address_mode_u))
        .setAddressModeV(address_mode_cast(address_mode_v))
        .setAddressModeW(address_mode_cast(address_mode_w))
        .setMagFilter(filter_mode_cast(mag_filter))
        .setMinFilter(filter_mode_cast(min_filter))
        .setMipmapFilter(mipmap_filter_mode_cast(mipmap_filter))
        .setLodMinClamp(lod_min_clamp)
        .setLodMaxClamp(lod_max_clamp)
        .setMaxAnisotropy(anisotropy_clamp);
    if (compare.has_value()) out.setCompare(compare_function_cast(*compare));
    return out;
}

ImageType Image::type() const noexcept {
    switch (texture_descriptor.dimension) {
        case wgpu::TextureDimension::e1D:
            return ImageType::e1D;
        case wgpu::TextureDimension::e3D:
            return ImageType::e3D;
        case wgpu::TextureDimension::e2D:
        default:
            return texture_descriptor.size.depthOrArrayLayers > 1 ? ImageType::e2DArray : ImageType::e2D;
    }
}

float Image::aspect_ratio() const {
    if (width() == 0 || height() == 0) {
        throw std::invalid_argument(
            "Failed to calculate aspect ratio: Image dimensions must be positive, non-zero values");
    }
    return static_cast<float>(width()) / static_cast<float>(height());
}

bool Image::is_compressed() const noexcept { return is_compressed_texture_format(texture_descriptor.format); }

std::optional<std::size_t> Image::pixel_data_offset(const glm::uvec3& coords) const noexcept {
    const auto pixel_size = texture_format_pixel_size(texture_descriptor.format);
    if (!pixel_size) return std::nullopt;
    if (texture_descriptor.dimension == wgpu::TextureDimension::e1D) {
        if (coords.x >= width()) return std::nullopt;
        return static_cast<std::size_t>(coords.x) * *pixel_size;
    }
    if (coords.x >= width() || coords.y >= height() || coords.z >= depth_or_layers()) return std::nullopt;
    const std::size_t pixel_offset = (static_cast<std::size_t>(coords.z) * height() + coords.y) * width() + coords.x;
    return pixel_offset * *pixel_size;
}

void Image::resize(const wgpu::Extent3D& new_size) {
    texture_descriptor.size = new_size;
    if (!m_has_data) return;
    if (const auto pixel_size = texture_format_pixel_size(texture_descriptor.format)) {
        const std::size_t volume =
            static_cast<std::size_t>(new_size.width) * new_size.height * new_size.depthOrArrayLayers;
        data.resize(volume * *pixel_size, std::byte{0});
    }
}

std::expected<void, TextureReinterpretationError> Image::reinterpret_size(const wgpu::Extent3D& new_size) {
    const auto& old_size = texture_descriptor.size;
    const std::size_t old_volume =
        static_cast<std::size_t>(old_size.width) * old_size.height * old_size.depthOrArrayLayers;
    const std::size_t new_volume =
        static_cast<std::size_t>(new_size.width) * new_size.height * new_size.depthOrArrayLayers;
    if (new_volume != old_volume) {
        return std::unexpected(TextureReinterpretationError{
            TextureReinterpretationError::IncompatibleSizes{.old = old_size, .new_size = new_size}});
    }
    texture_descriptor.size = new_size;
    return {};
}

void Image::resize_in_place(const wgpu::Extent3D& new_size) {
    const auto pixel_size = texture_format_pixel_size(texture_descriptor.format);
    if (!pixel_size) return;
    const wgpu::Extent3D old_size = texture_descriptor.size;
    texture_descriptor.size       = new_size;

    if (!m_has_data) {
        // Bevy defers to the GPU copy path when the payload is already gone.
        m_copy_on_resize = true;
        return;
    }

    std::vector<std::byte> resized(
        static_cast<std::size_t>(new_size.width) * new_size.height * new_size.depthOrArrayLayers * *pixel_size,
        std::byte{0});
    const std::size_t copy_width  = std::min(old_size.width, new_size.width);
    const std::size_t copy_height = std::min(old_size.height, new_size.height);
    const std::size_t copy_depth  = std::min(old_size.depthOrArrayLayers, new_size.depthOrArrayLayers);

    const std::size_t old_row_stride   = static_cast<std::size_t>(old_size.width) * *pixel_size;
    const std::size_t old_layer_stride = static_cast<std::size_t>(old_size.height) * old_row_stride;
    const std::size_t new_row_stride   = static_cast<std::size_t>(new_size.width) * *pixel_size;
    const std::size_t new_layer_stride = static_cast<std::size_t>(new_size.height) * new_row_stride;
    const std::size_t copy_bytes       = copy_width * *pixel_size;

    for (std::size_t z = 0; z < copy_depth; ++z) {
        for (std::size_t y = 0; y < copy_height; ++y) {
            const std::size_t old_offset = z * old_layer_stride + y * old_row_stride;
            const std::size_t new_offset = z * new_layer_stride + y * new_row_stride;
            if (old_offset + copy_bytes > data.size()) break;
            std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(old_offset), static_cast<std::ptrdiff_t>(copy_bytes),
                        resized.begin() + static_cast<std::ptrdiff_t>(new_offset));
        }
    }

    data = std::move(resized);
}

std::expected<void, TextureReinterpretationError> Image::reinterpret_stacked_2d_as_array(std::uint32_t layers) {
    if (texture_descriptor.dimension != wgpu::TextureDimension::e2D) {
        return std::unexpected(TextureReinterpretationError{TextureReinterpretationError::WrongDimension{}});
    }
    if (texture_descriptor.size.depthOrArrayLayers != 1) {
        return std::unexpected(TextureReinterpretationError{TextureReinterpretationError::InvalidLayerCount{}});
    }
    if (layers == 0 || height() % layers != 0) {
        return std::unexpected(TextureReinterpretationError{
            TextureReinterpretationError::HeightNotDivisibleByLayers{.height = height(), .layer_count = layers}});
    }

    return reinterpret_size(wgpu::Extent3D{width(), height() / layers, layers});
}

std::optional<std::span<const std::byte>> Image::pixel_bytes(const glm::uvec3& coords) const noexcept {
    const auto offset = pixel_data_offset(coords);
    if (!offset) return std::nullopt;
    const auto pixel_size = texture_format_pixel_size(texture_descriptor.format);
    if (!pixel_size || *offset + *pixel_size > data.size()) return std::nullopt;
    return std::span<const std::byte>(data.data() + *offset, *pixel_size);
}

std::optional<std::span<std::byte>> Image::pixel_bytes_mut(const glm::uvec3& coords) noexcept {
    const auto offset = pixel_data_offset(coords);
    if (!offset) return std::nullopt;
    const auto pixel_size = texture_format_pixel_size(texture_descriptor.format);
    if (!pixel_size || *offset + *pixel_size > data.size()) return std::nullopt;
    return std::span<std::byte>(data.data() + *offset, *pixel_size);
}

void Image::clear(std::span<const std::byte> pixel) {
    const auto pixel_size = texture_format_pixel_size(texture_descriptor.format);
    if (!pixel_size || *pixel_size == 0 || !m_has_data) return;
    // Rust's `copy_from_slice` panics unless the supplied slice is exactly one texel.
    // Throwing avoids an unchecked C++ copy while preserving that precondition.
    if (pixel.size() != *pixel_size) {
        throw std::invalid_argument("Image::clear requires exactly one complete pixel");
    }
    if (data.size() % *pixel_size != 0) {
        throw std::invalid_argument("Image::clear cannot fill an incomplete trailing pixel");
    }
    for (std::size_t offset = 0; offset + *pixel_size <= data.size(); offset += *pixel_size) {
        std::copy(pixel.begin(), pixel.end(), data.begin() + static_cast<std::ptrdiff_t>(offset));
    }
}

namespace {
template <typename T>
T load_scalar(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

template <typename T>
void store_scalar(std::span<std::byte> bytes, std::size_t offset, T value) noexcept {
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

std::uint8_t to_u8(float value) noexcept {
    if (std::isnan(value) || value <= 0.0f) return 0;
    if (value >= 1.0f) return std::numeric_limits<std::uint8_t>::max();
    return static_cast<std::uint8_t>(value * static_cast<float>(std::numeric_limits<std::uint8_t>::max()));
}

std::uint16_t to_u16(float value) noexcept {
    if (std::isnan(value) || value <= 0.0f) return 0;
    if (value >= 1.0f) return std::numeric_limits<std::uint16_t>::max();
    return static_cast<std::uint16_t>(value * static_cast<float>(std::numeric_limits<std::uint16_t>::max()));
}

std::uint32_t to_u32_via_f64(float value) noexcept {
    if (std::isnan(value) || value <= 0.0f) return 0;
    if (value >= 1.0f) return std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(static_cast<double>(value) *
                                      static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
}

std::uint32_t to_u32_via_f32(float value) noexcept {
    if (std::isnan(value) || value <= 0.0f) return 0;
    const float scaled = value * static_cast<float>(std::numeric_limits<std::uint32_t>::max());
    if (scaled >= static_cast<float>(std::numeric_limits<std::uint32_t>::max())) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(scaled);
}

float luminance(const glm::vec4& color) noexcept {
    // `Xyza::from(LinearRgba).y` in Bevy 0.18.
    return color.r * 0.2126729f + color.g * 0.7151522f + color.b * 0.0721750f;
}

float srgb_to_linear(float value) noexcept {
    // Bevy 0.18 `Srgba::gamma_function`.
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb(float value) noexcept {
    // Bevy 0.18 `Srgba::gamma_function_inverse`.
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

/** @brief Read exactly the texture formats supported by Bevy's color accessor. */
std::optional<glm::vec4> read_pixel_color(wgpu::TextureFormat format, std::span<const std::byte> bytes) noexcept {
    constexpr double u16_max  = static_cast<double>(std::numeric_limits<std::uint16_t>::max());
    constexpr double u32_max  = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    const auto normalized_u16 = [bytes](std::size_t offset) {
        return static_cast<float>(static_cast<double>(load_scalar<std::uint16_t>(bytes, offset)) / u16_max);
    };
    const auto normalized_u32 = [bytes](std::size_t offset) {
        return static_cast<float>(static_cast<double>(load_scalar<std::uint32_t>(bytes, offset)) / u32_max);
    };
    const auto half = [bytes](std::size_t offset) {
        return glm::unpackHalf1x16(load_scalar<std::uint16_t>(bytes, offset));
    };
    const auto floating = [bytes](std::size_t offset) { return load_scalar<float>(bytes, offset); };
    const auto raw      = reinterpret_cast<const std::uint8_t*>(bytes.data());
    switch (format) {
        case wgpu::TextureFormat::eR8Unorm:
        case wgpu::TextureFormat::eR8Uint: {
            const float value = static_cast<float>(raw[0]) / 255.0f;
            return glm::vec4{value, value, value, 1.0f};
        }
        case wgpu::TextureFormat::eR16Unorm:
        case wgpu::TextureFormat::eR16Uint: {
            const float value = normalized_u16(0);
            return glm::vec4{value, value, value, 1.0f};
        }
        case wgpu::TextureFormat::eR32Uint: {
            const float value = normalized_u32(0);
            return glm::vec4{value, value, value, 1.0f};
        }
        case wgpu::TextureFormat::eR16Float: {
            const float value = half(0);
            return glm::vec4{value, value, value, 1.0f};
        }
        case wgpu::TextureFormat::eR32Float: {
            const float value = floating(0);
            return glm::vec4{value, value, value, 1.0f};
        }
        case wgpu::TextureFormat::eRG8Unorm:
        case wgpu::TextureFormat::eRG8Uint:
            return glm::vec4{static_cast<float>(raw[0]) / 255.0f, static_cast<float>(raw[1]) / 255.0f, 0.0f, 1.0f};
        case wgpu::TextureFormat::eRG16Unorm:
        case wgpu::TextureFormat::eRG16Uint:
            return glm::vec4{normalized_u16(0), normalized_u16(2), 0.0f, 1.0f};
        case wgpu::TextureFormat::eRG32Uint:
            return glm::vec4{normalized_u32(0), normalized_u32(4), 0.0f, 1.0f};
        case wgpu::TextureFormat::eRG16Float:
            return glm::vec4{half(0), half(2), 0.0f, 1.0f};
        case wgpu::TextureFormat::eRG32Float:
            return glm::vec4{floating(0), floating(4), 0.0f, 1.0f};
        case wgpu::TextureFormat::eRGBA8Unorm:
        case wgpu::TextureFormat::eRGBA8Uint:
            return glm::vec4{static_cast<float>(raw[0]) / 255.0f, static_cast<float>(raw[1]) / 255.0f,
                             static_cast<float>(raw[2]) / 255.0f, static_cast<float>(raw[3]) / 255.0f};
        case wgpu::TextureFormat::eRGBA8UnormSrgb:
            return glm::vec4{srgb_to_linear(static_cast<float>(raw[0]) / 255.0f),
                             srgb_to_linear(static_cast<float>(raw[1]) / 255.0f),
                             srgb_to_linear(static_cast<float>(raw[2]) / 255.0f), static_cast<float>(raw[3]) / 255.0f};
        case wgpu::TextureFormat::eBGRA8Unorm:
            return glm::vec4{static_cast<float>(raw[2]) / 255.0f, static_cast<float>(raw[1]) / 255.0f,
                             static_cast<float>(raw[0]) / 255.0f, static_cast<float>(raw[3]) / 255.0f};
        case wgpu::TextureFormat::eBGRA8UnormSrgb:
            return glm::vec4{srgb_to_linear(static_cast<float>(raw[2]) / 255.0f),
                             srgb_to_linear(static_cast<float>(raw[1]) / 255.0f),
                             srgb_to_linear(static_cast<float>(raw[0]) / 255.0f), static_cast<float>(raw[3]) / 255.0f};
        case wgpu::TextureFormat::eRGBA16Unorm:
        case wgpu::TextureFormat::eRGBA16Uint:
            return glm::vec4{normalized_u16(0), normalized_u16(2), normalized_u16(4), normalized_u16(6)};
        case wgpu::TextureFormat::eRGBA32Uint:
            return glm::vec4{normalized_u32(0), normalized_u32(4), normalized_u32(8), normalized_u32(12)};
        case wgpu::TextureFormat::eRGBA16Float:
            return glm::vec4{half(0), half(2), half(4), half(6)};
        case wgpu::TextureFormat::eRGBA32Float:
            return glm::vec4{floating(0), floating(4), floating(8), floating(12)};
        default:
            return std::nullopt;
    }
}

/** @brief Encode linear RGBA into one pixel's raw bytes. */
bool write_pixel_color(wgpu::TextureFormat format, std::span<std::byte> bytes, const glm::vec4& color) noexcept {
    auto* raw = reinterpret_cast<std::uint8_t*>(bytes.data());
    switch (format) {
        case wgpu::TextureFormat::eR8Unorm:
        case wgpu::TextureFormat::eR8Uint:
            raw[0] = to_u8(luminance(color));
            return true;
        case wgpu::TextureFormat::eR16Unorm:
        case wgpu::TextureFormat::eR16Uint:
            store_scalar(bytes, 0, to_u16(luminance(color)));
            return true;
        case wgpu::TextureFormat::eR32Uint:
            store_scalar(bytes, 0, to_u32_via_f64(luminance(color)));
            return true;
        case wgpu::TextureFormat::eR16Float:
            store_scalar(bytes, 0, glm::packHalf1x16(luminance(color)));
            return true;
        case wgpu::TextureFormat::eR32Float:
            store_scalar(bytes, 0, luminance(color));
            return true;
        case wgpu::TextureFormat::eRG8Unorm:
        case wgpu::TextureFormat::eRG8Uint:
            raw[0] = to_u8(color.r);
            raw[1] = to_u8(color.g);
            return true;
        case wgpu::TextureFormat::eRG16Unorm:
        case wgpu::TextureFormat::eRG16Uint:
            store_scalar(bytes, 0, to_u16(color.r));
            store_scalar(bytes, 2, to_u16(color.g));
            return true;
        case wgpu::TextureFormat::eRG32Uint:
            store_scalar(bytes, 0, to_u32_via_f64(color.r));
            store_scalar(bytes, 4, to_u32_via_f64(color.g));
            return true;
        case wgpu::TextureFormat::eRG16Float:
            store_scalar(bytes, 0, glm::packHalf1x16(color.r));
            store_scalar(bytes, 2, glm::packHalf1x16(color.g));
            return true;
        case wgpu::TextureFormat::eRG32Float:
            store_scalar(bytes, 0, color.r);
            store_scalar(bytes, 4, color.g);
            return true;
        case wgpu::TextureFormat::eRGBA8Unorm:
        case wgpu::TextureFormat::eRGBA8Uint:
            raw[0] = to_u8(color.r);
            raw[1] = to_u8(color.g);
            raw[2] = to_u8(color.b);
            raw[3] = to_u8(color.a);
            return true;
        case wgpu::TextureFormat::eRGBA8UnormSrgb:
            raw[0] = to_u8(linear_to_srgb(color.r));
            raw[1] = to_u8(linear_to_srgb(color.g));
            raw[2] = to_u8(linear_to_srgb(color.b));
            raw[3] = to_u8(color.a);
            return true;
        case wgpu::TextureFormat::eBGRA8Unorm:
            raw[0] = to_u8(color.b);
            raw[1] = to_u8(color.g);
            raw[2] = to_u8(color.r);
            raw[3] = to_u8(color.a);
            return true;
        case wgpu::TextureFormat::eBGRA8UnormSrgb:
            raw[0] = to_u8(linear_to_srgb(color.b));
            raw[1] = to_u8(linear_to_srgb(color.g));
            raw[2] = to_u8(linear_to_srgb(color.r));
            raw[3] = to_u8(color.a);
            return true;
        case wgpu::TextureFormat::eRGBA16Unorm:
        case wgpu::TextureFormat::eRGBA16Uint:
            for (std::size_t channel = 0; channel < 4; ++channel) {
                store_scalar(bytes, channel * 2, to_u16(color[static_cast<glm::length_t>(channel)]));
            }
            return true;
        case wgpu::TextureFormat::eRGBA32Uint:
            for (std::size_t channel = 0; channel < 4; ++channel) {
                store_scalar(bytes, channel * 4, to_u32_via_f32(color[static_cast<glm::length_t>(channel)]));
            }
            return true;
        case wgpu::TextureFormat::eRGBA16Float:
            for (std::size_t channel = 0; channel < 4; ++channel) {
                store_scalar(bytes, channel * 2, glm::packHalf1x16(color[static_cast<glm::length_t>(channel)]));
            }
            return true;
        case wgpu::TextureFormat::eRGBA32Float:
            for (std::size_t channel = 0; channel < 4; ++channel) {
                store_scalar(bytes, channel * 4, color[static_cast<glm::length_t>(channel)]);
            }
            return true;
        default:
            return false;
    }
}
}  // namespace

namespace {
/** @brief Read the pixel at `coords`, mapping the failure to a typed access error. */
std::expected<glm::vec4, TextureAccessError> read_color_at(const Image& image, const glm::uvec3& coords) {
    const auto bytes = image.pixel_bytes(coords);
    if (!bytes) {
        return std::unexpected(TextureAccessError{TextureAccessError::OutOfBounds{coords.x, coords.y, coords.z}});
    }
    const auto color = read_pixel_color(image.texture_descriptor.format, *bytes);
    if (!color) {
        return std::unexpected(
            TextureAccessError{TextureAccessError::UnsupportedTextureFormat{image.texture_descriptor.format}});
    }
    return *color;
}

/** @brief Write the pixel at `coords`, mapping the failure to a typed access error. */
std::expected<void, TextureAccessError> write_color_at(Image& image, const glm::uvec3& coords, const glm::vec4& color) {
    const auto bytes = image.pixel_bytes_mut(coords);
    if (!bytes) {
        return std::unexpected(TextureAccessError{TextureAccessError::OutOfBounds{coords.x, coords.y, coords.z}});
    }
    if (!write_pixel_color(image.texture_descriptor.format, *bytes, color)) {
        return std::unexpected(
            TextureAccessError{TextureAccessError::UnsupportedTextureFormat{image.texture_descriptor.format}});
    }
    return {};
}

/** @brief True when the image is a 3D texture or a layered 2D texture (Bevy's
 * `*_at_3d` dimension requirement). */
bool is_3d_or_layered(const Image& image) noexcept {
    return image.texture_descriptor.dimension == wgpu::TextureDimension::e3D ||
           (image.texture_descriptor.dimension == wgpu::TextureDimension::e2D &&
            image.texture_descriptor.size.depthOrArrayLayers > 1);
}
}  // namespace

std::string to_string(const TextureAccessError& error) {
    return std::visit(
        [](const auto& value) -> std::string {
            using Alternative = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Alternative, TextureAccessError::OutOfBounds>) {
                return std::format("out of bounds (x: {}, y: {}, z: {})", value.x, value.y, value.z);
            } else if constexpr (std::same_as<Alternative, TextureAccessError::UnsupportedTextureFormat>) {
                // Bevy renders this format with its Rust `Debug`; Epix uses the WebGPU wrapper's own
                // name renderer instead of duplicating that table (recorded as G26).
                return std::format("unsupported texture format: {}", wgpu::to_string(value.format));
            } else {
                return std::string("attempt to access texture with different dimension");
            }
        },
        error.value);
}

std::expected<glm::vec4, TextureAccessError> Image::get_color_at(std::uint32_t x, std::uint32_t y) const {
    // Bevy checks the dimension only and reads layer 0, so a layered 2D image is
    // readable here (`image.rs:1535-1540`).
    if (texture_descriptor.dimension != wgpu::TextureDimension::e2D) {
        return std::unexpected(TextureAccessError{TextureAccessError::WrongDimension{}});
    }
    return read_color_at(*this, glm::uvec3{x, y, 0});
}

std::expected<glm::vec4, TextureAccessError> Image::get_color_at_1d(std::uint32_t x) const {
    if (texture_descriptor.dimension != wgpu::TextureDimension::e1D) {
        return std::unexpected(TextureAccessError{TextureAccessError::WrongDimension{}});
    }
    return read_color_at(*this, glm::uvec3{x, 0, 0});
}

std::expected<glm::vec4, TextureAccessError> Image::get_color_at_3d(std::uint32_t x,
                                                                    std::uint32_t y,
                                                                    std::uint32_t z) const {
    if (!is_3d_or_layered(*this)) return std::unexpected(TextureAccessError{TextureAccessError::WrongDimension{}});
    return read_color_at(*this, glm::uvec3{x, y, z});
}

std::expected<void, TextureAccessError> Image::set_color_at(std::uint32_t x, std::uint32_t y, const glm::vec4& color) {
    // Same dimension-only rule as `get_color_at` (`image.rs:1591-1596`).
    if (texture_descriptor.dimension != wgpu::TextureDimension::e2D) {
        return std::unexpected(TextureAccessError{TextureAccessError::WrongDimension{}});
    }
    return write_color_at(*this, glm::uvec3{x, y, 0}, color);
}

std::expected<void, TextureAccessError> Image::set_color_at_1d(std::uint32_t x, const glm::vec4& color) {
    if (texture_descriptor.dimension != wgpu::TextureDimension::e1D) {
        return std::unexpected(TextureAccessError{TextureAccessError::WrongDimension{}});
    }
    return write_color_at(*this, glm::uvec3{x, 0, 0}, color);
}

std::expected<void, TextureAccessError> Image::set_color_at_3d(std::uint32_t x,
                                                               std::uint32_t y,
                                                               std::uint32_t z,
                                                               const glm::vec4& color) {
    if (!is_3d_or_layered(*this)) return std::unexpected(TextureAccessError{TextureAccessError::WrongDimension{}});
    return write_color_at(*this, glm::uvec3{x, y, z}, color);
}

Image Image::create2d(std::uint32_t w, std::uint32_t h, wgpu::TextureFormat format) {
    Image img;
    set_descriptor(img, ImageType::e2D, w, h, 1, format);
    img.data.assign(static_cast<std::size_t>(w) * h * texture_format_pixel_size(format).value_or(0), std::byte(0));
    return img;
}

Image Image::create2d_array(std::uint32_t w, std::uint32_t h, std::uint32_t layers, wgpu::TextureFormat format) {
    Image img;
    const std::uint32_t layer_count = std::max(layers, 1u);
    set_descriptor(img, ImageType::e2DArray, w, h, layer_count, format);
    // An array image declares its array *view* explicitly. The view dimension
    // cannot be recovered from the texture once `depth_or_array_layers == 1`, so
    // a one-layer array texture would otherwise be handed out as a plain 2D view
    // and an array consumer's bind group would fail with
    // "expects dimension = D2Array, but given a view with dimension = D2".
    // `texture_view_descriptor` is Bevy's own mechanism for stating the view an
    // image requires (`bevy_image/src/ktx2.rs:287-297` sets it exactly this way
    // for layered textures), and render preparation honours it over the derived
    // fallback (`bevy_render/src/texture/gpu_image.rs`).
    img.texture_view_descriptor = wgpu::TextureViewDescriptor()
                                      .setLabel("image_create2d_array_view")
                                      .setDimension(wgpu::TextureViewDimension::e2DArray)
                                      .setMipLevelCount(1)
                                      .setBaseMipLevel(0)
                                      .setBaseArrayLayer(0)
                                      .setArrayLayerCount(layer_count);
    img.data.assign(static_cast<std::size_t>(w) * h * layer_count * texture_format_pixel_size(format).value_or(0),
                    std::byte(0));
    return img;
}

Image Image::create3d(std::uint32_t w, std::uint32_t h, std::uint32_t depth, wgpu::TextureFormat format) {
    Image img;
    const std::uint32_t slice_count = std::max(depth, 1u);
    set_descriptor(img, ImageType::e3D, w, h, slice_count, format);
    img.data.assign(static_cast<std::size_t>(w) * h * slice_count * texture_format_pixel_size(format).value_or(0),
                    std::byte(0));
    return img;
}

std::expected<std::array<float, 4>, ImageSampleError> Image::sample(std::uint32_t x) const { return sample(x, 0, 0); }

std::expected<std::array<float, 4>, ImageSampleError> Image::sample(std::uint32_t x, std::uint32_t y) const {
    return sample(x, y, 0);
}

std::expected<std::array<float, 4>, ImageSampleError> Image::sample(std::uint32_t x,
                                                                    std::uint32_t y,
                                                                    std::uint32_t z) const {
    const std::uint8_t components       = texture_format_components(texture_descriptor.format);
    const std::size_t pixel_size        = texture_format_pixel_size(texture_descriptor.format).value_or(0);
    const std::size_t bytes_per_channel = components == 0 ? 0 : pixel_size / components;
    const bool is_float                 = bytes_per_channel == 4 && is_float_texture_format(texture_descriptor.format);
    const bool is_16_bit                = bytes_per_channel == 2;
    std::array<float, 4> result{0};
    if (x >= width() || y >= height() || z >= depth_or_layers()) return std::unexpected(ImageSampleError::OutOfBounds);

    auto slice_stride = static_cast<std::size_t>(width()) * height() * pixel_size;
    const std::byte* pixelPtr =
        data.data() + z * slice_stride + (static_cast<std::size_t>(y) * width() + x) * pixel_size;

    for (std::uint32_t c = 0; c < components; c++) {
        float value = 0.0f;
        if (is_float) {
            value = *reinterpret_cast<const float*>(pixelPtr + static_cast<std::size_t>(c) * bytes_per_channel);
        } else if (is_16_bit) {
            std::uint16_t raw = 0;
            std::memcpy(&raw, pixelPtr + static_cast<std::size_t>(c) * bytes_per_channel, sizeof(raw));
            value = static_cast<float>(raw) / 65535.0f;
        } else {
            std::uint8_t raw = 0;
            std::memcpy(&raw, pixelPtr + static_cast<std::size_t>(c) * bytes_per_channel, sizeof(raw));
            value = static_cast<float>(raw) / 255.0f;
        }
        result[c] = value;
    }
    return result;
}

std::expected<void, ImageWriteError> Image::write(std::uint32_t x, std::span<const float> values) {
    return write(x, 0, 0, values);
}

std::expected<void, ImageWriteError> Image::write(std::uint32_t x, std::uint32_t y, std::span<const float> values) {
    return write(x, y, 0, values);
}

std::expected<void, ImageWriteError> Image::write(std::uint32_t x,
                                                  std::uint32_t y,
                                                  std::uint32_t z,
                                                  std::span<const float> values) {
    const std::uint8_t components       = texture_format_components(texture_descriptor.format);
    const std::size_t pixel_size        = texture_format_pixel_size(texture_descriptor.format).value_or(0);
    const std::size_t bytes_per_channel = components == 0 ? 0 : pixel_size / components;
    const bool is_float                 = bytes_per_channel == 4 && is_float_texture_format(texture_descriptor.format);
    const bool is_16_bit                = bytes_per_channel == 2;
    if (x >= width() || y >= height() || z >= depth_or_layers()) return std::unexpected(ImageWriteError::OutOfBounds);
    if (values.size() < components) return std::unexpected(ImageWriteError::DataSizeMismatch);

    auto slice_stride   = static_cast<std::size_t>(width()) * height() * pixel_size;
    std::byte* pixelPtr = data.data() + z * slice_stride + (static_cast<std::size_t>(y) * width() + x) * pixel_size;

    for (std::uint32_t c = 0; c < components; c++) {
        const std::size_t offset = static_cast<std::size_t>(c) * bytes_per_channel;
        const float value        = values[c];
        if (is_float) {
            std::memcpy(pixelPtr + offset, &value, sizeof(value));
        } else if (is_16_bit) {
            const std::uint16_t raw = static_cast<std::uint16_t>(std::clamp(value * 65535.0f, 0.0f, 65535.0f));
            std::memcpy(pixelPtr + offset, &raw, sizeof(raw));
        } else {
            const std::uint8_t raw = static_cast<std::uint8_t>(std::clamp(value * 255.0f, 0.0f, 255.0f));
            std::memcpy(pixelPtr + offset, &raw, sizeof(raw));
        }
    }
    return {};
}

std::optional<Image> Image::convert(wgpu::TextureFormat new_format) const {
    // Bevy: `self.clone().try_into_dynamic().ok().and_then(|img| match new_format { .. })`.
    auto dynamic_image = try_into_dynamic();
    if (!dynamic_image) return std::nullopt;

    if (new_format == wgpu::TextureFormat::eR8Unorm) {
        return from_dynamic(DynamicImage{dynamic_image->into_luma8()}, false, usage());
    }
    if (new_format == wgpu::TextureFormat::eRG8Unorm) {
        return from_dynamic(DynamicImage{dynamic_image->into_luma_alpha8()}, false, usage());
    }
    if (new_format == wgpu::TextureFormat::eRGBA8UnormSrgb) {
        return from_dynamic(DynamicImage{dynamic_image->into_rgba8()}, true, usage());
    }
    return std::nullopt;
}

Image Image::resampled(std::uint32_t newW, std::uint32_t newH) const {
    const wgpu::TextureFormat format    = texture_descriptor.format;
    const std::uint8_t components       = texture_format_components(format);
    const std::size_t pixel_size        = texture_format_pixel_size(format).value_or(0);
    const std::size_t bytes_per_channel = components == 0 ? 0 : pixel_size / components;
    const bool is_float                 = bytes_per_channel == 4 && is_float_texture_format(format);
    const bool is_16_bit                = bytes_per_channel == 2;
    auto target_height                  = type() == ImageType::e1D ? 1u : newH;

    Image result = create_resized_like(*this, newW, target_height, format);

    stbir_datatype type = is_float ? STBIR_TYPE_FLOAT : (is_16_bit ? STBIR_TYPE_UINT16 : STBIR_TYPE_UINT8);

    auto src_slice_stride = static_cast<std::size_t>(width()) * height() * pixel_size;
    auto dst_slice_stride = static_cast<std::size_t>(newW) * target_height * pixel_size;

    for (std::uint32_t z = 0; z < depth_or_layers(); ++z) {
        stbir_resize(data.data() + z * src_slice_stride, width(), height(), 0,
                     result.data.data() + z * dst_slice_stride, newW, target_height, 0,
                     static_cast<stbir_pixel_layout>(components),  // works because enum matches channel count mostly
                     type, STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT);
    }

    return result;
}
Image Image::blur(std::uint32_t radius) const {
    // For simplicity, process in a float working image and convert back unless the image is
    // already float. The values move through `read_color_at`/`write_color_at`, which follow
    // Bevy's `get_color_at`/`set_color_at` rules, so the round trip is this module's per-format
    // conversion rather than a second conversion pipeline.
    const wgpu::TextureFormat source_format = texture_descriptor.format;
    const bool needsConvert                 = !is_float_texture_format(source_format);

    Image work = *this;
    if (needsConvert) {
        work = create_like(*this, wgpu::TextureFormat::eRGBA32Float);
        for (std::uint32_t z = 0; z < depth_or_layers(); ++z) {
            for (std::uint32_t y = 0; y < height(); ++y) {
                for (std::uint32_t x = 0; x < width(); ++x) {
                    const auto color = read_color_at(*this, glm::uvec3{x, y, z});
                    if (!color) continue;
                    (void)write_color_at(work, glm::uvec3{x, y, z}, *color);
                }
            }
        }
    }

    const std::uint32_t c = needsConvert ? 4u : texture_format_components(source_format);
    Image temp            = create_like(work, work.texture_format());
    Image result          = create_like(work, work.texture_format());

    // Generate Kernel
    int size = radius * 2 + 1;
    std::vector<float> kernel(size);
    float sigma = std::max(radius / 2.0f, 1.0f);
    float sum   = 0.0f;
    for (int i = 0; i < size; i++) {
        float x   = i - radius;
        kernel[i] = std::exp(-(x * x) / (2 * sigma * sigma));
        sum += kernel[i];
    }
    for (float& k : kernel) k /= sum;

    std::uint32_t w   = work.width();
    std::uint32_t h   = work.height();
    float* srcData    = work.raw<float>();
    float* tmpData    = temp.raw<float>();
    float* dstData    = result.raw<float>();
    auto slice_texels = static_cast<std::size_t>(w) * h * c;

    // Horizontal Pass
    for (std::uint32_t z = 0; z < work.depth_or_layers(); ++z) {
        auto base = static_cast<std::size_t>(z) * slice_texels;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                for (std::uint32_t ch = 0; ch < c; ++ch) {
                    float val = 0.0f;
                    for (std::uint32_t k = 0; k < size; ++k) {
                        std::uint32_t ix = std::clamp(x + k - radius, 0u, w - 1);
                        val += srcData[base + (static_cast<std::size_t>(y) * w + ix) * c + ch] * kernel[k];
                    }
                    tmpData[base + (static_cast<std::size_t>(y) * w + x) * c + ch] = val;
                }
            }
        }
    }

    // Vertical Pass
    for (std::uint32_t z = 0; z < work.depth_or_layers(); ++z) {
        auto base = static_cast<std::size_t>(z) * slice_texels;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                for (std::uint32_t ch = 0; ch < c; ++ch) {
                    float val = 0.0f;
                    for (std::uint32_t k = 0; k < size; ++k) {
                        std::uint32_t iy = std::clamp(y + k - radius, 0u, h - 1);
                        val += tmpData[base + (static_cast<std::size_t>(iy) * w + x) * c + ch] * kernel[k];
                    }
                    dstData[base + (static_cast<std::size_t>(y) * w + x) * c + ch] = val;
                }
            }
        }
    }

    if (!needsConvert) return result;

    // Convert the blurred working image back to the source format.
    Image converted = create_like(result, source_format);
    for (std::uint32_t z = 0; z < result.depth_or_layers(); ++z) {
        for (std::uint32_t y = 0; y < result.height(); ++y) {
            for (std::uint32_t x = 0; x < result.width(); ++x) {
                const auto color = read_color_at(result, glm::uvec3{x, y, z});
                if (!color) continue;
                (void)write_color_at(converted, glm::uvec3{x, y, z}, *color);
            }
        }
    }
    return converted;
}

void ImagePlugin::attach(epix::app::App& app) {
    spdlog::debug("[image] Attaching ImagePlugin.");
    assets::app_register_asset<Image>(app);
    assets::app_register_loader<ImageLoader>(app);
    auto& images = app.world_mut().resource_mut<assets::Assets<Image>>();
    (void)images.insert(DEFAULT_IMAGE_HANDLE.id(), Image{});
    (void)images.insert(TRANSPARENT_IMAGE_HANDLE.id(), Image::transparent());
}

std::span<const wgpu::TextureFormat> srgb_view_formats(wgpu::TextureFormat format) noexcept {
    // Bevy's `TextureSrgbViewFormats for TextureFormat`
    // (`bevy_image/src/image.rs`): every arm has exactly one sRGB counterpart.
    // Bevy models the ASTC entries as `Astc { block, channel }` struct variants
    // where Epix's wgpu enum is flat, so each becomes one `Unorm`/`UnormSrgb`
    // pair here. Every other format yields an empty slice.
    switch (format) {
        case wgpu::TextureFormat::eRGBA8Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eRGBA8UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eBGRA8Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eBGRA8UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eBC1RGBAUnorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eBC1RGBAUnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eBC2RGBAUnorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eBC2RGBAUnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eBC3RGBAUnorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eBC3RGBAUnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eBC7RGBAUnorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eBC7RGBAUnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eETC2RGB8Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eETC2RGB8UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eETC2RGB8A1Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eETC2RGB8A1UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eETC2RGBA8Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eETC2RGBA8UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC4x4Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC4x4UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC5x4Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC5x4UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC5x5Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC5x5UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC6x5Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC6x5UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC6x6Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC6x6UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC8x5Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC8x5UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC8x6Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC8x6UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC8x8Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC8x8UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC10x5Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC10x5UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC10x6Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC10x6UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC10x8Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC10x8UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC10x10Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC10x10UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC12x10Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC12x10UnormSrgb};
            return kFormats;
        }
        case wgpu::TextureFormat::eASTC12x12Unorm: {
            static constexpr std::array kFormats{wgpu::TextureFormat::eASTC12x12UnormSrgb};
            return kFormats;
        }
        default:
            return {};
    }
}

}  // namespace epix::image
