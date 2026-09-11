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
#include <span>
#include <utility>
#include <vector>

namespace epix::image {
namespace {
std::optional<Format> format_from_texture_format(wgpu::TextureFormat format) noexcept;

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
            return true;
        default:
            return false;
    }
}

/** @brief The number of bytes one pixel of `format` occupies, or `std::nullopt`
 * for formats Epix has no size for (Bevy `TextureFormat::pixel_size`).
 *
 * Epix already tracks per-`Format` metadata for the formats it can decode, so the
 * size comes from that table via the descriptor's format. */
std::optional<std::size_t> pixel_size_of(wgpu::TextureFormat format) noexcept {
    if (is_compressed_texture_format(format)) return std::nullopt;
    const auto decoded = format_from_texture_format(format);
    if (!decoded) return std::nullopt;
    const auto& info = getFormatInfo(*decoded);
    if (info.pixelSize() == 0) return std::nullopt;
    return info.pixelSize();
}

/** @brief The WebGPU texture format for a decoded pixel format (Bevy's
 * `ImageFormat` to `TextureFormat` mapping).
 *
 * `Format::Unknown` (a descriptor with no pixel format) reports `Undefined`,
 * which is the only case the render module can reject. Every other format maps
 * directly, because Epix has no three-channel format to fall back on. */
wgpu::TextureFormat texture_format_cast(Format format) noexcept {
    switch (format) {
        case Format::Grey8:
            return wgpu::TextureFormat::eR8Unorm;
        case Format::GreyAlpha8:
            return wgpu::TextureFormat::eRG8Unorm;
        case Format::RGBA8:
            return wgpu::TextureFormat::eRGBA8Unorm;
        case Format::Grey16:
            return wgpu::TextureFormat::eR16Uint;
        case Format::RGBA16:
            return wgpu::TextureFormat::eRGBA16Uint;
        case Format::Grey32F:
            return wgpu::TextureFormat::eR32Float;
        case Format::RGBA32F:
            return wgpu::TextureFormat::eRGBA32Float;
        default:
            return wgpu::TextureFormat::eUndefined;
    }
}

/** @brief The reverse of `texture_format_cast`, used by `Image::format`. */
std::optional<Format> format_from_texture_format(wgpu::TextureFormat format) noexcept {
    switch (format) {
        case wgpu::TextureFormat::eR8Unorm:
            return Format::Grey8;
        case wgpu::TextureFormat::eRG8Unorm:
            return Format::GreyAlpha8;
        case wgpu::TextureFormat::eRGBA8Unorm:
        case wgpu::TextureFormat::eRGBA8UnormSrgb:
            return Format::RGBA8;
        case wgpu::TextureFormat::eBGRA8Unorm:
        case wgpu::TextureFormat::eBGRA8UnormSrgb:
            return Format::RGBA8;
        case wgpu::TextureFormat::eR16Uint:
            return Format::Grey16;
        case wgpu::TextureFormat::eRGBA16Uint:
            return Format::RGBA16;
        case wgpu::TextureFormat::eR32Float:
            return Format::Grey32F;
        case wgpu::TextureFormat::eRGBA32Float:
            return Format::RGBA32F;
        default:
            return std::nullopt;
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
                    Format format) {
    image.texture_descriptor.size.width              = width;
    image.texture_descriptor.size.height             = height;
    image.texture_descriptor.size.depthOrArrayLayers = depth_or_layers;
    image.texture_descriptor.dimension               = dimension_cast(type);
    image.texture_descriptor.format                  = texture_format_cast(format);
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

Image create_like(const Image& image, Format format) {
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

Image create_resized_like(const Image& image, std::uint32_t width, std::uint32_t height, Format format) {
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

const FormatInfo& getFormatInfo(Format fmt) {
    static FormatInfo info1{1, 1, false, false};
    static FormatInfo info2{2, 1, false, false};
    static FormatInfo info4{4, 1, false, false};
    static FormatInfo info5{1, 2, false, true};
    static FormatInfo info7{4, 2, false, true};
    static FormatInfo info8{1, 4, true, false};
    static FormatInfo info10{4, 4, true, false};
    static FormatInfo info0{0, 0, false, false};
    switch (fmt) {
        case Format::Grey8:
            return info1;
        case Format::GreyAlpha8:
            return info2;
        case Format::RGBA8:
            return info4;
        case Format::Grey16:
            return info5;
        case Format::RGBA16:
            return info7;
        case Format::Grey32F:
            return info8;
        case Format::RGBA32F:
            return info10;
        default:
            return info0;
    }
}

Image Image::create(ImageType type, std::uint32_t w, std::uint32_t h, std::uint32_t depth_or_layers, Format fmt) {
    switch (type) {
        case ImageType::e1D:
            return create1d(w, fmt);
        case ImageType::e2D:
            return create2d(w, h, fmt);
        case ImageType::e2DArray:
            return create2d_array(w, h, depth_or_layers, fmt);
        case ImageType::e3D:
            return create3d(w, h, depth_or_layers, fmt);
        default:
            return create2d(w, h, fmt);
    }
}

Image::Image() : data(4, std::byte{0xFF}) {
    // Bevy's default image is an opaque white 1x1 RGBA8 image.
    set_descriptor(*this, ImageType::e2D, 1, 1, 1, Format::RGBA8);
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

Image Image::create1d(std::uint32_t w, Format fmt) {
    Image img;
    set_descriptor(img, ImageType::e1D, w, 1, 1, fmt);
    img.data.assign(static_cast<std::size_t>(w) * getFormatInfo(fmt).pixelSize(), std::byte(0));
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

Format Image::format() const noexcept {
    const auto mapped = format_from_texture_format(texture_descriptor.format);
    return mapped.value_or(Format::Unknown);
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
    if (coords.x >= width() || coords.y >= height() || coords.z >= depth_or_layers()) return std::nullopt;
    const auto pixel_size = pixel_size_of(texture_descriptor.format);
    if (!pixel_size) return std::nullopt;
    const std::size_t row_stride   = static_cast<std::size_t>(width()) * *pixel_size;
    const std::size_t layer_stride = static_cast<std::size_t>(height()) * row_stride;
    return coords.z * layer_stride + coords.y * row_stride + coords.x * *pixel_size;
}

void Image::resize_size(const wgpu::Extent3D& new_size) {
    texture_descriptor.size = new_size;
    if (!m_has_data) return;
    if (const auto pixel_size = pixel_size_of(texture_descriptor.format)) {
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
    const auto pixel_size = pixel_size_of(texture_descriptor.format);
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
    const auto pixel_size = pixel_size_of(texture_descriptor.format);
    if (!pixel_size || *offset + *pixel_size > data.size()) return std::nullopt;
    return std::span<const std::byte>(data.data() + *offset, *pixel_size);
}

std::optional<std::span<std::byte>> Image::pixel_bytes_mut(const glm::uvec3& coords) noexcept {
    const auto offset = pixel_data_offset(coords);
    if (!offset) return std::nullopt;
    const auto pixel_size = pixel_size_of(texture_descriptor.format);
    if (!pixel_size || *offset + *pixel_size > data.size()) return std::nullopt;
    return std::span<std::byte>(data.data() + *offset, *pixel_size);
}

void Image::clear(std::span<const std::byte> pixel) {
    const auto pixel_size = pixel_size_of(texture_descriptor.format);
    if (!pixel_size || *pixel_size == 0 || pixel.empty()) return;
    // Bevy cycles the fill data across the whole payload, so a shorter `pixel`
    // repeats and a longer one fills what fits.
    for (std::size_t index = 0; index < data.size(); ++index) {
        data[index] = pixel[index % pixel.size()];
    }
}

namespace {
/** @brief Read the linear RGBA value of one pixel from raw bytes. */
glm::vec4 read_pixel_color(wgpu::TextureFormat format, std::span<const std::byte> bytes) noexcept {
    const auto channels = static_cast<std::size_t>(bytes.size());
    glm::vec4 color{0.0f, 0.0f, 0.0f, 1.0f};
    if (channels == 0) return color;
    const unsigned char* raw = reinterpret_cast<const unsigned char*>(bytes.data());
    switch (format) {
        case wgpu::TextureFormat::eR8Unorm:
            color.r = static_cast<float>(raw[0]) / 255.0f;
            color.g = color.r;
            color.b = color.r;
            return color;
        case wgpu::TextureFormat::eRG8Unorm:
            color.r = static_cast<float>(raw[0]) / 255.0f;
            color.g = static_cast<float>(raw[1]) / 255.0f;
            color.b = 0.0f;
            return color;
        case wgpu::TextureFormat::eRGBA8Unorm:
        case wgpu::TextureFormat::eRGBA8UnormSrgb:
            color.r = static_cast<float>(raw[0]) / 255.0f;
            color.g = static_cast<float>(raw[1]) / 255.0f;
            color.b = static_cast<float>(raw[2]) / 255.0f;
            color.a = static_cast<float>(raw[3]) / 255.0f;
            return color;
        case wgpu::TextureFormat::eBGRA8Unorm:
        case wgpu::TextureFormat::eBGRA8UnormSrgb:
            color.r = static_cast<float>(raw[2]) / 255.0f;
            color.g = static_cast<float>(raw[1]) / 255.0f;
            color.b = static_cast<float>(raw[0]) / 255.0f;
            color.a = static_cast<float>(raw[3]) / 255.0f;
            return color;
        default: {
            // 16-bit and 32-bit channel formats are read through the generic path.
            const auto info = format_from_texture_format(format);
            if (!info) return color;
            const auto& meta              = getFormatInfo(*info);
            const std::size_t per_channel = meta.bytesPerChannel;
            if (per_channel * meta.channels > channels) return color;
            for (std::size_t channel = 0; channel < meta.channels && channel < 4; ++channel) {
                const unsigned char* channel_bytes = raw + channel * per_channel;
                float value                        = 0.0f;
                if (per_channel == 2) {
                    std::uint16_t bits = 0;
                    std::memcpy(&bits, channel_bytes, sizeof(bits));
                    value = static_cast<float>(bits) / 65535.0f;
                } else {
                    std::memcpy(&value, channel_bytes, sizeof(value));
                }
                color[channel] = value;
            }
            if (meta.channels < 4) color.a = 1.0f;
            return color;
        }
    }
}

/** @brief Encode linear RGBA into one pixel's raw bytes. */
void write_pixel_color(wgpu::TextureFormat format, std::span<std::byte> bytes, const glm::vec4& color) noexcept {
    unsigned char* raw  = reinterpret_cast<unsigned char*>(bytes.data());
    const auto quantize = [](float value) {
        return static_cast<unsigned char>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    switch (format) {
        case wgpu::TextureFormat::eR8Unorm:
            raw[0] = quantize(color.r);
            return;
        case wgpu::TextureFormat::eRG8Unorm:
            raw[0] = quantize(color.r);
            raw[1] = quantize(color.g);
            return;
        case wgpu::TextureFormat::eRGBA8Unorm:
        case wgpu::TextureFormat::eRGBA8UnormSrgb:
            raw[0] = quantize(color.r);
            raw[1] = quantize(color.g);
            raw[2] = quantize(color.b);
            raw[3] = quantize(color.a);
            return;
        case wgpu::TextureFormat::eBGRA8Unorm:
        case wgpu::TextureFormat::eBGRA8UnormSrgb:
            raw[0] = quantize(color.b);
            raw[1] = quantize(color.g);
            raw[2] = quantize(color.r);
            raw[3] = quantize(color.a);
            return;
        default: {
            const auto info = format_from_texture_format(format);
            if (!info) return;
            const auto& meta         = getFormatInfo(*info);
            const std::size_t stride = meta.bytesPerChannel;
            if (stride * meta.channels > bytes.size()) return;
            for (std::size_t channel = 0; channel < meta.channels && channel < 4; ++channel) {
                unsigned char* channel_bytes = raw + channel * stride;
                if (stride == 2) {
                    const auto bits = static_cast<std::uint16_t>(
                        std::clamp(color[static_cast<glm::length_t>(channel)], 0.0f, 1.0f) * 65535.0f + 0.5f);
                    std::memcpy(channel_bytes, &bits, sizeof(bits));
                } else if (stride == 4) {
                    const float value = color[static_cast<glm::length_t>(channel)];
                    std::memcpy(channel_bytes, &value, sizeof(value));
                }
            }
            return;
        }
    }
}
}  // namespace

namespace {
/** @brief Read the pixel at `coords`, mapping the failure to a typed access error. */
std::expected<glm::vec4, TextureAccessError> read_color_at(const Image& image, const glm::uvec3& coords) {
    const auto bytes = image.pixel_bytes(coords);
    if (!bytes) {
        if (!pixel_size_of(image.texture_descriptor.format)) {
            return std::unexpected(
                TextureAccessError{TextureAccessError::UnsupportedTextureFormat{image.texture_descriptor.format}});
        }
        return std::unexpected(TextureAccessError{TextureAccessError::OutOfBounds{coords.x, coords.y, coords.z}});
    }
    return read_pixel_color(image.texture_descriptor.format, *bytes);
}

/** @brief Write the pixel at `coords`, mapping the failure to a typed access error. */
std::expected<void, TextureAccessError> write_color_at(Image& image, const glm::uvec3& coords, const glm::vec4& color) {
    const auto bytes = image.pixel_bytes_mut(coords);
    if (!bytes) {
        if (!pixel_size_of(image.texture_descriptor.format)) {
            return std::unexpected(
                TextureAccessError{TextureAccessError::UnsupportedTextureFormat{image.texture_descriptor.format}});
        }
        return std::unexpected(TextureAccessError{TextureAccessError::OutOfBounds{coords.x, coords.y, coords.z}});
    }
    write_pixel_color(image.texture_descriptor.format, *bytes, color);
    return {};
}

/** @brief True when the image is a 2D texture with a single layer (Bevy's
 * `get_color_at`/`set_color_at` dimension requirement). */
bool is_plain_2d(const Image& image) noexcept {
    return image.texture_descriptor.dimension == wgpu::TextureDimension::e2D &&
           image.texture_descriptor.size.depthOrArrayLayers == 1;
}

/** @brief True when the image is a 3D texture or a layered 2D texture (Bevy's
 * `*_at_3d` dimension requirement). */
bool is_3d_or_layered(const Image& image) noexcept {
    return image.texture_descriptor.dimension == wgpu::TextureDimension::e3D ||
           (image.texture_descriptor.dimension == wgpu::TextureDimension::e2D &&
            image.texture_descriptor.size.depthOrArrayLayers > 1);
}
}  // namespace

std::expected<glm::vec4, TextureAccessError> Image::get_color_at(std::uint32_t x, std::uint32_t y) const {
    if (!is_plain_2d(*this)) return std::unexpected(TextureAccessError{TextureAccessError::WrongDimension{}});
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
    if (!is_plain_2d(*this)) return std::unexpected(TextureAccessError{TextureAccessError::WrongDimension{}});
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

Image Image::create2d(std::uint32_t w, std::uint32_t h, Format fmt) {
    Image img;
    set_descriptor(img, ImageType::e2D, w, h, 1, fmt);
    img.data.assign(static_cast<std::size_t>(w) * h * getFormatInfo(fmt).pixelSize(), std::byte(0));
    return img;
}

Image Image::create2d_array(std::uint32_t w, std::uint32_t h, std::uint32_t layers, Format fmt) {
    Image img;
    const std::uint32_t layer_count = std::max(layers, 1u);
    set_descriptor(img, ImageType::e2DArray, w, h, layer_count, fmt);
    img.data.assign(static_cast<std::size_t>(w) * h * layer_count * getFormatInfo(fmt).pixelSize(), std::byte(0));
    return img;
}

Image Image::create3d(std::uint32_t w, std::uint32_t h, std::uint32_t depth, Format fmt) {
    Image img;
    const std::uint32_t slice_count = std::max(depth, 1u);
    set_descriptor(img, ImageType::e3D, w, h, slice_count, fmt);
    img.data.assign(static_cast<std::size_t>(w) * h * slice_count * getFormatInfo(fmt).pixelSize(), std::byte(0));
    return img;
}

std::expected<std::array<float, 4>, ImageSampleError> Image::sample(std::uint32_t x) const { return sample(x, 0, 0); }

std::expected<std::array<float, 4>, ImageSampleError> Image::sample(std::uint32_t x, std::uint32_t y) const {
    return sample(x, y, 0);
}

std::expected<std::array<float, 4>, ImageSampleError> Image::sample(std::uint32_t x,
                                                                    std::uint32_t y,
                                                                    std::uint32_t z) const {
    const FormatInfo& inf = format_info();
    std::array<float, 4> result{0};
    if (x >= width() || y >= height() || z >= depth_or_layers()) return std::unexpected(ImageSampleError::OutOfBounds);

    auto slice_stride = static_cast<std::size_t>(width()) * height() * inf.pixelSize();
    const std::byte* pixelPtr =
        data.data() + z * slice_stride + (static_cast<std::size_t>(y) * width() + x) * inf.pixelSize();

    for (std::uint32_t c = 0; c < inf.channels; c++) {
        float value = 0.0f;
        if (inf.isFloat) {
            if (inf.bytesPerChannel == 4) {
                value = *reinterpret_cast<const float*>(pixelPtr + c * inf.bytesPerChannel);
            }
        } else if (inf.is16Bit) {
            if (inf.bytesPerChannel == 2) {
                std::uint16_t raw = *reinterpret_cast<const std::uint16_t*>(pixelPtr + c * inf.bytesPerChannel);
                value             = static_cast<float>(raw) / 65535.0f;
            }
        } else {
            if (inf.bytesPerChannel == 1) {
                std::uint8_t raw = *reinterpret_cast<const std::uint8_t*>(pixelPtr + c * inf.bytesPerChannel);
                value            = static_cast<float>(raw) / 255.0f;
            }
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
    const FormatInfo& inf = format_info();
    if (x >= width() || y >= height() || z >= depth_or_layers()) return std::unexpected(ImageWriteError::OutOfBounds);
    if (values.size() < inf.channels) return std::unexpected(ImageWriteError::DataSizeMismatch);

    auto slice_stride = static_cast<std::size_t>(width()) * height() * inf.pixelSize();
    std::byte* pixelPtr =
        data.data() + z * slice_stride + (static_cast<std::size_t>(y) * width() + x) * inf.pixelSize();

    for (std::uint32_t c = 0; c < inf.channels; c++) {
        float value = values[c];
        if (inf.isFloat) {
            if (inf.bytesPerChannel == 4) {
                *reinterpret_cast<float*>(pixelPtr + c * inf.bytesPerChannel) = value;
            }
        } else if (inf.is16Bit) {
            if (inf.bytesPerChannel == 2) {
                std::uint16_t raw = static_cast<std::uint16_t>(std::clamp(value * 65535.0f, 0.0f, 65535.0f));
                *reinterpret_cast<std::uint16_t*>(pixelPtr + c * inf.bytesPerChannel) = raw;
            }
        } else {
            if (inf.bytesPerChannel == 1) {
                std::uint8_t raw = static_cast<std::uint8_t>(std::clamp(value * 255.0f, 0.0f, 255.0f));
                *reinterpret_cast<std::uint8_t*>(pixelPtr + c * inf.bytesPerChannel) = raw;
            }
        }
    }
    return {};
}
Image Image::convert(Format targetFmt) const {
    if (targetFmt == format()) {
        auto result = create_like(*this, format());
        std::memcpy(result.data.data(), data.data(), data.size());
        return result;
    }

    const FormatInfo& srcInfo = format_info();
    const FormatInfo& dstInfo = getFormatInfo(targetFmt);

    // promote to float, rearrange channels, demote to target.
    // A more optimized version would have switch-cases for specific conversions.

    Image result     = create_like(*this, targetFmt);
    auto pixel_count = static_cast<std::size_t>(width()) * height() * depth_or_layers();

    for (std::size_t i = 0; i < pixel_count; ++i) {
        float r = 0, g = 0, b = 0, a = 1.0f;

        // 1. Read Source into normalized float (0.0 - 1.0)
        if (srcInfo.isFloat) {
            const float* p = raw<float>() + i * srcInfo.channels;
            r              = p[0];
            if (srcInfo.channels >= 2) g = (srcInfo.channels == 2) ? p[1] : p[1];  // GA vs RGB
            if (srcInfo.channels >= 3) b = p[2];
            if (srcInfo.channels == 4) a = p[3];
            // Fix for Grey/GreyAlpha cases
            if (srcInfo.channels <= 2) {
                g = r;
                b = r;
                if (srcInfo.channels == 2) a = p[1];
            }
        } else if (srcInfo.is16Bit) {
            const std::uint16_t* p = raw<std::uint16_t>() + i * srcInfo.channels;
            auto norm              = [](std::uint16_t v) { return v / 65535.0f; };
            r                      = norm(p[0]);
            if (srcInfo.channels <= 2) {
                g = r;
                b = r;
                if (srcInfo.channels == 2) a = norm(p[1]);
            } else {
                g = norm(p[1]);
                b = norm(p[2]);
                if (srcInfo.channels == 4) a = norm(p[3]);
            }
        } else {  // 8 bit
            const std::uint8_t* p = raw<std::uint8_t>() + i * srcInfo.channels;
            auto norm             = [](std::uint8_t v) { return v / 255.0f; };
            r                     = norm(p[0]);
            if (srcInfo.channels <= 2) {
                g = r;
                b = r;
                if (srcInfo.channels == 2) a = norm(p[1]);
            } else {
                g = norm(p[1]);
                b = norm(p[2]);
                if (srcInfo.channels == 4) a = norm(p[3]);
            }
        }

        // 2. Write to Destination
        if (dstInfo.isFloat) {
            float* p = result.raw<float>() + i * dstInfo.channels;
            p[0]     = r;
            if (dstInfo.channels >= 2) p[1] = (dstInfo.channels == 2) ? a : g;
            if (dstInfo.channels >= 3) p[2] = b;
            if (dstInfo.channels == 4) p[3] = a;
        } else if (dstInfo.is16Bit) {
            std::uint16_t* p = result.raw<std::uint16_t>() + i * dstInfo.channels;
            auto denorm      = [](float v) { return static_cast<std::uint16_t>(std::clamp(v, 0.0f, 1.0f) * 65535.0f); };
            p[0]             = denorm(r);
            if (dstInfo.channels >= 2) p[1] = (dstInfo.channels == 2) ? denorm(a) : denorm(g);
            if (dstInfo.channels >= 3) p[2] = denorm(b);
            if (dstInfo.channels == 4) p[3] = denorm(a);
        } else {  // 8 bit
            std::uint8_t* p = result.raw<std::uint8_t>() + i * dstInfo.channels;
            auto denorm     = [](float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
            p[0]            = denorm(r);
            if (dstInfo.channels >= 2) p[1] = (dstInfo.channels == 2) ? denorm(a) : denorm(g);
            if (dstInfo.channels >= 3) p[2] = denorm(b);
            if (dstInfo.channels == 4) p[3] = denorm(a);
        }
    }

    return result;
}
Image Image::resize(std::uint32_t newW, std::uint32_t newH) const {
    auto& inf          = format_info();
    auto target_height = type() == ImageType::e1D ? 1u : newH;

    Image result = create_resized_like(*this, newW, target_height, format());

    stbir_datatype type = inf.isFloat ? STBIR_TYPE_FLOAT : (inf.is16Bit ? STBIR_TYPE_UINT16 : STBIR_TYPE_UINT8);

    auto src_slice_stride = static_cast<std::size_t>(width()) * height() * inf.pixelSize();
    auto dst_slice_stride = static_cast<std::size_t>(newW) * target_height * inf.pixelSize();

    for (std::uint32_t z = 0; z < depth_or_layers(); ++z) {
        stbir_resize(data.data() + z * src_slice_stride, width(), height(), 0,
                     result.data.data() + z * dst_slice_stride, newW, target_height, 0,
                     static_cast<stbir_pixel_layout>(inf.channels),  // works because enum matches channel count mostly
                     type, STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT);
    }

    return result;
}
Image Image::blur(std::uint32_t radius) const {
    // For simplicity, convert to float for processing, then convert back
    // unless it's already float. The float working format is four-channel
    // because Epix has no three-channel format (Bevy widens three-channel data
    // the same way), and `convert` fills the missing channels.
    bool needsConvert = !format_info().isFloat;
    Image work        = needsConvert ? this->convert(Format::RGBA32F) : *this;

    auto& inf    = work.format_info();
    Image temp   = create_like(work, work.format());
    Image result = create_like(work, work.format());

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
    std::uint32_t c   = inf.channels;
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

    if (needsConvert) {
        return result.convert(format());
    }
    return result;
}

void ImagePlugin::attach(epix::app::App& app) {
    spdlog::debug("[image] Attaching ImagePlugin.");
    assets::app_register_asset<Image>(app);
    assets::app_register_loader<ImageLoader>(app);
    auto& images = app.world_mut().resource_mut<assets::Assets<Image>>();
    (void)images.insert(DEFAULT_IMAGE_HANDLE.id(), Image{});
    (void)images.insert(TRANSPARENT_IMAGE_HANDLE.id(), Image::transparent());
}

}  // namespace epix::image
