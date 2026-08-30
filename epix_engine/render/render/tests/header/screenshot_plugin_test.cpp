#include <gtest/gtest.h>

#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/render/screenshot.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <webgpu/webgpu.hpp>

using namespace epix::app;
using namespace epix::ecs;
using namespace epix::render;
using namespace epix::render::screenshot;

namespace {
struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(
            epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4).build()});
    }
} g_io_task_pool_init;
}  // namespace

TEST(ScreenshotPlugin, WindowRenderPluginInstallsAndCapturesComponentRequest) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(FrameCountPlugin{});

    try {
        RenderPlugin{}.attach(app);
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available: " << error.what();
    }

    // ScreenshotPlugin is installed by WindowRenderPlugin, not manually here.
    ASSERT_TRUE(app.world().get_resource<CapturedScreenshots>().has_value());
    ASSERT_TRUE(app.world().get_resource<Events<ScreenshotCaptured>>().has_value());
    app.run_schedule(Startup);

    auto& device = app.resource_mut<wgpu::Device>();
    auto texture = device.createTexture(wgpu::TextureDescriptor()
                                            .setLabel("screenshot_test_texture")
                                            .setSize(wgpu::Extent3D(4, 4, 1))
                                            .setFormat(wgpu::TextureFormat::eRGBA8Unorm)
                                            .setMipLevelCount(1)
                                            .setSampleCount(1)
                                            .setUsage(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eCopySrc));
    ASSERT_TRUE(texture);

    const Entity first = app.world_mut().spawn(Screenshot::image(texture)).id();
    const Entity duplicate = app.world_mut().spawn(Screenshot::image(texture)).id();
    app.run_schedule(PreUpdate);

    const bool first_capturing = app.world().get_entity(first).transform(
        [](const EntityRef& entity) { return entity.contains<Capturing>(); }).value_or(false);
    const Entity request = first_capturing ? first : duplicate;
    const Entity duplicate_request = first_capturing ? duplicate : first;
    ASSERT_FALSE(app.world().get_entity(duplicate_request).has_value());

    auto render_sub = app.take_sub_app(Render);
    ASSERT_TRUE(render_sub);
    for (int frame = 0; frame != 16; ++frame) {
        render_sub->extract(app);
        render_sub->update();
        device.poll(wgpu::Bool(true));
        render_sub->extract(app);
        if (!app.resource<Events<ScreenshotCaptured>>().empty()) break;
    }
    app.insert_sub_app(Render, std::move(render_sub));
    app.run_schedule(PreUpdate);

    const auto& captures = app.resource<Events<ScreenshotCaptured>>();
    ASSERT_FALSE(captures.empty());
    const auto* captured = captures.get(captures.head());
    ASSERT_NE(captured, nullptr);
    EXPECT_EQ(captured->entity, request);
    EXPECT_EQ(captured->image.raw_view().size(), 4u * 4u * 4u);
    EXPECT_TRUE(app.world().entity(request).contains<Captured>());
}
