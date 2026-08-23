
#include <spdlog/spdlog.h>

#include <epix/render.hpp>
#include <epix/render/window.hpp>

using namespace epix::render::window;
using namespace epix::window;
using namespace epix::ecs;
using namespace epix::app;

namespace {
/** @brief sRGB-suffixed variant of a surface format, or nullopt when the
 * format is already sRGB (Bevy TextureFormat::add_srgb_suffix). Defined below
 * in this namespace alongside resolve_present_mode. */
std::optional<wgpu::TextureFormat> srgb_suffix(wgpu::TextureFormat format);
}  // namespace

void epix::render::window::WindowSurfaces::remove(const Entity& entity) {
    surfaces.erase(entity);
    configured_windows.erase(entity);
}

void WindowRenderPlugin::attach(App& app) {
    auto& render_app = app.sub_app_mut(epix::render::Render);
    render_app.world_mut().insert_resource(ExtractedWindows{});
    render_app.world_mut().insert_resource(WindowSurfaces{});
    render_app.add_systems(ExtractSchedule, into(extract_windows).set_name("extract windows"));
    render_app.add_systems(epix::render::Render,
                           into(create_surfaces).set_name("create_surfaces").before(prepare_windows));
    render_app.add_systems(
        epix::render::Render,
        into(prepare_windows).set_name("prepare windows").in_set(epix::render::RenderSystems::ManageViews));
    if (handle_present) {
        render_app.add_systems(
            epix::render::Render,
            into(present_windows).set_name("present windows").after(epix::render::RenderSystems::Cleanup));
    }
}

void epix::render::window::extract_windows(
    ResMut<ExtractedWindows> extracted_windows,
    Extract<Query<Item<Entity, const Window&, const SurfaceCreation&, Has<PrimaryWindow>>>> windows,
    ResMut<WindowSurfaces> window_surfaces,
    Extract<EventReader<WindowClosed>> closed) {
    // Update existing windows and add new ones
    for (auto&& [entity, window, surface_fn, primary] : windows.iter()) {
        auto it = extracted_windows->windows.find(entity);
        if (it != extracted_windows->windows.end()) {
            // Update existing window
            auto& extracted = it->second;
            // Bevy clamps the physical size to at least 1 pixel (a 0-size
            // minimized window would fail surface configuration).
            const std::uint32_t physical_width  = std::max<std::uint32_t>(1, window.size.first);
            const std::uint32_t physical_height = std::max<std::uint32_t>(1, window.size.second);
            if (extracted.physical_width != physical_width || extracted.physical_height != physical_height) {
                extracted.physical_width  = physical_width;
                extracted.physical_height = physical_height;
                extracted.size_changed    = true;
            }
            if (extracted.present_mode != window.present_mode) {
                extracted.present_mode         = window.present_mode;
                extracted.present_mode_changed = true;
            }
            // Bevy window/mod.rs:159-165: if we called present on the previous
            // swap-chain texture last update (so swapchain_texture was taken),
            // drop the swap chain frame here; otherwise we can keep it for the
            // next update as an optimization (prepare_windows reuses it).
            if (!extracted.has_swapchain_texture()) {
                extracted.swapchain_texture_view = nullptr;
            }
        } else {
            // Not extracted, extract.
            extracted_windows->windows.emplace(
                entity, ExtractedWindow{
                            .entity          = entity,
                            .create_surface  = surface_fn,
                            .physical_width  = static_cast<int>(std::max<std::uint32_t>(1, window.size.first)),
                            .physical_height = static_cast<int>(std::max<std::uint32_t>(1, window.size.second)),
                            .present_mode    = window.present_mode,
                            .alpha_mode      = window.composite_alpha_mode,
                        });
        }
        if (primary) {
            extracted_windows->primary = entity;
        }
    }

    // Remove closed windows
    for (auto&& event : closed.read()) {
        Entity closed_entity = event.window;
        spdlog::debug("[render.window] Window entity {} closed, removing.", closed_entity.index);
        extracted_windows->windows.erase(closed_entity);
        window_surfaces->remove(closed_entity);
        if (extracted_windows->primary.has_value() && extracted_windows->primary.value() == closed_entity) {
            extracted_windows->primary = std::nullopt;
        }
    }
}

void epix::render::window::prepare_windows(ResMut<ExtractedWindows> windows,
                                           ResMut<WindowSurfaces> window_surfaces,
                                           Res<wgpu::Device> device,
                                           Res<wgpu::Instance> instance) {
    std::vector<std::pair<Entity, std::string>> errors;
    for (auto&& window : std::views::values(std::views::all(windows->windows))) {
        auto it = window_surfaces->surfaces.find(window.entity);
        if (it == window_surfaces->surfaces.end()) continue;

        // Bevy window/mod.rs:272-275: if the previous frame was not presented
        // (swapchain texture still held), keep using it instead of acquiring a
        // new one. Re-acquiring an unpresented texture would exhaust the
        // swapchain and surface as a ~1s Timeout/"Unknown" error.
        if (window.has_swapchain_texture() && !window.size_changed && !window.present_mode_changed) {
            continue;
        }

        window.size_changed         = false;
        window.present_mode_changed = false;

        auto& surface_data = it->second;

        auto& surface = surface_data.surface;
        if (!surface) continue;

        surface.getCurrentTexture(&window.swapchain_texture);
        switch (window.swapchain_texture.status) {
            case wgpu::SurfaceGetCurrentTextureStatus::eSuccessSuboptimal:
            case wgpu::SurfaceGetCurrentTextureStatus::eSuccessOptimal: {
                // Bevy set_swapchain_texture (window/mod.rs:77-88): create the
                // view with the sRGB-suffixed format (registered in the surface
                // view_formats) so the output attachment writes through
                // hardware sRGB encoding; shaders stay in linear space.
                window.swapchain_texture_view_format = surface_data.config.format;
                if (auto srgb_format = srgb_suffix(surface_data.config.format)) {
                    window.swapchain_texture_view_format = *srgb_format;
                }
                wgpu::TextureViewDescriptor view_desc;
                view_desc.setFormat(window.swapchain_texture_view_format)
                    .setDimension(wgpu::TextureViewDimension::e2D)
                    .setBaseMipLevel(0)
                    .setMipLevelCount(1)
                    .setBaseArrayLayer(0)
                    .setArrayLayerCount(1)
                    .setAspect(wgpu::TextureAspect::eAll);
                window.swapchain_texture_view   = window.swapchain_texture.texture.createView(view_desc);
                window.swapchain_texture_format = surface_data.config.format;
                break;
            }
            case wgpu::SurfaceGetCurrentTextureStatus::eOutdated: {
                surface.configure(surface_data.config);
                surface.getCurrentTexture(&window.swapchain_texture);
                switch (window.swapchain_texture.status) {
                    case wgpu::SurfaceGetCurrentTextureStatus::eSuccessSuboptimal:
                    case wgpu::SurfaceGetCurrentTextureStatus::eSuccessOptimal: {
                        // Bevy set_swapchain_texture (window/mod.rs:77-88), sRGB-suffixed view.
                        window.swapchain_texture_view_format = surface_data.config.format;
                        if (auto srgb_format = srgb_suffix(surface_data.config.format)) {
                            window.swapchain_texture_view_format = *srgb_format;
                        }
                        wgpu::TextureViewDescriptor view_desc;
                        view_desc.setFormat(window.swapchain_texture_view_format)
                            .setDimension(wgpu::TextureViewDimension::e2D)
                            .setBaseMipLevel(0)
                            .setMipLevelCount(1)
                            .setBaseArrayLayer(0)
                            .setArrayLayerCount(1)
                            .setAspect(wgpu::TextureAspect::eAll);
                        window.swapchain_texture_view   = window.swapchain_texture.texture.createView(view_desc);
                        window.swapchain_texture_format = surface_data.config.format;
                        break;
                    }
                    default: {
                        window.swapchain_texture_view = nullptr;
                        window.swapchain_texture      = wgpu::SurfaceTexture{};
                        errors.emplace_back(window.entity,
                                            "Failed to acquire swapchain image after reconfiguration: " +
                                                std::string(wgpu::to_string(window.swapchain_texture.status)));
                        break;
                    }
                }
                break;
            }
            // case wgpu::SurfaceGetCurrentTextureStatus::eTimeout:
            default: {
                window.swapchain_texture_view = nullptr;
                window.swapchain_texture      = wgpu::SurfaceTexture{};
                errors.emplace_back(window.entity, "Error acquiring swapchain image: " +
                                                       std::string(wgpu::to_string(window.swapchain_texture.status)));
                break;
            }
        }
    }
    if (!errors.empty()) {
        std::string error_msg = "Failed to acquire swapchain images for windows: ";
        for (auto&& [entity, error] : errors) {
            error_msg += "\n  Entity: " + std::to_string(entity.index) + ", Error: " + error;
        }
        throw std::runtime_error(error_msg);
    }
}

namespace {
/** @brief Resolve the requested present mode against the surface
 * capabilities using Bevy's fallback chains (window/mod.rs
 * `present_mode`); always ends in Fifo so a supported mode is guaranteed. */
wgpu::PresentMode resolve_present_mode(::epix::window::PresentMode requested,
                                       const wgpu::SurfaceCapabilities& capabilities) {
    // wgpu-native has no AutoVsync/AutoNoVsync values; map the requested mode
    // directly to Bevy's fallback chains (window/mod.rs present_mode).
    std::vector<wgpu::PresentMode> fallbacks;
    switch (requested) {
        case ::epix::window::PresentMode::AutoNoVsync:
            fallbacks = {wgpu::PresentMode::eImmediate, wgpu::PresentMode::eMailbox, wgpu::PresentMode::eFifo};
            break;
        case ::epix::window::PresentMode::AutoVsync:
            fallbacks = {wgpu::PresentMode::eFifoRelaxed, wgpu::PresentMode::eFifo};
            break;
        case ::epix::window::PresentMode::Mailbox:
            fallbacks = {wgpu::PresentMode::eMailbox, wgpu::PresentMode::eImmediate, wgpu::PresentMode::eFifo};
            break;
        case ::epix::window::PresentMode::Immediate:
            fallbacks = {wgpu::PresentMode::eImmediate, wgpu::PresentMode::eFifo};
            break;
        case ::epix::window::PresentMode::FifoRelaxed:
            fallbacks = {wgpu::PresentMode::eFifoRelaxed, wgpu::PresentMode::eFifo};
            break;
        case ::epix::window::PresentMode::Fifo:
        default:
            fallbacks = {wgpu::PresentMode::eFifo};
            break;
    }
    for (auto mode : fallbacks) {
        for (auto available : capabilities.presentModes) {
            if (available == mode) {
                return mode;
            }
        }
    }
    return wgpu::PresentMode::eFifo;
}

/** @brief sRGB-suffixed variant of a surface format, or nullopt when the
 * format is already sRGB (Bevy TextureFormat::add_srgb_suffix). */
std::optional<wgpu::TextureFormat> srgb_suffix(wgpu::TextureFormat format) {
    switch (format) {
        case wgpu::TextureFormat::eBGRA8Unorm:
            return wgpu::TextureFormat::eBGRA8UnormSrgb;
        case wgpu::TextureFormat::eRGBA8Unorm:
            return wgpu::TextureFormat::eRGBA8UnormSrgb;
        default:
            return std::nullopt;
    }
}
}  // namespace

void epix::render::window::create_surfaces(ResMut<ExtractedWindows> windows,
                                           ResMut<WindowSurfaces> window_surfaces,
                                           Res<wgpu::Instance> instance,
                                           Res<wgpu::Adapter> adapter,
                                           Res<wgpu::Device> device) {
    for (auto&& window : std::views::values(std::views::all(windows->windows))) {
        if (!window_surfaces->surfaces.contains(window.entity)) {
            spdlog::debug("[render.window] Creating surface for window entity {}.", window.entity.index);
            wgpu::Surface surface = window.create_surface(*instance);
            wgpu::SurfaceCapabilities capabilities;
            auto status = surface.getCapabilities(*adapter, &capabilities);
            if (status != wgpu::Status::eSuccess) {
                throw std::runtime_error("Failed to get surface capabilities");
            }
            // Bevy: start with the first available format, prefer sRGB
            // (window/mod.rs:374-383).
            if (capabilities.formats.empty()) {
                throw std::runtime_error("No supported formats for surface");
            }
            wgpu::TextureFormat format = capabilities.formats[0];
            for (auto available : capabilities.formats) {
                if (available == wgpu::TextureFormat::eBGRA8UnormSrgb ||
                    available == wgpu::TextureFormat::eRGBA8UnormSrgb) {
                    format = available;
                    break;
                }
            }
            // View formats: the sRGB-suffixed twin when the main format is
            // non-sRGB (Bevy create_surfaces:396-406).
            std::array<wgpu::TextureFormat, 1> view_formats{format};
            const std::size_t view_format_count = srgb_suffix(format).has_value() ? 1 : 0;
            if (view_format_count) {
                view_formats[0] = *srgb_suffix(format);
            }
            auto config =
                wgpu::SurfaceConfiguration()
                    .setDevice(*device)
                    .setUsage(wgpu::TextureUsage::eRenderAttachment | wgpu::TextureUsage::eCopySrc |
                              wgpu::TextureUsage::eCopyDst)
                    .setFormat(format)
                    .setWidth(window.physical_width)
                    .setHeight(window.physical_height)
                    // Bevy validates the requested present mode against
                    // capabilities with fallbacks (window/mod.rs:439-483).
                    .setPresentMode(resolve_present_mode(window.present_mode, capabilities))
                    .setViewFormats(std::span<const wgpu::TextureFormat>(view_formats.data(), view_format_count))
                    // Bevy default desired maximum frame latency = 2.
                    .setNextInChain(wgpu::SurfaceConfigurationExtras().setDesiredMaximumFrameLatency(2))
                    .setAlphaMode([&]() {
                        switch (window.alpha_mode) {
                            case CompositeAlphaMode::Auto:
                                return wgpu::CompositeAlphaMode::eAuto;
                            case CompositeAlphaMode::Opacity:
                                return wgpu::CompositeAlphaMode::eOpaque;
                            case CompositeAlphaMode::PreMultiplied:
                                return wgpu::CompositeAlphaMode::ePremultiplied;
                            case CompositeAlphaMode::PostMultiplied:
                                return wgpu::CompositeAlphaMode::eUnpremultiplied;
                            case CompositeAlphaMode::Inherit:
                                return wgpu::CompositeAlphaMode::eInherit;
                            default:
                                return wgpu::CompositeAlphaMode::eAuto;
                        }
                    }());
            surface.configure(config);
            window_surfaces->surfaces.emplace(window.entity, SurfaceData(std::move(surface), config));
        }

        if (window.size_changed || window.present_mode_changed) {
            spdlog::debug(
                "[render.window] Reconfiguring surface for window entity {} (size_changed={}, "
                "present_mode_changed={}).",
                window.entity.index, window.size_changed, window.present_mode_changed);
            auto& data = window_surfaces->surfaces.at(window.entity);
            data.config.setWidth(window.physical_width);
            data.config.setHeight(window.physical_height);
            // Re-validate the present mode against current capabilities
            // (Bevy reconfiguration path does the same, window/mod.rs:410-437).
            // Bevy window/mod.rs:447-455: the swapchain texture is normally
            // released on present, but double-check here — reconfiguring a
            // surface while a frame is still acquired triggers wgpu validation
            // errors (e.g. when the previous frame never presented).
            window.swapchain_texture      = wgpu::SurfaceTexture{};
            window.swapchain_texture_view = nullptr;
            wgpu::SurfaceCapabilities reconfig_capabilities;
            data.surface.getCapabilities(*adapter, &reconfig_capabilities);
            data.config.setPresentMode(resolve_present_mode(window.present_mode, reconfig_capabilities));
            data.surface.configure(data.config);
        }
        window_surfaces->configured_windows.insert(window.entity);
    }
}

void epix::render::window::present_windows(
    ResMut<WindowSurfaces> window_surfaces,
    ResMut<ExtractedWindows> windows,
    Query<Item<Entity, const camera::ExtractedCamera&, const view::ViewTarget&>> views) {
    for (auto&& [entity, surface_data] : window_surfaces->surfaces) {
        auto& window = windows->windows.at(entity);
        // Gate on whether a texture is actually held: after a failed acquire
        // swapchain_texture is reset to a default SurfaceTexture whose status
        // is eSuccessOptimal (enum 0) but whose texture is null — presenting
        // that would hit wgpu with no acquired frame (Bevy ExtractedWindow::present
        // takes the Option and no-ops when None).
        if (window.has_swapchain_texture()) {
            // Bevy render_system present gate: present when a camera targeting
            // this window wrote to its output, or once for the initial frame.
            bool view_needs_present = false;
            for (auto&& [cam_entity, camera, view_target] : views.iter()) {
                (void)cam_entity;
                if (!view_target.needs_present()) continue;
                if (auto* win_ref = std::get_if<camera::WindowRef>(&camera.render_target);
                    win_ref && win_ref->window_entity == entity) {
                    view_needs_present = true;
                    break;
                }
            }
            if (view_needs_present || window.needs_initial_present) {
                // Bevy ExtractedWindow::present: present + take the texture;
                // when not presenting, keep the texture so prepare_windows
                // skips re-acquiring next frame.
                surface_data.surface.present();
                window.release_swapchain_texture();
                window.needs_initial_present = false;
            }
        }
    }
}
