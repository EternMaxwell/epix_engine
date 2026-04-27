module;
#ifndef EPIX_IMPORT_STD
#include <filesystem>
#include <optional>
#include <vector>
#endif

export module epix.render.screenshot;

import epix.core;
import epix.render;
import epix.image;
import epix.assets;
import epix.input;
import webgpu;
#ifdef EPIX_IMPORT_STD
import std;
#endif

namespace epix::render::screenshot {
using namespace epix::core;
using namespace epix::assets;
using namespace epix::image;
using namespace epix::render;
namespace assets = epix::assets;
namespace image  = epix::image;

/** @brief Event sent by the user to request a frame capture.
 *
 * Set @c target to control which render target is captured. Defaults to the
 * primary window swapchain (same as @c camera::RenderTarget::from_primary()). */
export struct ScreenCapture {
    camera::RenderTarget target = camera::RenderTarget::from_primary();
};

/** @brief Event fired with the captured frame's asset handle. */
export struct ScreenCaptureResult {
    assets::Handle<image::Image> handle;
};

/** @brief Resource for storing & configuring the hotkey that triggers a screenshot capture when pressed. */
struct ScreenshotHotkey {
    epix::input::KeyCode key;
};

/** @brief Plugin that adds screenshot capture support.
 *
 * Usage: send a @c ScreenCapture event via EventWriter<ScreenCapture> from any
 * main-world system. On the next frame the swapchain is copied to an
 * epix::image::Image and a ScreenCaptureResult event is fired with a strong
 * Handle<image::Image> for the captured frame.
 *
 * If @c save_path is set (as a directory), each captured image is written to
 * that directory as @c screenshot_<ISO-timestamp>.png.
 *
 * If @c capture_key is set (defaults to @c KeyCode::KeyF12), the @c ScreenshotHotkey
 * resource will be inserted and the plugin will fire a @c ScreenCapture event whenever
 * that key is pressed. also see @c ScreenshotHotkey.
 *
 * Requires @c RenderPlugin (and therefore @c ImagePlugin) to be registered. */
export struct ScreenshotPlugin {
    /** @brief Optional output directory for auto-saving captures to disk. */
    std::optional<std::filesystem::path> save_path = "screenshots";
    /** @brief Key that triggers an automatic capture. nullopt disables the hotkey. */
    std::optional<epix::input::KeyCode> capture_key = epix::input::KeyCode::KeyF12;

    void build(epix::core::App& app);
};

}  // namespace epix::render::screenshot
