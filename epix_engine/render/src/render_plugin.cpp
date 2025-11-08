#include <nvrhi/validation.h>

#include <stacktrace>

#include "epix/render.hpp"
#include "epix/render/extract.hpp"

using namespace epix;
using namespace epix::render;

RenderPlugin& RenderPlugin::set_validation(int level) {
    validation = level;
    return *this;
}

struct VolkHandler {
    VolkHandler() {
#ifdef EPIX_USE_VOLK
        if (volkInitialize() != VK_SUCCESS) {
            throw std::runtime_error("Failed to initialize Vulkan loader");
        }
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
#endif
    }
    ~VolkHandler() {
#ifdef EPIX_USE_VOLK
        volkFinalize();
#endif
    }
};

static std::optional<VolkHandler> volk_handler;

void RenderPlugin::build(epix::App& app) {
#ifdef EPIX_USE_VOLK
    if (!volk_handler) {
        volk_handler.emplace();
    }
#endif

    app.add_sub_app(Render);
    // render_app.config.enable_tracy = app.config.enable_tracy;

    // schedules for render app
    app.sub_app_mut(Render)
        .add_schedule(epix::Schedule(epix::render::ExtractSchedule))
        .add_schedule(epix::render::Render.render_schedule())
        .set_extract_fn([](App& render_app, World& main_world) { render_app.run_schedule(ExtractSchedule); });
    app.sub_app_mut(Render).schedule_order().insert_begin(epix::render::Render);

    app.add_plugins(epix::render::window::WindowRenderPlugin{});
    app.add_plugins(epix::render::camera::CameraPlugin{});
    app.add_plugins(epix::render::view::ViewPlugin{});
    app.add_plugins(epix::render::ShaderPlugin{});
    app.add_plugins(epix::render::PipelineServerPlugin{});
}
void RenderPlugin::finish(epix::App& app) {
    spdlog::info("[render] Creating vulkan resources.");

    auto latyers = std::vector<const char*>();
    if (validation == 2) {
        // validation 2: enable vulkan validation layers
        latyers.push_back("VK_LAYER_KHRONOS_validation");
    }

    auto app_info =
        vk::ApplicationInfo().setPApplicationName("Epix").setPEngineName("Epix").setApiVersion(VK_API_VERSION_1_3);
    auto extensions = [&] {
        uint32_t count = 0;
        auto exts      = glfwGetRequiredInstanceExtensions(&count);
        if (!exts) {
            throw std::runtime_error("Failed to get required instance extensions");
        }
        std::vector<const char*> extension_names(exts, exts + count);
        // if validation is enabled, add the debug utils extension
        if (validation) {
            extension_names.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        return extension_names;
    }();
    auto instance = vk::createInstance(vk::InstanceCreateInfo()
                                           .setPApplicationInfo(&app_info)
                                           .setPEnabledLayerNames(latyers)
                                           .setPEnabledExtensionNames(extensions));
#ifdef EPIX_USE_VOLK
    volkLoadInstance(instance);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
#endif
    if (validation == 2) {
        auto debug_utils_messenger_create_info =
            vk::DebugUtilsMessengerCreateInfoEXT()
                .setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
                .setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance);
        debug_utils_messenger_create_info.pfnUserCallback =
            [](vk::DebugUtilsMessageSeverityFlagBitsEXT message_severity,
               vk::DebugUtilsMessageTypeFlagsEXT message_type,
               const vk::DebugUtilsMessengerCallbackDataEXT* p_callback_data, void* p_user_data) -> vk::Bool32 {
            static std::shared_ptr<spdlog::logger> logger = spdlog::default_logger()->clone("vulkan");
            if (message_severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
                logger->error("Vulkan: {}: {}\n    with stack trace:\n{}", vk::to_string(message_type),
                              p_callback_data->pMessage, std::to_string(std::stacktrace::current()));
            } else if (message_severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
                logger->warn("Vulkan: {}: {}", vk::to_string(message_type), p_callback_data->pMessage);
            } else {
                logger->info("Vulkan: {}: {}", vk::to_string(message_type), p_callback_data->pMessage);
            }
            return VK_FALSE;  // return false to continue processing
        };
        auto func_create_debug_utils_messenger =
            (PFN_vkCreateDebugUtilsMessengerEXT)instance.getProcAddr("vkCreateDebugUtilsMessengerEXT");
        vk::DebugUtilsMessengerEXT debug_messenger = [&]() {
            if (!func_create_debug_utils_messenger) {
                throw std::runtime_error("Failed to get vkCreateDebugUtilsMessengerEXT function");
            }
            VkDebugUtilsMessengerEXT vk_debug_messenger;
            auto result = func_create_debug_utils_messenger(
                instance,
                reinterpret_cast<const VkDebugUtilsMessengerCreateInfoEXT*>(&debug_utils_messenger_create_info),
                nullptr, &vk_debug_messenger);
            if (result != VK_SUCCESS) {
                throw std::runtime_error("Failed to create debug utils messenger: " +
                                         vk::to_string(static_cast<vk::Result>(result)));
            }
            return vk_debug_messenger;
        }();
        app.world_mut().insert_resource(debug_messenger);
    }
    auto physical_devices = instance.enumeratePhysicalDevices();
    if (physical_devices.empty()) {
        throw std::runtime_error("No Vulkan physical devices found");
    }
    auto physical_device = physical_devices[0];
    // search for a queue family that supports both graphics and compute
    auto queue_families    = physical_device.getQueueFamilyProperties();
    auto find_queue_family = [&](vk::QueueFlags flags) -> int {
        for (int i = 0; i < queue_families.size(); ++i) {
            if (queue_families[i].queueFlags & flags) {
                return i;
            }
        }
        return -1;  // not found
    };
    int queue_family_index =
        find_queue_family(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer);
    auto device_extensions = std::array{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    };
    auto device = [&] {
        std::array<float, 1> queue_priorities = {1.0f};
        auto queue_create_infos =
            std::views::single(queue_family_index) | std::views::filter([](int index) {
                return index >= 0;  // only include valid indices
            }) |
            std::views::transform([&](int index) {
                return vk::DeviceQueueCreateInfo().setQueueFamilyIndex(index).setQueuePriorities(queue_priorities);
            }) |
            std::ranges::to<std::vector>();
        vk::PhysicalDeviceVulkan12Features features12;
        features12.setTimelineSemaphore(true);
        vk::PhysicalDeviceVulkan13Features features13;
        features13.setSynchronization2(true);
        features13.setDynamicRendering(true);
        features12.setPNext(&features13);
        return physical_device.createDevice(vk::DeviceCreateInfo()
                                                .setQueueCreateInfos(queue_create_infos)
                                                .setPEnabledExtensionNames(device_extensions)
                                                .setPNext(&features12));
    }();
// we only have one device if only one app exists and only this plugin
// handles vulkan resources, so we can add these lines to improve
// performance
#ifdef EPIX_USE_VOLK
    volkLoadDevice(device);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
#endif

    struct MsgCallback : public nvrhi::IMessageCallback {
        void message(nvrhi::MessageSeverity severity, const char* message) override {
            if (severity == nvrhi::MessageSeverity::Error || severity == nvrhi::MessageSeverity::Fatal) {
                spdlog::error("[nvrhi-vk] {}: {}\n with stack trace:\n {}",
                              severity == nvrhi::MessageSeverity::Fatal ? "[fatal]" : "", message,
                              std::to_string(std::stacktrace::current()));
            } else if (severity == nvrhi::MessageSeverity::Warning) {
                spdlog::warn("[nvrhi-vk]: {}", message);
            } else if (severity == nvrhi::MessageSeverity::Info) {
                spdlog::info("[nvrhi-vk]: {}", message);
            }
        }
    };

    nvrhi::DeviceHandle nvrhi_device = nvrhi::vulkan::createDevice(nvrhi::vulkan::DeviceDesc{
        .errorCB               = validation ? new MsgCallback() : nullptr,
        .instance              = instance,
        .physicalDevice        = physical_device,
        .device                = device,
        .graphicsQueue         = device.getQueue(queue_family_index, 0),
        .graphicsQueueIndex    = queue_family_index,
        .instanceExtensions    = extensions.data(),
        .numInstanceExtensions = extensions.size(),
        .deviceExtensions      = device_extensions.data(),
        .numDeviceExtensions   = device_extensions.size(),
    });
    if (validation) {
        nvrhi_device = nvrhi::validation::createValidationLayer(nvrhi_device);
    }

    nvrhi_device = epix::render::create_async_device(nvrhi_device);

    auto queue = device.getQueue(queue_family_index, 0);
    app.world_mut().insert_resource(instance);
    app.world_mut().insert_resource(physical_device);
    app.world_mut().insert_resource(device);
    app.world_mut().insert_resource(queue);
    app.world_mut().insert_resource(nvrhi_device);
    auto& render_app = app.sub_app_mut(Render);
    {
        render_app.world_mut().emplace_resource<graph::RenderGraph>();

        render_app.add_systems(
            Render, into([](Res<nvrhi::DeviceHandle> nvrhi_device) { nvrhi_device.get()->runGarbageCollection(); },
                         [](World& world) { world.clear_entities(); })
                        .set_names(std::array{"nvrhi garbage collect", "clear render entities"})
                        .after(RenderSet::Cleanup));
        render_app.add_systems(Render,
                               into(render_system).in_set(RenderSet::Render).set_names(std::array{"render system"}));
    }
    render_app.world_mut().insert_resource(instance);
    render_app.world_mut().insert_resource(physical_device);
    render_app.world_mut().insert_resource(device);
    render_app.world_mut().insert_resource(queue);
    render_app.world_mut().insert_resource(nvrhi_device);
}
void RenderPlugin::finalize(epix::App& app) { volk_handler.reset(); }