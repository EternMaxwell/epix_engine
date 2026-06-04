#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <epix/core.hpp>
#include <epix/render.hpp>
#include <epix/image.hpp>
#include <epix/assets.hpp>
#include <epix/input.hpp>
#include <webgpu/webgpu.hpp>
namespace epix::render::screenshot {

/** @brief Event sent by the user to request a frame capture.
 *
 * Set @c target to control which render target is captured. Defaults to the
 * primary window swapchain (same as @c epix::render::camera::RenderTarget::from_primary()). */
struct ScreenCapture {
    epix::render::camera::RenderTarget target = epix::render::camera::RenderTarget::from_primary();
};

/** @brief Event fired with the captured frame's asset handle. */
struct ScreenCaptureResult {
    epix::assets::Handle<epix::image::Image> handle;
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
struct ScreenshotPlugin {
    /** @brief Optional output directory for auto-saving captures to disk. */
    std::optional<std::filesystem::path> save_path = "screenshots";
    /** @brief Key that triggers an automatic capture. nullopt disables the hotkey. */
    std::optional<epix::input::KeyCode> capture_key = epix::input::KeyCode::KeyF12;

    void attach(epix::core::App& app);
};

}  // namespace epix::render::screenshot
