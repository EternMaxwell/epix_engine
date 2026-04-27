module;
#ifndef EPIX_IMPORT_STD
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <variant>
#include <vector>
#endif
#include <spdlog/spdlog.h>

module epix.render.screenshot;

import epix.core;
import epix.render;
import epix.image;
import epix.assets;
import epix.tasks;
import epix.input;
import webgpu;
#ifdef EPIX_IMPORT_STD
import std;
#endif

using namespace epix::render::screenshot;
using namespace epix::core;
using namespace epix::assets;
using namespace epix::image;
using namespace epix::render;
using namespace epix::render::window;
namespace assets = epix::assets;
namespace image  = epix::image;

namespace epix::render::screenshot {

/** @brief Internal render-world state for the screenshot plugin. */
struct ScreenshotState {
    std::vector<camera::RenderTarget> pending;
    std::vector<image::Image> completed;
    std::optional<std::filesystem::path> save_path;
};

/** @brief ExtractSchedule system — delivers completed images to the main world and
 *  queues new capture requests from the main world's ScreenCapture events. */
static void extract_captures_and_deliver(ResMut<ScreenshotState> state,
                                         Extract<EventReader<ScreenCapture>> captures,
                                         Extract<ResMut<Events<ScreenCaptureResult>>> result_events,
                                         Extract<ResMut<assets::Assets<image::Image>>> images) {
    // Deliver results from previous frame into main world
    for (auto& img : state->completed) {
        // Auto-save to disk if an output directory was configured
        if (state->save_path.has_value()) {
            auto now  = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
            auto name = std::format("screenshot_{:%Y%m%d_%H%M%S}.png", now);
            auto abs  = std::filesystem::absolute(*state->save_path / name);

            std::error_code ec;
            std::filesystem::create_directories(abs.parent_path(), ec);
            if (ec) {
                spdlog::warn("[render.screenshot] Could not create directory '{}': {}", abs.parent_path().string(),
                             ec.message());
            }

            auto* io_pool = epix::tasks::IoTaskPool::try_get();
            if (io_pool) {
                io_pool->spawn([img_copy = img, abs]() mutable {
                    auto result = image::Image::save(abs, img_copy);
                    if (!result) {
                        spdlog::warn("[render.screenshot] Failed to save screenshot to '{}'", abs.string());
                    } else {
                        spdlog::info("[render.screenshot] Screenshot saved to '{}' through `ScreenshotPlugin`",
                                     abs.string());
                    }
                });
            } else {
                // IoTaskPool not initialised — fall back to synchronous save
                auto result = image::Image::save(abs, img);
                if (!result) {
                    spdlog::warn("[render.screenshot] Failed to save screenshot to '{}'", abs.string());
                } else {
                    spdlog::info("[render.screenshot] Screenshot saved to '{}' through `ScreenshotPlugin`",
                                 abs.string());
                }
            }
        }
        auto handle = images->add(std::move(img));
        result_events->push(ScreenCaptureResult{.handle = std::move(handle)});
    }
    state->completed.clear();

    // Collect new ScreenCapture requests from main world
    for (auto&& cap : captures.read()) {
        state->pending.push_back(cap.target);
        spdlog::info("[render.screenshot] Capture requested for target '{}' through ScreenCapture event",
                     std::visit(epix::utils::visitor{
                                    [](const camera::WindowRef& win_ref) {
                                        if (win_ref.primary) {
                                            return std::string("primary window");
                                        } else {
                                            return std::format("window entity {}", win_ref.window_entity.index);
                                        }
                                    },
                                    [](const wgpu::Texture& tex) {
                                        return std::format("wgpu texture {}", static_cast<void*>(tex.raw()));
                                    },
                                },
                                cap.target));
    }
}

/** @brief Cleanup-set system — copies the primary window's swapchain texture to a
 *  staging buffer, maps it synchronously, and stores the resulting Image for delivery
 *  to the main world on the next extract pass. */
static void capture_frame(ResMut<ScreenshotState> state,
                          Res<wgpu::Device> device,
                          Res<wgpu::Queue> queue,
                          Res<ExtractedWindows> windows) {
    if (state->pending.empty()) return;

    for (auto& req_target : state->pending) {
        // Resolve the render target to a concrete texture + dimensions
        wgpu::Texture texture;
        uint32_t width = 0, height = 0;
        wgpu::TextureFormat format = wgpu::TextureFormat::eUndefined;

        std::visit(epix::utils::visitor{
                       [&](const camera::WindowRef& win_ref) {
                           Entity win_entity;
                           if (win_ref.primary) {
                               if (!windows->primary.has_value()) return;
                               win_entity = windows->primary.value();
                           } else {
                               win_entity = win_ref.window_entity;
                           }
                           auto it = windows->windows.find(win_entity);
                           if (it == windows->windows.end()) return;
                           const auto& win = it->second;
                           if (!win.swapchain_texture.texture) return;
                           texture = win.swapchain_texture.texture;
                           width   = static_cast<uint32_t>(win.physical_width);
                           height  = static_cast<uint32_t>(win.physical_height);
                           format  = win.swapchain_texture_format;
                       },
                       [&](const wgpu::Texture& tex) {
                           if (!tex) return;
                           texture = tex;
                           width   = tex.getWidth();
                           height  = tex.getHeight();
                           format  = tex.getFormat();
                       },
                   },
                   req_target);

        if (!texture || width == 0 || height == 0) {
            spdlog::warn("[render.screenshot] Could not resolve render target for capture, skipping");
            continue;
        }

        // Map wgpu texture format → bytes per pixel, image format, BGRA swap needed
        struct CaptureFormatInfo {
            uint32_t bytes_per_pixel = 0;
            image::Format img_format = image::Format::Unknown;
            bool bgra_swap           = false;
            bool supported           = false;
        };
        auto get_fmt = [](wgpu::TextureFormat fmt) -> CaptureFormatInfo {
            switch (fmt) {
                case wgpu::TextureFormat::eRGBA8Unorm:
                case wgpu::TextureFormat::eRGBA8UnormSrgb:
                    return {4, image::Format::RGBA8, false, true};
                case wgpu::TextureFormat::eBGRA8Unorm:
                case wgpu::TextureFormat::eBGRA8UnormSrgb:
                    return {4, image::Format::RGBA8, true, true};
                case wgpu::TextureFormat::eR8Unorm:
                    return {1, image::Format::Grey8, false, true};
                case wgpu::TextureFormat::eRG8Unorm:
                    return {2, image::Format::GreyAlpha8, false, true};
                case wgpu::TextureFormat::eRGBA16Uint:
                case wgpu::TextureFormat::eRGBA16Sint:
                case wgpu::TextureFormat::eRGBA16Float:
                    return {8, image::Format::RGBA16, false, true};
                case wgpu::TextureFormat::eRGBA32Float:
                    return {16, image::Format::RGBA32F, false, true};
                default:
                    return {};
            }
        };
        auto fmt_info = get_fmt(format);
        if (!fmt_info.supported) {
            spdlog::warn("[render.screenshot] Unsupported texture format {} for capture, skipping",
                         static_cast<int>(format));
            continue;
        }

        // bytesPerRow must be a multiple of 256 (WGPU_COPY_BYTES_PER_ROW_ALIGNMENT)
        constexpr uint32_t ALIGNMENT   = 256;
        const uint32_t bytes_per_pixel = fmt_info.bytes_per_pixel;
        const uint32_t bytes_per_row   = ((width * bytes_per_pixel + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT;
        const uint64_t buffer_size     = static_cast<uint64_t>(bytes_per_row) * height;

        // Create staging buffer (CopyDst | MapRead)
        auto staging_buffer =
            device->createBuffer(wgpu::BufferDescriptor()
                                     .setLabel("epix.screenshot.staging")
                                     .setUsage(wgpu::BufferUsage::eCopyDst | wgpu::BufferUsage::eMapRead)
                                     .setSize(buffer_size));
        if (!staging_buffer) {
            spdlog::error("[render.screenshot] Failed to create staging buffer");
            continue;
        }

        // Record copy: swapchain texture → staging buffer
        auto encoder =
            device->createCommandEncoder(wgpu::CommandEncoderDescriptor().setLabel("epix.screenshot.encoder"));
        encoder.copyTextureToBuffer(
            wgpu::TexelCopyTextureInfo().setTexture(texture),
            wgpu::TexelCopyBufferInfo()
                .setBuffer(staging_buffer)
                .setLayout(
                    wgpu::TexelCopyBufferLayout().setOffset(0).setBytesPerRow(bytes_per_row).setRowsPerImage(height)),
            wgpu::Extent3D(width, height, 1));
        auto cmd_buf = encoder.finish();

        // Submit and wait for GPU copy
        queue->submit(cmd_buf);

        // Map the staging buffer synchronously
        bool mapped = false;
        staging_buffer.mapAsync(wgpu::MapMode::eRead, 0, buffer_size,
                                wgpu::BufferMapCallbackInfo()
                                    .setMode(wgpu::CallbackMode::eAllowProcessEvents)
                                    .setCallback([&mapped](wgpu::MapAsyncStatus status, wgpu::StringView msg) {
                                        if (status == wgpu::MapAsyncStatus::eSuccess) {
                                            mapped = true;
                                        } else {
                                            spdlog::warn("[render.screenshot] mapAsync failed: {}",
                                                         std::string_view(msg));
                                        }
                                    }));
        // poll(true) blocks until all submitted GPU work is done and fires pending callbacks
        device->poll(wgpu::Bool(true));

        if (!mapped) {
            spdlog::error("[render.screenshot] Buffer mapping did not complete");
            staging_buffer.destroy();
            continue;
        }

        // Copy pixel data into a packed (no-padding) buffer
        const void* raw_data = staging_buffer.getConstMappedRange(0, buffer_size);
        const auto* src      = reinterpret_cast<const std::byte*>(raw_data);

        std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * bytes_per_pixel);
        if (fmt_info.bgra_swap) {
            // BGRA→RGBA: swap B and R channels (always 4 bytes/pixel)
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    const std::byte* src_px = src + y * bytes_per_row + x * 4;
                    std::byte* dst_px       = pixels.data() + (y * width + x) * 4;
                    dst_px[0]               = src_px[2];  // R ← B
                    dst_px[1]               = src_px[1];  // G
                    dst_px[2]               = src_px[0];  // B ← R
                    dst_px[3]               = src_px[3];  // A
                }
            }
        } else {
            // No channel reorder: copy row by row, stripping alignment padding
            for (uint32_t y = 0; y < height; ++y) {
                std::memcpy(pixels.data() + y * width * bytes_per_pixel, src + y * bytes_per_row,
                            width * bytes_per_pixel);
            }
        }

        staging_buffer.unmap();
        staging_buffer.destroy();

        // Build Image with the format matching the source texture
        auto img_opt = image::Image::create2d(width, height, fmt_info.img_format, pixels);
        if (!img_opt.has_value()) {
            spdlog::error("[render.screenshot] create2d failed (data size mismatch)");
            continue;
        }
        img_opt->set_usage(image::ImageUsage::Main);

        state->completed.push_back(std::move(img_opt.value()));
        spdlog::debug("[render.screenshot] Frame captured ({}x{}, wgpu_fmt={}{})", width, height,
                      static_cast<int>(format), fmt_info.bgra_swap ? ", BGRA→RGBA" : "");
    }  // end for req_target
    state->pending.clear();
}

static void screenshot_capture_on_key(Res<ScreenshotHotkey> hotkey,
                                      Res<epix::input::ButtonInput<epix::input::KeyCode>> keys,
                                      ResMut<Events<ScreenCapture>> captures) {
    if (keys->just_pressed(hotkey->key)) {
        captures->push(ScreenCapture{});
        // spdlog::info("[render.screenshot] Capture requested through hotkey '{}'",
        // epix::input::key_name(hotkey->key));
    }
}

void ScreenshotPlugin::build(epix::core::App& app) {
    app.add_event<ScreenCapture>();
    app.add_event<ScreenCaptureResult>();

    auto& render_app = app.sub_app_mut(Render);
    render_app.world_mut().insert_resource(ScreenshotState{.save_path = save_path});

    render_app.add_systems(ExtractSchedule,
                           into(extract_captures_and_deliver).set_name("screenshot: extract & deliver"));

    render_app.add_systems(Render,
                           into(capture_frame).in_set(RenderSet::Cleanup).set_name("screenshot: capture frame"));

    if (capture_key.has_value()) {
        app.world_mut().insert_resource(ScreenshotHotkey{.key = *capture_key});
    }
    app.add_systems(PreUpdate,
                    into(screenshot_capture_on_key)
                        .set_name("screenshot: hotkey capture")
                        .run_if([](std::optional<Res<ScreenshotHotkey>> hotkey) { return hotkey.has_value(); }));
}
}  // namespace epix::render::screenshot