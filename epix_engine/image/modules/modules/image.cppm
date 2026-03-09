export module epix.image;

import epix.core;
import epix.assets;
import std;

export namespace image {

// Enum Format (8bit, 16bit, Float)
enum class Format {
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

// Format Info Class
struct FormatInfo {
    std::uint32_t channels;
    std::uint32_t bytesPerChannel;
    bool isFloat;
    bool is16Bit;

    std::size_t pixelSize() const { return channels * bytesPerChannel; }
};

const FormatInfo& getFormatInfo(Format fmt);

enum class ImageLoadError { FileNotFound, UnsupportedFormat, LoadFailed };
enum class ImageSaveError { SaveFailed };
enum class ImageSampleError { OutOfBounds };
enum class ImageWriteError { OutOfBounds, DataSizeMismatch };

template <typename T>
struct span_type {
    using span = decltype(std::span(std::declval<T>()));
    using type = typename span::value_type;
};

enum ImageUsage : std::uint8_t {
    Main   = 0x1,
    Render = 0x2,
    Both   = Main | Render,
};

export enum class ImageType {
    e1D,
    e2D,
    e2DArray,
    e3D,
};

// Image Struct
export class Image {
   private:
    // Format/Size constant after construct
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
    // Constructors
    static Image create(ImageType type, std::uint32_t w, std::uint32_t h, std::uint32_t depth_or_layers, Format fmt);
    static Image create1d(std::uint32_t w, Format fmt);
    static Image create2d(std::uint32_t w, std::uint32_t h, Format fmt);
    static Image create2d_array(std::uint32_t w, std::uint32_t h, std::uint32_t layers, Format fmt);
    static Image create3d(std::uint32_t w, std::uint32_t h, std::uint32_t depth, Format fmt);
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create(
        ImageType type, std::uint32_t w, std::uint32_t h, std::uint32_t depth_or_layers, Format fmt, T&& initData);
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create1d(std::uint32_t w, Format fmt, T&& initData);
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create2d(std::uint32_t w, std::uint32_t h, Format fmt, T&& initData);
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create2d_array(
        std::uint32_t w, std::uint32_t h, std::uint32_t layers, Format fmt, T&& initData);
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create3d(
        std::uint32_t w, std::uint32_t h, std::uint32_t depth, Format fmt, T&& initData);

    // Loader and Saver
    static std::expected<Image, ImageLoadError> load(const std::filesystem::path& path);
    static std::expected<void, ImageSaveError> save(const std::filesystem::path& path, const Image& image);

    // Getters
    std::uint32_t width() const { return m_width; }
    std::uint32_t height() const { return m_height; }
    std::uint32_t depth_or_layers() const { return m_depth_or_layers; }
    std::uint32_t depth() const { return m_type == ImageType::e3D ? m_depth_or_layers : 1; }
    std::uint32_t layers() const { return m_type == ImageType::e2DArray ? m_depth_or_layers : 1; }
    ImageType type() const { return m_type; }
    Format format() const { return m_format; }
    const FormatInfo& format_info() const { return getFormatInfo(m_format); }
    ImageUsage usage() const { return m_usage; }
    void set_usage(ImageUsage usage) { m_usage = usage; }

    // views
    std::span<const std::byte> raw_view() const { return std::as_bytes(std::span(data)); }

    // sample and write
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

    // processing
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

export struct ImageLoader {
    static std::span<const char* const> extensions() noexcept;
    static Image load(const std::filesystem::path& path, assets::LoadContext& context);
};
export struct ImagePlugin {
    void build(core::App& app);
};

template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
static std::optional<Image> Image::create(
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
static std::optional<Image> Image::create1d(std::uint32_t w, Format fmt, T&& initData) {
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
static std::optional<Image> Image::create2d(std::uint32_t w, std::uint32_t h, Format fmt, T&& initData) {
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
static std::optional<Image> Image::create2d_array(
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
static std::optional<Image> Image::create3d(
    std::uint32_t w, std::uint32_t h, std::uint32_t depth, Format fmt, T&& initData) {
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
}  // namespace image

template <>
struct std::hash<assets::AssetId<image::Image>> {
    std::size_t operator()(const assets::AssetId<image::Image>& id) const {
        return std::visit([]<typename U>(const U& index) { return std::hash<U>()(index); }, id);
    }
};