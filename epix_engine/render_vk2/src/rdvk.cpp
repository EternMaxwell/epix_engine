#include "epix/rdvk.h"

namespace epix::render::vulkan2::backend {
static VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData,
    void* pUserData
) {
    // #if !defined(NDEBUG)
    switch (static_cast<uint32_t>(pCallbackData->messageIdNumber)) {
        case 0:
            // Validation Warning: Override layer has override paths set to
            // C:/VulkanSDK/<version>/Bin
            return vk::False;
        case 0x822806fa:
            // Validation Warning: vkCreateInstance(): to enable extension
            // VK_EXT_debug_utils, but this extension is intended to support use
            // by applications when debugging and it is strongly recommended
            // that it be otherwise avoided.
            return vk::False;
        case 0xe8d1a9fe:
            // Validation Performance Warning: Using debug builds of the
            // validation layers *will* adversely affect performance.
            return vk::False;
    }
    // #endif

    auto* logger = (spdlog::logger*)pUserData;

    std::string msg = std::format(
        "{}: {}: {}\n",
        vk::to_string(static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(
            messageSeverity
        )),
        vk::to_string(
            static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(messageTypes)
        ),
        pCallbackData->pMessage
    );

    msg += std::format(
        "\tmessageIDName   = <{}>\n\tmessageIdNumber = {:#018x}\n",
        pCallbackData->pMessageIdName,
        *((uint32_t*)(&pCallbackData->messageIdNumber))
    );

    if (0 < pCallbackData->queueLabelCount) {
        msg += std::format(
            "\tqueueLabelCount = {}\n", pCallbackData->queueLabelCount
        );
        for (uint32_t i = 0; i < pCallbackData->queueLabelCount; i++) {
            msg += std::format(
                "\t\tlabelName = <{}>\n",
                pCallbackData->pQueueLabels[i].pLabelName
            );
        }
    }
    if (0 < pCallbackData->cmdBufLabelCount) {
        msg += std::format(
            "\tcmdBufLabelCount = {}\n", pCallbackData->cmdBufLabelCount
        );
        for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++) {
            msg += std::format(
                "\t\tlabelName = <{}>\n",
                pCallbackData->pCmdBufLabels[i].pLabelName
            );
        }
    }
    if (0 < pCallbackData->objectCount) {
        msg += std::format("\tobjectCount = {}\n", pCallbackData->objectCount);
        for (uint32_t i = 0; i < pCallbackData->objectCount; i++) {
            msg += std::format(
                "\t\tobjectType = {}\n",
                vk::to_string(static_cast<vk::ObjectType>(
                    pCallbackData->pObjects[i].objectType
                ))
            );
            msg += std::format(
                "\t\tobjectHandle = {:#018x}\n",
                pCallbackData->pObjects[i].objectHandle
            );
            if (pCallbackData->pObjects[i].pObjectName) {
                msg += std::format(
                    "\t\tobjectName = <{}>\n",
                    pCallbackData->pObjects[i].pObjectName
                );
            }
        }
    }

    logger->warn(msg);

    return vk::False;
}
EPIX_API Instance Instance::create(
    const char* app_name,
    uint32_t app_version,
    std::shared_ptr<spdlog::logger> logger,
    bool debug
) {
    auto app_info = vk::ApplicationInfo()
                        .setPApplicationName(app_name)
                        .setApplicationVersion(app_version)
                        .setPEngineName("Pixel Engine")
                        .setEngineVersion(VK_MAKE_VERSION(0, 1, 0))
                        .setApiVersion(VK_API_VERSION_1_3);
    return create(app_info, logger, debug);
};

EPIX_API Instance Instance::create(
    vk::ApplicationInfo app_info,
    std::shared_ptr<spdlog::logger> logger,
    bool debug
) {
    Instance instance;
    instance.logger                 = logger;
    std::vector<char const*> layers = {};
    if (debug) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    uint32_t glfwExtensionCount = 0;
    auto glfwExtensions =
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    auto instance_extensions = vk::enumerateInstanceExtensionProperties();
    logger->debug("Creating Vulkan Instance");

    std::vector<const char*> extensions;
    for (auto& extension : instance_extensions) {
        extensions.push_back(extension.extensionName);
    }
    for (uint32_t i = 0; i < glfwExtensionCount; i++) {
        if (std::find_if(
                extensions.begin(), extensions.end(),
                [&](const char* ext) { return strcmp(ext, glfwExtensions[i]); }
            ) == extensions.end()) {
            logger->error(
                "GLFW requested extension {} not supported by Vulkan",
                glfwExtensions[i]
            );
            throw std::runtime_error(
                "GLFW requested extension not supported by Vulkan. "
                "Extension: " +
                std::string(glfwExtensions[i])
            );
        }
    }

    auto instance_info = vk::InstanceCreateInfo()
                             .setPApplicationInfo(&app_info)
                             .setPEnabledExtensionNames(extensions)
                             .setPEnabledLayerNames(layers);

    std::string instance_layers_info = "Instance Layers:\n";
    for (auto& layer : layers) {
        instance_layers_info += std::format("\t{}\n", layer);
    }
    logger->debug(instance_layers_info);
    std::string instance_extensions_info = "Instance Extensions:\n";
    for (auto& extension : extensions) {
        instance_extensions_info += std::format("\t{}\n", extension);
    }
    logger->debug(instance_extensions_info);

    instance.instance = vk::createInstance(instance_info);
    if (debug) {
        auto debugMessengerInfo =
            vk::DebugUtilsMessengerCreateInfoEXT()
                .setMessageSeverity(
                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                    vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
                )
                .setMessageType(
                    vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                    vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                    vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance
                )
                .setPfnUserCallback(debugUtilsMessengerCallback)
                .setPUserData(logger.get());
        auto func =
            (PFN_vkCreateDebugUtilsMessengerEXT)glfwGetInstanceProcAddress(
                instance.instance, "vkCreateDebugUtilsMessengerEXT"
            );
        if (!func) {
            logger->error(
                "Failed to load function vkCreateDebugUtilsMessengerEXT"
            );
            throw std::runtime_error(
                "Failed to load function vkCreateDebugUtilsMessengerEXT"
            );
        }
        func(
            instance.instance,
            reinterpret_cast<VkDebugUtilsMessengerCreateInfoEXT*>(
                &debugMessengerInfo
            ),
            nullptr,
            reinterpret_cast<VkDebugUtilsMessengerEXT*>(
                &instance.debug_messenger
            )
        );
    }
    return instance;
}
EPIX_API void Instance::destroy() {
    if (debug_messenger) {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            instance.getProcAddr("vkDestroyDebugUtilsMessengerEXT")
        );
        if (func) {
            func(instance, debug_messenger, nullptr);
        }
    }
    instance.destroy();
}
EPIX_API std::vector<vk::PhysicalDevice> Instance::enumerate_physical_devices(
) {
    return instance.enumeratePhysicalDevices();
}
EPIX_API std::vector<vk::PhysicalDeviceGroupProperties>
Instance::enumerate_physical_device_groups() {
    return instance.enumeratePhysicalDeviceGroups();
}

EPIX_API Buffer Device::create_buffer(
    vk::BufferCreateInfo& create_info, AllocationCreateInfo& alloc_info
) {
    Buffer buffer;
    vmaCreateBuffer(
        allocator, reinterpret_cast<VkBufferCreateInfo*>(&create_info),
        reinterpret_cast<VmaAllocationCreateInfo*>(&alloc_info),
        reinterpret_cast<VkBuffer*>(&buffer.buffer), &buffer.allocation, nullptr
    );
    return buffer;
}
EPIX_API void Device::destroy_buffer(Buffer& buffer) {
    vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
}
EPIX_API void Device::destroy(Buffer& buffer) {
    vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
}
EPIX_API Image Device::create_image(
    vk::ImageCreateInfo& create_info, AllocationCreateInfo& alloc_info
) {
    Image image;
    vmaCreateImage(
        allocator, reinterpret_cast<VkImageCreateInfo*>(&create_info),
        reinterpret_cast<VmaAllocationCreateInfo*>(&alloc_info),
        reinterpret_cast<VkImage*>(&image.image), &image.allocation, nullptr
    );
    return image;
}
EPIX_API void Device::destroy_image(Image& image) {
    vmaDestroyImage(allocator, image.image, image.allocation);
}
EPIX_API void Device::destroy(Image& image) {
    vmaDestroyImage(allocator, image.image, image.allocation);
}
EPIX_API Device Device::create(
    Instance& instance,
    PhysicalDevice& physical_device,
    vk::QueueFlags queue_flags
) {
    Device device;
    device.instance        = instance;
    device.physical_device = physical_device;

    auto queue_family_properties = physical_device.getQueueFamilyProperties();
    auto find_iterator           = std::find_if(
        queue_family_properties.begin(), queue_family_properties.end(),
        [&](auto& queue_family_property) {
            return queue_family_property.queueFlags & queue_flags;
        }
    );
    if (find_iterator == queue_family_properties.end()) {
        instance.logger->error(
            "No queue family found with flags: {}", vk::to_string(queue_flags)
        );
        throw std::runtime_error(
            "No queue family found with flags: " +
            std::string(vk::to_string(queue_flags))
        );
    }

    device.queue_family_index =
        std::distance(queue_family_properties.begin(), find_iterator);
    float queue_priority = 1.0f;
    auto queue_info      = vk::DeviceQueueCreateInfo()
                          .setQueueFamilyIndex(device.queue_family_index)
                          .setQueueCount(1)
                          .setPQueuePriorities(&queue_priority);
    std::vector<char const*> required_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
    };
    auto extensions = physical_device.enumerateDeviceExtensionProperties();
    for (auto& extension : required_extensions) {
        if (std::find_if(
                extensions.begin(), extensions.end(),
                [&](const vk::ExtensionProperties& ext) {
                    return strcmp(ext.extensionName, extension) == 0;
                }
            ) == extensions.end()) {
            instance.logger->error(
                "Device required extension {} not supported by physical device",
                extension
            );
            throw std::runtime_error(
                "Device required extension not supported by physical device. "
                "Extension: " +
                std::string(extension)
            );
        }
    }
    std::vector<char const*> device_extensions;
    for (auto& extension : extensions) {
        device_extensions.push_back(extension.extensionName);
    }

    std::string device_extensions_info = "Device Extensions:\n";
    for (auto& extension : device_extensions) {
        device_extensions_info += std::format("\t{}\n", extension);
    }
    instance.logger->debug(device_extensions_info);

    auto dynamic_rendering_features =
        vk::PhysicalDeviceDynamicRenderingFeaturesKHR().setDynamicRendering(
            VK_TRUE
        );
    auto descriptor_indexing_features =
        vk::PhysicalDeviceDescriptorIndexingFeatures()
            .setRuntimeDescriptorArray(VK_TRUE)
            .setDescriptorBindingPartiallyBound(VK_TRUE)
            // Enable non uniform array indexing
            // (#extension GL_EXT_nonuniform_qualifier : require)
            .setShaderStorageBufferArrayNonUniformIndexing(true)
            .setShaderSampledImageArrayNonUniformIndexing(true)
            .setShaderStorageImageArrayNonUniformIndexing(true)
            // All of these enables to update after the
            // commandbuffer used the bindDescriptorsSet
            .setDescriptorBindingStorageBufferUpdateAfterBind(true)
            .setDescriptorBindingSampledImageUpdateAfterBind(true)
            .setDescriptorBindingStorageImageUpdateAfterBind(true);
    dynamic_rendering_features.setPNext(&descriptor_indexing_features);
    auto device_feature2  = physical_device.getFeatures2();
    device_feature2.pNext = &dynamic_rendering_features;
    auto device_info      = vk::DeviceCreateInfo()
                           .setQueueCreateInfos(queue_info)
                           .setPNext(&device_feature2)
                           .setPEnabledExtensionNames(device_extensions);
    device           = physical_device.createDevice(device_info);
    device.allocator = VmaAllocator();
    VmaAllocatorCreateInfo allocator_info = {};
    allocator_info.physicalDevice         = physical_device;
    allocator_info.device                 = device;
    allocator_info.instance               = instance.instance;
    vmaCreateAllocator(&allocator_info, &device.allocator);

    return device;
}
EPIX_API Device::operator bool() const { return vk::Device::operator bool(); }
EPIX_API void Device::operator=(const vk::Device& device) {
    vk::Device::operator=(device);
}
EPIX_API void Device::destroy() {
    vmaDestroyAllocator(allocator);
    vk::Device::destroy();
}

EPIX_API vk::Image& Image::operator*() { return image; }
EPIX_API Image::operator bool() const { return image; }
EPIX_API Image Image::create(
    Device& device,
    vk::ImageCreateInfo& create_info,
    AllocationCreateInfo& alloc_info
) {
    Image image;
    vmaCreateImage(
        device.allocator, reinterpret_cast<VkImageCreateInfo*>(&create_info),
        reinterpret_cast<VmaAllocationCreateInfo*>(&alloc_info),
        reinterpret_cast<VkImage*>(&image.image), &image.allocation, nullptr
    );
    return image;
}
EPIX_API void Image::destroy() {
    if (!device || !allocation) return;
    vmaDestroyImage(device.allocator, image, allocation);
}
EPIX_API vk::SubresourceLayout Image::get_subresource_layout(
    const vk::ImageSubresource& subresource
) {
    return device.getImageSubresourceLayout(image, subresource);
}
EPIX_API vk::Buffer& Buffer::operator*() { return buffer; }
EPIX_API Buffer::operator bool() const { return buffer; }
EPIX_API Buffer Buffer::create(
    Device& device,
    vk::BufferCreateInfo& create_info,
    AllocationCreateInfo& alloc_info
) {
    Buffer buffer;
    vmaCreateBuffer(
        device.allocator, reinterpret_cast<VkBufferCreateInfo*>(&create_info),
        reinterpret_cast<VmaAllocationCreateInfo*>(&alloc_info),
        reinterpret_cast<VkBuffer*>(&buffer.buffer), &buffer.allocation, nullptr
    );
    return buffer;
}
EPIX_API Buffer Buffer::create_device(
    Device& device, uint64_t size, vk::BufferUsageFlags usage
) {
    return create(
        device,
        vk::BufferCreateInfo().setSize(size).setUsage(usage).setSharingMode(
            vk::SharingMode::eExclusive
        ),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .setFlags(
                VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            )
    );
}
EPIX_API Buffer Buffer::create_device_dedicated(
    Device& device, uint64_t size, vk::BufferUsageFlags usage
) {
    return create(
        device,
        vk::BufferCreateInfo().setSize(size).setUsage(usage).setSharingMode(
            vk::SharingMode::eExclusive
        ),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)
    );
}
EPIX_API Buffer
Buffer::create_host(Device& device, uint64_t size, vk::BufferUsageFlags usage) {
    return create(
        device,
        vk::BufferCreateInfo().setSize(size).setUsage(usage).setSharingMode(
            vk::SharingMode::eExclusive
        ),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
            .setFlags(
                VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            )
    );
}
EPIX_API void Buffer::destroy() {
    if (!device || !allocation) return;
    vmaDestroyBuffer(device.allocator, buffer, allocation);
}
EPIX_API void* Buffer::map() {
    void* data;
    vmaMapMemory(device.allocator, allocation, &data);
    return data;
}
EPIX_API void Buffer::unmap() { vmaUnmapMemory(device.allocator, allocation); }
EPIX_API AllocationCreateInfo::AllocationCreateInfo() { create_info = {}; }
EPIX_API AllocationCreateInfo::AllocationCreateInfo(
    const VmaMemoryUsage& usage, const VmaAllocationCreateFlags& flags
) {
    create_info.usage = usage;
    create_info.flags = flags;
}

EPIX_API AllocationCreateInfo::operator VmaAllocationCreateInfo() const {
    return create_info;
}
EPIX_API AllocationCreateInfo& AllocationCreateInfo::setUsage(
    VmaMemoryUsage usage
) {
    create_info.usage = usage;
    return *this;
}
EPIX_API AllocationCreateInfo& AllocationCreateInfo::setFlags(
    VmaAllocationCreateFlags flags
) {
    create_info.flags = flags;
    return *this;
}
EPIX_API AllocationCreateInfo& AllocationCreateInfo::setRequiredFlags(
    VkMemoryPropertyFlags flags
) {
    create_info.requiredFlags = flags;
    return *this;
}
EPIX_API AllocationCreateInfo& AllocationCreateInfo::setPreferredFlags(
    VkMemoryPropertyFlags flags
) {
    create_info.preferredFlags = flags;
    return *this;
}
EPIX_API AllocationCreateInfo& AllocationCreateInfo::setMemoryTypeBits(
    uint32_t bits
) {
    create_info.memoryTypeBits = bits;
    return *this;
}
EPIX_API AllocationCreateInfo& AllocationCreateInfo::setPool(VmaPool pool) {
    create_info.pool = pool;
    return *this;
}
EPIX_API AllocationCreateInfo& AllocationCreateInfo::setPUserData(void* data) {
    create_info.pUserData = data;
    return *this;
}
EPIX_API AllocationCreateInfo& AllocationCreateInfo::setPriority(float priority
) {
    create_info.priority = priority;
    return *this;
}
EPIX_API VmaAllocationCreateInfo& AllocationCreateInfo::operator*() {
    return create_info;
}

EPIX_API vk::SurfaceKHR& Surface::operator*() { return surface; }
EPIX_API Surface::operator bool() const { return surface; }
EPIX_API Surface Surface::create(Instance& instance, GLFWwindow* window) {
    Surface surface;
    if (glfwCreateWindowSurface(
            instance.instance, window, nullptr,
            reinterpret_cast<VkSurfaceKHR*>(&surface.surface)
        ) != VK_SUCCESS) {
        instance.logger->error("Failed to create window surface");
        throw std::runtime_error("Failed to create window surface");
    }
    surface.instance = instance;
    return surface;
}
EPIX_API void Surface::destroy() {
    instance.instance.destroySurfaceKHR(surface);
}
EPIX_API Swapchain
Swapchain::create(Device& device, Surface& surface, bool vsync) {
    Swapchain swapchain;
    swapchain.device  = device;
    swapchain.surface = surface;
    swapchain.others  = std::make_shared<Others>();
    auto capabilities =
        device.physical_device.getSurfaceCapabilitiesKHR(surface.surface);
    auto formats = device.physical_device.getSurfaceFormatsKHR(surface.surface);
    auto present_modes =
        device.physical_device.getSurfacePresentModesKHR(surface.surface);
    auto srgb_iter =
        std::find_if(formats.begin(), formats.end(), [](auto& format) {
            return format.format == vk::Format::eR8G8B8A8Srgb;
        });
    if (srgb_iter == formats.end()) {
        device.instance.logger->error("No sRGB format found");
        throw std::runtime_error("No sRGB format found");
    }
    swapchain.surface_format = *srgb_iter;
    if (capabilities.currentExtent.width == UINT32_MAX) {
        swapchain.others->extent = vk::Extent2D{800, 600};
    } else {
        swapchain.others->extent = capabilities.currentExtent;
    }
    if (vsync) {
        swapchain.present_mode = vk::PresentModeKHR::eFifo;
    } else {
        auto mode_iter = std::find(
            present_modes.begin(), present_modes.end(),
            vk::PresentModeKHR::eMailbox
        );
        if (mode_iter == present_modes.end()) {
            mode_iter = std::find(
                present_modes.begin(), present_modes.end(),
                vk::PresentModeKHR::eImmediate
            );
            surface.instance.logger->warn(
                "Mailbox present mode not supported, using immediate"
            );
        }
        if (mode_iter == present_modes.end()) {
            mode_iter = std::find(
                present_modes.begin(), present_modes.end(),
                vk::PresentModeKHR::eFifo
            );
            surface.instance.logger->warn(
                "Immediate present mode not supported, using FIFO"
            );
        }
        swapchain.present_mode = *mode_iter;
    }
    uint32_t image_count = std::clamp(
        capabilities.minImageCount + 1, capabilities.minImageCount,
        capabilities.maxImageCount
    );
    auto create_info =
        vk::SwapchainCreateInfoKHR()
            .setSurface(surface.surface)
            .setMinImageCount(image_count)
            .setImageFormat(swapchain.surface_format.format)
            .setImageColorSpace(swapchain.surface_format.colorSpace)
            .setImageExtent(swapchain.others->extent)
            .setImageArrayLayers(1)
            .setImageUsage(
                vk::ImageUsageFlagBits::eColorAttachment |
                vk::ImageUsageFlagBits::eTransferDst |
                vk::ImageUsageFlagBits::eTransferSrc |
                vk::ImageUsageFlagBits::eStorage |
                vk::ImageUsageFlagBits::eSampled |
                vk::ImageUsageFlagBits::eInputAttachment
            )
            .setImageSharingMode(vk::SharingMode::eExclusive)
            .setQueueFamilyIndices(device.queue_family_index)
            .setPreTransform(capabilities.currentTransform)
            .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
            .setPresentMode(swapchain.present_mode)
            .setClipped(true);
    swapchain.others->swapchain = device.createSwapchainKHR(create_info);
    swapchain.others->images =
        device.getSwapchainImagesKHR(swapchain.others->swapchain);
    swapchain.others->image_views.resize(swapchain.others->images.size());
    for (int i = 0; i < swapchain.others->images.size(); i += 1) {
        swapchain.others->image_views[i] = device.createImageView(
            vk::ImageViewCreateInfo()
                .setImage(swapchain.others->images[i])
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(swapchain.surface_format.format)
                .setComponents(vk::ComponentMapping()
                                   .setR(vk::ComponentSwizzle::eIdentity)
                                   .setG(vk::ComponentSwizzle::eIdentity)
                                   .setB(vk::ComponentSwizzle::eIdentity)
                                   .setA(vk::ComponentSwizzle::eIdentity))
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
    for (int i = 0; i < 2; i++) {
        swapchain.in_flight_fence[i] = device.createFence(
            vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled)
        );
    }
    return swapchain;
}
EPIX_API void Swapchain::destroy() {
    for (int i = 0; i < 2; i++) {
        device.destroyFence(in_flight_fence[i]);
    }
    for (auto& image_view : others->image_views) {
        device.destroyImageView(image_view);
    }
    device.destroySwapchainKHR(others->swapchain);
}
EPIX_API void Swapchain::recreate() {
    auto capabilities =
        device.physical_device.getSurfaceCapabilitiesKHR(surface.surface);
    if (others->extent == capabilities.currentExtent) {
        return;
    }
    for (auto& image_view : others->image_views) {
        device.destroyImageView(image_view);
    }
    device.destroySwapchainKHR(others->swapchain);
    if (capabilities.currentExtent.width == UINT32_MAX) {
        others->extent = vk::Extent2D{800, 600};
    } else {
        others->extent = capabilities.currentExtent;
    }
    uint32_t image_count = std::clamp(
        capabilities.minImageCount + 1, capabilities.minImageCount,
        capabilities.maxImageCount
    );
    auto create_info =
        vk::SwapchainCreateInfoKHR()
            .setSurface(surface.surface)
            .setMinImageCount(image_count)
            .setImageFormat(surface_format.format)
            .setImageColorSpace(surface_format.colorSpace)
            .setImageExtent(others->extent)
            .setImageArrayLayers(1)
            .setImageUsage(
                vk::ImageUsageFlagBits::eColorAttachment |
                vk::ImageUsageFlagBits::eTransferDst |
                vk::ImageUsageFlagBits::eTransferSrc |
                vk::ImageUsageFlagBits::eStorage |
                vk::ImageUsageFlagBits::eSampled |
                vk::ImageUsageFlagBits::eInputAttachment
            )
            .setImageSharingMode(vk::SharingMode::eExclusive)
            .setQueueFamilyIndices(device.queue_family_index)
            .setPreTransform(capabilities.currentTransform)
            .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
            .setPresentMode(present_mode)
            .setClipped(true);
    others->swapchain = device.createSwapchainKHR(create_info);
    others->images    = device.getSwapchainImagesKHR(others->swapchain);
    others->image_views.resize(others->images.size());
    for (int i = 0; i < others->images.size(); i += 1) {
        others->image_views[i] = device.createImageView(
            vk::ImageViewCreateInfo()
                .setImage(others->images[i])
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(surface_format.format)
                .setComponents(vk::ComponentMapping()
                                   .setR(vk::ComponentSwizzle::eIdentity)
                                   .setG(vk::ComponentSwizzle::eIdentity)
                                   .setB(vk::ComponentSwizzle::eIdentity)
                                   .setA(vk::ComponentSwizzle::eIdentity))
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
}
EPIX_API Image Swapchain::next_image() {
    others->current_frame = (others->current_frame + 1) % 2;
    device.waitForFences(
        in_flight_fence[others->current_frame], VK_TRUE, UINT64_MAX
    );
    device.resetFences(in_flight_fence[others->current_frame]);
    others->image_index = device
                              .acquireNextImageKHR(
                                  others->swapchain, UINT64_MAX, nullptr,
                                  in_flight_fence[others->current_frame]
                              )
                              .value;
    return others->images[others->image_index];
}
EPIX_API Image Swapchain::current_image() const {
    return others->images[others->image_index];
}
EPIX_API ImageView Swapchain::current_image_view() const {
    return others->image_views[others->image_index];
}
EPIX_API vk::Fence Swapchain::fence() const {
    return in_flight_fence[others->current_frame];
}
}  // namespace epix::render::vulkan2::backend

namespace epix::render::vulkan2 {
EPIX_API RenderVKPlugin& RenderVKPlugin::set_debug_callback(bool debug) {
    debug_callback = debug;
    return *this;
}
EPIX_API void RenderVKPlugin::build(epix::App& app) {
    auto window_plugin = app.get_plugin<window::WindowPlugin>();
    window_plugin->primary_desc().set_hints(
        {{GLFW_RESIZABLE, GLFW_TRUE}, {GLFW_CLIENT_API, GLFW_NO_API}}
    );
    app.add_system(PreStartup, systems::create_context)
        .in_set(window::WindowStartUpSets::after_window_creation);
    app.add_system(Extraction, systems::extract_context);
    app.add_system(
           Prepare, systems::recreate_swap_chain, systems::get_next_image
    )
        .chain();
    app.add_system(
           PostRender, systems::present_frame, systems::clear_extracted_context
    )
        .chain();
    app.add_system(PostExit, systems::destroy_context);
}
}  // namespace epix::render::vulkan2