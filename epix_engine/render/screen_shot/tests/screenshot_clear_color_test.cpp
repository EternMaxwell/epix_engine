// Test: ScreenshotPlugin captures a solid clear-color offscreen texture.
// The test manually drives the render sub-app (no window / GLFW required).

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

import epix.core;
import epix.render;
import epix.render.screenshot;
import epix.image;
import epix.assets;
import epix.tasks;
import webgpu;

using namespace epix::core;
using namespace epix::render;
using namespace epix::render::screenshot;
using namespace epix::image;
using namespace epix::assets;

namespace {

// Ensure an IO task pool exists (required by AssetPlugin / ShaderPlugin).
struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::tasks::IoTaskPool::get_or_init([] { return epix::tasks::TaskPool{4}; });
    }
} g_io_task_pool_init;

// Known clear color (linear, not sRGB).
constexpr float CLEAR_R = 0.5f;
constexpr float CLEAR_G = 0.25f;
constexpr float CLEAR_B = 0.75f;
constexpr float CLEAR_A = 1.0f;

// Texture dimensions
constexpr uint32_t TEX_W = 4u;
constexpr uint32_t TEX_H = 4u;

}  // namespace

// ---------------------------------------------------------------------------
// Test
// ---------------------------------------------------------------------------

TEST(ScreenshotPlugin, CaptureClearColorTexture) {
    App app = App::create();

    // Build RenderPlugin first; skip test if GPU/Vulkan is unavailable.
    try {
        RenderPlugin{}.build(app);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << e.what();
        return;
    }

    ScreenshotPlugin{}.build(app);

    // Run startup schedule so plugin-registered startup systems can execute.
    app.run_schedule(Startup);

    // ------------------------------------------------------------------
    // Step 1: create a 4x4 RGBA8Unorm offscreen texture and clear it
    //         with a known color using a WebGPU render pass.
    // ------------------------------------------------------------------
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
        device.poll(wgpu::Bool(true));  // wait for GPU to finish
    }

    // ------------------------------------------------------------------
    // Step 2: push a ScreenCapture event into the main world targeting
    //         the offscreen texture we just cleared.
    // ------------------------------------------------------------------
    app.resource_mut<Events<ScreenCapture>>().push(
        ScreenCapture{.target = camera::RenderTarget::from_texture(texture)});

    // ------------------------------------------------------------------
    // Step 3: manually drive the render sub-app.
    //
    //   extract() run #1  – reads the ScreenCapture from the main world
    //                        and queues it in ScreenshotState::pending.
    //   update()           – runs capture_frame (Cleanup set): copies the
    //                        texture to a staging buffer, stores the
    //                        resulting Image in ScreenshotState::completed.
    //   extract() run #2  – delivers completed images to the main world's
    //                        Assets<Image> and fires ScreenCaptureResult.
    // ------------------------------------------------------------------
    auto render_sub = app.take_sub_app(epix::render::Render);
    ASSERT_TRUE(render_sub) << "Render sub-app not found";

    render_sub->extract(app);  // ExtractSchedule: queues capture request
    render_sub->update();      // Render: capture_frame -> completed
    render_sub->extract(app);  // ExtractSchedule: delivers results
    app.insert_sub_app(epix::render::Render, std::move(render_sub));

    // ------------------------------------------------------------------
    // Step 4: verify the captured image.
    // ------------------------------------------------------------------
    const auto& result_events = app.resource<Events<ScreenCaptureResult>>();
    ASSERT_FALSE(result_events.empty()) << "No ScreenCaptureResult was delivered";

    const ScreenCaptureResult* ev = result_events.get(result_events.head());
    ASSERT_TRUE(ev != nullptr) << "Could not access first ScreenCaptureResult";

    const auto& images = app.resource<Assets<Image>>();
    auto img_opt       = images.get(ev->handle.id());
    ASSERT_TRUE(img_opt.has_value()) << "Image not found in Assets<Image>";

    const Image& img = img_opt->get();
    auto bytes       = img.raw_view();  // std::span<const std::byte>

    constexpr std::size_t pixel_count = TEX_W * TEX_H;
    ASSERT_EQ(bytes.size(), pixel_count * 4u) << "Unexpected image byte size: " << bytes.size();

    // Expected uint8 values after float [0,1] -> u8 rounding.
    const int exp_r = static_cast<int>(CLEAR_R * 255.0f + 0.5f);
    const int exp_g = static_cast<int>(CLEAR_G * 255.0f + 0.5f);
    const int exp_b = static_cast<int>(CLEAR_B * 255.0f + 0.5f);
    const int exp_a = static_cast<int>(CLEAR_A * 255.0f + 0.5f);

    constexpr int kTolerance = 2;  // allow ±2 for float->u8 rounding
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
