module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <asio/awaitable.hpp>

export module epix.image;

import epix.core;
import epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
namespace epix::image {

/** @brief Pixel format for images.
 *
 * Each variant specifies channels and bit depth.
 */
// Enum Format (8bit, 16bit, Float)
export enum class Format {
    Unknown = 0,
    Grey8,       // 1 channel, 8-bit
    GreyAlpha8,  // 2 channels, 8-bit
    RGB8,        // 3 channels, 8-bit
    RGBA8,       // 4 channels, 8-bit
    Grey16,      // 1 channel, 16-bit
    RGB16,       // 3 channels, 16-bit
    RGBA16,      // 4 channels, 16-bit
    Grey32F,     // 1 channel, float
    RGB32F,      // 3 channels, float
    RGBA32F      // 4 channels, float
};

/** @brief Descriptive metadata for a pixel Format. */
export struct FormatInfo {
    /** @brief Number of color channels (1–4). */
    std::uint32_t channels;
    /** @brief Bytes per channel (1, 2, or 4). */
    std::uint32_t bytesPerChannel;
    /** @brief True if channels are 32-bit floating point. */
    bool isFloat;
    /** @brief True if channels are 16-bit integer. */
    bool is16Bit;

    /** @brief Get the total byte size of a single pixel. */
    std::size_t pixelSize() const { return channels * bytesPerChannel; }
};

/** @brief Look up the FormatInfo for a given pixel Format.
 * @param fmt The pixel format to query.
 * @return Reference to the static FormatInfo descriptor. */
export const FormatInfo& getFormatInfo(Format fmt);

/** @brief Error codes returned when loading an image from disk. */
export enum class ImageLoadError { FileNotFound, UnsupportedFormat, LoadFailed };
export std::exception_ptr to_exception_ptr(ImageLoadError error);
/** @brief Error codes returned when saving an image to disk. */
export enum class ImageSaveError { SaveFailed };
/** @brief Error codes returned when sampling a pixel out of bounds. */
export enum class ImageSampleError { OutOfBounds };
/** @brief Error codes returned when writing pixel data to an image. */
export enum class ImageWriteError { OutOfBounds, DataSizeMismatch };

template <typename T>
struct span_type {
    using span = decltype(std::span(std::declval<T>()));
    using type = typename span::value_type;
};

/** @brief Bitmask specifying how an image is used (main world, render
 * world, or both). */
export enum ImageUsage : std::uint8_t {
    /** @brief Used in the main world only. */
    Main = 0x1,
    /** @brief Used in the render world only. */
    Render = 0x2,
    /** @brief Used in both main and render worlds. */
    Both = Main | Render,
};

/** @brief Image dimension type. */
export enum class ImageType {
    /** @brief One-dimensional image. */
    e1D,
    /** @brief Two-dimensional image. */
    e2D,
    /** @brief Array of 2D image layers. */
    e2DArray,
    /** @brief Three-dimensional (volumetric) image. */
    e3D,
};

/** @brief Multi-dimensional image with runtime pixel format.
 *
 * Supports 1D, 2D, 2D array, and 3D images. Provides factory methods,
 * loading/saving, sampling, writing, and conversion utilities.
 */
export class Image {
   private:
    std::uint32_t m_width           = 1;
    std::uint32_t m_height          = 1;
    std::uint32_t m_depth_or_layers = 1;
    ImageType m_type                = ImageType::e2D;
    Format m_format                 = Format::Unknown;
    ImageUsage m_usage              = ImageUsage::Both;

    std::vector<std::byte> data;

    template <typename T>
    T* raw() {
        return reinterpret_cast<T*>(data.data());
    }
    template <typename T>
    const T* raw() const {
        return reinterpret_cast<const T*>(data.data());
    }

    Image() = default;

   public:
    /** @brief Create a zero-initialized image with the given type and
     * dimensions.
     * @param type Image dimension type.
     * @param w Width in pixels.
     * @param h Height in pixels.
     * @param depth_or_layers Depth (3D) or layer count (2D array).
     * @param fmt Pixel format. */
    static Image create(ImageType type, std::uint32_t w, std::uint32_t h, std::uint32_t depth_or_layers, Format fmt);
    /** @brief Create a zero-initialized 1D image.
     * @param w Width in pixels.
     * @param fmt Pixel format. */
    static Image create1d(std::uint32_t w, Format fmt);
    /** @brief Create a zero-initialized 2D image.
     * @param w Width in pixels.
     * @param h Height in pixels.
     * @param fmt Pixel format. */
    static Image create2d(std::uint32_t w, std::uint32_t h, Format fmt);
    /** @brief Create a zero-initialized 2D array image.
     * @param w Width in pixels.
     * @param h Height in pixels.
     * @param layers Number of array layers.
     * @param fmt Pixel format. */
    static Image create2d_array(std::uint32_t w, std::uint32_t h, std::uint32_t layers, Format fmt);
    /** @brief Create a zero-initialized 3D image.
     * @param w Width in pixels.
     * @param h Height in pixels.
     * @param depth Depth in pixels.
     * @param fmt Pixel format. */
    static Image create3d(std::uint32_t w, std::uint32_t h, std::uint32_t depth, Format fmt);
    /** @brief Create an image initialized with the provided data.
     * @tparam T Contiguous range of trivially-copyable elements.
     * @param type Image dimension type.
     * @param w Width in pixels.
     * @param h Height in pixels.
     * @param depth_or_layers Depth (3D) or layer count (2D array).
     * @param fmt Pixel format.
     * @param initData Initial pixel data; must match the expected byte size.
     * @return The image, or std::nullopt if the data size is wrong. */
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create(
        ImageType type, std::uint32_t w, std::uint32_t h, std::uint32_t depth_or_layers, Format fmt, T&& initData);
    /** @brief Create a 1D image initialized with the provided data.
     * @tparam T Contiguous range of trivially-copyable elements. */
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create1d(std::uint32_t w, Format fmt, T&& initData);
    /** @brief Create a 2D image initialized with the provided data.
     * @tparam T Contiguous range of trivially-copyable elements. */
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create2d(std::uint32_t w, std::uint32_t h, Format fmt, T&& initData);
    /** @brief Create a 2D array image initialized with the provided data.
     * @tparam T Contiguous range of trivially-copyable elements. */
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create2d_array(
        std::uint32_t w, std::uint32_t h, std::uint32_t layers, Format fmt, T&& initData);
    /** @brief Create a 3D image initialized with the provided data.
     * @tparam T Contiguous range of trivially-copyable elements. */
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create3d(
        std::uint32_t w, std::uint32_t h, std::uint32_t depth, Format fmt, T&& initData);

    /** @brief Load an image from the filesystem.
     * @param path Path to the image file.
     * @return The loaded Image or an ImageLoadError. */
    static std::expected<Image, ImageLoadError> load(const std::filesystem::path& path);
    /** @brief Save an image to the filesystem.
     * @param path Destination file path.
     * @param image The image to write.
     * @return Void on success or an ImageSaveError. */
    static std::expected<void, ImageSaveError> save(const std::filesystem::path& path, const Image& image);

    /** @brief Get the image width in pixels. */
    std::uint32_t width() const { return m_width; }
    /** @brief Get the image height in pixels. */
    std::uint32_t height() const { return m_height; }
    /** @brief Get the raw depth-or-layers value. */
    std::uint32_t depth_or_layers() const { return m_depth_or_layers; }
    /** @brief Get the depth (1 if not a 3D image). */
    std::uint32_t depth() const { return m_type == ImageType::e3D ? m_depth_or_layers : 1; }
    /** @brief Get the layer count (1 if not a 2D array image). */
    std::uint32_t layers() const { return m_type == ImageType::e2DArray ? m_depth_or_layers : 1; }
    /** @brief Get the image dimension type. */
    ImageType type() const { return m_type; }
    /** @brief Get the pixel format. */
    Format format() const { return m_format; }
    /** @brief Get the format metadata for this image's pixel format. */
    const FormatInfo& format_info() const { return getFormatInfo(m_format); }
    /** @brief Get the usage flags for this image. */
    ImageUsage usage() const { return m_usage; }
    /** @brief Set the usage flags.
     * @param usage New usage bitmask. */
    void set_usage(ImageUsage usage) { m_usage = usage; }

    /** @brief Get a read-only byte span of the raw pixel data. */
    std::span<const std::byte> raw_view() const { return std::as_bytes(std::span(data)); }
    std::span<std::byte> raw_view_mut() { return std::as_writable_bytes(std::span(data)); }

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
     * @brief Convert image to target format, return the converted image.
     */
    Image convert(Format targetFmt) const;
    /**
     * @brief Resize image to new width and height, return the resized image.
     */
    Image resize(std::uint32_t newW, std::uint32_t newH) const;
    /**
     * @brief Apply Gaussian blur to the image and return the blurred image.
     */
    Image blur(std::uint32_t radius) const;
};

/** @brief Asset loader for image files.
 *
 * Registered with the asset server to load supported image formats. */
export struct ImageLoader {
    using Asset = Image;
    struct Settings {};
    using Error = ImageLoadError;

    /** @brief Get the list of supported file extensions.
     * @return Span of extension strings (e.g. ".png", ".jpg"). */
    static std::span<std::string_view> extensions() noexcept;
    /** @brief Load an image asset from a reader.
     * @param context Asset loading context. */
    static asio::awaitable<std::expected<Image, ImageLoadError>> load(assets::Reader& reader,
                                                                      const Settings& settings,
                                                                      assets::LoadContext& context);
};
/** @brief Plugin that registers the image asset loader and related
 * systems. */
export struct ImagePlugin {
    void build(core::App& app);
};

template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::optional<Image> Image::create(
    ImageType type, std::uint32_t w, std::uint32_t h, std::uint32_t depth_or_layers, Format fmt, T&& initData) {
    std::optional<Image> img;
    auto& info               = getFormatInfo(fmt);
    std::size_t expectedSize = static_cast<std::size_t>(w) * h * depth_or_layers * info.pixelSize();
    auto init_bytes          = std::as_bytes(std::span(std::forward<T>(initData)));
    if (init_bytes.size() != expectedSize) return img;
    img.emplace(create(type, w, h, depth_or_layers, fmt));
    std::memcpy(img->data.data(), init_bytes.data(), expectedSize);
    return img;
}
template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::optional<Image> Image::create1d(std::uint32_t w, Format fmt, T&& initData) {
    std::optional<Image> img;
    auto& info               = getFormatInfo(fmt);
    std::size_t expectedSize = static_cast<std::size_t>(w) * info.pixelSize();
    auto init_bytes          = std::as_bytes(std::span(std::forward<T>(initData)));
    if (init_bytes.size() != expectedSize) return img;
    img.emplace(create1d(w, fmt));
    std::memcpy(img->data.data(), init_bytes.data(), expectedSize);
    return img;
}
template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::optional<Image> Image::create2d(std::uint32_t w, std::uint32_t h, Format fmt, T&& initData) {
    std::optional<Image> img;
    auto& info               = getFormatInfo(fmt);
    std::size_t expectedSize = static_cast<std::size_t>(w) * h * info.pixelSize();
    auto init_bytes          = std::as_bytes(std::span(std::forward<T>(initData)));
    if (init_bytes.size() != expectedSize) return img;
    img.emplace(create2d(w, h, fmt));
    std::memcpy(img->data.data(), init_bytes.data(), expectedSize);
    return img;
}
template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::optional<Image> Image::create2d_array(
    std::uint32_t w, std::uint32_t h, std::uint32_t layers, Format fmt, T&& initData) {
    std::optional<Image> img;
    auto& info               = getFormatInfo(fmt);
    std::size_t expectedSize = static_cast<std::size_t>(w) * h * layers * info.pixelSize();
    auto init_bytes          = std::as_bytes(std::span(std::forward<T>(initData)));
    if (init_bytes.size() != expectedSize) return img;
    img.emplace(create2d_array(w, h, layers, fmt));
    std::memcpy(img->data.data(), init_bytes.data(), expectedSize);
    return img;
}
template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::optional<Image> Image::create3d(std::uint32_t w, std::uint32_t h, std::uint32_t depth, Format fmt, T&& initData) {
    std::optional<Image> img;
    auto& info               = getFormatInfo(fmt);
    std::size_t expectedSize = static_cast<std::size_t>(w) * h * std::max(depth, 1u) * info.pixelSize();
    auto init_bytes          = std::as_bytes(std::span(std::forward<T>(initData)));
    if (init_bytes.size() != expectedSize) return img;
    img.emplace(create3d(w, h, depth, fmt));
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
    const FormatInfo& inf = format_info();
    auto bytes            = std::as_bytes(std::span(std::forward<T>(data)));
    if (x >= m_width || y >= m_height || z >= m_depth_or_layers) return std::unexpected(ImageWriteError::OutOfBounds);
    if (bytes.size() != inf.pixelSize()) return std::unexpected(ImageWriteError::DataSizeMismatch);

    auto slice_stride = static_cast<std::size_t>(m_width) * m_height * inf.pixelSize();
    std::byte* pixelPtr =
        this->data.data() + z * slice_stride + (static_cast<std::size_t>(y) * m_width + x) * inf.pixelSize();
    std::memcpy(pixelPtr, bytes.data(), inf.pixelSize());
    return {};
}
}  // namespace epix::image

template <>
struct std::hash<epix::assets::AssetId<epix::image::Image>> {
    std::size_t operator()(const epix::assets::AssetId<epix::image::Image>& id) const {
        return std::visit([]<typename U>(const U& index) { return std::hash<U>()(index); }, id);
    }
};