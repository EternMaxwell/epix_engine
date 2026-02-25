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

// Image Struct
export class Image {
   private:
    // Format/Size constant after construct
    std::uint32_t m_width;
    std::uint32_t m_height;
    Format m_format;
    ImageUsage m_usage = ImageUsage::Both;

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
    static Image create(std::uint32_t w, std::uint32_t h, Format fmt);
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    static std::optional<Image> create(std::uint32_t w, std::uint32_t h, Format fmt, T&& initData);

    // Loader and Saver
    static std::expected<Image, ImageLoadError> load(const std::filesystem::path& path);
    static std::expected<void, ImageSaveError> save(const std::filesystem::path& path, const Image& image);

    // Getters
    std::uint32_t width() const { return m_width; }
    std::uint32_t height() const { return m_height; }
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
    std::expected<std::array<float, 4>, ImageSampleError> sample(std::uint32_t x, std::uint32_t y) const;
    /**
     * @brief Write pixel data at (x, y). The data size must match the pixel size of the image format.
     */
    template <typename T>
        requires requires(T&& t) {
            { std::span(std::forward<T>(t)) };
            requires std::is_trivially_copyable_v<typename span_type<T>::type>;
        }
    std::expected<void, ImageWriteError> write_raw(std::uint32_t x, std::uint32_t y, T&& data);
    /**
     * @brief Write pixel data at (x, y). The values are normalized floats (0.0 - 1.0) for each channel, unless hdr.
     */
    std::expected<void, ImageWriteError> write(std::uint32_t x, std::uint32_t y, std::span<const float> values);

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
static std::optional<Image> Image::create(std::uint32_t w, std::uint32_t h, Format fmt, T&& initData) {
    std::optional<Image> img;
    auto& info               = getFormatInfo(fmt);
    std::size_t expectedSize = w * h * info.pixelSize();
    auto data                = std::as_bytes(std::span(std::forward<T>(initData)));
    if (data.size() != expectedSize) return img;
    img.emplace(create(w, h, fmt));
    std::memcpy(img->data.data(), data.data(), expectedSize);
    return img;
}
template <typename T>
    requires requires(T&& t) {
        { std::span(std::forward<T>(t)) };
        requires std::is_trivially_copyable_v<typename span_type<T>::type>;
    }
std::expected<void, ImageWriteError> Image::write_raw(std::uint32_t x, std::uint32_t y, T&& data) {
    const FormatInfo& inf = format_info();
    auto span             = std::as_bytes(std::span(std::forward<T>(data)));
    if (x >= m_width || y >= m_height) return std::unexpected(ImageWriteError::OutOfBounds);
    if (span.size() != inf.pixelSize()) return std::unexpected(ImageWriteError::DataSizeMismatch);

    std::byte* pixelPtr = data.data() + (y * m_width + x) * inf.pixelSize();
    std::memcpy(pixelPtr, span.data(), inf.pixelSize());
    return {};
}
}  // namespace image

template <>
struct std::hash<assets::AssetId<image::Image>> {
    std::size_t operator()(const assets::AssetId<image::Image>& id) const {
        return std::visit([]<typename U>(const U& index) { return std::hash<U>()(index); }, id);
    }
};