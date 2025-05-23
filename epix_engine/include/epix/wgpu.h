#pragma once

#include <webgpu/webgpu.hpp>
#include <epix/common.h>

struct GLFWwindow;

namespace epix::webgpu::utils {
    EPIX_API WGPUSurface create_surface(WGPUInstance instance, GLFWwindow* window);
}