#include "epix/render.h"

EPIX_API void epix::render::RenderPlugin::build(epix::App& app) {
    // create webgpu render resources
    spdlog::info("Creating vulkan render resources");
    auto instance = vk::createInstance(vk::InstanceCreateInfo());
    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
    auto physical_devices = instance.enumeratePhysicalDevices();
    if (physical_devices.empty()) {
        throw std::runtime_error("No Vulkan physical devices found");
    }
    auto physical_device = physical_devices[0];
    // search for a queue family that supports both graphics and compute
    auto queue_families = physical_device.getQueueFamilyProperties();
    uint32_t queue_family_index = 0;
    auto device          = physical_device.createDevice(vk::DeviceCreateInfo().setQueueCreateInfos(
        {vk::DeviceQueueCreateInfo()
             .setQueueFamilyIndex(0)
             .setQueuePriorities({1.0f})}));
    // we only have one device if only one app exists and only this plugin
    // handles vulkan resources, so we can add this line to improve performance
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);

}