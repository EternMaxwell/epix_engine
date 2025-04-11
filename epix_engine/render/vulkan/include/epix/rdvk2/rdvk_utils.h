#pragma once

#include <epix/common.h>
#include <epix/vulkan.h>

#include <memory>
#include <mutex>
#include <spirv_glsl.hpp>
#include <vector>

namespace epix::render::vulkan2 {

namespace util {
EPIX_API void get_shader_resource_bindings(
    std::vector<std::vector<vk::DescriptorSetLayoutBinding>>& bindings,
    spirv_cross::CompilerGLSL& glsl,
    vk::ShaderStageFlags stage,
    uint32_t max_descriptor_count = 65536
);

EPIX_API bool get_push_constant_ranges(
    vk::PushConstantRange& range,
    spirv_cross::CompilerGLSL& glsl,
    vk::ShaderStageFlags stage,
    uint32_t offset = 0u
);

EPIX_API std::vector<vk::VertexInputAttributeDescription>
get_vertex_input_attributes(spirv_cross::CompilerGLSL& vert);
EPIX_API void default_blend_attachment(
    vk::PipelineColorBlendAttachmentState* state
);
[[nodiscard]] EPIX_API std::vector<vk::PipelineColorBlendAttachmentState>
default_blend_attachments(uint32_t count);
EPIX_API void default_depth_stencil(
    vk::PipelineDepthStencilStateCreateInfo* state
);
[[nodiscard]] EPIX_API std::vector<vk::DynamicState> default_dynamic_states(
    vk::PipelineDynamicStateCreateInfo* state
);
EPIX_API void default_input_assembly(
    vk::PipelineInputAssemblyStateCreateInfo* state,
    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList
);
EPIX_API void default_multisample(vk::PipelineMultisampleStateCreateInfo* state
);
EPIX_API void default_rasterization(
    vk::PipelineRasterizationStateCreateInfo* state,
    bool cull      = false,
    bool cull_back = false,
    bool ccw       = true
);
EPIX_API std::tuple<std::vector<vk::Viewport>, std::vector<vk::Rect2D>>
default_viewport_scissor(
    vk::PipelineViewportStateCreateInfo* state,
    vk::Extent2D extent,
    uint32_t count
);
EPIX_API void default_tessellation(
    vk::PipelineTessellationStateCreateInfo* state
);
EPIX_API void default_vertex_input(
    vk::PipelineVertexInputStateCreateInfo* state,
    std::vector<vk::VertexInputBindingDescription>& bindings,
    std::vector<vk::VertexInputAttributeDescription>& attributes
);
template <typename... Args>
std::vector<vk::PipelineShaderStageCreateInfo> default_shader_stages(
    vk::ShaderStageFlagBits flags, vk::ShaderModule& module, Args&&... args
) {
    if constexpr (sizeof...(Args) == 0) {
        return {vk::PipelineShaderStageCreateInfo()
                    .setStage(flags)
                    .setModule(module)
                    .setPName("main")};
    } else {
        auto stages = default_shader_stages(std::forward<Args>(args)...);
        stages.push_back(vk::PipelineShaderStageCreateInfo()
                             .setStage(flags)
                             .setModule(module)
                             .setPName("main"));
        return std::move(stages);
    }
}
}  // namespace util
}  // namespace epix::render::vulkan2