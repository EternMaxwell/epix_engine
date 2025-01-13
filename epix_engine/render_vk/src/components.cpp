#include "epix/render_vk.h"

using namespace epix;
using namespace epix::prelude;
using namespace epix::window;
using namespace epix::render_vk::components;

static VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData,
    void* pUserData
) {
#if !defined(NDEBUG)
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
#endif

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
    std::shared_ptr<spdlog::logger> logger
) {
    Instance instance;
    instance.logger = logger;
    auto app_info   = vk::ApplicationInfo()
                        .setPApplicationName(app_name)
                        .setApplicationVersion(app_version)
                        .setPEngineName("Pixel Engine")
                        .setEngineVersion(VK_MAKE_VERSION(0, 1, 0))
                        .setApiVersion(VK_API_VERSION_1_3);
    std::vector<char const*> layers = {
#if !defined(NDEBUG)
        "VK_LAYER_KHRONOS_validation"
#endif
    };
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
#if !defined(NDEBUG)
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
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)glfwGetInstanceProcAddress(
        instance.instance, "vkCreateDebugUtilsMessengerEXT"
    );
    if (!func) {
        logger->error("Failed to load function vkCreateDebugUtilsMessengerEXT");
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
        reinterpret_cast<VkDebugUtilsMessengerEXT*>(&instance.debug_messenger)
    );
#endif
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

EPIX_API Instance::operator vk::Instance() const { return instance; }
EPIX_API Instance::operator VkInstance() const { return instance; }
EPIX_API bool Instance::operator!() const { return !instance; }
EPIX_API vk::Instance* Instance::operator->() { return &instance; }
EPIX_API vk::Instance& Instance::operator*() { return instance; }

EPIX_API PhysicalDevice PhysicalDevice::create(Instance instance) {
    PhysicalDevice physical_device;
    auto physical_devices = instance->enumeratePhysicalDevices();
    if (physical_devices.size() == 0) {
        instance.logger->error("No physical devices found");
        throw std::runtime_error("No physical devices found");
    }
    physical_device.physical_device = physical_devices[0];
    return physical_device;
}

EPIX_API PhysicalDevice::operator vk::PhysicalDevice() const {
    return physical_device;
}
EPIX_API PhysicalDevice::operator VkPhysicalDevice() const {
    return physical_device;
}
EPIX_API bool PhysicalDevice::operator!() const { return !physical_device; }
EPIX_API vk::PhysicalDevice* PhysicalDevice::operator->() {
    return &physical_device;
}
EPIX_API vk::PhysicalDevice& PhysicalDevice::operator*() {
    return physical_device;
}

EPIX_API Device Device::create(
    Instance& instance,
    PhysicalDevice& physical_device,
    vk::QueueFlags queue_flags
) {
    Device device;
    float queue_priority        = 1.0f;
    uint32_t queue_family_index = 0;
    for (auto& queue_family_property :
         physical_device->getQueueFamilyProperties()) {
        if (queue_family_property.queueFlags & queue_flags) {
            break;
        }
        queue_family_index++;
    }
    if (queue_family_index ==
        physical_device->getQueueFamilyProperties().size()) {
        instance.logger->error("No queue family with graphics support");
        throw std::runtime_error("No queue family with graphics support");
    }
    device.queue_family_index = queue_family_index;
    auto queue_create_info    = vk::DeviceQueueCreateInfo()
                                 .setQueueFamilyIndex(queue_family_index)
                                 .setQueueCount(1)
                                 .setPQueuePriorities(&queue_priority);
    std::vector<char const*> required_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
    };
    auto extensions = physical_device->enumerateDeviceExtensionProperties();
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
    auto device_feature2  = physical_device->getFeatures2();
    device_feature2.pNext = &dynamic_rendering_features;
    auto device_info      = vk::DeviceCreateInfo()
                           .setQueueCreateInfos(queue_create_info)
                           .setPNext(&device_feature2)
                           .setPEnabledExtensionNames(device_extensions);
    device.device = physical_device->createDevice(device_info);
    VmaAllocatorCreateInfo allocator_info = {};
    allocator_info.physicalDevice         = physical_device;
    allocator_info.device                 = device.device;
    allocator_info.instance               = instance.instance;
    vmaCreateAllocator(&allocator_info, &device.allocator);
    return device;
}

EPIX_API void Device::destroy() {
    vmaDestroyAllocator(allocator);
    device.destroy();
}

EPIX_API Device::operator vk::Device() const { return device; }
EPIX_API Device::operator VkDevice() const { return device; }
EPIX_API bool Device::operator!() const { return !device; }
EPIX_API vk::Device* Device::operator->() { return &device; }
EPIX_API vk::Device& Device::operator*() { return device; }

EPIX_API Queue Queue::create(Device& device) {
    Queue queue;
    queue.queue = device->getQueue(device.queue_family_index, 0);
    return queue;
}

EPIX_API Queue::operator vk::Queue() const { return queue; }
EPIX_API Queue::operator VkQueue() const { return queue; }
EPIX_API bool Queue::operator!() const { return !queue; }
EPIX_API vk::Queue* Queue::operator->() { return &queue; }
EPIX_API vk::Queue& Queue::operator*() { return queue; }

EPIX_API CommandPool CommandPool::create(Device& device) {
    auto command_pool_info =
        vk::CommandPoolCreateInfo()
            .setQueueFamilyIndex(device.queue_family_index)
            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    CommandPool command_pool;
    command_pool.command_pool = device->createCommandPool(command_pool_info);
    return command_pool;
}

EPIX_API void CommandPool::destroy(Device device) {
    device->destroyCommandPool(command_pool);
}
EPIX_API CommandPool::operator vk::CommandPool() const { return command_pool; }
EPIX_API CommandPool::operator VkCommandPool() const { return command_pool; }
EPIX_API bool CommandPool::operator!() const { return !command_pool; }
EPIX_API vk::CommandPool* CommandPool::operator->() { return &command_pool; }
EPIX_API vk::CommandPool& CommandPool::operator*() { return command_pool; }

EPIX_API CommandBuffer
CommandBuffer::allocate_primary(Device& device, CommandPool& command_pool) {
    auto command_buffer = device->allocateCommandBuffers(
        vk::CommandBufferAllocateInfo()
            .setCommandPool(command_pool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1)
    )[0];
    return CommandBuffer{command_buffer};
}
EPIX_API CommandBuffer
CommandBuffer::allocate_secondary(Device& device, CommandPool& command_pool) {
    auto command_buffer = device->allocateCommandBuffers(
        vk::CommandBufferAllocateInfo()
            .setCommandPool(command_pool)
            .setLevel(vk::CommandBufferLevel::eSecondary)
            .setCommandBufferCount(1)
    )[0];
    return CommandBuffer{command_buffer};
}
EPIX_API std::vector<CommandBuffer> CommandBuffer::allocate_primary(
    Device& device, CommandPool& command_pool, uint32_t count
) {
    auto command_buffers = device->allocateCommandBuffers(
        vk::CommandBufferAllocateInfo()
            .setCommandPool(command_pool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(count)
    );
    std::vector<CommandBuffer> buffers;
    for (auto& command_buffer : command_buffers) {
        buffers.push_back(CommandBuffer{command_buffer});
    }
    return buffers;
}
EPIX_API std::vector<CommandBuffer> CommandBuffer::allocate_secondary(
    Device& device, CommandPool& command_pool, uint32_t count
) {
    auto command_buffers = device->allocateCommandBuffers(
        vk::CommandBufferAllocateInfo()
            .setCommandPool(command_pool)
            .setLevel(vk::CommandBufferLevel::eSecondary)
            .setCommandBufferCount(count)
    );
    std::vector<CommandBuffer> buffers;
    for (auto& command_buffer : command_buffers) {
        buffers.push_back(CommandBuffer{command_buffer});
    }
    return buffers;
}

EPIX_API void CommandBuffer::free(Device& device, CommandPool& command_pool) {
    device->freeCommandBuffers(command_pool, command_buffer);
}
EPIX_API CommandBuffer::operator vk::CommandBuffer() const {
    return command_buffer;
}
EPIX_API CommandBuffer::operator VkCommandBuffer() const {
    return command_buffer;
}
EPIX_API bool CommandBuffer::operator!() const { return !command_buffer; }
EPIX_API vk::CommandBuffer* CommandBuffer::operator->() {
    return &command_buffer;
}
EPIX_API vk::CommandBuffer& CommandBuffer::operator*() {
    return command_buffer;
}

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

EPIX_API Buffer Buffer::create(
    Device& device,
    vk::BufferCreateInfo create_info,
    AllocationCreateInfo alloc_info
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
            .setFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT)
    );
}

EPIX_API void Buffer::destroy(Device& device) {
    vmaDestroyBuffer(device.allocator, buffer, allocation);
}
EPIX_API void* Buffer::map(Device& device) {
    void* data;
    vmaMapMemory(device.allocator, allocation, &data);
    return data;
}
EPIX_API void Buffer::unmap(Device& device) {
    vmaUnmapMemory(device.allocator, allocation);
}
EPIX_API Buffer::operator vk::Buffer() const { return buffer; }
EPIX_API Buffer::operator VkBuffer() const { return buffer; }
EPIX_API bool Buffer::operator!() const { return !buffer; }
EPIX_API vk::Buffer* Buffer::operator->() { return &buffer; }
EPIX_API vk::Buffer& Buffer::operator*() { return buffer; }

EPIX_API Image Image::create(
    Device& device,
    vk::ImageCreateInfo create_info,
    AllocationCreateInfo alloc_info
) {
    Image image;
    vmaCreateImage(
        device.allocator, reinterpret_cast<VkImageCreateInfo*>(&create_info),
        reinterpret_cast<VmaAllocationCreateInfo*>(&alloc_info),
        reinterpret_cast<VkImage*>(&image.image), &image.allocation, nullptr
    );
    return image;
}
EPIX_API Image Image::create_1d(
    Device& device,
    uint32_t width,
    uint32_t levels,
    uint32_t layers,
    vk::Format format,
    vk::ImageUsageFlags usage
) {
    return create(
        device,
        vk::ImageCreateInfo()
            .setImageType(vk::ImageType::e1D)
            .setExtent(vk::Extent3D(width, 1, 1))
            .setMipLevels(levels)
            .setArrayLayers(layers)
            .setFormat(format)
            .setUsage(usage)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setSharingMode(vk::SharingMode::eExclusive),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)
    );
}
EPIX_API Image Image::create_2d(
    Device& device,
    uint32_t width,
    uint32_t height,
    uint32_t levels,
    uint32_t layers,
    vk::Format format,
    vk::ImageUsageFlags usage
) {
    return create(
        device,
        vk::ImageCreateInfo()
            .setImageType(vk::ImageType::e2D)
            .setExtent(vk::Extent3D(width, height, 1))
            .setMipLevels(levels)
            .setArrayLayers(layers)
            .setFormat(format)
            .setUsage(usage)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setSharingMode(vk::SharingMode::eExclusive),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)
    );
}
EPIX_API Image Image::create_3d(
    Device& device,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    uint32_t levels,
    uint32_t layers,
    vk::Format format,
    vk::ImageUsageFlags usage
) {
    return create(
        device,
        vk::ImageCreateInfo()
            .setImageType(vk::ImageType::e3D)
            .setExtent(vk::Extent3D(width, height, depth))
            .setMipLevels(levels)
            .setArrayLayers(layers)
            .setFormat(format)
            .setUsage(usage)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setSharingMode(vk::SharingMode::eExclusive),
        AllocationCreateInfo()
            .setUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
            .setFlags(VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)
    );
}

EPIX_API void Image::destroy(Device& device) {
    vmaDestroyImage(device.allocator, image, allocation);
}
EPIX_API Image::operator vk::Image() const { return image; }
EPIX_API Image::operator VkImage() const { return image; }
EPIX_API bool Image::operator!() const { return !image; }
EPIX_API vk::Image* Image::operator->() { return &image; }
EPIX_API vk::Image& Image::operator*() { return image; }

EPIX_API ImageView
ImageView::create(Device& device, vk::ImageViewCreateInfo create_info) {
    ImageView image_view;
    image_view.image_view = device->createImageView(create_info);
    return image_view;
}
EPIX_API ImageView ImageView::create_1d(
    Device& device,
    Image& image,
    vk::Format format,
    vk::ImageAspectFlags aspect_flags,
    uint32_t base_level,
    uint32_t level_count,
    uint32_t base_layer,
    uint32_t layer_count
) {
    return create(
        device,
        vk::ImageViewCreateInfo()
            .setImage(image)
            .setViewType(vk::ImageViewType::e1D)
            .setFormat(format)
            .setSubresourceRange(vk::ImageSubresourceRange(
                aspect_flags, base_level, level_count, base_layer, layer_count
            ))
    );
}
EPIX_API ImageView ImageView::create_1d(
    Device& device,
    Image& image,
    vk::Format format,
    vk::ImageAspectFlags aspect_flags
) {
    return create_1d(device, image, format, aspect_flags, 0, 1, 0, 1);
}
EPIX_API ImageView ImageView::create_2d(
    Device& device,
    Image& image,
    vk::Format format,
    vk::ImageAspectFlags aspect_flags,
    uint32_t base_level,
    uint32_t level_count,
    uint32_t base_layer,
    uint32_t layer_count
) {
    return create(
        device,
        vk::ImageViewCreateInfo()
            .setImage(image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(format)
            .setSubresourceRange(vk::ImageSubresourceRange(
                aspect_flags, base_level, level_count, base_layer, layer_count
            ))
    );
}
EPIX_API ImageView ImageView::create_2d(
    Device& device,
    Image& image,
    vk::Format format,
    vk::ImageAspectFlags aspect_flags
) {
    return create_2d(device, image, format, aspect_flags, 0, 1, 0, 1);
}
EPIX_API ImageView ImageView::create_3d(
    Device& device,
    Image& image,
    vk::Format format,
    vk::ImageAspectFlags aspect_flags,
    uint32_t base_level,
    uint32_t level_count,
    uint32_t base_layer,
    uint32_t layer_count
) {
    return create(
        device,
        vk::ImageViewCreateInfo()
            .setImage(image)
            .setViewType(vk::ImageViewType::e3D)
            .setFormat(format)
            .setSubresourceRange(vk::ImageSubresourceRange(
                aspect_flags, base_level, level_count, base_layer, layer_count
            ))
    );
}
EPIX_API ImageView ImageView::create_3d(
    Device& device,
    Image& image,
    vk::Format format,
    vk::ImageAspectFlags aspect_flags
) {
    return create_3d(device, image, format, aspect_flags, 0, 1, 0, 1);
}

EPIX_API void ImageView::destroy(Device& device) {
    device->destroyImageView(image_view);
}
EPIX_API ImageView::operator vk::ImageView() const { return image_view; }
EPIX_API ImageView::operator VkImageView() const { return image_view; }
EPIX_API bool ImageView::operator!() const { return !image_view; }
EPIX_API vk::ImageView* ImageView::operator->() { return &image_view; }
EPIX_API vk::ImageView& ImageView::operator*() { return image_view; }

EPIX_API Sampler
Sampler::create(Device& device, vk::SamplerCreateInfo create_info) {
    Sampler sampler;
    sampler.sampler = device->createSampler(create_info);
    return sampler;
}
EPIX_API Sampler Sampler::create(
    Device& device,
    vk::Filter min_filter,
    vk::Filter mag_filter,
    vk::SamplerAddressMode u_address_mode,
    vk::SamplerAddressMode v_address_mode,
    vk::SamplerAddressMode w_address_mode,
    vk::BorderColor border_color
) {
    return create(
        device, vk::SamplerCreateInfo()
                    .setMagFilter(mag_filter)
                    .setMinFilter(min_filter)
                    .setAddressModeU(u_address_mode)
                    .setAddressModeV(v_address_mode)
                    .setAddressModeW(w_address_mode)
                    .setAnisotropyEnable(VK_TRUE)
                    .setBorderColor(border_color)
                    .setCompareEnable(VK_TRUE)
                    .setCompareOp(vk::CompareOp::eAlways)
                    .setMipmapMode(vk::SamplerMipmapMode::eLinear)
                    .setMipLodBias(0.0f)
                    .setMinLod(0.0f)
                    .setMaxLod(0.0f)
    );
}

EPIX_API void Sampler::destroy(Device& device) {
    device->destroySampler(sampler);
}
EPIX_API Sampler::operator vk::Sampler() const { return sampler; }
EPIX_API Sampler::operator VkSampler() const { return sampler; }
EPIX_API bool Sampler::operator!() const { return !sampler; }
EPIX_API vk::Sampler* Sampler::operator->() { return &sampler; }
EPIX_API vk::Sampler& Sampler::operator*() { return sampler; }

EPIX_API DescriptorSetLayout DescriptorSetLayout::create(
    Device& device, vk::DescriptorSetLayoutCreateInfo create_info
) {
    DescriptorSetLayout descriptor_set_layout;
    descriptor_set_layout.descriptor_set_layout =
        device->createDescriptorSetLayout(create_info);
    return descriptor_set_layout;
}
EPIX_API DescriptorSetLayout DescriptorSetLayout::create(
    Device& device, const std::vector<vk::DescriptorSetLayoutBinding>& bindings
) {
    return create(
        device, vk::DescriptorSetLayoutCreateInfo().setBindings(bindings)
    );
}

EPIX_API void DescriptorSetLayout::destroy(Device& device) {
    device->destroyDescriptorSetLayout(descriptor_set_layout);
}
EPIX_API DescriptorSetLayout::operator vk::DescriptorSetLayout() const {
    return descriptor_set_layout;
}
EPIX_API DescriptorSetLayout::operator VkDescriptorSetLayout() const {
    return descriptor_set_layout;
}
EPIX_API bool DescriptorSetLayout::operator!() const {
    return !descriptor_set_layout;
}
EPIX_API vk::DescriptorSetLayout* DescriptorSetLayout::operator->() {
    return &descriptor_set_layout;
}
EPIX_API vk::DescriptorSetLayout& DescriptorSetLayout::operator*() {
    return descriptor_set_layout;
}

EPIX_API PipelineLayout PipelineLayout::create(
    Device& device, vk::PipelineLayoutCreateInfo create_info
) {
    PipelineLayout pipeline_layout;
    pipeline_layout.pipeline_layout = device->createPipelineLayout(create_info);
    return pipeline_layout;
}
EPIX_API PipelineLayout PipelineLayout::create(
    Device& device, DescriptorSetLayout& descriptor_set_layout
) {
    return create(
        device,
        vk::PipelineLayoutCreateInfo().setSetLayouts(*descriptor_set_layout)
    );
}
EPIX_API PipelineLayout PipelineLayout::create(
    Device& device,
    DescriptorSetLayout& descriptor_set_layout,
    vk::PushConstantRange& push_constant_range
) {
    return create(
        device, vk::PipelineLayoutCreateInfo()
                    .setSetLayouts(*descriptor_set_layout)
                    .setPushConstantRanges(push_constant_range)
    );
}
EPIX_API PipelineLayout PipelineLayout::create(
    Device& device,
    std::vector<DescriptorSetLayout>& descriptor_set_layouts,
    std::vector<vk::PushConstantRange>& push_constant_ranges
) {
    std::vector<vk::DescriptorSetLayout> descriptor_set_layouts_vk;
    descriptor_set_layouts_vk.reserve(descriptor_set_layouts.size());
    for (auto& layout : descriptor_set_layouts) {
        descriptor_set_layouts_vk.push_back(*layout);
    }
    return create(
        device, vk::PipelineLayoutCreateInfo()
                    .setSetLayouts(descriptor_set_layouts_vk)
                    .setPushConstantRanges(push_constant_ranges)
    );
}

EPIX_API void PipelineLayout::destroy(Device& device) {
    device->destroyPipelineLayout(pipeline_layout);
}
EPIX_API PipelineLayout::operator vk::PipelineLayout() const {
    return pipeline_layout;
}
EPIX_API PipelineLayout::operator VkPipelineLayout() const {
    return pipeline_layout;
}
EPIX_API bool PipelineLayout::operator!() const { return !pipeline_layout; }
EPIX_API vk::PipelineLayout* PipelineLayout::operator->() {
    return &pipeline_layout;
}
EPIX_API vk::PipelineLayout& PipelineLayout::operator*() {
    return pipeline_layout;
}

EPIX_API Pipeline Pipeline::create(
    Device& device,
    vk::PipelineCache pipeline_cache,
    vk::GraphicsPipelineCreateInfo create_info
) {
    Pipeline pipeline;
    pipeline.pipeline =
        device->createGraphicsPipeline(pipeline_cache, create_info).value;
    return pipeline;
}
EPIX_API Pipeline
Pipeline::create(Device& device, vk::GraphicsPipelineCreateInfo create_info) {
    return create(device, vk::PipelineCache(), create_info);
}
EPIX_API Pipeline Pipeline::create(
    Device& device,
    vk::PipelineCache pipeline_cache,
    vk::ComputePipelineCreateInfo create_info
) {
    Pipeline pipeline;
    pipeline.pipeline =
        device->createComputePipeline(pipeline_cache, create_info).value;
    return pipeline;
}
EPIX_API Pipeline
Pipeline::create(Device& device, vk::ComputePipelineCreateInfo create_info) {
    return create(device, vk::PipelineCache(), create_info);
}

EPIX_API void Pipeline::destroy(Device& device) {
    device->destroyPipeline(pipeline);
}
EPIX_API Pipeline::operator vk::Pipeline() const { return pipeline; }
EPIX_API Pipeline::operator VkPipeline() const { return pipeline; }
EPIX_API bool Pipeline::operator!() const { return !pipeline; }
EPIX_API vk::Pipeline* Pipeline::operator->() { return &pipeline; }
EPIX_API vk::Pipeline& Pipeline::operator*() { return pipeline; }

EPIX_API DescriptorPool DescriptorPool::create(
    Device& device, vk::DescriptorPoolCreateInfo create_info
) {
    DescriptorPool descriptor_pool;
    descriptor_pool.descriptor_pool = device->createDescriptorPool(create_info);
    return descriptor_pool;
}

EPIX_API void DescriptorPool::destroy(Device& device) {
    device->destroyDescriptorPool(descriptor_pool);
}
EPIX_API DescriptorPool::operator vk::DescriptorPool() const {
    return descriptor_pool;
}
EPIX_API DescriptorPool::operator VkDescriptorPool() const {
    return descriptor_pool;
}
EPIX_API bool DescriptorPool::operator!() const { return !descriptor_pool; }
EPIX_API vk::DescriptorPool* DescriptorPool::operator->() {
    return &descriptor_pool;
}
EPIX_API vk::DescriptorPool& DescriptorPool::operator*() {
    return descriptor_pool;
}

EPIX_API DescriptorSet DescriptorSet::create(
    Device& device, vk::DescriptorSetAllocateInfo allocate_info
) {
    DescriptorSet descriptor_set;
    descriptor_set.descriptor_set =
        device->allocateDescriptorSets(allocate_info)[0];
    return descriptor_set;
}
EPIX_API DescriptorSet DescriptorSet::create(
    Device& device,
    DescriptorPool& descriptor_pool,
    vk::DescriptorSetLayout descriptor_set_layout
) {
    vk::DescriptorSetAllocateInfo allocate_info;
    allocate_info.setDescriptorPool(descriptor_pool)
        .setSetLayouts(descriptor_set_layout);
    return create(device, allocate_info);
}
EPIX_API std::vector<DescriptorSet> DescriptorSet::create(
    Device& device,
    DescriptorPool& descriptor_pool,
    std::vector<DescriptorSetLayout>& descriptor_set_layouts
) {
    std::vector<vk::DescriptorSetLayout> descriptor_set_layouts_vk;
    descriptor_set_layouts_vk.reserve(descriptor_set_layouts.size());
    for (auto& layout : descriptor_set_layouts) {
        descriptor_set_layouts_vk.push_back(*layout);
    }
    vk::DescriptorSetAllocateInfo allocate_info;
    allocate_info.setDescriptorPool(descriptor_pool)
        .setSetLayouts(descriptor_set_layouts_vk);
    auto descriptor_sets = device->allocateDescriptorSets(allocate_info);
    std::vector<DescriptorSet> sets;
    sets.reserve(descriptor_sets.size());
    for (auto& descriptor_set : descriptor_sets) {
        sets.push_back(DescriptorSet{descriptor_set});
    }
    return sets;
}

EPIX_API void DescriptorSet::destroy(
    Device& device, DescriptorPool& descriptor_pool
) {
    device->freeDescriptorSets(descriptor_pool, descriptor_set);
}
EPIX_API DescriptorSet::operator vk::DescriptorSet() const {
    return descriptor_set;
}
EPIX_API DescriptorSet::operator VkDescriptorSet() const {
    return descriptor_set;
}
EPIX_API bool DescriptorSet::operator!() const { return !descriptor_set; }
EPIX_API vk::DescriptorSet* DescriptorSet::operator->() {
    return &descriptor_set;
}
EPIX_API vk::DescriptorSet& DescriptorSet::operator*() {
    return descriptor_set;
}

EPIX_API Fence Fence::create(Device& device, vk::FenceCreateInfo create_info) {
    Fence fence;
    fence.fence = device->createFence(create_info);
    return fence;
}

EPIX_API void Fence::destroy(Device& device) { device->destroyFence(fence); }
EPIX_API Fence::operator vk::Fence() const { return fence; }
EPIX_API Fence::operator VkFence() const { return fence; }
EPIX_API bool Fence::operator!() const { return !fence; }
EPIX_API vk::Fence* Fence::operator->() { return &fence; }
EPIX_API vk::Fence& Fence::operator*() { return fence; }

EPIX_API Semaphore
Semaphore::create(Device& device, vk::SemaphoreCreateInfo create_info) {
    Semaphore semaphore;
    semaphore.semaphore = device->createSemaphore(create_info);
    return semaphore;
}

EPIX_API void Semaphore::destroy(Device& device) {
    device->destroySemaphore(semaphore);
}
EPIX_API Semaphore::operator vk::Semaphore() const { return semaphore; }
EPIX_API Semaphore::operator VkSemaphore() const { return semaphore; }
EPIX_API bool Semaphore::operator!() const { return !semaphore; }
EPIX_API vk::Semaphore* Semaphore::operator->() { return &semaphore; }
EPIX_API vk::Semaphore& Semaphore::operator*() { return semaphore; }

EPIX_API Surface Surface::create(Instance& instance, GLFWwindow* window) {
    Surface surface_component;
    if (glfwCreateWindowSurface(
            instance, window, nullptr,
            reinterpret_cast<VkSurfaceKHR*>(&surface_component.surface)
        ) != VK_SUCCESS) {
        spdlog::error("Failed to create window surface");
        throw std::runtime_error("Failed to create window surface");
    }
    return surface_component;
}

EPIX_API void Surface::destroy(Instance& instance) {
    instance->destroySurfaceKHR(surface);
}
EPIX_API Surface::operator vk::SurfaceKHR() const { return surface; }
EPIX_API Surface::operator VkSurfaceKHR() const { return surface; }
EPIX_API bool Surface::operator!() const { return !surface; }
EPIX_API vk::SurfaceKHR* Surface::operator->() { return &surface; }
EPIX_API vk::SurfaceKHR& Surface::operator*() { return surface; }

EPIX_API Swapchain Swapchain::create(
    PhysicalDevice& physical_device,
    Device& device,
    Surface& surface,
    bool vsync
) {
    Swapchain swapchain;
    auto surface_capabilities =
        physical_device->getSurfaceCapabilitiesKHR(surface);
    auto surface_formats = physical_device->getSurfaceFormatsKHR(surface);
    auto present_modes   = physical_device->getSurfacePresentModesKHR(surface);
    auto format_iter     = std::find_if(
        surface_formats.begin(), surface_formats.end(),
        [](const vk::SurfaceFormatKHR& format) {
            return format.format == vk::Format::eB8G8R8A8Srgb &&
                   format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        }
    );
    auto mode_iter = std::find(
        present_modes.begin(), present_modes.end(), vk::PresentModeKHR::eMailbox
    );
    if (format_iter == surface_formats.end()) {
        throw std::runtime_error("No suitable surface format");
    }
    swapchain.surface_format = *format_iter;
    swapchain.present_mode   = mode_iter == present_modes.end() || vsync
                                   ? vk::PresentModeKHR::eFifo
                                   : *mode_iter;
    swapchain.extent         = surface_capabilities.currentExtent;
    uint32_t image_count     = surface_capabilities.minImageCount + 1;
    image_count              = std::clamp(
        image_count, surface_capabilities.minImageCount,
        surface_capabilities.maxImageCount
    );
    vk::SwapchainCreateInfoKHR create_info;
    create_info.setSurface(surface)
        .setMinImageCount(image_count)
        .setImageFormat(swapchain.surface_format.format)
        .setImageColorSpace(swapchain.surface_format.colorSpace)
        .setImageExtent(swapchain.extent)
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
        .setQueueFamilyIndexCount(1)
        .setPQueueFamilyIndices(&device.queue_family_index)
        .setPreTransform(surface_capabilities.currentTransform)
        .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
        .setPresentMode(swapchain.present_mode)
        .setClipped(VK_TRUE);
    swapchain.swapchain = device->createSwapchainKHR(create_info);
    swapchain.images    = device->getSwapchainImagesKHR(swapchain.swapchain);
    // spdlog::info("Swapchain Image Count: {}", swapchain.images.size());
    swapchain.image_views.resize(swapchain.images.size());
    for (int i = 0; i < swapchain.images.size(); i++) {
        swapchain.image_views[i] = ImageView::create(
            device,
            vk::ImageViewCreateInfo()
                .setImage(swapchain.images[i])
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
        swapchain.in_flight_fence[i] =
            device->createFence(vk::FenceCreateInfo());
    }
    return swapchain;
}

EPIX_API void Swapchain::recreate(
    PhysicalDevice& physical_device, Device& device, Surface& surface
) {
    if (extent ==
        physical_device->getSurfaceCapabilitiesKHR(surface).currentExtent) {
        return;
    }
    ZoneScopedN("Vulkan recreate swap chain");
    device->waitIdle();
    for (auto& image_view : image_views) {
        image_view.destroy(device);
    }
    device->destroySwapchainKHR(swapchain);
    auto surface_capabilities =
        physical_device->getSurfaceCapabilitiesKHR(surface);
    extent               = surface_capabilities.currentExtent;
    uint32_t image_count = surface_capabilities.minImageCount + 1;
    image_count          = std::clamp(
        image_count, surface_capabilities.minImageCount,
        surface_capabilities.maxImageCount
    );
    vk::SwapchainCreateInfoKHR create_info;
    create_info.setSurface(surface)
        .setMinImageCount(image_count)
        .setImageFormat(surface_format.format)
        .setImageColorSpace(surface_format.colorSpace)
        .setImageExtent(extent)
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
        .setQueueFamilyIndexCount(1)
        .setPQueueFamilyIndices(&device.queue_family_index)
        .setPreTransform(surface_capabilities.currentTransform)
        .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
        .setPresentMode(present_mode)
        .setClipped(VK_TRUE);
    swapchain = device->createSwapchainKHR(create_info);
    images    = device->getSwapchainImagesKHR(swapchain);
    image_views.resize(images.size());
    for (int i = 0; i < images.size(); i += 1) {
        image_views[i] = ImageView::create(
            device,
            vk::ImageViewCreateInfo()
                .setImage(images[i])
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
EPIX_API void Swapchain::destroy(Device& device) {
    for (int i = 0; i < 2; i++) {
        device->destroyFence(in_flight_fence[i]);
    }
    for (auto& image_view : image_views) {
        image_view.destroy(device);
    }
    device->destroySwapchainKHR(swapchain);
}
EPIX_API Swapchain::operator vk::SwapchainKHR() const { return swapchain; }
EPIX_API Swapchain::operator VkSwapchainKHR() const { return swapchain; }
EPIX_API Swapchain::operator vk::SwapchainKHR&() { return swapchain; }
EPIX_API Image Swapchain::next_image(Device& device) {
    current_frame = (current_frame + 1) % 2;
    image_index =
        device
            ->acquireNextImageKHR(
                swapchain, UINT64_MAX, nullptr, in_flight_fence[current_frame]
            )
            .value;
    return Image{.image = images[image_index]};
}
EPIX_API Image Swapchain::current_image() const {
    return Image{.image = images[image_index]};
}
EPIX_API ImageView Swapchain::current_image_view() const {
    return image_views[image_index];
}
EPIX_API vk::Fence& Swapchain::fence() {
    return in_flight_fence[current_frame];
}
EPIX_API bool Swapchain::operator!() const { return !swapchain; }
EPIX_API vk::SwapchainKHR* Swapchain::operator->() { return &swapchain; }
EPIX_API vk::SwapchainKHR& Swapchain::operator*() { return swapchain; }

EPIX_API RenderPass
RenderPass::create(Device& device, vk::RenderPassCreateInfo create_info) {
    RenderPass render_pass;
    render_pass.render_pass = device->createRenderPass(create_info);
    return render_pass;
}

EPIX_API void RenderPass::destroy(Device& device) {
    device->destroyRenderPass(render_pass);
}
EPIX_API RenderPass::operator vk::RenderPass() const { return render_pass; }
EPIX_API RenderPass::operator VkRenderPass() const { return render_pass; }
EPIX_API bool RenderPass::operator!() const { return !render_pass; }
EPIX_API vk::RenderPass* RenderPass::operator->() { return &render_pass; }
EPIX_API vk::RenderPass& RenderPass::operator*() { return render_pass; }

EPIX_API Framebuffer
Framebuffer::create(Device& device, vk::FramebufferCreateInfo create_info) {
    Framebuffer framebuffer;
    framebuffer.framebuffer = device->createFramebuffer(create_info);
    return framebuffer;
}

EPIX_API void Framebuffer::destroy(Device& device) {
    device->destroyFramebuffer(framebuffer);
}
EPIX_API Framebuffer::operator vk::Framebuffer() const { return framebuffer; }
EPIX_API Framebuffer::operator VkFramebuffer() const { return framebuffer; }
EPIX_API bool Framebuffer::operator!() const { return !framebuffer; }
EPIX_API vk::Framebuffer* Framebuffer::operator->() { return &framebuffer; }
EPIX_API vk::Framebuffer& Framebuffer::operator*() { return framebuffer; }

EPIX_API ShaderModule ShaderModule::create(
    Device& device, const vk::ShaderModuleCreateInfo& create_info
) {
    ShaderModule shader_module;
    shader_module.shader_module = device->createShaderModule(create_info);
    return shader_module;
}

EPIX_API void ShaderModule::destroy(Device& device) {
    device->destroyShaderModule(shader_module);
}
EPIX_API ShaderModule::operator vk::ShaderModule() const {
    return shader_module;
}
EPIX_API ShaderModule::operator VkShaderModule() const { return shader_module; }
EPIX_API bool ShaderModule::operator!() const { return !shader_module; }
EPIX_API vk::ShaderModule* ShaderModule::operator->() { return &shader_module; }
EPIX_API vk::ShaderModule& ShaderModule::operator*() { return shader_module; }

EPIX_API PipelineCache PipelineCache::create(Device& device) {
    PipelineCache pipeline_cache;
    pipeline_cache.pipeline_cache = device->createPipelineCache({});
    return pipeline_cache;
}

EPIX_API void PipelineCache::destroy(Device& device) {
    device->destroyPipelineCache(pipeline_cache);
}
EPIX_API PipelineCache::operator vk::PipelineCache() const {
    return pipeline_cache;
}
EPIX_API PipelineCache::operator VkPipelineCache() const {
    return pipeline_cache;
}
EPIX_API bool PipelineCache::operator!() const { return !pipeline_cache; }
EPIX_API vk::PipelineCache* PipelineCache::operator->() {
    return &pipeline_cache;
}
EPIX_API vk::PipelineCache& PipelineCache::operator*() {
    return pipeline_cache;
}

EPIX_API RenderTarget& RenderTarget::set_extent(
    uint32_t width, uint32_t height
) {
    extent = vk::Extent2D(width, height);
    return *this;
}

EPIX_API RenderTarget& RenderTarget::add_color_image(
    Image& image, vk::Format format
) {
    color_attachments.emplace_back(image, ImageView{}, format);
    return *this;
}

EPIX_API RenderTarget& RenderTarget::add_color_image(
    Image& image, ImageView& image_view, vk::Format format
) {
    color_attachments.emplace_back(image, image_view, format);
    return *this;
}

EPIX_API RenderTarget& RenderTarget::add_color_image(
    Device& device, Image& image, vk::Format format
) {
    auto image_view = ImageView::create(
        device, vk::ImageViewCreateInfo()
                    .setImage(image)
                    .setViewType(vk::ImageViewType::e2D)
                    .setFormat(format)
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
    color_attachments.emplace_back(image, image_view, format);
    return *this;
}

EPIX_API RenderTarget& RenderTarget::set_depth_image(
    Image& image, vk::Format format
) {
    depth_attachment = {image, ImageView{}, format};
    return *this;
}

EPIX_API RenderTarget& RenderTarget::set_depth_image(
    Image& image, ImageView& image_view, vk::Format format
) {
    depth_attachment = {image, image_view, format};
    return *this;
}

EPIX_API RenderTarget& RenderTarget::set_depth_image(
    Device& device, Image& image, vk::Format format
) {
    auto image_view = ImageView::create(
        device, vk::ImageViewCreateInfo()
                    .setImage(image)
                    .setViewType(vk::ImageViewType::e2D)
                    .setFormat(format)
                    .setComponents(vk::ComponentMapping()
                                       .setR(vk::ComponentSwizzle::eIdentity)
                                       .setG(vk::ComponentSwizzle::eIdentity)
                                       .setB(vk::ComponentSwizzle::eIdentity)
                                       .setA(vk::ComponentSwizzle::eIdentity))
                    .setSubresourceRange(
                        vk::ImageSubresourceRange()
                            .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                            .setBaseMipLevel(0)
                            .setLevelCount(1)
                            .setBaseArrayLayer(0)
                            .setLayerCount(1)
                    )
    );
    depth_attachment = {image, image_view, format};
    return *this;
}

EPIX_API RenderTarget& RenderTarget::complete(Device& device) {
    for (auto&& [image, view, format] : color_attachments) {
        if (!view) {
            view = ImageView::create(
                device,
                vk::ImageViewCreateInfo()
                    .setImage(image)
                    .setViewType(vk::ImageViewType::e2D)
                    .setFormat(format)
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
    auto&& [image, view, format] = depth_attachment;
    if (!view) {
        view = ImageView::create(
            device,
            vk::ImageViewCreateInfo()
                .setImage(image)
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(format)
                .setComponents(vk::ComponentMapping()
                                   .setR(vk::ComponentSwizzle::eIdentity)
                                   .setG(vk::ComponentSwizzle::eIdentity)
                                   .setB(vk::ComponentSwizzle::eIdentity)
                                   .setA(vk::ComponentSwizzle::eIdentity))
                .setSubresourceRange(
                    vk::ImageSubresourceRange()
                        .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                        .setBaseMipLevel(0)
                        .setLevelCount(1)
                        .setBaseArrayLayer(0)
                        .setLayerCount(1)
                )
        );
    }
    return *this;
}