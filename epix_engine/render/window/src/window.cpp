#include "epix/render/window.h"

using namespace epix::render::window;

EPIX_API void ExtractedWindow::set_swapchain_texture(
    wgpu::SurfaceTexture texture
) {
    swapchain_texture = texture;
    swapchain_texture_view =
        wgpu::Texture(texture.texture)
            .createView(WGPUTextureViewDescriptor{
                .format          = swapchain_texture_format,
                .dimension       = wgpu::TextureViewDimension::_2D,
                .baseMipLevel    = 0,
                .mipLevelCount   = 1,
                .baseArrayLayer  = 0,
                .arrayLayerCount = 1,
                .aspect          = wgpu::TextureAspect::All,
                .usage           = wgpu::TextureUsage::RenderAttachment,
            });
}

EPIX_API void epix::render::window::WindowSurfaces::remove(const Entity& entity
) {
    auto it = surfaces.find(entity);
    if (it != surfaces.end()) {
        it->second.surface.release();
        surfaces.erase(it);
    }
}

EPIX_API void WindowRenderPlugin::build(epix::App& app) {
    auto& render_world = app.world(epix::app::RenderWorld);
    render_world.insert_resource(ExtractedWindows{});
    render_world.insert_resource(WindowSurfaces{});
    app.add_systems(
        epix::Extraction, epix::into(extract_windows, create_surfaces)
                              .set_names({"extract windows", "create_surfaces"})
                              .chain()
    );
    app.add_systems(
        epix::Prepare, epix::into(prepare_windows).set_name("prepare windows")
    );
    if (handle_present) {
        app.add_systems(
            epix::PostRender,
            epix::into(present_windows).set_name("present windows")
        );
    }
}

EPIX_API void epix::render::window::extract_windows(
    ResMut<ExtractedWindows> extracted_windows,
    Extract<EventReader<epix::window::WindowClosed>> closed,
    Extract<Query<
        Get<Entity, epix::window::Window, Has<epix::window::PrimaryWindow>>>>
        windows,
    Extract<Res<glfw::GLFWwindows>> glfw_windows,
    ResMut<WindowSurfaces> window_surfaces
) {
    for (auto&& close : closed.read()) {
        auto it = extracted_windows->windows.find(close.window);
        if (it != extracted_windows->windows.end()) {
            auto& window = it->second;
            extracted_windows->windows.erase(it);
        }
        window_surfaces->remove(close.window);
    }

    for (auto&& [entity, window, primary] : windows.iter()) {
        if (primary) {
            extracted_windows->primary = entity;
        }

        auto [new_width, new_height] = window.physical_size();
        new_width                    = std::max(new_width, 1);
        new_height                   = std::max(new_height, 1);

        if (!extracted_windows->windows.contains(entity) &&
            glfw_windows->windows.contains(entity)) {
            extracted_windows->windows.emplace(
                entity,
                ExtractedWindow{
                    .entity          = entity,
                    .handle          = glfw_windows->windows.at(entity),
                    .physical_width  = new_width,
                    .physical_height = new_height,
                    .present_mode    = window.present_mode,
                    .alpha_mode      = window.alpha_mode,
                }
            );
        }

        auto& extracted_window = extracted_windows->windows.at(entity);
        if (extracted_window.swapchain_texture_view) {
            extracted_window.swapchain_texture_view.release();
        }
        if (extracted_window.swapchain_texture.texture) {
            wgpu::Texture(extracted_window.swapchain_texture.texture).release();
        }

        extracted_window.size_changed =
            new_width != extracted_window.physical_width ||
            new_height != extracted_window.physical_height;
        extracted_window.present_mode_changed =
            window.present_mode != extracted_window.present_mode;

        if (extracted_window.size_changed) {
            extracted_window.physical_width  = new_width;
            extracted_window.physical_height = new_height;
        }
        if (extracted_window.present_mode_changed) {
            extracted_window.present_mode = window.present_mode;
        }
    }
}
EPIX_API void epix::render::window::create_surfaces(
    Res<ExtractedWindows> windows,
    ResMut<WindowSurfaces> window_surfaces,
    Res<wgpu::Instance> instance,
    Res<wgpu::Adapter> adapter,
    Res<wgpu::Device> device
) {
    for (auto&& window :
         std::views::all(windows->windows) | std::views::values) {
        auto match_present_mode = [](const wgpu::SurfaceCapabilities& caps,
                                     epix::window::PresentMode present_mode) {
            switch (present_mode) {
                case epix::window::PresentMode::AutoNoVsync: {
                    // immediate
                    auto res = std::find_if(
                        caps.presentModes,
                        caps.presentModes + caps.presentModeCount,
                        [](auto&& mode) {
                            return mode == wgpu::PresentMode::Immediate;
                        }
                    );
                    if (res != caps.presentModes + caps.presentModeCount) {
                        return wgpu::PresentMode::Immediate;
                    }
                    // mailbox
                    res = std::find_if(
                        caps.presentModes,
                        caps.presentModes + caps.presentModeCount,
                        [](auto&& mode) {
                            return mode == wgpu::PresentMode::Mailbox;
                        }
                    );
                    if (res != caps.presentModes + caps.presentModeCount) {
                        return wgpu::PresentMode::Mailbox;
                    }
                    // fifo
                    res = std::find_if(
                        caps.presentModes,
                        caps.presentModes + caps.presentModeCount,
                        [](auto&& mode) {
                            return mode == wgpu::PresentMode::Fifo;
                        }
                    );
                    if (res != caps.presentModes + caps.presentModeCount) {
                        return wgpu::PresentMode::Fifo;
                    }
                }
                case epix::window::PresentMode::AutoVsync: {
                    // fifo relaxed
                    auto res = std::find_if(
                        caps.presentModes,
                        caps.presentModes + caps.presentModeCount,
                        [](auto&& mode) {
                            return mode == wgpu::PresentMode::FifoRelaxed;
                        }
                    );
                    if (res != caps.presentModes + caps.presentModeCount) {
                        return wgpu::PresentMode::FifoRelaxed;
                    }
                    // fifo
                    res = std::find_if(
                        caps.presentModes,
                        caps.presentModes + caps.presentModeCount,
                        [](auto&& mode) {
                            return mode == wgpu::PresentMode::Fifo;
                        }
                    );
                    if (res != caps.presentModes + caps.presentModeCount) {
                        return wgpu::PresentMode::Fifo;
                    }
                }
                case epix::window::PresentMode::Immediate: {
                    return wgpu::PresentMode::Immediate;
                }
                case epix::window::PresentMode::Mailbox: {
                    return wgpu::PresentMode::Mailbox;
                }
                case epix::window::PresentMode::Fifo: {
                    return wgpu::PresentMode::Fifo;
                }
                case epix::window::PresentMode::FifoRelaxed: {
                    return wgpu::PresentMode::FifoRelaxed;
                }
                default: {
                    return wgpu::PresentMode::Fifo;
                }
            }
        };
        if (!window_surfaces->surfaces.contains(window.entity)) {
            wgpu::Surface surface =
                epix::webgpu::utils::create_surface(*instance, window.handle);
            wgpu::SurfaceCapabilities caps;
            surface.getCapabilities(*adapter, &caps);
            auto formats = caps.formats;
            auto format  = formats[0];
            for (auto&& available :
                 std::ranges::subrange(formats, formats + caps.formatCount)) {
                if (available == wgpu::TextureFormat::BGRA8UnormSrgb ||
                    available == wgpu::TextureFormat::RGBA8UnormSrgb) {
                    format = available;
                    break;
                }
            }
            auto presentMode = match_present_mode(caps, window.present_mode);

            auto config = WGPUSurfaceConfiguration{
                .device = *device,
                .format = format,
                .usage  = wgpu::TextureUsage::RenderAttachment,
                .width  = static_cast<uint32_t>(window.physical_width),
                .height = static_cast<uint32_t>(window.physical_height),
                .alphaMode =
                    [](epix::window::CompositeAlphaMode mode) {
                        switch (mode) {
                            case epix::window::CompositeAlphaMode::Opacity:
                                return wgpu::CompositeAlphaMode::Opaque;
                            case epix::window::CompositeAlphaMode::
                                PreMultiplied:
                                return wgpu::CompositeAlphaMode::Premultiplied;
                            case epix::window::CompositeAlphaMode::
                                PostMultiplied:
                                return wgpu::CompositeAlphaMode::
                                    Unpremultiplied;
                            case epix::window::CompositeAlphaMode::Inherit:
                                return wgpu::CompositeAlphaMode::Inherit;
                            default:
                                return wgpu::CompositeAlphaMode::Auto;
                        }
                    }(window.alpha_mode),
                .presentMode = presentMode,
            };

            surface.configure(config);
            window_surfaces->surfaces.emplace(
                window.entity,
                SurfaceData{
                    .surface = surface,
                    .config  = config,
                }
            );
        }

        if (window.size_changed || window.present_mode_changed) {
            auto& surface        = window_surfaces->surfaces.at(window.entity);
            surface.config.width = static_cast<uint32_t>(window.physical_width);
            surface.config.height =
                static_cast<uint32_t>(window.physical_height);
            wgpu::SurfaceCapabilities caps;
            surface.surface.getCapabilities(*adapter, &caps);
            auto presentMode = match_present_mode(caps, window.present_mode);
            surface.config.presentMode = presentMode;
            surface.surface.configure(surface.config);
        }
    }
}

EPIX_API void epix::render::window::prepare_windows(
    ResMut<ExtractedWindows> windows,
    ResMut<WindowSurfaces> window_surfaces,
    Res<wgpu::Device> device,
    Res<wgpu::Adapter> adapter
) {
    for (auto&& window :
         std::views::all(windows->windows) | std::views::values) {
        auto it = window_surfaces->surfaces.find(window.entity);
        if (it == window_surfaces->surfaces.end()) continue;
        auto& surface_data = it->second;

        auto not_configured =
            !window_surfaces->configured.contains(window.entity);
        if (not_configured) {
            window_surfaces->configured.insert(window.entity);
        }
        auto& surface = surface_data.surface;

        if (not_configured || window.size_changed ||
            window.present_mode_changed) {
            wgpu::SurfaceTexture texture;
            surface.getCurrentTexture(&texture);
            if (texture.status ==
                    wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal ||
                texture.status ==
                    wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
                window.set_swapchain_texture(texture);
            } else {
                throw std::runtime_error(
                    "Failed to get current texture for window"
                );
            }
        } else {
            wgpu::SurfaceTexture texture;
            surface.getCurrentTexture(&texture);
            if (texture.status ==
                wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal) {
                window.set_swapchain_texture(texture);
            } else if (texture.status ==
                           wgpu::SurfaceGetCurrentTextureStatus::Outdated ||
                       texture.status == wgpu::SurfaceGetCurrentTextureStatus::
                                             SuccessSuboptimal) {
                surface.configure(surface_data.config);
                surface.getCurrentTexture(&texture);
                if (texture.status ==
                        wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal ||
                    texture.status == wgpu::SurfaceGetCurrentTextureStatus::
                                          SuccessSuboptimal) {
                    window.set_swapchain_texture(texture);
                } else {
                    throw std::runtime_error(
                        "Failed to get current texture for window"
                    );
                }
            } else {
                throw std::runtime_error(
                    "Failed to get current texture for window"
                );
            }
        }

        window.swapchain_texture_format = surface_data.config.format;
    }
}

EPIX_API void epix::render::window::present_windows(
    ResMut<WindowSurfaces> window_surfaces
) {
    for (auto&& window :
         std::views::all(window_surfaces->surfaces) | std::views::values) {
        auto& surface = window.surface;
        surface.present();
    }
}