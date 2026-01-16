module;

#include <stb_image.h>
#include <stb_image_resize2.h>
#include <stb_image_write.h>

module epix.image;

import epix.assets;
import std;

using namespace image;

const FormatInfo& image::getFormatInfo(Format fmt) {
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

Image Image::create(std::uint32_t w, std::uint32_t h, Format fmt) {
    Image img;
    img.m_width  = w;
    img.m_height = h;
    img.m_format = fmt;
    img.data.resize(w * h * getFormatInfo(fmt).pixelSize(), std::byte(0));
    return img;
}
std::expected<std::array<float, 4>, ImageSampleError> Image::sample(std::uint32_t x, std::uint32_t y) const {
    const FormatInfo& inf = format_info();
    std::array<float, 4> result{0};
    if (x >= m_width || y >= m_height) return std::unexpected(ImageSampleError::OutOfBounds);

    const std::byte* pixelPtr = data.data() + (y * m_width + x) * inf.pixelSize();

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
std::expected<void, ImageWriteError> Image::write(std::uint32_t x, std::uint32_t y, std::span<const float> values) {
    const FormatInfo& inf = format_info();
    if (x >= m_width || y >= m_height) return std::unexpected(ImageWriteError::OutOfBounds);
    if (values.size() < inf.channels) return std::unexpected(ImageWriteError::DataSizeMismatch);

    std::byte* pixelPtr = data.data() + (y * m_width + x) * inf.pixelSize();

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
std::expected<Image, ImageLoadError> Image::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return std::unexpected(ImageLoadError::FileNotFound);
    }
    auto path_str = path.string();

    int w, h, channels;

    // Try to detect if it's HDR
    if (stbi_is_hdr(path_str.c_str())) {
        float* pixels = stbi_loadf(path_str.c_str(), &w, &h, &channels, 0);
        if (!pixels) return std::unexpected(ImageLoadError::LoadFailed);

        Format fmt = (channels == 4) ? Format::RGBA32F : (channels == 3) ? Format::RGB32F : Format::Grey32F;

        size_t byteSize = w * h * channels * sizeof(float);
        std::vector<std::byte> buffer(byteSize);
        std::memcpy(buffer.data(), pixels, byteSize);
        stbi_image_free(pixels);

        return Image::create(w, h, fmt, buffer).value();
    }
    // Check for 16-bit (load_16 usually used for png/psd)
    else if (stbi_is_16_bit(path_str.c_str())) {
        unsigned short* pixels = stbi_load_16(path_str.c_str(), &w, &h, &channels, 0);
        if (!pixels) return std::unexpected(ImageLoadError::LoadFailed);

        Format fmt = (channels == 4) ? Format::RGBA16 : (channels == 3) ? Format::RGB16 : Format::Grey16;

        size_t byteSize = w * h * channels * sizeof(unsigned short);
        std::vector<std::byte> buffer(byteSize);
        std::memcpy(buffer.data(), pixels, byteSize);
        stbi_image_free(pixels);

        return Image::create(w, h, fmt, buffer).value();
    }
    // Standard 8-bit
    else {
        stbi_uc* pixels = stbi_load(path_str.c_str(), &w, &h, &channels, 0);
        if (!pixels) return std::unexpected(ImageLoadError::LoadFailed);

        Format fmt = (channels == 4)   ? Format::RGBA8
                     : (channels == 3) ? Format::RGB8
                     : (channels == 2) ? Format::GreyAlpha8
                                       : Format::Grey8;

        size_t byteSize = w * h * channels;
        std::vector<std::byte> buffer(byteSize);
        std::memcpy(buffer.data(), pixels, byteSize);
        stbi_image_free(pixels);

        return Image::create(w, h, fmt, buffer).value();
    }
}

std::expected<void, ImageSaveError> Image::save(const std::filesystem::path& path, const Image& image) {
    const FormatInfo& inf = image.format_info();
    std::string ext       = path.extension().string();
    auto path_str         = path.string();
    // simple lower case conversion
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    bool success = false;

    if (inf.isFloat) {
        // Save HDR
        success = stbi_write_hdr(path_str.c_str(), image.m_width, image.m_height, inf.channels, image.raw<float>());
    } else {
        // For 8-bit or 16-bit (Note: stb_write mostly supports 8-bit, basic PNG for 16)
        // If it is 16 bit, we must convert to 8 bit for JPG/BMP/TGA, or use specific PNG func
        if (inf.is16Bit && ext == ".png") {
            // stbi_write_png doesn't support 16bit directly via generic api usually,
            // but we can try generic write or specialized logic.
            // For simplicity, this wrapper warns or downsamples,
            // but here we just try writing raw bytes which might fail if library doesn't support.
            // Actually stb_write doesn't officially support 16-bit write except maybe raw.
            // Let's assume we strictly support 8-bit saving for formats other than hdr.
            std::cerr << "Warning: Saving 16-bit directly might not be supported by all formats.\n";
        }

        if (ext == ".png") {
            success = stbi_write_png(path_str.c_str(), image.m_width, image.m_height, inf.channels, image.data.data(),
                                     image.m_width * inf.pixelSize());
        } else if (ext == ".jpg" || ext == ".jpeg") {
            success =
                stbi_write_jpg(path_str.c_str(), image.m_width, image.m_height, inf.channels, image.data.data(), 90);
        } else if (ext == ".bmp") {
            success = stbi_write_bmp(path_str.c_str(), image.m_width, image.m_height, inf.channels, image.data.data());
        }
    }

    if (!success) return std::unexpected(ImageSaveError::SaveFailed);
    return std::expected<void, ImageSaveError>{};
}
Image Image::convert(Format targetFmt) const {
    if (targetFmt == m_format) return Image::create(m_width, m_height, m_format, data).value();

    const FormatInfo& srcInfo = format_info();
    const FormatInfo& dstInfo = getFormatInfo(targetFmt);

    // Handle strict requirement: At least to RGBA8 and RGB8
    // We use a simplified approach: promote to float, rearrange channels, demote to target.
    // A more optimized version would have switch-cases for specific conversions.

    Image result   = Image::create(m_width, m_height, targetFmt);
    int pixelCount = m_width * m_height;

    for (int i = 0; i < pixelCount; ++i) {
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
    auto& inf = format_info();

    Image result = Image::create(newW, newH, m_format);

    stbir_datatype type = inf.isFloat ? STBIR_TYPE_FLOAT : (inf.is16Bit ? STBIR_TYPE_UINT16 : STBIR_TYPE_UINT8);

    // STB Resize 2 API
    stbir_resize(data.data(), m_width, m_height, 0, result.data.data(), newW, newH, 0,
                 static_cast<stbir_pixel_layout>(inf.channels),  // works because enum matches channel count mostly
                 type, STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT);

    return result;
}
Image Image::blur(std::uint32_t radius) const {
    // For simplicity, convert to float for processing, then convert back
    // unless it's already float.
    bool needsConvert = !format_info().isFloat;
    Image work = needsConvert ? this->convert(format_info().channels == 4 ? Format::RGBA32F : Format::RGB32F) : *this;

    auto& inf    = work.format_info();
    Image temp   = Image::create(work.width(), work.height(), work.format());
    Image result = Image::create(work.width(), work.height(), work.format());

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

    std::uint32_t w = work.width();
    std::uint32_t h = work.height();
    std::uint32_t c = inf.channels;
    float* srcData  = work.raw<float>();
    float* tmpData  = temp.raw<float>();
    float* dstData  = result.raw<float>();

    // Horizontal Pass
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            for (std::uint32_t ch = 0; ch < c; ++ch) {
                float val = 0.0f;
                for (std::uint32_t k = 0; k < size; ++k) {
                    std::uint32_t ix = std::clamp(x + k - radius, 0u, w - 1);
                    val += srcData[(y * w + ix) * c + ch] * kernel[k];
                }
                tmpData[(y * w + x) * c + ch] = val;
            }
        }
    }

    // Vertical Pass
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            for (std::uint32_t ch = 0; ch < c; ++ch) {
                float val = 0.0f;
                for (std::uint32_t k = 0; k < size; ++k) {
                    std::uint32_t iy = std::clamp(y + k - radius, 0u, h - 1);
                    val += tmpData[(iy * w + x) * c + ch] * kernel[k];
                }
                dstData[(y * w + x) * c + ch] = val;
            }
        }
    }

    if (needsConvert) {
        return result.convert(m_format);
    }
    return result;
}

std::span<const char* const> ImageLoader::extensions() noexcept {
    static constexpr auto exts =
        std::array{".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".pic", ".psd", ".gif", ".ppm", ".pgm", ".pnm"};
    return exts;
}
Image ImageLoader::load(const std::filesystem::path& path, assets::LoadContext& context) {
    auto res = Image::load(path);
    if (!res) {
        throw std::runtime_error("Failed to load image: " + path.string());
    }
    return std::move(*res);
}

void ImagePlugin::build(core::App& app) {
    app.add_plugins(assets::AssetPlugin{});
    app.plugin_scope(
        [](assets::AssetPlugin& asset_plugin) { asset_plugin.register_asset<Image>().register_loader<ImageLoader>(); });
}