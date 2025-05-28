#include "epix/render.h"

EPIX_API void epix::render::RenderPlugin::build(epix::App& app) {
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Failed to initialize Vulkan loader");
    }
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
    // create webgpu render resources
    spdlog::info("Creating vulkan render resources");
    auto validation_layers = std::array{
        "VK_LAYER_KHRONOS_validation",
    };
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
        return extension_names;
    }();
    auto instance =
        vk::createInstance(vk::InstanceCreateInfo()
                               .setPApplicationInfo(&app_info)
                               .setPEnabledLayerNames(validation_layers)
                               .setPEnabledExtensionNames(extensions));
    volkLoadInstance(instance);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
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
    volkLoadDevice(device);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
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
                      ResMut<epix::render::CommandPools> pools) {
            device->waitIdle();
            pools->destroy();
            device->destroy();
            instance->destroy();
            volkFinalize();
        }).set_name("Destroy Vulkan resources")
    );

    app.add_plugins(epix::render::window::WindowRenderPlugin{});
}