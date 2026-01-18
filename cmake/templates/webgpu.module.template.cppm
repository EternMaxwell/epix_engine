// Generated C++20 module interface for WebGPU
// This module provides RAII-by-default WebGPU bindings
// Based on WebGPU-Cpp wrapper with RAII as the default

module;

// Include the generated headers in the global module fragment
#include <webgpu/webgpu.h>

#define WEBGPU_CPP_IMPLEMENTATION
#include "webgpu.hpp"
#include "webgpu-raii.hpp"

export module webgpu;

// Export the wgpu namespace with RAII types as the default
export namespace wgpu {
    // Re-export all types from the base wgpu namespace
    using namespace ::wgpu;
    
    // Import RAII wrappers and make them the default
    // This allows code to use wgpu::Device (RAII) instead of wgpu::raii::Device
    using ::wgpu::raii::Adapter;
    using ::wgpu::raii::BindGroup;
    using ::wgpu::raii::BindGroupLayout;
    using ::wgpu::raii::Buffer;
    using ::wgpu::raii::CommandBuffer;
    using ::wgpu::raii::CommandEncoder;
    using ::wgpu::raii::ComputePassEncoder;
    using ::wgpu::raii::ComputePipeline;
    using ::wgpu::raii::Device;
    using ::wgpu::raii::Instance;
    using ::wgpu::raii::PipelineLayout;
    using ::wgpu::raii::QuerySet;
    using ::wgpu::raii::Queue;
    using ::wgpu::raii::RenderBundle;
    using ::wgpu::raii::RenderBundleEncoder;
    using ::wgpu::raii::RenderPassEncoder;
    using ::wgpu::raii::RenderPipeline;
    using ::wgpu::raii::Sampler;
    using ::wgpu::raii::ShaderModule;
    using ::wgpu::raii::Surface;
    using ::wgpu::raii::SwapChain;
    using ::wgpu::raii::Texture;
    using ::wgpu::raii::TextureView;
    
    // Keep raw namespace available for users who need non-RAII handles
    namespace raw {
        using namespace ::wgpu;
    }
}
