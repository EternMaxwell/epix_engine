#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/render/screenshot.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
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
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(FrameCountPlugin{});

    try {
        RenderPlugin{}.attach(app);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << e.what();
        return;
    }

    ScreenshotPlugin{.save_path = std::nullopt, .capture_key = std::nullopt}.attach(app);

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
        ScreenCapture{.target = ::epix::camera::RenderTarget::from_texture(texture)});
    const Entity first_component_request     = app.world_mut().spawn(Screenshot::image(texture)).id();
    const Entity duplicate_component_request = app.world_mut().spawn(Screenshot::image(texture)).id();
    app.run_schedule(PreUpdate);
    const bool first_is_capturing = app.world()
                                        .get_entity(first_component_request)
                                        .transform([](const EntityRef& entity) { return entity.contains<Capturing>(); })
                                        .value_or(false);
    const bool duplicate_is_capturing =
        app.world()
            .get_entity(duplicate_component_request)
            .transform([](const EntityRef& entity) { return entity.contains<Capturing>(); })
            .value_or(false);
    ASSERT_NE(first_is_capturing, duplicate_is_capturing) << "Duplicate screenshot targets must be coalesced";
    const Entity component_request = first_is_capturing ? first_component_request : duplicate_component_request;
    const Entity duplicate_request = first_is_capturing ? duplicate_component_request : first_component_request;
    EXPECT_FALSE(app.world().get_entity(duplicate_request).has_value());

    auto render_sub = app.take_sub_app(epix::render::Render);
    ASSERT_TRUE(render_sub) << "Render sub-app not found";

    // Readback mapping is asynchronous. Drive several render/extract frames
    // instead of relying on the retired synchronous screenshot implementation.
    for (int frame = 0; frame != 16; ++frame) {
        render_sub->extract(app);
        render_sub->update();
        device.poll(wgpu::Bool(true));
        render_sub->extract(app);
        if (!app.resource<Events<ScreenCaptureResult>>().empty() && !app.resource<Events<ScreenshotCaptured>>().empty())
            break;
    }
    app.insert_sub_app(epix::render::Render, std::move(render_sub));
    app.run_schedule(PreUpdate);

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

    const auto& captured_events = app.resource<Events<ScreenshotCaptured>>();
    ASSERT_FALSE(captured_events.empty()) << "No component ScreenshotCaptured event was delivered";
    const ScreenshotCaptured* captured = captured_events.get(captured_events.head());
    ASSERT_TRUE(captured != nullptr);
    EXPECT_EQ(captured->entity, component_request);
    ASSERT_EQ(captured->image.raw_view().size(), pixel_count * 4u);
    // Component screenshots intentionally capture the graph output through a
    // temporary attachment (Bevy's lifecycle); this test has no camera graph.
    // The real sprite example covers component pixel output visually.
    ASSERT_TRUE(app.world().entity(component_request).contains<Capturing>());
    ASSERT_TRUE(app.world().entity(component_request).contains<Captured>());

    app.run_schedule(Last);
    EXPECT_TRUE(app.world().get_entity(component_request).has_value())
        << "Captured request must survive the completion frame";
    app.world_mut().clear_trackers();
    app.run_schedule(Last);
    EXPECT_FALSE(app.world().get_entity(component_request).has_value());
}
