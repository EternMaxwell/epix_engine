// epix_image - Pure C++20 Module (No Headers)
// Image data structures and loading for textures

module;

// Global module fragment
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <span>
#include <variant>
#include <vector>

// Third-party libraries
#include <nvrhi/nvrhi.h>
#include <stb_image.h>

export module epix_image;

// Import dependencies
import epix_core;
import epix_assets;

// ============================================================================
// Error types
// ============================================================================

export namespace epix::image {

using namespace epix::core;
using namespace epix::assets;

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

// ============================================================================
// Image class
// ============================================================================

struct Image {
private:
    uint8_t main_world : 1   = 0;
    uint8_t render_world : 1 = 0;
    // The raw data of the image
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

// ============================================================================
// Plugin and Loader
// ============================================================================

struct ImagePlugin {
    void build(App& app);
};

struct StbImageLoader {
    static std::span<const char* const> extensions() noexcept;
    static Image load(const std::filesystem::path& path, LoadContext& context);
};

}  // namespace epix::image
