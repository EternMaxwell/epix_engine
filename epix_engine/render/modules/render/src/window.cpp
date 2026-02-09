module epix.render;

import :window;

using namespace render::window;
using namespace window;
using namespace core;

ExtractedWindow ExtractedWindow::from_window(const Entity& entity, const Window& window) {
    ExtractedWindow extracted;
    extracted.entity          = entity;
    extracted.physical_width  = window.size.first;
    extracted.physical_height = window.size.second;
    extracted.present_mode    = window.present_mode;
    extracted.alpha_mode      = window.composite_alpha_mode;
    return extracted;
}

void render::window::WindowSurfaces::remove(const Entity& entity) {
    surfaces.erase(entity);
    configured_windows.erase(entity);
}

void WindowRenderPlugin::build(App& app) {
    auto& render_app = app.sub_app_mut(render::Render);
    render_app.world_mut().insert_resource(ExtractedWindows{});
    render_app.world_mut().insert_resource(WindowSurfaces{});
    render_app.add_systems(render::Render, into(create_surfaces).set_name("create_surfaces").before(prepare_windows));
    render_app.add_systems(render::Render,
                           into(prepare_windows).set_name("prepare windows").in_set(render::RenderSet::ManageViews));
    if (handle_present) {
        render_app.add_systems(render::Render,
                               into(present_windows).set_name("present windows").after(render::RenderSet::Cleanup));
    }
}

void update_extracted(ResMut<ExtractedWindows> extracted_windows,
                      Extract<Query<Item<Entity, const Window&, Has<PrimaryWindow>>>> windows,
                      ResMut<WindowSurfaces> window_surfaces,
                      Extract<EventReader<WindowClosed>> closed) {
    // Update existing windows and add new ones
    for (auto&& [entity, window, primary] : windows.iter()) {
        auto it = extracted_windows->windows.find(entity);
        if (it != extracted_windows->windows.end()) {
            // Update existing window
            auto& extracted = it->second;
            if (extracted.physical_width != window.size.first || extracted.physical_height != window.size.second) {
                extracted.physical_width  = window.size.first;
                extracted.physical_height = window.size.second;
                extracted.size_changed    = true;
            }
            if (extracted.present_mode != window.present_mode) {
                extracted.present_mode         = window.present_mode;
                extracted.present_mode_changed = true;
            }
        }
    }

    // Remove closed windows
    for (auto&& event : closed.read()) {
        Entity closed_entity = event.window;
        extracted_windows->windows.erase(closed_entity);
        window_surfaces->remove(closed_entity);
        if (extracted_windows->primary.has_value() && extracted_windows->primary.value() == closed_entity) {
            extracted_windows->primary = std::nullopt;
        }
    }
}

void render::window::prepare_windows(ResMut<ExtractedWindows> windows,
                                     ResMut<WindowSurfaces> window_surfaces,
                                     Res<wgpu::Device> device,
                                     Res<wgpu::Instance> instance) {
    std::vector<std::pair<Entity, std::string>> errors;
    for (auto&& window : std::views::all(windows->windows) | std::views::values) {
        auto it = window_surfaces->surfaces.find(window.entity);
        if (it == window_surfaces->surfaces.end()) continue;
        auto& surface_data = it->second;

        auto& surface = surface_data.surface;
        if (!surface) continue;

        surface.getCurrentTexture(&window.swapchain_texture);
        switch (window.swapchain_texture.status) {
            case wgpu::SurfaceGetCurrentTextureStatus::eSuccessOptimal: {
                window.swapchain_texture_view =
                    window.swapchain_texture.texture.createView(wgpu::TextureViewDescriptor{});
                window.swapchain_texture_format = surface_data.config.format;
                break;
            }
            case wgpu::SurfaceGetCurrentTextureStatus::eSuccessSuboptimal:
            case wgpu::SurfaceGetCurrentTextureStatus::eTimeout:
            case wgpu::SurfaceGetCurrentTextureStatus::eOutdated: {
                surface.configure(surface_data.config);
                surface.getCurrentTexture(&window.swapchain_texture);
                switch (window.swapchain_texture.status) {
                    case wgpu::SurfaceGetCurrentTextureStatus::eSuccessSuboptimal:
                    case wgpu::SurfaceGetCurrentTextureStatus::eSuccessOptimal: {
                        window.swapchain_texture_view =
                            window.swapchain_texture.texture.createView(wgpu::TextureViewDescriptor{});
                        window.swapchain_texture_format = surface_data.config.format;
                        break;
                    }
                    default: {
                        errors.emplace_back(window.entity, "Failed to acquire swapchain image after reconfiguration");
                        break;
                    }
                }
                break;
            }
            default: {
                errors.emplace_back(window.entity, "Unknown error acquiring swapchain image");
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

void render::window::create_surfaces(Res<ExtractedWindows> windows,
                                     ResMut<WindowSurfaces> window_surfaces,
                                     Res<wgpu::Instance> instance,
                                     Res<wgpu::Adapter> adapter,
                                     Res<wgpu::Device> device) {
    for (auto&& window : std::views::all(windows->windows) | std::views::values) {
        if (!window_surfaces->surfaces.contains(window.entity)) {
            wgpu::Surface surface = window.create_surface(*instance);
            wgpu::SurfaceCapabilities capabilities;
            auto status = surface.getCapabilities(*adapter, &capabilities);
            if (status != wgpu::Status::eSuccess) {
                throw std::runtime_error("Failed to get surface capabilities");
            }
            wgpu::TextureFormat format = wgpu::TextureFormat::eUndefined;
            for (auto available : capabilities.formats) {
                if (available == wgpu::TextureFormat::eBGRA8UnormSrgb ||
                    available == wgpu::TextureFormat::eRGBA8UnormSrgb) {
                    format = available;
                    break;
                }
            }
            if (format == wgpu::TextureFormat::eUndefined) {
                throw std::runtime_error("No suitable surface format found");
            }
            auto config = wgpu::SurfaceConfiguration()
                              .setUsage(wgpu::TextureUsage::eRenderAttachment)
                              .setFormat(format)
                              .setWidth(window.physical_width)
                              .setHeight(window.physical_height)
                              .setPresentMode([&]() {
                                  switch (window.present_mode) {
                                      case PresentMode::AutoNoVsync: {
                                          for (auto available : capabilities.presentModes) {
                                              if (available == wgpu::PresentMode::eImmediate) {
                                                  return wgpu::PresentMode::eImmediate;
                                              }
                                              if (available == wgpu::PresentMode::eMailbox) {
                                                  return wgpu::PresentMode::eMailbox;
                                              }
                                          }
                                          return wgpu::PresentMode::eFifo;
                                      }
                                      case PresentMode::AutoVsync: {
                                          for (auto available : capabilities.presentModes) {
                                              if (available == wgpu::PresentMode::eFifoRelaxed) {
                                                  return wgpu::PresentMode::eFifoRelaxed;
                                              }
                                          }
                                          return wgpu::PresentMode::eFifo;
                                      }
                                      case PresentMode::Immediate:
                                          return wgpu::PresentMode::eImmediate;
                                      case PresentMode::Mailbox:
                                          return wgpu::PresentMode::eMailbox;
                                      case PresentMode::Fifo:
                                          return wgpu::PresentMode::eFifo;
                                      case PresentMode::FifoRelaxed:
                                          return wgpu::PresentMode::eFifoRelaxed;
                                      default:
                                          return wgpu::PresentMode::eFifo;
                                  }
                              }())
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
            auto& data = window_surfaces->surfaces.at(window.entity);
            data.config.setWidth(window.physical_width);
            data.config.setHeight(window.physical_height);
            data.config.setPresentMode([&]() {
                switch (window.present_mode) {
                    case PresentMode::AutoNoVsync: {
                        wgpu::SurfaceCapabilities capabilities;
                        data.surface.getCapabilities(*adapter, &capabilities);
                        for (auto available : capabilities.presentModes) {
                            if (available == wgpu::PresentMode::eImmediate) {
                                return wgpu::PresentMode::eImmediate;
                            }
                            if (available == wgpu::PresentMode::eMailbox) {
                                return wgpu::PresentMode::eMailbox;
                            }
                        }
                        return wgpu::PresentMode::eFifo;
                    }
                    case PresentMode::AutoVsync: {
                        wgpu::SurfaceCapabilities capabilities;
                        data.surface.getCapabilities(*adapter, &capabilities);
                        for (auto available : capabilities.presentModes) {
                            if (available == wgpu::PresentMode::eFifoRelaxed) {
                                return wgpu::PresentMode::eFifoRelaxed;
                            }
                        }
                        return wgpu::PresentMode::eFifo;
                    }
                    case PresentMode::Immediate:
                        return wgpu::PresentMode::eImmediate;
                    case PresentMode::Mailbox:
                        return wgpu::PresentMode::eMailbox;
                    case PresentMode::Fifo:
                        return wgpu::PresentMode::eFifo;
                    case PresentMode::FifoRelaxed:
                        return wgpu::PresentMode::eFifoRelaxed;
                    default:
                        return wgpu::PresentMode::eFifo;
                }
            }());
            data.surface.configure(data.config);
        }
        window_surfaces->configured_windows.insert(window.entity);
    }
}

void render::window::present_windows(ResMut<WindowSurfaces> window_surfaces, ResMut<ExtractedWindows> windows) {
    for (auto&& [entity, surface_data] : window_surfaces->surfaces) {
        auto& window                  = windows->windows.at(entity);
        window.swapchain_texture_view = nullptr;
        surface_data.surface.present();
    }
}
