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
/** @brief The WebGPU texture format for a decoded pixel format (Bevy's
 * `ImageFormat` to `TextureFormat` mapping).
 *
 * Formats with no direct WebGPU equivalent report `Undefined`; `image_loader`
 * converts 3-channel data to the 4-channel equivalent before it reaches a
 * texture, so those cases are unreachable for prepared images. */
wgpu::TextureFormat texture_format_cast(Format format) noexcept {
    switch (format) {
        case Format::Grey8:
            return wgpu::TextureFormat::eR8Unorm;
        case Format::GreyAlpha8:
            return wgpu::TextureFormat::eRG8Unorm;
        case Format::RGB8:
            return wgpu::TextureFormat::eUndefined;
        case Format::RGBA8:
            return wgpu::TextureFormat::eRGBA8Unorm;
        case Format::Grey16:
            return wgpu::TextureFormat::eR16Uint;
        case Format::RGB16:
            return wgpu::TextureFormat::eUndefined;
        case Format::RGBA16:
            return wgpu::TextureFormat::eRGBA16Uint;
        case Format::Grey32F:
            return wgpu::TextureFormat::eR32Float;
        case Format::RGB32F:
            return wgpu::TextureFormat::eUndefined;
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
    static FormatInfo info3{3, 1, false, false};
    static FormatInfo info4{4, 1, false, false};
    static FormatInfo info5{1, 2, false, true};
    static FormatInfo info6{3, 2, false, true};
    static FormatInfo info7{4, 2, false, true};
    static FormatInfo info8{1, 4, true, false};
    static FormatInfo info9{3, 4, true, false};
    static FormatInfo info10{4, 4, true, false};
    static FormatInfo info0{0, 0, false, false};
    switch (fmt) {
        case Format::Grey8:
            return info1;
        case Format::GreyAlpha8:
            return info2;
        case Format::RGB8:
            return info3;
        case Format::RGBA8:
            return info4;
        case Format::Grey16:
            return info5;
        case Format::RGB16:
            return info6;
        case Format::RGBA16:
            return info7;
        case Format::Grey32F:
            return info8;
        case Format::RGB32F:
            return info9;
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
    extracted.m_sampler_descriptor    = m_sampler_descriptor;
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
    // unless it's already float.
    bool needsConvert = !format_info().isFloat;
    Image work = needsConvert ? this->convert(format_info().channels == 4 ? Format::RGBA32F : Format::RGB32F) : *this;

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
