#include "epix/render/window.h"

using namespace epix::render::window;

EPIX_API void ExtractedWindow::set_swapchain_texture(
    vk::Device device, vk::Image image, vk::Format format
) {
    swapchain_texture        = image;
    swapchain_texture_format = format;
    swapchain_texture_view   = device.createImageView(
        vk::ImageViewCreateInfo()
            .setImage(image)
            .setFormat(format)
            .setViewType(vk::ImageViewType::e2D)
            .setSubresourceRange(
                vk::ImageSubresourceRange()
                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                    .setBaseMipLevel(0)
                    .setLevelCount(1)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1)
            )
    );
}

EPIX_API void epix::render::window::WindowSurfaces::remove(const Entity& entity
) {
    auto it = surfaces.find(entity);
    if (it != surfaces.end()) {
        auto& data = it->second;
        if (data.swapchain) {
            data.device.destroySwapchainKHR(data.swapchain);
        }
        if (data.surface) {
            data.instance.destroySurfaceKHR(data.surface);
        }
        for (auto& fence : data.swapchain_image_fences) {
            data.device.destroyFence(fence);
        }
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
    Res<vk::Device> device,
    Extract<EventReader<epix::window::WindowClosed>> closed,
    Extract<Query<
        Get<Entity, epix::window::Window, Has<epix::window::PrimaryWindow>>>>
        windows,
    Extract<Res<glfw::GLFWwindows>> glfw_windows,
    ResMut<WindowSurfaces> window_surfaces
) {
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
                    .device          = *device,
                }
            );
        }

        auto& extracted_window = extracted_windows->windows.at(entity);
        if (extracted_window.swapchain_texture_view) {
            device->destroyImageView(extracted_window.swapchain_texture_view);
            extracted_window.swapchain_texture_view = nullptr;
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

    for (auto&& close : closed.read()) {
        auto it = extracted_windows->windows.find(close.window);
        if (it != extracted_windows->windows.end()) {
            auto& window = it->second;
            extracted_windows->windows.erase(it);
        }
        window_surfaces->remove(close.window);
    }
}
EPIX_API void epix::render::window::create_surfaces(
    Res<ExtractedWindows> windows,
    ResMut<WindowSurfaces> window_surfaces,
    Res<vk::Instance> instance,
    Res<vk::PhysicalDevice> physical_device,
    Res<vk::Device> device
) {
    for (auto&& window :
         std::views::all(windows->windows) | std::views::values) {
        auto match_present_mode = [&](vk::SurfaceKHR surface,
                                      epix::window::PresentMode present_mode) {
            auto presentModes =
                physical_device->getSurfacePresentModesKHR(surface);
            switch (present_mode) {
                case epix::window::PresentMode::AutoNoVsync: {
                    // immediate
                    auto res = std::find_if(
                        presentModes.begin(), presentModes.end(),
                        [](auto&& mode) {
                            return mode == vk::PresentModeKHR::eImmediate;
                        }
                    );
                    if (res != presentModes.end()) {
                        return vk::PresentModeKHR::eImmediate;
                    }
                    // mailbox
                    res = std::find_if(
                        presentModes.begin(), presentModes.end(),
                        [](auto&& mode) {
                            return mode == vk::PresentModeKHR::eMailbox;
                        }
                    );
                    if (res != presentModes.end()) {
                        return vk::PresentModeKHR::eMailbox;
                    }
                    // fifo
                    res = std::find_if(
                        presentModes.begin(), presentModes.end(),
                        [](auto&& mode) {
                            return mode == vk::PresentModeKHR::eFifo;
                        }
                    );
                    if (res != presentModes.end()) {
                        return vk::PresentModeKHR::eFifo;
                    }
                }
                case epix::window::PresentMode::AutoVsync: {
                    // fifo relaxed
                    auto res = std::find_if(
                        presentModes.begin(), presentModes.end(),
                        [](auto&& mode) {
                            return mode == vk::PresentModeKHR::eFifoRelaxed;
                        }
                    );
                    if (res != presentModes.end()) {
                        return vk::PresentModeKHR::eFifoRelaxed;
                    }
                    // fifo
                    res = std::find_if(
                        presentModes.begin(), presentModes.end(),
                        [](auto&& mode) {
                            return mode == vk::PresentModeKHR::eFifo;
                        }
                    );
                    if (res != presentModes.end()) {
                        return vk::PresentModeKHR::eFifo;
                    }
                }
                case epix::window::PresentMode::Immediate: {
                    return vk::PresentModeKHR::eImmediate;
                }
                case epix::window::PresentMode::Mailbox: {
                    return vk::PresentModeKHR::eMailbox;
                }
                case epix::window::PresentMode::Fifo: {
                    return vk::PresentModeKHR::eFifo;
                }
                case epix::window::PresentMode::FifoRelaxed: {
                    return vk::PresentModeKHR::eFifoRelaxed;
                }
                default: {
                    return vk::PresentModeKHR::eFifo;  // default to FIFO
                }
            }
        };
        if (!window_surfaces->surfaces.contains(window.entity)) {
            vk::SurfaceKHR surface;
            {
                VkSurfaceKHR s;
                if (glfwCreateWindowSurface(
                        *instance, window.handle, nullptr, &s
                    ) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to create window surface");
                }
                surface = s;
            }
            auto formats = physical_device->getSurfaceFormatsKHR(surface);
            auto format  = formats[0];
            for (auto&& available : formats) {
                // Prefer SRGB format if available
                if (available.format == vk::Format::eB8G8R8A8Srgb &&
                    available.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                    format = available;
                    break;
                }
            }
            auto presentMode = match_present_mode(surface, window.present_mode);

            auto swapchain_create_info =
                vk::SwapchainCreateInfoKHR()
                    .setSurface(surface)
                    .setMinImageCount(2)  // Double buffering
                    .setImageFormat(format.format)
                    .setImageColorSpace(format.colorSpace)
                    .setImageExtent(vk::Extent2D(
                        window.physical_width, window.physical_height
                    ))
                    .setImageArrayLayers(1)
                    .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
                    .setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
                    .setCompositeAlpha([&] {
                        switch (window.alpha_mode) {
                            case epix::window::CompositeAlphaMode::Opacity:
                                return vk::CompositeAlphaFlagBitsKHR::eOpaque;
                            case epix::window::CompositeAlphaMode::
                                PreMultiplied:
                                return vk::CompositeAlphaFlagBitsKHR::
                                    ePreMultiplied;
                            case epix::window::CompositeAlphaMode::
                                PostMultiplied:
                                return vk::CompositeAlphaFlagBitsKHR::
                                    ePostMultiplied;
                            case epix::window::CompositeAlphaMode::Inherit:
                                return vk::CompositeAlphaFlagBitsKHR::eInherit;
                            default:
                                return vk::CompositeAlphaFlagBitsKHR::eOpaque;
                        }
                    }())  // Opaque alpha
                    .setPresentMode(presentMode)
                    .setClipped(true);

            auto swapchain = device->createSwapchainKHR(swapchain_create_info);
            auto actual_count = device->getSwapchainImagesKHR(swapchain).size();
            swapchain_create_info.setMinImageCount(actual_count);
            auto fences =
                std::views::iota((size_t)0, actual_count) |
                std::views::transform([&](auto&&) {
                    return device->createFence(vk::FenceCreateInfo().setFlags(
                        vk::FenceCreateFlagBits::eSignaled
                    ));
                }) |
                std::ranges::to<std::vector>();
            window_surfaces->surfaces.emplace(
                window.entity,
                SurfaceData{
                    .device                 = *device,
                    .instance               = *instance,
                    .surface                = surface,
                    .swapchain              = swapchain,
                    .config                 = swapchain_create_info,
                    .swapchain_image_fences = std::move(fences),
                }
            );
        }

        if (window.size_changed || window.present_mode_changed) {
            auto& surface = window_surfaces->surfaces.at(window.entity);
            surface.config.setImageExtent(
                vk::Extent2D(window.physical_width, window.physical_height)
            );
            auto presentMode =
                match_present_mode(surface.surface, window.present_mode);
            surface.config.presentMode = presentMode;
            surface.config.setOldSwapchain(surface.swapchain);
            auto old          = surface.swapchain;
            surface.swapchain = device->createSwapchainKHR(surface.config);
            if (old) {
                device->destroySwapchainKHR(old);
            }
        }
    }
}

EPIX_API void epix::render::window::prepare_windows(
    ResMut<ExtractedWindows> windows,
    ResMut<WindowSurfaces> window_surfaces,
    Res<vk::Device> device,
    Res<vk::PhysicalDevice> physical_device
) {
    for (auto&& window :
         std::views::all(windows->windows) | std::views::values) {
        auto it = window_surfaces->surfaces.find(window.entity);
        if (it == window_surfaces->surfaces.end()) continue;
        auto& surface_data = it->second;

        auto& surface = surface_data.surface;

        auto res = device->acquireNextImageKHR(
            surface_data.swapchain, UINT64_MAX, nullptr,
            surface_data.swapchain_image_fences[surface_data.fence_index],
            &surface_data.current_image_index
        );
        auto res2 = device->waitForFences(
            surface_data.swapchain_image_fences[surface_data.fence_index], true,
            UINT64_MAX
        );
        device->resetFences(
            surface_data.swapchain_image_fences[surface_data.fence_index]
        );
        if (res == vk::Result::eSuccess || res == vk::Result::eSuboptimalKHR) {
            auto image = device->getSwapchainImagesKHR(surface_data.swapchain
            )[surface_data.current_image_index];
            window.set_swapchain_texture(
                *device, image, surface_data.config.imageFormat
            );
        } else {
            throw std::runtime_error("Failed to get current texture for window"
            );
        }
    }
}

EPIX_API void epix::render::window::present_windows(
    ResMut<WindowSurfaces> window_surfaces, Res<vk::Queue> queue
) {
    for (auto&& window :
         std::views::all(window_surfaces->surfaces) | std::views::values) {
        auto res =
            queue->presentKHR(vk::PresentInfoKHR()
                                  .setSwapchains(window.swapchain)
                                  .setImageIndices(window.current_image_index));
    }
}
