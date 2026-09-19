#include <epix/image/image.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace epix::image {

namespace {
/** @brief The `image` crate's sRGB-to-CIE-Y luminance coefficients
 * (`image-0.25.10/src/color.rs:605`). */
constexpr std::uint32_t kSrgbLumaRed   = 2126;
constexpr std::uint32_t kSrgbLumaGreen = 7152;
constexpr std::uint32_t kSrgbLumaBlue  = 722;
constexpr std::uint32_t kSrgbLumaDiv   = 10000;

/** @brief `image::color::rgb_to_luma` for 8-bit samples.
 *
 * The crate accumulates in `u32` (`Enlargeable for u8 { type Larger = u32; }`) and truncates
 * through `clamp_from`, so the division is integer division clamped to the sample maximum. */
std::uint8_t rgb_to_luma(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept {
    const std::uint32_t luma = kSrgbLumaRed * red + kSrgbLumaGreen * green + kSrgbLumaBlue * blue;
    return static_cast<std::uint8_t>(std::min<std::uint32_t>(luma / kSrgbLumaDiv, 255));
}

/** @brief `Primitive::DEFAULT_MAX_VALUE`, the value `image` widens a missing alpha to. */
template <typename T>
constexpr T kDefaultMaxValue = T{};
template <>
constexpr std::uint8_t kDefaultMaxValue<std::uint8_t> = 255;
template <>
constexpr std::uint16_t kDefaultMaxValue<std::uint16_t> = 65535;
template <>
constexpr float kDefaultMaxValue<float> = 1.0f;

/** @brief The raw bytes of a typed pixel buffer. */
template <typename T>
std::vector<std::byte> to_bytes(const std::vector<T>& samples) {
    std::vector<std::byte> bytes(samples.size() * sizeof(T));
    if (!samples.empty()) std::memcpy(bytes.data(), samples.data(), bytes.size());
    return bytes;
}
}  // namespace

std::uint32_t DynamicImage::width() const noexcept {
    return std::visit([](const auto& buffer) { return buffer.width; }, static_cast<const Base&>(*this));
}

std::uint32_t DynamicImage::height() const noexcept {
    return std::visit([](const auto& buffer) { return buffer.height; }, static_cast<const Base&>(*this));
}

Rgba8Image DynamicImage::into_rgba8() const {
    return std::visit(
        [](const auto& buffer) -> Rgba8Image {
            using Buffer = std::remove_cvref_t<decltype(buffer)>;
            Rgba8Image rgba;
            rgba.width  = buffer.width;
            rgba.height = buffer.height;
            const std::size_t pixels = buffer.pixel_count();
            rgba.data.resize(pixels * 4);

            if constexpr (std::is_same_v<Buffer, Luma8Image>) {
                for (std::size_t index = 0; index < pixels; ++index) {
                    const std::uint8_t luma = buffer.data[index];
                    rgba.data[index * 4 + 0] = luma;
                    rgba.data[index * 4 + 1] = luma;
                    rgba.data[index * 4 + 2] = luma;
                    rgba.data[index * 4 + 3] = kDefaultMaxValue<std::uint8_t>;
                }
            } else if constexpr (std::is_same_v<Buffer, LumaA8Image>) {
                for (std::size_t index = 0; index < pixels; ++index) {
                    const std::uint8_t luma  = buffer.data[index * 2 + 0];
                    rgba.data[index * 4 + 0] = luma;
                    rgba.data[index * 4 + 1] = luma;
                    rgba.data[index * 4 + 2] = luma;
                    rgba.data[index * 4 + 3] = buffer.data[index * 2 + 1];
                }
            } else if constexpr (std::is_same_v<Buffer, Rgb8Image>) {
                for (std::size_t index = 0; index < pixels; ++index) {
                    rgba.data[index * 4 + 0] = buffer.data[index * 3 + 0];
                    rgba.data[index * 4 + 1] = buffer.data[index * 3 + 1];
                    rgba.data[index * 4 + 2] = buffer.data[index * 3 + 2];
                    rgba.data[index * 4 + 3] = kDefaultMaxValue<std::uint8_t>;
                }
            } else if constexpr (std::is_same_v<Buffer, Rgba8Image>) {
                rgba.data = buffer.data;
            } else {
                // Only the 8-bit variants are reachable: `Image::from_dynamic` handles the
                // 16-bit and float buffers directly and `Image::convert` converts from
                // `Image::try_into_dynamic`'s three 8-bit buffers.
                assert(false && "into_rgba8 is only implemented for the 8-bit variants");
            }

            return rgba;
        },
        static_cast<const Base&>(*this));
}

Rgba16Image DynamicImage::into_rgba16() const {
    return std::visit(
        [](const auto& buffer) -> Rgba16Image {
            using Buffer = std::remove_cvref_t<decltype(buffer)>;
            Rgba16Image rgba;
            rgba.width  = buffer.width;
            rgba.height = buffer.height;
            const std::size_t pixels = buffer.pixel_count();
            rgba.data.resize(pixels * 4);

            if constexpr (std::is_same_v<Buffer, Rgb16Image>) {
                for (std::size_t index = 0; index < pixels; ++index) {
                    rgba.data[index * 4 + 0] = buffer.data[index * 3 + 0];
                    rgba.data[index * 4 + 1] = buffer.data[index * 3 + 1];
                    rgba.data[index * 4 + 2] = buffer.data[index * 3 + 2];
                    rgba.data[index * 4 + 3] = kDefaultMaxValue<std::uint16_t>;
                }
            } else if constexpr (std::is_same_v<Buffer, Rgba16Image>) {
                rgba.data = buffer.data;
            } else {
                // Only the 16-bit variants are reachable: `Image::from_dynamic` widens `Rgb16`
                // through this call and keeps `Rgba16` as it is.
                assert(false && "into_rgba16 is only implemented for the 16-bit variants");
            }

            return rgba;
        },
        static_cast<const Base&>(*this));
}

Luma8Image DynamicImage::into_luma8() const {
    return std::visit(
        [](const auto& buffer) -> Luma8Image {
            using Buffer = std::remove_cvref_t<decltype(buffer)>;
            Luma8Image luma;
            luma.width  = buffer.width;
            luma.height = buffer.height;
            const std::size_t pixels = buffer.pixel_count();
            luma.data.resize(pixels);

            if constexpr (std::is_same_v<Buffer, Luma8Image>) {
                luma.data = buffer.data;
            } else if constexpr (std::is_same_v<Buffer, LumaA8Image>) {
                for (std::size_t index = 0; index < pixels; ++index) {
                    luma.data[index] = buffer.data[index * 2];
                }
            } else if constexpr (std::is_same_v<Buffer, Rgb8Image>) {
                for (std::size_t index = 0; index < pixels; ++index) {
                    luma.data[index] = rgb_to_luma(
                        buffer.data[index * 3 + 0], buffer.data[index * 3 + 1], buffer.data[index * 3 + 2]);
                }
            } else if constexpr (std::is_same_v<Buffer, Rgba8Image>) {
                for (std::size_t index = 0; index < pixels; ++index) {
                    luma.data[index] = rgb_to_luma(
                        buffer.data[index * 4 + 0], buffer.data[index * 4 + 1], buffer.data[index * 4 + 2]);
                }
            } else {
                assert(false && "into_luma8 is only implemented for the 8-bit variants");
            }

            return luma;
        },
        static_cast<const Base&>(*this));
}

LumaA8Image DynamicImage::into_luma_alpha8() const {
    return std::visit(
        [](const auto& buffer) -> LumaA8Image {
            using Buffer = std::remove_cvref_t<decltype(buffer)>;
            LumaA8Image luma_alpha;
            luma_alpha.width  = buffer.width;
            luma_alpha.height = buffer.height;
            const std::size_t pixels = buffer.pixel_count();
            luma_alpha.data.resize(pixels * 2);

            if constexpr (std::is_same_v<Buffer, Luma8Image>) {
                for (std::size_t index = 0; index < pixels; ++index) {
                    luma_alpha.data[index * 2 + 0] = buffer.data[index];
                    luma_alpha.data[index * 2 + 1] = kDefaultMaxValue<std::uint8_t>;
                }
            } else if constexpr (std::is_same_v<Buffer, LumaA8Image>) {
                luma_alpha.data = buffer.data;
            } else if constexpr (std::is_same_v<Buffer, Rgb8Image>) {
                for (std::size_t index = 0; index < pixels; ++index) {
                    luma_alpha.data[index * 2 + 0] = rgb_to_luma(
                        buffer.data[index * 3 + 0], buffer.data[index * 3 + 1], buffer.data[index * 3 + 2]);
                    luma_alpha.data[index * 2 + 1] = kDefaultMaxValue<std::uint8_t>;
                }
            } else if constexpr (std::is_same_v<Buffer, Rgba8Image>) {
                for (std::size_t index = 0; index < pixels; ++index) {
                    luma_alpha.data[index * 2 + 0] = rgb_to_luma(
                        buffer.data[index * 4 + 0], buffer.data[index * 4 + 1], buffer.data[index * 4 + 2]);
                    luma_alpha.data[index * 2 + 1] = buffer.data[index * 4 + 3];
                }
            } else {
                assert(false && "into_luma_alpha8 is only implemented for the 8-bit variants");
            }

            return luma_alpha;
        },
        static_cast<const Base&>(*this));
}

std::string IntoDynamicImageError::to_string() const {
    return std::visit(
        [](const auto& value) -> std::string {
            using Alternative = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Alternative, into_dynamic_image_error::UnsupportedFormat>) {
                // Bevy renders `{0:?}`; the name space is the WebGPU wrapper's, as in G26.
                return std::format("Conversion into dynamic image not supported for {}.", wgpu::to_string(value.format));
            } else if constexpr (std::same_as<Alternative, into_dynamic_image_error::UnknownConversionError>) {
                return std::format("Failed to convert into {}.", wgpu::to_string(value.format));
            } else {
                return std::string("Image has no texture data");
            }
        },
        static_cast<const Base&>(*this));
}

Image Image::from_dynamic(DynamicImage dyn_img, bool is_srgb, assets::RenderAssetUsages asset_usage) {
    const std::uint32_t width  = dyn_img.width();
    const std::uint32_t height = dyn_img.height();

    std::vector<std::byte> data;
    wgpu::TextureFormat format = wgpu::TextureFormat::eUndefined;

    if (std::holds_alternative<Luma8Image>(dyn_img) || std::holds_alternative<LumaA8Image>(dyn_img) ||
        std::holds_alternative<Rgb8Image>(dyn_img)) {
        // The three-channel and luma buffers are all widened to RGBA.
        data   = to_bytes(dyn_img.into_rgba8().data);
        format = is_srgb ? wgpu::TextureFormat::eRGBA8UnormSrgb : wgpu::TextureFormat::eRGBA8Unorm;
    } else if (const auto* image = std::get_if<Rgba8Image>(&dyn_img)) {
        data   = to_bytes(image->data);
        format = is_srgb ? wgpu::TextureFormat::eRGBA8UnormSrgb : wgpu::TextureFormat::eRGBA8Unorm;
    } else if (const auto* image = std::get_if<Luma16Image>(&dyn_img)) {
        data   = to_bytes(image->data);
        format = wgpu::TextureFormat::eR16Uint;
    } else if (const auto* image = std::get_if<LumaA16Image>(&dyn_img)) {
        data   = to_bytes(image->data);
        format = wgpu::TextureFormat::eRG16Uint;
    } else if (std::holds_alternative<Rgb16Image>(dyn_img)) {
        data   = to_bytes(dyn_img.into_rgba16().data);
        format = wgpu::TextureFormat::eRGBA16Unorm;
    } else if (const auto* image = std::get_if<Rgba16Image>(&dyn_img)) {
        data   = to_bytes(image->data);
        format = wgpu::TextureFormat::eRGBA16Unorm;
    } else if (const auto* image = std::get_if<Rgb32FImage>(&dyn_img)) {
        format = wgpu::TextureFormat::eRGBA32Float;
        data   = std::vector<std::byte>(image->pixel_count() * 4 * sizeof(float));
        auto* target = reinterpret_cast<float*>(data.data());
        for (std::size_t index = 0; index < image->pixel_count(); ++index) {
            target[index * 4 + 0] = image->data[index * 3 + 0];
            target[index * 4 + 1] = image->data[index * 3 + 1];
            target[index * 4 + 2] = image->data[index * 3 + 2];
            target[index * 4 + 3] = kDefaultMaxValue<float>;
        }
    } else if (const auto* image = std::get_if<Rgba32FImage>(&dyn_img)) {
        data   = to_bytes(image->data);
        format = wgpu::TextureFormat::eRGBA32Float;
    }

    return Image{wgpu::Extent3D{width, height, 1},
                 wgpu::TextureDimension::e2D,
                 std::move(data),
                 format,
                 asset_usage};
}

std::expected<DynamicImage, IntoDynamicImageError> Image::try_into_dynamic() const {
    if (!m_has_data) {
        return std::unexpected(IntoDynamicImageError{into_dynamic_image_error::UninitializedImage{}});
    }

    const std::uint32_t width  = this->width();
    const std::uint32_t height = this->height();
    const std::size_t pixels   = static_cast<std::size_t>(width) * height;
    const auto bytes           = raw_view();

    // `ImageBuffer::from_raw` answers `None` when the payload does not match the dimensions.
    const auto mismatched = [this]() -> std::expected<DynamicImage, IntoDynamicImageError> {
        return std::unexpected(IntoDynamicImageError{
            into_dynamic_image_error::UnknownConversionError{texture_descriptor.format}});
    };

    switch (texture_descriptor.format) {
        case wgpu::TextureFormat::eR8Unorm: {
            if (bytes.size() != pixels) return mismatched();
            Luma8Image image{width, height, {}};
            image.data.resize(pixels);
            std::memcpy(image.data.data(), bytes.data(), bytes.size());
            return DynamicImage{std::move(image)};
        }
        case wgpu::TextureFormat::eRG8Unorm: {
            if (bytes.size() != pixels * 2) return mismatched();
            LumaA8Image image{width, height, {}};
            image.data.resize(pixels * 2);
            std::memcpy(image.data.data(), bytes.data(), bytes.size());
            return DynamicImage{std::move(image)};
        }
        case wgpu::TextureFormat::eRGBA8UnormSrgb: {
            if (bytes.size() != pixels * 4) return mismatched();
            Rgba8Image image{width, height, {}};
            image.data.resize(pixels * 4);
            std::memcpy(image.data.data(), bytes.data(), bytes.size());
            return DynamicImage{std::move(image)};
        }
        case wgpu::TextureFormat::eBGRA8UnormSrgb:
        case wgpu::TextureFormat::eBGRA8Unorm: {
            if (bytes.size() != pixels * 4) return mismatched();
            Rgba8Image image{width, height, {}};
            image.data.resize(pixels * 4);
            std::memcpy(image.data.data(), bytes.data(), bytes.size());
            // `try_into_dynamic` swaps the blue and red channels of a BGRA payload.
            for (std::size_t index = 0; index < pixels; ++index) {
                std::swap(image.data[index * 4 + 0], image.data[index * 4 + 2]);
            }
            return DynamicImage{std::move(image)};
        }
        default:
            return std::unexpected(
                IntoDynamicImageError{into_dynamic_image_error::UnsupportedFormat{texture_descriptor.format}});
    }
}

}  // namespace epix::image
