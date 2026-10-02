// Verifies that GpuImage preparation matches bevy_render's texture handling:
// the fallback image defaults (bevy_render/src/texture/fallback_image.rs), the
// view and mip metadata taken from the image's own descriptors
// (bevy_render/src/texture/gpu_image.rs) and the mip- and layer-aware upload
// that honours TextureDataOrder.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <epix/app.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <webgpu/webgpu.hpp>

namespace {

namespace image   = epix::image;
namespace render  = epix::render;
namespace texture = epix::render::texture;

constexpr std::uint32_t kBytesPerRow   = 256;
constexpr std::uint32_t kRowsPerRegion = 2;
constexpr std::uint64_t kRegionSize    = kBytesPerRow * kRowsPerRegion;

struct ReadbackRequest {
    wgpu::Origin3D origin;
    std::uint32_t mip_level = 0;
    wgpu::Extent3D extent{1, 1, 1};
};

// Creates a Vulkan device and publishes the resources GpuImage preparation
// needs, mirroring tests/render/header/mesh_allocator_tests.cpp.
bool initialize_image_test_app(epix::app::App& app) {
    wgpu::Instance instance = wgpu::createInstance();
    if (!instance) return false;
    wgpu::Adapter adapter =
        instance.requestAdapter(wgpu::RequestAdapterOptions().setBackendType(wgpu::BackendType::eVulkan));
    if (!adapter) return false;
    wgpu::Device device = adapter.requestDevice(wgpu::DeviceDescriptor{});
    if (!device) return false;
    wgpu::Queue queue = device.getQueue();
    if (!queue) return false;
    wgpu::SamplerDescriptor sampler_descriptor;
    sampler_descriptor.setLabel("image_gpu_tests sampler");
    wgpu::Sampler sampler = device.createSampler(sampler_descriptor);
    if (!sampler) return false;

    epix::ecs::World& world = app.world_mut();
    world.insert_resource(instance.clone());
    world.insert_resource(adapter.clone());
    world.insert_resource(device.clone());
    world.insert_resource(queue.clone());
    world.insert_resource(render::DefaultImageSampler{.sampler = sampler});
    return true;
}

bool has_usage(wgpu::TextureUsage usage, wgpu::TextureUsage bit) noexcept {
    return (static_cast<std::uint64_t>(usage) & static_cast<std::uint64_t>(bit)) != 0;
}

// Copies the requested texture regions into one buffer and maps it back. Each
// region occupies kRegionSize bytes so the layout stays uniform no matter how
// many rows the copied extent covers.
std::vector<std::uint8_t> read_texture_regions(const wgpu::Device& device,
                                               const wgpu::Queue& queue,
                                               const wgpu::Texture& texture,
                                               const std::vector<ReadbackRequest>& requests) {
    const std::uint64_t buffer_size = kRegionSize * requests.size();
    wgpu::Buffer buffer = device.createBuffer(wgpu::BufferDescriptor()
                                                  .setSize(buffer_size)
                                                  .setUsage(wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eMapRead)
                                                  .setLabel("image_gpu_tests readback"));
    if (!buffer) return {};

    wgpu::CommandEncoder encoder = device.createCommandEncoder();
    for (std::size_t region = 0; region < requests.size(); ++region) {
        const ReadbackRequest& request = requests[region];
        wgpu::TexelCopyTextureInfo source;
        source.setTexture(texture)
            .setMipLevel(request.mip_level)
            .setOrigin(request.origin)
            .setAspect(wgpu::TextureAspect::eAll);
        wgpu::TexelCopyBufferInfo destination;
        destination.setBuffer(buffer).setLayout(wgpu::TexelCopyBufferLayout()
                                                    .setOffset(kRegionSize * region)
                                                    .setBytesPerRow(kBytesPerRow)
                                                    .setRowsPerImage(kRowsPerRegion));
        encoder.copyTextureToBuffer(source, destination, request.extent);
    }
    queue.submit(encoder.finish());

    std::vector<std::uint8_t> result(static_cast<std::size_t>(buffer_size), 0);
    bool mapped = false;
    wgpu::BufferMapCallback callback = wgpu::BufferMapCallback([&](wgpu::MapAsyncStatus status, wgpu::StringView) {
        if (status != wgpu::MapAsyncStatus::eSuccess) return;
        const void* range = buffer.getConstMappedRange(0, buffer.getSize());
        if (range != nullptr) {
            std::memcpy(result.data(), range, result.size());
            mapped = true;
        }
        buffer.unmap();
    });
    buffer.mapAsync(wgpu::MapMode::eRead, 0, buffer_size,
                    wgpu::BufferMapCallbackInfo()
                        .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                        .setCallback(std::move(callback)));
    for (int attempt = 0; attempt < 1000 && !mapped; ++attempt) {
        device.poll(wgpu::Bool(true));
    }
    return mapped ? result : std::vector<std::uint8_t>{};
}

// A 2x2, 2-layer, 2-mip RGBA8 payload whose every byte of block (layer, mip)
// equals 16 * layer + mip, laid out in the requested order.
std::vector<std::byte> make_mip_layer_data(image::TextureDataOrder order) {
    constexpr std::uint32_t layer_count   = 2;
    constexpr std::uint32_t mip_count     = 2;
    constexpr std::uint32_t mip_0_bytes   = 2 * 2 * 4;
    constexpr std::uint32_t mip_1_bytes   = 1 * 1 * 4;
    const std::uint32_t mip_bytes[mip_count] = {mip_0_bytes, mip_1_bytes};

    std::vector<std::byte> data;
    data.reserve(2 * (mip_0_bytes + mip_1_bytes));
    const auto append_block = [&](std::uint32_t layer, std::uint32_t mip) {
        const std::uint8_t value = static_cast<std::uint8_t>(16u * layer + mip);
        data.insert(data.end(), mip_bytes[mip], static_cast<std::byte>(value));
    };
    if (order == image::TextureDataOrder::LayerMajor) {
        for (std::uint32_t layer = 0; layer < layer_count; ++layer) {
            for (std::uint32_t mip = 0; mip < mip_count; ++mip) append_block(layer, mip);
        }
    } else {
        for (std::uint32_t mip = 0; mip < mip_count; ++mip) {
            for (std::uint32_t layer = 0; layer < layer_count; ++layer) append_block(layer, mip);
        }
    }
    return data;
}

// State for the one-shot preparation system below; it must outlive the app so
// the registered system never reads a destroyed object.
struct PrepareState {
    std::optional<image::Image> pending;
    std::optional<texture::GpuImage> prepared;
};

void register_preparation_system(epix::app::App& app, PrepareState& state, const char* name) {
    app.add_systems(epix::app::Update,
                    epix::ecs::into([&state](render::RenderAsset<texture::GpuImage>::Param params) {
                        if (!state.pending) return;
                        render::RenderAsset<texture::GpuImage> adapter;
                        auto result = adapter.prepare_asset(std::move(*state.pending),
                                                            epix::assets::AssetId<image::Image>::invalid(), params,
                                                            nullptr);
                        state.pending.reset();
                        if (result) state.prepared = std::move(*result);
                    }).set_name(name));
}

image::Image make_two_layer_two_mip_image(image::TextureDataOrder order) {
    image::Image source{wgpu::Extent3D{2, 2, 2}, wgpu::TextureDimension::e2D, make_mip_layer_data(order),
                        wgpu::TextureFormat::eRGBA8UnormSrgb, epix::assets::RenderAssetUsages::RENDER_WORLD};
    source.texture_descriptor.mipLevelCount = 2;
    source.data_order                       = order;
    return source;
}

TEST(GpuImage, AspectRatioRejectsZeroDimensions) {
    texture::GpuImage image;
    image.size = wgpu::Extent3D{4, 2, 1};
    EXPECT_FLOAT_EQ(image.aspect_ratio(), 2.0f);

    image.size = wgpu::Extent3D{0, 2, 1};
    EXPECT_THROW(static_cast<void>(image.aspect_ratio()), std::invalid_argument);

    image.size = wgpu::Extent3D{4, 0, 1};
    EXPECT_THROW(static_cast<void>(image.aspect_ratio()), std::invalid_argument);
}

TEST(GpuImageAsset, FallbackImagesMatchBevyDefaults) {
    epix::app::App app = epix::app::App::create();
    if (!initialize_image_test_app(app)) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping device test";
    }

    const wgpu::TextureFormat expected_format = epix::image::bevy_default<wgpu::TextureFormat>();
    texture::FallbackImage fallback         = texture::FallbackImage::from_world(app.world_mut());
    texture::FallbackImageZero zero         = texture::FallbackImageZero::from_world(app.world_mut());
    texture::FallbackImageCubemap cubemap   = texture::FallbackImageCubemap::from_world(app.world_mut());

    struct FallbackCase {
        wgpu::TextureViewDimension view;
        wgpu::TextureDimension dimension;
        std::uint32_t layers;
    };
    const FallbackCase cases[] = {
        {wgpu::TextureViewDimension::e1D, wgpu::TextureDimension::e1D, 1},
        {wgpu::TextureViewDimension::e2D, wgpu::TextureDimension::e2D, 1},
        {wgpu::TextureViewDimension::e2DArray, wgpu::TextureDimension::e2D, 1},
        {wgpu::TextureViewDimension::eCube, wgpu::TextureDimension::e2D, 6},
        {wgpu::TextureViewDimension::eCubeArray, wgpu::TextureDimension::e2D, 6},
        {wgpu::TextureViewDimension::e3D, wgpu::TextureDimension::e3D, 1},
    };
    for (const FallbackCase& expected : cases) {
        const texture::GpuImage* image = fallback.get(expected.view);
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->texture_format, expected_format);
        EXPECT_FALSE(image->texture_view_format.has_value());
        EXPECT_EQ(image->mip_level_count, 1u);
        EXPECT_TRUE(image->had_data);
        EXPECT_EQ(image->size.width, 1u);
        EXPECT_EQ(image->size.height, 1u);
        EXPECT_EQ(image->size.depthOrArrayLayers, expected.layers);
        EXPECT_TRUE(static_cast<bool>(image->texture));

        const wgpu::TextureUsage usage = image->texture.getUsage();
        EXPECT_TRUE(has_usage(usage, wgpu::TextureUsage::eTextureBinding));
        EXPECT_TRUE(has_usage(usage, wgpu::TextureUsage::eCopyDst));
        EXPECT_TRUE(has_usage(usage, wgpu::TextureUsage::eCopySrc));
        // Bevy adds render-attachment usage for 2D fallbacks only
        // (fallback_image.rs:107-110).
        EXPECT_EQ(has_usage(usage, wgpu::TextureUsage::eRenderAttachment),
                  image->texture.getDimension() == wgpu::TextureDimension::e2D);
        EXPECT_EQ(image->texture.getFormat(), expected_format);
        EXPECT_EQ(image->texture.getDimension(), expected.dimension);
        EXPECT_EQ(image->texture.getMipLevelCount(), 1u);
        EXPECT_EQ(image->texture.getSampleCount(), 1u);
        EXPECT_EQ(image->texture.getWidth(), 1u);
        EXPECT_EQ(image->texture.getHeight(), 1u);
        EXPECT_EQ(image->texture.getDepthOrArrayLayers(), expected.layers);
    }

    EXPECT_EQ(zero.image.texture_format, expected_format);
    EXPECT_FALSE(zero.image.texture_view_format.has_value());
    EXPECT_EQ(zero.image.texture.getDimension(), wgpu::TextureDimension::e2D);
    EXPECT_EQ(zero.image.texture.getDepthOrArrayLayers(), 1u);
    EXPECT_TRUE(has_usage(zero.image.texture.getUsage(), wgpu::TextureUsage::eCopySrc));

    EXPECT_EQ(cubemap.image.texture_format, expected_format);
    EXPECT_FALSE(cubemap.image.texture_view_format.has_value());
    EXPECT_EQ(cubemap.image.texture.getDimension(), wgpu::TextureDimension::e2D);
    EXPECT_EQ(cubemap.image.texture.getDepthOrArrayLayers(), 6u);

    // The uploaded fill is observable: white for the main fallbacks and the
    // cubemap, transparent black for the zero fallback.
    auto device = app.world_mut().get_resource<wgpu::Device>();
    auto queue  = app.world_mut().get_resource<wgpu::Queue>();
    ASSERT_TRUE(device);
    ASSERT_TRUE(queue);
    const std::vector<ReadbackRequest> requests{{wgpu::Origin3D{0, 0, 0}, 0, wgpu::Extent3D{1, 1, 1}}};
    const std::vector<std::uint8_t> white =
        read_texture_regions(device->get(), queue->get(), fallback.d2.texture, requests);
    ASSERT_EQ(white.size(), kRegionSize);
    for (std::size_t channel = 0; channel < 4; ++channel) {
        EXPECT_EQ(white[channel], 255u);
    }
    const std::vector<std::uint8_t> transparent =
        read_texture_regions(device->get(), queue->get(), zero.image.texture, requests);
    ASSERT_EQ(transparent.size(), kRegionSize);
    for (std::size_t channel = 0; channel < 4; ++channel) {
        EXPECT_EQ(transparent[channel], 0u);
    }
    const std::vector<std::uint8_t> cube_face =
        read_texture_regions(device->get(), queue->get(), cubemap.image.texture, requests);
    ASSERT_EQ(cube_face.size(), kRegionSize);
    for (std::size_t channel = 0; channel < 4; ++channel) {
        EXPECT_EQ(cube_face[channel], 255u);
    }
}

TEST(GpuImageAsset, PrepareAssetUsesTheDefaultViewDescriptorWhenNoneIsGiven) {
    epix::app::App app = epix::app::App::create();
    if (!initialize_image_test_app(app)) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping device test";
    }

    PrepareState state;
    state.pending = make_two_layer_two_mip_image(image::TextureDataOrder::LayerMajor);
    register_preparation_system(app, state, "prepare image with the default view descriptor");
    app.update();

    ASSERT_FALSE(state.pending.has_value()) << "the preparation system did not run";
    ASSERT_TRUE(state.prepared.has_value());
    // Bevy leaves `texture_view_format` unset unless the image carries an
    // explicit view descriptor (gpu_image.rs:130-137).
    EXPECT_FALSE(state.prepared->texture_view_format.has_value());
    EXPECT_EQ(state.prepared->texture_format, wgpu::TextureFormat::eRGBA8UnormSrgb);
    EXPECT_EQ(state.prepared->mip_level_count, 2u);
    EXPECT_EQ(state.prepared->texture.getMipLevelCount(), 2u);
    EXPECT_EQ(state.prepared->texture.getDepthOrArrayLayers(), 2u);
}

TEST(GpuImageAsset, PrepareAssetHonoursAnExplicitViewDescriptor) {
    epix::app::App app = epix::app::App::create();
    if (!initialize_image_test_app(app)) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping device test";
    }

    PrepareState state;
    state.pending = make_two_layer_two_mip_image(image::TextureDataOrder::LayerMajor);
    // Bevy hands an explicit `texture_view_descriptor` straight to create_view
    // (gpu_image.rs:113-118). A format *override* additionally needs the texture
    // to declare that format in `TextureDescriptor::viewFormats`, which Bevy only
    // does in Image::new_target_texture (bevy_image/src/image.rs:1137-1184) - a
    // constructor the port does not have (IMG-182). This descriptor therefore
    // views the texture with the texture's own format, and what is asserted is
    // that the explicit descriptor, not the default one, was used.
    state.pending->texture_view_descriptor =
        wgpu::TextureViewDescriptor().setFormat(wgpu::TextureFormat::eRGBA8UnormSrgb);
    register_preparation_system(app, state, "prepare image with an explicit view descriptor");
    app.update();

    ASSERT_FALSE(state.pending.has_value()) << "the preparation system did not run";
    ASSERT_TRUE(state.prepared.has_value());
    ASSERT_TRUE(state.prepared->texture_view_format.has_value());
    EXPECT_EQ(*state.prepared->texture_view_format, wgpu::TextureFormat::eRGBA8UnormSrgb);
    EXPECT_EQ(state.prepared->mip_level_count, 2u);
}

class GpuImageDataOrder : public ::testing::TestWithParam<image::TextureDataOrder> {};

TEST_P(GpuImageDataOrder, PrepareAssetUploadsEveryMipAndLayerInDataOrder) {
    epix::app::App app = epix::app::App::create();
    if (!initialize_image_test_app(app)) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping device test";
    }

    const image::TextureDataOrder order = GetParam();
    PrepareState state;
    state.pending = make_two_layer_two_mip_image(order);
    register_preparation_system(app, state, "prepare image for the data order under test");
    app.update();

    ASSERT_TRUE(state.prepared.has_value());
    const texture::GpuImage& gpu_image = *state.prepared;
    ASSERT_EQ(gpu_image.mip_level_count, 2u);

    auto device = app.world_mut().get_resource<wgpu::Device>();
    auto queue  = app.world_mut().get_resource<wgpu::Queue>();
    ASSERT_TRUE(device);
    ASSERT_TRUE(queue);

    std::vector<ReadbackRequest> requests;
    for (std::uint32_t layer = 0; layer < 2; ++layer) {
        for (std::uint32_t mip = 0; mip < 2; ++mip) {
            const std::uint32_t extent = mip == 0 ? 2u : 1u;
            requests.push_back(ReadbackRequest{wgpu::Origin3D{0, 0, layer}, mip, wgpu::Extent3D{extent, extent, 1}});
        }
    }
    const std::vector<std::uint8_t> data =
        read_texture_regions(device->get(), queue->get(), gpu_image.texture, requests);
    ASSERT_EQ(data.size(), kRegionSize * requests.size());

    for (std::uint32_t layer = 0; layer < 2; ++layer) {
        for (std::uint32_t mip = 0; mip < 2; ++mip) {
            const std::uint8_t expected = static_cast<std::uint8_t>(16u * layer + mip);
            const std::uint8_t* region  = data.data() + kRegionSize * (layer * 2 + mip);
            const std::uint32_t rows    = mip == 0 ? 2u : 1u;
            const std::uint32_t row_bytes = mip == 0 ? 8u : 4u;
            for (std::uint32_t row = 0; row < rows; ++row) {
                for (std::uint32_t byte = 0; byte < row_bytes; ++byte) {
                    EXPECT_EQ(region[row * kBytesPerRow + byte], expected)
                        << "layer " << layer << " mip " << mip << " row " << row << " byte " << byte;
                }
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(DataOrders,
                         GpuImageDataOrder,
                         ::testing::Values(image::TextureDataOrder::LayerMajor,
                                           image::TextureDataOrder::MipMajor));

}  // namespace
