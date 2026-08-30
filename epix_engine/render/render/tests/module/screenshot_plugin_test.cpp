#include <gtest/gtest.h>

import epix.assets;
import epix.camera;
import epix.ecs;
import epix.image;
import epix.render;
import epix.render.screenshot;
import epix.time;
import webgpu;

using namespace epix::app;
using namespace epix::ecs;
using namespace epix::render;
using namespace epix::render::screenshot;

TEST(ScreenshotPlugin, WindowRenderPluginInstallsComponentApi) {
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

    EXPECT_TRUE(app.world().get_resource<CapturedScreenshots>().has_value());
    EXPECT_TRUE(app.world().get_resource<Events<ScreenshotCaptured>>().has_value());
}
