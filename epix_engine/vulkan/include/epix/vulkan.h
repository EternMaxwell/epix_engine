#include <epix/common.h>

#define VK_NO_PROTOTYPES
#include <volk.h>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#ifdef EPIX_SHARED
#define VULKAN_HPP_STORAGE_SHARED
#endif
#include <vulkan/vulkan.hpp>

#define VMA_CALL_PRE EPIX_API
#include <vk_mem_alloc.h>