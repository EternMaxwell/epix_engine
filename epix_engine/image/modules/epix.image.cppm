/**
 * @file epix.image.cppm
 * @brief Image module for epix engine
 * 
 * This module provides image loading, processing, and management functionality.
 * It integrates with the asset system and provides loaders for common image formats.
 */

export module epix.image;

// Standard library includes
#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

// Third-party includes (not modules yet)
#include <nvrhi/nvrhi.h>
#include <stb_image.h>

// Module imports (when these are converted to modules)
// For now, we include the headers
#include <epix/assets.hpp>
#include <epix/core.hpp>

/**
 * Image processing and loading functionality
 */
export namespace epix::image {
    
    /**
     * Error types for image operations
     */
    struct SizeMismatchError {
        size_t expected_size;
        size_t given_size;
    };
    
    struct CompressedImageNotSupported {
        nvrhi::Format image_format;
    };
    
    struct ExtendOutOfBoundsError {};
    
    struct ImageDataError : std::variant<SizeMismatchError, CompressedImageNotSupported, ExtendOutOfBoundsError> {
        using variant::variant;
    };
    
    /**
     * Image class representing a 2D image with various formats
     */
    class Image {
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

        friend struct StbImageLoader;

        std::expected<void, ImageDataError> set_data_internal(
            size_t x, size_t y, size_t width, size_t height, const void* data, size_t data_size);

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
            size_t x, size_t y, size_t width, size_t height, std::span<const T> data) {
            return set_data_internal(x, y, width, height, data.data(), sizeof(T) * data.size());
        }
    };

    /**
     * Plugin for registering image functionality with the app
     */
    struct ImagePlugin {
        void build(epix::App& app);
    };

    /**
     * STB image loader for common formats
     */
    struct StbImageLoader {
        static std::span<const char* const> extensions() noexcept;
        static Image load(const std::filesystem::path& path, epix::assets::LoadContext& context);
    };

}  // namespace epix::image
