#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/async_channel.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::render::screenshot {

/** @brief Component requesting a screenshot of a render target.
 *
 * Spawn this component on a request entity.  The plugin adds @c Capturing
 * while work is in flight, emits @c ScreenshotCaptured when the image is
 * available, adds @c Captured, then despawns the request entity on the next
 * frame.  This is the component-oriented counterpart to Bevy's Screenshot.
 *
 * Epix render targets use direct wgpu textures rather than asset handles, so
 * @c image accepts a texture directly. */
EPIX_EXPORT struct Screenshot {
    ::epix::camera::RenderTarget target = ::epix::camera::RenderTarget::from_primary();

    Screenshot() = default;
    explicit Screenshot(::epix::camera::RenderTarget render_target) : target(std::move(render_target)) {}

    static Screenshot window(ecs::Entity window_entity) noexcept {
        return Screenshot(::epix::camera::RenderTarget::from_window(window_entity));
    }
    static Screenshot primary_window() noexcept { return Screenshot(::epix::camera::RenderTarget::from_primary()); }
    static Screenshot image(wgpu::Texture texture, float scale_factor = 1.0f) {
        return Screenshot(::epix::camera::RenderTarget::from_texture(std::move(texture), scale_factor));
    }
    static Screenshot texture_view(::epix::camera::ManualTextureViewHandle texture_view) noexcept {
        return Screenshot(::epix::camera::RenderTarget::from_manual_texture_view(texture_view));
    }
};

/** @brief Marker component indicating a Screenshot request is in flight. */
EPIX_EXPORT struct Capturing {};

/** @brief Marker component indicating ScreenshotCaptured has been delivered. */
EPIX_EXPORT struct Captured {};

/** @brief Event emitted when a component-based screenshot request completes. */
EPIX_EXPORT struct ScreenshotCaptured {
    ecs::Entity entity;
    image::Image image;
};

/** @brief Main-world receiver for component-based screenshot completions.
 *
 * The render world sends completed
 * captures through this thread-safe channel,
 * matching Bevy's CapturedScreenshots receiver. The plugin drains it
 * into
 * Events<ScreenshotCaptured> during PreUpdate. */
EPIX_EXPORT class CapturedScreenshots {
   public:
    CapturedScreenshots() = default;
    explicit CapturedScreenshots(async_channel::Receiver<ScreenshotCaptured> receiver)
        : m_receiver(std::move(receiver)) {}

    bool empty() const noexcept { return m_receiver.is_empty(); }
    std::size_t size() const noexcept { return m_receiver.len(); }
    std::optional<ScreenshotCaptured> try_recv() const {
        auto capture = m_receiver.try_recv();
        return capture ? std::optional<ScreenshotCaptured>(std::move(*capture)) : std::nullopt;
    }

   private:
    async_channel::Receiver<ScreenshotCaptured> m_receiver;
};

/** @brief Returns a handler which writes a component-based screenshot to disk. */
EPIX_EXPORT std::function<void(const ScreenshotCaptured&)> save_to_disk(std::filesystem::path path);

/** @brief Plugin that adds Bevy-style component screenshot capture support.
 *
 * This is installed by WindowRenderPlugin. Spawn Screenshot on a request
 * entity and observe ScreenshotCaptured to receive its asynchronously read
 * back image. */
EPIX_EXPORT struct ScreenshotPlugin {
    void attach(app::App& app);
};

}  // namespace epix::render::screenshot
