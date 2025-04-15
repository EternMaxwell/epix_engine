#pragma once

#include <epix/common.h>
#include <webgpu/webgpu.h>

struct GLFWwindow;

namespace epix::webgpu::utils {
EPIX_API WGPUSurface create_surface(WGPUInstance instance, GLFWwindow* window);
}