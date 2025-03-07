#pragma once

#include <epix/window.h>

namespace epix::render::api {
enum class RenderAPI { Vulkan, OpenGL, DirectX, Metal };
class Context;
class Swapchain;
class GraphicPipeline;
class ComputePipeline;
class PipelineLayout;
class DescriptorSetLayout;
class DescriptorSet;
class Shader;
class Buffer;
class Image;
class ImageView;
class CommandBuffer;
}  // namespace epix::render::api

namespace epix::render::api {
class Context {
    virtual void create()                                       = 0;
    virtual Swapchain* create_swapchain(window::Window& window) = 0;
    virtual GraphicPipeline* new_graphic_pipeline()          = 0;
    virtual ComputePipeline* new_compute_pipeline()          = 0;
    virtual ~Context()                                          = 0;
};
class Swapchain {
    virtual void swap()                = 0;
    virtual void set_vsync(bool vsync) = 0;
    virtual ~Swapchain()               = 0;
};
class GraphicPipeline {
    virtual ~GraphicPipeline() = 0;
};
class ComputePipeline {
    virtual ~ComputePipeline() = 0;
};
class Shader {
    virtual void glsl(void* source)  = 0;
    virtual void hlsl(void* source)  = 0;
    virtual void spirv(void* source) = 0;
    virtual ~Shader()                = 0;
};
}  // namespace epix::render::api