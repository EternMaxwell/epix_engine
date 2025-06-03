#include <stacktrace>

#include "epix/render.h"

EPIX_API epix::render::RenderPlugin&
epix::render::RenderPlugin::enable_validation(bool enable) {
    validation = enable;
    return *this;
}

EPIX_API void epix::render::RenderPlugin::build(epix::App& app) {
    #ifdef EPIX_USE_VOLK
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize Vulkan loader");
    }
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
    #endif
    // create webgpu render resources
    spdlog::info("Creating vulkan render resources");
    auto layers = std::vector<const char*>();
    if (validation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    auto app_info = vk::ApplicationInfo()
                        .setPApplicationName("Epix")
                        .setPEngineName("Epix")
                        .setApiVersion(VK_API_VERSION_1_3);
    auto extensions = [&] {
        uint32_t count = 0;
        auto exts      = glfwGetRequiredInstanceExtensions(&count);
        if (!exts) {
            throw std::runtime_error(
                "Failed to get required instance extensions"
            );
        }
        std::vector<const char*> extension_names(exts, exts + count);
        // if validation is enabled, add the debug utils extension
        if (validation) {
            extension_names.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        return extension_names;
    }();
    auto instance =
        vk::createInstance(vk::InstanceCreateInfo()
                               .setPApplicationInfo(&app_info)
                               .setPEnabledLayerNames(layers)
                               .setPEnabledExtensionNames(extensions));
    #ifdef EPIX_USE_VOLK
    volkLoadInstance(instance);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
    #endif
    if (validation) {
        auto debug_utils_messenger_create_info =
            vk::DebugUtilsMessengerCreateInfoEXT()
                .setMessageSeverity(
                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
                )
                .setMessageType(
                    vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                    vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                    vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance
                )
                .setPfnUserCallback(
                    [](vk::DebugUtilsMessageSeverityFlagBitsEXT
                           message_severity,
                       vk::DebugUtilsMessageTypeFlagsEXT message_type,
                       const vk::DebugUtilsMessengerCallbackDataEXT*
                           p_callback_data,
                       void* p_user_data) -> vk::Bool32 {
                        static std::shared_ptr<spdlog::logger> logger =
                            spdlog::default_logger()->clone("vulkan");
                        if (message_severity >=
                            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
                            logger->error(
                                "Vulkan: {}: {}\n    with stack trace:\n{}",
                                vk::to_string(message_type),
                                p_callback_data->pMessage,
                                std::to_string(std::stacktrace::current())
                            );
                        } else if (message_severity >=
                                   vk::DebugUtilsMessageSeverityFlagBitsEXT::
                                       eWarning) {
                            logger->warn(
                                "Vulkan: {}: {}", vk::to_string(message_type),
                                p_callback_data->pMessage
                            );
                        } else {
                            logger->info(
                                "Vulkan: {}: {}", vk::to_string(message_type),
                                p_callback_data->pMessage
                            );
                        }
                        return VK_FALSE;  // return false to continue processing
                    }
                );
        auto debug_messenger = instance.createDebugUtilsMessengerEXT(
            debug_utils_messenger_create_info
        );
        app.insert_resource(debug_messenger);
    }
    auto physical_devices = instance.enumeratePhysicalDevices();
    if (physical_devices.empty()) {
        throw std::runtime_error("No Vulkan physical devices found");
    }
    auto physical_device = physical_devices[0];
    // search for a queue family that supports both graphics and compute
    auto queue_families         = physical_device.getQueueFamilyProperties();
    uint32_t queue_family_index = [&] {
        for (uint32_t i = 0; i < queue_families.size(); ++i) {
            if (queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics &&
                queue_families[i].queueFlags & vk::QueueFlagBits::eCompute) {
                return i;
            }
        }
        throw std::runtime_error(
            "No suitable queue family found: need both graphics and compute "
            "support"
        );
    }();
    auto device = [&] {
        std::array<float, 1> queue_priorities = {1.0f};
        std::array queue_create_infos         = {
            vk::DeviceQueueCreateInfo()
                .setQueueFamilyIndex(queue_family_index)
                .setQueuePriorities(queue_priorities),
            // vk::DeviceQueueCreateInfo()
            //     .setQueueFamilyIndex(queue_family_index)
            //     .setQueuePriorities(queue_priorities)
        };
        auto device_extensions = std::array{
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };
        return physical_device.createDevice(
            vk::DeviceCreateInfo()
                .setQueueCreateInfos(queue_create_infos)
                .setPEnabledExtensionNames(device_extensions)
        );
    }();
    // we only have one device if only one app exists and only this plugin
    // handles vulkan resources, so we can add these lines to improve
    // performance
    #ifdef EPIX_USE_VOLK
    volkLoadDevice(device);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
    #endif
    auto queue = device.getQueue(queue_family_index, 0);
    app.insert_resource(instance);
    app.insert_resource(physical_device);
    app.insert_resource(device);
    app.insert_resource(queue);
    auto& render_world = app.world(epix::app::RenderWorld);
    render_world.insert_resource(instance);
    render_world.insert_resource(physical_device);
    render_world.insert_resource(device);
    render_world.insert_resource(queue);
    // command pools resource should be across worlds, so use add_resource
    auto pools = UntypedRes::create(
        std::make_shared<epix::render::CommandPools>(device, queue_family_index)
    );
    app.add_resource(pools);
    render_world.add_resource(pools);

    app.add_systems(
        epix::PostExit,
        epix::into([](Res<vk::Instance> instance,
                      Res<vk::PhysicalDevice> physical_device,
                      Res<vk::Device> device, Res<vk::Queue> queue,
                      std::optional<Res<vk::DebugUtilsMessengerEXT>>
                          debug_utils_messenger,
                      ResMut<epix::render::CommandPools> pools) {
            device->waitIdle();
            pools->destroy();
            device->destroy();
            if (debug_utils_messenger) {
                instance->destroyDebugUtilsMessengerEXT(
                    **debug_utils_messenger, nullptr,
                    VULKAN_HPP_DEFAULT_DISPATCHER
                );
            }
            instance->destroy();
            volkFinalize();
        }).set_name("Destroy Vulkan resources")
    );

    app.add_plugins(epix::render::window::WindowRenderPlugin{});
}