#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/render/screenshot.hpp>
#include <epix/task.hpp>
#include <webgpu/webgpu.hpp>

using namespace epix::ecs;
using namespace epix::app;
using namespace epix::render;
using namespace epix::render::screenshot;
using namespace epix::image;
using namespace epix::assets;

namespace {

struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4).build()});
    }
} g_io_task_pool_init;

constexpr float CLEAR_R = 0.5f;
constexpr float CLEAR_G = 0.25f;
constexpr float CLEAR_B = 0.75f;
constexpr float CLEAR_A = 1.0f;

constexpr uint32_t TEX_W = 4u;
constexpr uint32_t TEX_H = 4u;

}  // namespace

TEST(ScreenshotPlugin, CaptureClearColorTexture) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();

    try {
        RenderPlugin{}.attach(app);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << e.what();
        return;
    }

    ScreenshotPlugin{}.attach(app);

    app.run_schedule(Startup);

    auto& device = app.resource_mut<wgpu::Device>();
    auto& queue  = app.resource_mut<wgpu::Queue>();

    wgpu::Texture texture =
        device.createTexture(wgpu::TextureDescriptor()
                                 .setLabel("screenshot_test_texture")
                                 .setSize(wgpu::Extent3D(TEX_W, TEX_H, 1))
                                 .setFormat(wgpu::TextureFormat::eRGBA8Unorm)
                                 .setSampleCount(1)
                                 .setMipLevelCount(1)
                                 .setUsage(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eCopySrc));
    ASSERT_TRUE(texture) << "Failed to create test texture";

    {
        auto view    = texture.createView();
        auto encoder = device.createCommandEncoder();
        auto rp      = encoder.beginRenderPass(wgpu::RenderPassDescriptor().setColorAttachments(
            std::array{wgpu::RenderPassColorAttachment()
                           .setView(view)
                           .setLoadOp(wgpu::LoadOp::eClear)
                           .setStoreOp(wgpu::StoreOp::eStore)
                           .setDepthSlice(~0u)
                           .setClearValue(wgpu::Color(CLEAR_R, CLEAR_G, CLEAR_B, CLEAR_A))}));
        rp.end();
        queue.submit(encoder.finish());
        device.poll(wgpu::Bool(true));
    }

    app.resource_mut<Events<ScreenCapture>>().push(
        ScreenCapture{.target = camera::RenderTarget::from_texture(texture)});

    auto render_sub = app.take_sub_app(epix::render::Render);
    ASSERT_TRUE(render_sub) << "Render sub-app not found";

    render_sub->extract(app);
    render_sub->update();
    render_sub->extract(app);
    app.insert_sub_app(epix::render::Render, std::move(render_sub));

    const auto& result_events = app.resource<Events<ScreenCaptureResult>>();
    ASSERT_FALSE(result_events.empty()) << "No ScreenCaptureResult was delivered";

    const ScreenCaptureResult* ev = result_events.get(result_events.head());
    ASSERT_TRUE(ev != nullptr) << "Could not access first ScreenCaptureResult";

    const auto& images = app.resource<Assets<Image>>();
    auto img_opt       = images.get(ev->handle.id());
    ASSERT_TRUE(img_opt.has_value()) << "Image not found in Assets<Image>";

    const Image& img = img_opt->get();
    auto bytes       = img.raw_view();

    constexpr std::size_t pixel_count = TEX_W * TEX_H;
    ASSERT_EQ(bytes.size(), pixel_count * 4u) << "Unexpected image byte size: " << bytes.size();

    const int exp_r = static_cast<int>(CLEAR_R * 255.0f + 0.5f);
    const int exp_g = static_cast<int>(CLEAR_G * 255.0f + 0.5f);
    const int exp_b = static_cast<int>(CLEAR_B * 255.0f + 0.5f);
    const int exp_a = static_cast<int>(CLEAR_A * 255.0f + 0.5f);

    constexpr int kTolerance = 2;
    for (std::size_t i = 0; i < pixel_count; ++i) {
        int r = static_cast<int>(static_cast<uint8_t>(bytes[i * 4 + 0]));
        int g = static_cast<int>(static_cast<uint8_t>(bytes[i * 4 + 1]));
        int b = static_cast<int>(static_cast<uint8_t>(bytes[i * 4 + 2]));
        int a = static_cast<int>(static_cast<uint8_t>(bytes[i * 4 + 3]));

        EXPECT_NEAR(r, exp_r, kTolerance) << "Pixel[" << i << "] R";
        EXPECT_NEAR(g, exp_g, kTolerance) << "Pixel[" << i << "] G";
        EXPECT_NEAR(b, exp_b, kTolerance) << "Pixel[" << i << "] B";
        EXPECT_NEAR(a, exp_a, kTolerance) << "Pixel[" << i << "] A";
    }
}
