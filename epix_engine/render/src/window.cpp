#include "epix/render/extract.hpp"
#include "epix/render/schedule.hpp"
#include "epix/render/window.hpp"

using namespace epix::render::window;
using namespace epix;

void epix::render::window::WindowSurfaces::remove(const Entity& entity) {
    if (auto it = surfaces.find(entity); it != surfaces.end()) {
        auto& data = it->second;
        for (auto& image : data.swapchain_images) {
            image = nullptr;
        }
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

void WindowRenderPlugin::build(epix::App& app) {
    auto& render_app = app.sub_app_mut(epix::render::Render);
    render_app.world_mut().insert_resource(ExtractedWindows{});
    render_app.world_mut().insert_resource(WindowSurfaces{});
    render_app.add_systems(epix::render::ExtractSchedule,
                           epix::into(extract_windows).set_names(std::array{"extract windows"}));
    render_app.add_systems(
        epix::render::Render,
        epix::into(create_surfaces).set_names(std::array{"create_surfaces"}).before(prepare_windows));
    render_app.add_systems(
        epix::render::Render,
        epix::into(prepare_windows).set_name("prepare windows").in_set(epix::render::RenderSet::ManageViews));
    if (handle_present) {
        render_app.add_systems(
            epix::render::Render,
            epix::into(present_windows).set_name("present windows").after(epix::render::RenderSet::Cleanup));
    }
}

void epix::render::window::extract_windows(
    ResMut<ExtractedWindows> extracted_windows,
    Res<vk::Device> device,
    Extract<EventReader<epix::window::WindowClosed>> closed,
    Extract<Query<Item<Entity, const epix::window::Window&, Has<epix::window::PrimaryWindow>>>> windows,
    Extract<Res<glfw::GLFWwindows>> glfw_windows,
    ResMut<WindowSurfaces> window_surfaces) {
    for (auto&& [entity, window, primary] : windows.iter()) {
        if (primary) {
            extracted_windows->primary = entity;
        }

        auto [new_width, new_height] = window.size;
        bool valid                   = new_width > 0 && new_height > 0;
        new_width                    = std::max(new_width, 1);
        new_height                   = std::max(new_height, 1);

        if (!extracted_windows->windows.contains(entity) && glfw_windows->contains(entity)) {
            extracted_windows->windows.emplace(entity, ExtractedWindow{
                                                           .entity          = entity,
                                                           .handle          = glfw_windows->at(entity).first,
                                                           .physical_width  = new_width,
                                                           .physical_height = new_height,
                                                           .present_mode    = window.present_mode,
                                                           .alpha_mode      = window.composite_alpha_mode,
                                                       });
        }

        auto& extracted_window = extracted_windows->windows.at(entity);
        if (extracted_window.swapchain_texture) {
            extracted_window.swapchain_texture = nullptr;
        }

        extracted_window.size_changed =
            new_width != extracted_window.physical_width || new_height != extracted_window.physical_height;
        extracted_window.valid                = valid;
        extracted_window.present_mode_changed = window.present_mode != extracted_window.present_mode;

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
void epix::render::window::create_surfaces(Res<ExtractedWindows> windows,
                                           ResMut<WindowSurfaces> window_surfaces,
                                           Res<vk::Instance> instance,
                                           Res<vk::PhysicalDevice> physical_device,
                                           Res<vk::Device> device,
                                           Res<vk::Queue> queue,
                                           Res<nvrhi::DeviceHandle> nvrhi_device) {
    auto match_present_mode = [&](vk::SurfaceKHR surface, epix::window::PresentMode present_mode) {
        auto presentModes = physical_device->getSurfacePresentModesKHR(surface);
        switch (present_mode) {
            case epix::window::PresentMode::AutoNoVsync: {
                // immediate
                auto res = std::find_if(presentModes.begin(), presentModes.end(),
                                        [](auto&& mode) { return mode == vk::PresentModeKHR::eImmediate; });
                if (res != presentModes.end()) {
                    return vk::PresentModeKHR::eImmediate;
                }
                // mailbox
                res = std::find_if(presentModes.begin(), presentModes.end(),
                                   [](auto&& mode) { return mode == vk::PresentModeKHR::eMailbox; });
                if (res != presentModes.end()) {
                    return vk::PresentModeKHR::eMailbox;
                }
                // fifo
                res = std::find_if(presentModes.begin(), presentModes.end(),
                                   [](auto&& mode) { return mode == vk::PresentModeKHR::eFifo; });
                if (res != presentModes.end()) {
                    return vk::PresentModeKHR::eFifo;
                }
            }
            case epix::window::PresentMode::AutoVsync: {
                // fifo relaxed
                auto res = std::find_if(presentModes.begin(), presentModes.end(),
                                        [](auto&& mode) { return mode == vk::PresentModeKHR::eFifoRelaxed; });
                if (res != presentModes.end()) {
                    return vk::PresentModeKHR::eFifoRelaxed;
                }
                // fifo
                res = std::find_if(presentModes.begin(), presentModes.end(),
                                   [](auto&& mode) { return mode == vk::PresentModeKHR::eFifo; });
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

    nvrhi::CommandListHandle commandlist =
        nvrhi_device.get()->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
    commandlist->open();
    vk::CommandBuffer cmd_buffer = (VkCommandBuffer)commandlist->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);
    bool cmd_empty               = true;
    for (auto&& window : std::views::all(windows->windows) | std::views::values) {
        if (!window_surfaces->surfaces.contains(window.entity)) {
            vk::SurfaceKHR surface;
            {
                VkSurfaceKHR s;
                if (glfwCreateWindowSurface(*instance, window.handle, nullptr, &s) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to create window surface");
                }
                surface = s;
            }
            auto formats = physical_device->getSurfaceFormatsKHR(surface);
            auto format  = formats[0];

            // desired formats in nvrhi
            auto desired_formats = std::array{
                nvrhi::Format::SBGRA8_UNORM,
                nvrhi::Format::SRGBA8_UNORM,
                nvrhi::Format::RGBA8_UNORM,
                nvrhi::Format::BGRA8_UNORM,
            };
            nvrhi::Format found_format = nvrhi::Format::UNKNOWN;

            for (auto&& desired : desired_formats) {
                auto it = std::find_if(formats.begin(), formats.end(), [&](const vk::SurfaceFormatKHR& f) {
                    return f.format == vk::Format(nvrhi::vulkan::convertFormat(desired)) &&
                           f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
                });
                if (it != formats.end()) {
                    format       = *it;
                    found_format = desired;
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
                    .setImageExtent(vk::Extent2D(window.physical_width, window.physical_height))
                    .setImageArrayLayers(1)
                    .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst)
                    .setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
                    .setCompositeAlpha([&] {
                        switch (window.alpha_mode) {
                            case epix::window::CompositeAlphaMode::Opacity:
                                return vk::CompositeAlphaFlagBitsKHR::eOpaque;
                            case epix::window::CompositeAlphaMode::PreMultiplied:
                                return vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
                            case epix::window::CompositeAlphaMode::PostMultiplied:
                                return vk::CompositeAlphaFlagBitsKHR::ePostMultiplied;
                            case epix::window::CompositeAlphaMode::Inherit:
                                return vk::CompositeAlphaFlagBitsKHR::eInherit;
                            default:
                                return vk::CompositeAlphaFlagBitsKHR::eOpaque;
                        }
                    }())  // Opaque alpha
                    .setPresentMode(presentMode)
                    .setClipped(true);

            auto swapchain = device->createSwapchainKHR(swapchain_create_info);
            auto images    = device->getSwapchainImagesKHR(swapchain);
            for (auto&& image : images) {
                auto barrier = vk::ImageMemoryBarrier()
                                   .setSrcAccessMask(vk::AccessFlagBits::eNone)
                                   .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
                                   .setOldLayout(vk::ImageLayout::eUndefined)
                                   .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                                   .setImage(image)
                                   .setSubresourceRange(vk::ImageSubresourceRange()
                                                            .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                            .setBaseMipLevel(0)
                                                            .setLevelCount(1)
                                                            .setBaseArrayLayer(0)
                                                            .setLayerCount(1));
                cmd_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                           vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, {}, {}, barrier);
                cmd_empty = false;
            }
            auto actual_count = images.size();
            swapchain_create_info.setMinImageCount(actual_count);
            auto fences = std::views::iota((size_t)0, actual_count) |
                          std::views::transform([&](auto&&) { return device->createFence(vk::FenceCreateInfo()); }) |
                          std::ranges::to<std::vector>();
            window_surfaces->surfaces.emplace(
                window.entity, SurfaceData{
                                   .device           = *device,
                                   .instance         = *instance,
                                   .surface          = surface,
                                   .image_format     = found_format,
                                   .swapchain        = swapchain,
                                   .config           = swapchain_create_info,
                                   .swapchain_images = images | std::views::transform([&](VkImage image) {
                                                           auto desc =
                                                               nvrhi::TextureDesc()
                                                                   .setDimension(nvrhi::TextureDimension::Texture2D)
                                                                   .setFormat(found_format)
                                                                   .setWidth(swapchain_create_info.imageExtent.width)
                                                                   .setHeight(swapchain_create_info.imageExtent.height)
                                                                   .setInitialState(nvrhi::ResourceStates::Present)
                                                                   .setKeepInitialState(true)
                                                                   .setIsRenderTarget(true)
                                                                   .setDebugName("Swap Chain Image");
                                                           return nvrhi_device.get()->createHandleForNativeTexture(
                                                               nvrhi::ObjectTypes::VK_Image, image, desc);
                                                       }) |
                                                       std::ranges::to<std::vector>(),
                                   .swapchain_image_fences = std::move(fences),
                               });
        }

        if (window.size_changed || window.present_mode_changed) {
            auto& surface = window_surfaces->surfaces.at(window.entity);
            surface.config.setImageExtent(vk::Extent2D(window.physical_width, window.physical_height));
            if (!window.valid) {
                // If the size is invalid, clear all data, and create Offscreen
                // textures
                surface.swapchain_images.clear();
                device->destroySwapchainKHR(surface.swapchain);
                surface.swapchain = nullptr;
                auto desc         = nvrhi::TextureDesc()
                                .setDimension(nvrhi::TextureDimension::Texture2D)
                                .setFormat(surface.image_format)
                                .setWidth(1)
                                .setHeight(1)
                                .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                .setKeepInitialState(true)
                                .setIsRenderTarget(true)
                                .setDebugName("Swap Chain Image");
                for (auto&& index : std::views::iota(0u, surface.config.minImageCount)) {
                    surface.swapchain_images.push_back(nvrhi_device.get()->createTexture(desc));
                }
                continue;
            }
            auto presentMode           = match_present_mode(surface.surface, window.present_mode);
            surface.config.presentMode = presentMode;
            surface.config.setOldSwapchain(surface.swapchain);
            auto old          = surface.swapchain;
            surface.swapchain = device->createSwapchainKHR(surface.config);
            for (auto& image : surface.swapchain_images) {
                image = nullptr;  // Clear old images
            }
            if (old) {
                device->destroySwapchainKHR(old);
            }
            for (auto&& image : device->getSwapchainImagesKHR(surface.swapchain)) {
                auto barrier = vk::ImageMemoryBarrier()
                                   .setSrcAccessMask(vk::AccessFlagBits::eNone)
                                   .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
                                   .setOldLayout(vk::ImageLayout::eUndefined)
                                   .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                   .setImage(image)
                                   .setSubresourceRange(vk::ImageSubresourceRange()
                                                            .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                            .setBaseMipLevel(0)
                                                            .setLevelCount(1)
                                                            .setBaseArrayLayer(0)
                                                            .setLayerCount(1));
                cmd_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                           vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, {}, {}, barrier);
                cmd_empty = false;
            }
            surface.swapchain_images =
                device->getSwapchainImagesKHR(surface.swapchain) | std::views::transform([&](VkImage image) {
                    auto desc = nvrhi::TextureDesc()
                                    .setDimension(nvrhi::TextureDimension::Texture2D)
                                    .setFormat(surface.image_format)
                                    .setWidth(surface.config.imageExtent.width)
                                    .setHeight(surface.config.imageExtent.height)
                                    .setInitialState(nvrhi::ResourceStates::Present)
                                    .setKeepInitialState(true)
                                    .setIsRenderTarget(true)
                                    .setDebugName("Swap Chain Image");
                    return nvrhi_device.get()->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, image, desc);
                }) |
                std::ranges::to<std::vector>();
        }
    }
    commandlist->close();
    nvrhi_device.get()->executeCommandList(commandlist);
}

void epix::render::window::prepare_windows(ResMut<ExtractedWindows> windows,
                                           ResMut<WindowSurfaces> window_surfaces,
                                           Res<vk::Device> device,
                                           Res<vk::PhysicalDevice> physical_device,
                                           Res<nvrhi::DeviceHandle> nvrhi_device) {
    std::vector<std::pair<Entity, std::string>> errors;
    for (auto&& window : std::views::all(windows->windows) | std::views::values) {
        auto it = window_surfaces->surfaces.find(window.entity);
        if (it == window_surfaces->surfaces.end()) continue;
        auto& surface_data = it->second;

        auto& surface = surface_data.surface;
        // if no swapchain provided, use offscreen texture
        if (!surface_data.swapchain) {
            surface_data.current_image_index = (surface_data.fence_index + 1) % surface_data.swapchain_images.size();
            window.swapchain_texture         = surface_data.swapchain_images[surface_data.current_image_index];
            continue;
        }

        try {
            std::exception_ptr exception_ptr;
            vk::Result res;
            res = device->acquireNextImageKHR(surface_data.swapchain, UINT64_MAX, nullptr,
                                              surface_data.swapchain_image_fences[surface_data.fence_index],
                                              &surface_data.current_image_index);
            auto res2 =
                device->waitForFences(surface_data.swapchain_image_fences[surface_data.fence_index], true, UINT64_MAX);
            device->resetFences(surface_data.swapchain_image_fences[surface_data.fence_index]);
            surface_data.fence_index = (surface_data.fence_index + 1) % surface_data.swapchain_image_fences.size();
            window.swapchain_texture = surface_data.swapchain_images[surface_data.current_image_index];
            if (res == vk::Result::eSuccess || res == vk::Result::eSuboptimalKHR) {
            } else {
                errors.emplace_back(window.entity, vk::to_string(res));
            }
            if (exception_ptr) {
                // If an exception occurred, rethrow it
                std::rethrow_exception(exception_ptr);
            }
        } catch (const std::exception& e) {
            errors.emplace_back(window.entity, e.what());
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

void epix::render::window::present_windows(ResMut<WindowSurfaces> window_surfaces, Res<vk::Queue> queue) {
    std::vector<vk::SwapchainKHR> swapchains;
    swapchains.reserve(window_surfaces->surfaces.size());
    std::vector<uint32_t> image_indices;
    image_indices.reserve(window_surfaces->surfaces.size());
    for (auto&& window : std::views::all(window_surfaces->surfaces) | std::views::values |
                             std::views::filter([](const auto& surface) { return surface.swapchain != nullptr; })) {
        swapchains.push_back(window.swapchain);
        image_indices.push_back(window.current_image_index);
    }
    if (swapchains.empty()) {
        return;  // Nothing to present
    }
    std::exception_ptr exception_ptr;

    // wrap the present call in a try-catch block so that the fence is still
    // waited and destroyed even if an exception occurs
    try {
        auto res = queue->presentKHR(vk::PresentInfoKHR().setSwapchains(swapchains).setImageIndices(image_indices));
    } catch (...) {
        exception_ptr = std::current_exception();
    }

    if (exception_ptr) {
        // If an exception occurred, rethrow it
        std::rethrow_exception(exception_ptr);
    }
}
