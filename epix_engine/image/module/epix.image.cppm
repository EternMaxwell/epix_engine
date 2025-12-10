/**
 * @file epix.image.cppm
 * @brief C++20 module interface for image handling.
 *
 * This module provides image loading and manipulation functionality.
 */
module;

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <variant>
#include <vector>

// Third-party headers
#include <nvrhi/nvrhi.h>

export module epix.image;

export import epix.core;
export import epix.assets;

export namespace epix::image {

/**
 * @brief Error when image data size doesn't match expected size.
 */
struct SizeMismatchError {
    size_t expected_size;
    size_t given_size;
};

/**
 * @brief Error when trying to manipulate compressed image data.
 */
struct CompressedImageNotSupported {
    nvrhi::Format image_format;
};

/**
 * @brief Error when extending image data out of bounds.
 */
struct ExtendOutOfBoundsError {};

/**
 * @brief Image data error variant.
 */
struct ImageDataError : std::variant<SizeMismatchError, CompressedImageNotSupported, ExtendOutOfBoundsError> {
    using variant::variant;
};

/**
 * @brief Image data container with format and dimension information.
 */
struct Image {
   private:
    uint8_t main_world : 1   = 0;
    uint8_t render_world : 1 = 0;
    std::vector<uint8_t> data;
    nvrhi::TextureDesc info = nvrhi::TextureDesc()
                                  .setDepth(1)
                                  .setArraySize(1)
                                  .setMipLevels(1)
                                  .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                  .setKeepInitialState(true);

   public:
    const nvrhi::TextureDesc& get_desc() const { return info; }
    nvrhi::TextureDesc& get_desc_mut() { return info; }
    std::span<const uint8_t> get_data() const { return data; }
    bool is_main_world() const { return main_world; }
    bool is_render_world() const { return render_world; }

    static Image srgba8norm(uint32_t width, uint32_t height);
    static Image srgba8unorm_render(uint32_t width, uint32_t height);
    static Image srgba8unorm_main(uint32_t width, uint32_t height);
    static Image rgba32float(uint32_t width, uint32_t height);
    static Image rgba32float_render(uint32_t width, uint32_t height);
    static Image rgba32float_main(uint32_t width, uint32_t height);

    void flip_vertical();

    template <typename T>
    std::expected<void, ImageDataError> set_data(
        size_t x, size_t y, size_t width, size_t height, std::span<const T> data);
};

/**
 * @brief Plugin for image loading support.
 */
struct ImagePlugin {
    void build(epix::core::App& app);
};

/**
 * @brief STB-based image loader.
 */
struct StbImageLoader {
    static std::span<const char* const> extensions() noexcept;
    static Image load(const std::filesystem::path& path, epix::assets::LoadContext& context);
};

}  // namespace epix::image
