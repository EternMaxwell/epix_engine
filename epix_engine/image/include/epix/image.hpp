#pragma once

#include <nvrhi/nvrhi.h>

#include <epix/assets.hpp>
#include <epix/core.hpp>

namespace epix {
namespace image {
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
struct Rect {
    size_t x = 0;
    size_t y = 0;
    union {
        size_t z = 0;
        size_t layer;
    };
    size_t width  = 0;
    size_t height = 0;
    union {
        size_t depth = 1;
        size_t layers;
    };

    static Rect rect2d(size_t width, size_t height) {
        return Rect{
            .x      = 0,
            .y      = 0,
            .layer  = 0,
            .width  = width,
            .height = height,
            .layers = 1,
        };
    }
    static Rect rect2d(size_t x, size_t y, size_t width, size_t height) {
        return Rect{
            .x      = x,
            .y      = y,
            .layer  = 0,
            .width  = width,
            .height = height,
            .layers = 1,
        };
    }
    static Rect rect3d(size_t width, size_t height, size_t depth) {
        return Rect{
            .x      = 0,
            .y      = 0,
            .layer  = 0,
            .width  = width,
            .height = height,
            .depth  = depth,
        };
    }
    static Rect rect3d(size_t x, size_t y, size_t layer, size_t width, size_t height, size_t depth) {
        return Rect{
            .x      = x,
            .y      = y,
            .layer  = layer,
            .width  = width,
            .height = height,
            .depth  = depth,
        };
    }
};
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

    std::expected<void, ImageDataError> set_data_internal(Rect rect, const void* data, size_t data_size);

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
    static std::optional<Image> with_desc(const nvrhi::TextureDesc& desc);
    void flip_vertical();
    void* pixel_mut(size_t x, size_t y, size_t layer) {
        size_t bytes_per_pixel = nvrhi::getFormatInfo(info.format).bytesPerBlock;
        return &data[layer * info.width * info.height * bytes_per_pixel + y * info.width * bytes_per_pixel +
                     x * bytes_per_pixel];
    }
    const void* pixel(size_t x, size_t y, size_t layer) const {
        size_t bytes_per_pixel = nvrhi::getFormatInfo(info.format).bytesPerBlock;
        return &data[layer * info.width * info.height * bytes_per_pixel + y * info.width * bytes_per_pixel +
                     x * bytes_per_pixel];
    }
    std::expected<void, ImageDataError> write_data(Rect rect, const void* data, size_t data_size) {
        return set_data_internal(rect, data, data_size);
    }
    template <typename T>
    std::expected<void, ImageDataError> set_data(Rect rect, std::span<const T> data) {
        return set_data_internal(rect, data.data(), sizeof(T) * data.size());
    }
};

struct ImagePlugin {
    void build(App& app);
};

struct StbImageLoader {
    static std::span<const char* const> extensions() noexcept;
    static Image load(const std::filesystem::path& path, assets::LoadContext& context);
};
}  // namespace image
// template <>
// struct epix::render::assets::RenderAsset<image::Image> {
//     using Param          = ParamSet<Res<nvrhi::DeviceHandle>>;
//     using ProcessedAsset = nvrhi::TextureHandle;

//     ProcessedAsset process(image::Image&& asset, Param& param);
//     RenderAssetUsage usage(const image::Image& asset);
// };
}  // namespace epix
