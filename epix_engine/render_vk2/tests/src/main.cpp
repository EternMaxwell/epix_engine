#include <epix/app.h>
#include <epix/input.h>
#include <epix/rdvk.h>
#include <epix/rdvk_res.h>
#include <epix/window.h>

#include <filesystem>
#include <glm/glm.hpp>
#include <pfr.hpp>
#include <spirv_glsl.hpp>

#include "shaders/vertex_shader.h"

using namespace epix::render::vulkan2::backend;

void get_shader_resource_bindings(
    std::vector<std::vector<vk::DescriptorSetLayoutBinding>>& bindings,
    spirv_cross::CompilerGLSL& glsl,
    vk::ShaderStageFlags stage,
    uint32_t max_descriptor_count = 65536
) {
    auto&& resources = glsl.get_shader_resources();
    for (auto& resource : resources.uniform_buffers) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t count = type.array.size() ? (type.array_size_literal[0]
                                                  ? type.array[0]
                                                  : max_descriptor_count)
                                           : 1;
        auto res_binding =
            vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(vk::DescriptorType::eUniformBuffer)
                .setDescriptorCount(count)
                .setStageFlags(stage);
        decltype(bindings[set].begin()) it = std::find_if(
            bindings[set].begin(), bindings[set].end(),
            [binding](const vk::DescriptorSetLayoutBinding& b) {
                return b.binding == binding;
            }
        );
        if (it != bindings[set].end()) {
            if (it->descriptorType != res_binding.descriptorType) {
                spdlog::error(
                    "Descriptor type mismatch for binding {} in "
                    "{} shader and {} shader, expected {} got {}",
                    binding, vk::to_string(it->stageFlags),
                    vk::to_string(stage), vk::to_string(it->descriptorType),
                    vk::to_string(res_binding.descriptorType)
                );
                throw std::runtime_error(
                    "Descriptor type mismatch for binding " +
                    std::to_string(binding)
                );
            }
            it->stageFlags |= stage;
            it->descriptorCount = std::max(it->descriptorCount, count);
            continue;
        }
        bindings[set].push_back(res_binding);
    }
    for (auto& resource : resources.storage_buffers) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t count = type.array.size() ? (type.array_size_literal[0]
                                                  ? type.array[0]
                                                  : max_descriptor_count)
                                           : 1;
        auto res_binding =
            vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(vk::DescriptorType::eStorageBuffer)
                .setDescriptorCount(count)
                .setStageFlags(stage);
        decltype(bindings[set].begin()) it = std::find_if(
            bindings[set].begin(), bindings[set].end(),
            [binding](const vk::DescriptorSetLayoutBinding& b) {
                return b.binding == binding;
            }
        );
        if (it != bindings[set].end()) {
            if (it->descriptorType != res_binding.descriptorType) {
                spdlog::error(
                    "Descriptor type mismatch for binding {} in "
                    "{} shader and {} shader, expected {} got {}",
                    binding, vk::to_string(it->stageFlags),
                    vk::to_string(stage), vk::to_string(it->descriptorType),
                    vk::to_string(res_binding.descriptorType)
                );
                throw std::runtime_error(
                    "Descriptor type mismatch for binding " +
                    std::to_string(binding)
                );
            }
            it->stageFlags |= stage;
            it->descriptorCount = std::max(it->descriptorCount, count);
            continue;
        }
        bindings[set].push_back(res_binding);
    }
    for (auto& resource : resources.sampled_images) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t count = type.array.size() ? (type.array_size_literal[0]
                                                  ? type.array[0]
                                                  : max_descriptor_count)
                                           : 1;
        auto res_binding =
            vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
                .setDescriptorCount(count)
                .setStageFlags(stage);
        decltype(bindings[set].begin()) it = std::find_if(
            bindings[set].begin(), bindings[set].end(),
            [binding](const vk::DescriptorSetLayoutBinding& b) {
                return b.binding == binding;
            }
        );
        if (it != bindings[set].end()) {
            if (it->descriptorType != res_binding.descriptorType) {
                spdlog::error(
                    "Descriptor type mismatch for binding {} in "
                    "{} shader and {} shader, expected {} got {}",
                    binding, vk::to_string(it->stageFlags),
                    vk::to_string(stage), vk::to_string(it->descriptorType),
                    vk::to_string(res_binding.descriptorType)
                );
                throw std::runtime_error(
                    "Descriptor type mismatch for binding " +
                    std::to_string(binding)
                );
            }
            it->stageFlags |= stage;
            it->descriptorCount = std::max(it->descriptorCount, count);
            continue;
        }
        bindings[set].push_back(res_binding);
    }
    for (auto& resource : resources.separate_images) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t count   = type.array.size() ? (type.array_size_literal[0]
                                                    ? type.array[0]
                                                    : max_descriptor_count)
                                             : 1;
        auto res_binding = vk::DescriptorSetLayoutBinding()
                               .setBinding(binding)
                               .setDescriptorType(
                                   type.image.dim == spv::Dim::DimBuffer
                                       ? vk::DescriptorType::eStorageTexelBuffer
                                       : vk::DescriptorType::eSampledImage
                               )
                               .setDescriptorCount(count)
                               .setStageFlags(stage);
        decltype(bindings[set].begin()) it = std::find_if(
            bindings[set].begin(), bindings[set].end(),
            [binding](const vk::DescriptorSetLayoutBinding& b) {
                return b.binding == binding;
            }
        );
        if (it != bindings[set].end()) {
            if (it->descriptorType != res_binding.descriptorType) {
                spdlog::error(
                    "Descriptor type mismatch for binding {} in "
                    "{} shader and {} shader, expected {} got {}",
                    binding, vk::to_string(it->stageFlags),
                    vk::to_string(stage), vk::to_string(it->descriptorType),
                    vk::to_string(res_binding.descriptorType)
                );
                throw std::runtime_error(
                    "Descriptor type mismatch for binding " +
                    std::to_string(binding)
                );
            }
            it->stageFlags |= stage;
            it->descriptorCount = std::max(it->descriptorCount, count);
            continue;
        }
        bindings[set].push_back(res_binding);
    }
    for (auto& resource : resources.separate_samplers) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t count   = type.array.size() ? (type.array_size_literal[0]
                                                    ? type.array[0]
                                                    : max_descriptor_count)
                                             : 1;
        auto res_binding = vk::DescriptorSetLayoutBinding()
                               .setBinding(binding)
                               .setDescriptorType(vk::DescriptorType::eSampler)
                               .setDescriptorCount(count)
                               .setStageFlags(stage);
        decltype(bindings[set].begin()) it = std::find_if(
            bindings[set].begin(), bindings[set].end(),
            [binding](const vk::DescriptorSetLayoutBinding& b) {
                return b.binding == binding;
            }
        );
        if (it != bindings[set].end()) {
            if (it->descriptorType != res_binding.descriptorType) {
                spdlog::error(
                    "Descriptor type mismatch for binding {} in "
                    "{} shader and {} shader, expected {} got {}",
                    binding, vk::to_string(it->stageFlags),
                    vk::to_string(stage), vk::to_string(it->descriptorType),
                    vk::to_string(res_binding.descriptorType)
                );
                throw std::runtime_error(
                    "Descriptor type mismatch for binding " +
                    std::to_string(binding)
                );
            }
            it->stageFlags |= stage;
            it->descriptorCount = std::max(it->descriptorCount, count);
            continue;
        }
        bindings[set].push_back(res_binding);
    }
    for (auto& resource : resources.storage_images) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t count   = type.array.size() ? (type.array_size_literal[0]
                                                    ? type.array[0]
                                                    : max_descriptor_count)
                                             : 1;
        auto res_binding = vk::DescriptorSetLayoutBinding()
                               .setBinding(binding)
                               .setDescriptorType(
                                   type.image.dim == spv::Dim::DimBuffer
                                       ? vk::DescriptorType::eStorageTexelBuffer
                                       : vk::DescriptorType::eStorageImage
                               )
                               .setDescriptorCount(count)
                               .setStageFlags(stage);
        decltype(bindings[set].begin()) it = std::find_if(
            bindings[set].begin(), bindings[set].end(),
            [binding](const vk::DescriptorSetLayoutBinding& b) {
                return b.binding == binding;
            }
        );
        if (it != bindings[set].end()) {
            if (it->descriptorType != res_binding.descriptorType) {
                spdlog::error(
                    "Descriptor type mismatch for binding {} in "
                    "{} shader and {} shader, expected {} got {}",
                    binding, vk::to_string(it->stageFlags),
                    vk::to_string(stage), vk::to_string(it->descriptorType),
                    vk::to_string(res_binding.descriptorType)
                );
                throw std::runtime_error(
                    "Descriptor type mismatch for binding " +
                    std::to_string(binding)
                );
            }
            it->stageFlags |= stage;
            it->descriptorCount = std::max(it->descriptorCount, count);
            continue;
        }
        bindings[set].push_back(res_binding);
    }
    vk::DescriptorImageInfo image_info;
    for (auto& resource : resources.subpass_inputs) {
        auto& type = glsl.get_type(resource.type_id);
        auto set =
            glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
        if (set >= bindings.size()) {
            bindings.resize(set + 1);
        }
        auto binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t count = type.array.size() ? (type.array_size_literal[0]
                                                  ? type.array[0]
                                                  : max_descriptor_count)
                                           : 1;
        auto res_binding =
            vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(vk::DescriptorType::eInputAttachment)
                .setDescriptorCount(count)
                .setStageFlags(stage);
        decltype(bindings[set].begin()) it = std::find_if(
            bindings[set].begin(), bindings[set].end(),
            [binding](const vk::DescriptorSetLayoutBinding& b) {
                return b.binding == binding;
            }
        );
        if (it != bindings[set].end()) {
            if (it->descriptorType != res_binding.descriptorType) {
                spdlog::error(
                    "Descriptor type mismatch for binding {} in "
                    "{} shader and {} shader, expected {} got {}",
                    binding, vk::to_string(it->stageFlags),
                    vk::to_string(stage), vk::to_string(it->descriptorType),
                    vk::to_string(res_binding.descriptorType)
                );
                throw std::runtime_error(
                    "Descriptor type mismatch for binding " +
                    std::to_string(binding)
                );
            }
            it->stageFlags |= stage;
            it->descriptorCount = std::max(it->descriptorCount, count);
            continue;
        }
        bindings[set].push_back(res_binding);
    }
}

bool get_push_constant_ranges(
    vk::PushConstantRange& range,
    spirv_cross::CompilerGLSL& glsl,
    vk::ShaderStageFlags stage,
    uint32_t offset = 0u
) {
    auto&& resources = glsl.get_shader_resources();
    for (auto& resource : resources.push_constant_buffers) {
        auto& type = glsl.get_type(resource.type_id);
        auto size  = glsl.get_declared_struct_size(type);
        range      = vk::PushConstantRange().setOffset(offset).setSize(size);
        range.stageFlags |= stage;
        return true;
    }
    return false;
}

std::vector<vk::VertexInputAttributeDescription> get_vertex_input_attributes(
    spirv_cross::CompilerGLSL& vert
) {
    std::vector<vk::VertexInputAttributeDescription> bindings;
    auto&& resources = vert.get_shader_resources();
    uint32_t offset  = 0;
    std::vector<spirv_cross::Resource> inputs;
    for (auto&& res : resources.stage_inputs) {
        inputs.push_back(res);
    }
    std::sort(inputs.begin(), inputs.end(), [&vert](auto& a, auto& b) {
        return vert.get_decoration(a.id, spv::DecorationLocation) <
               vert.get_decoration(b.id, spv::DecorationLocation);
    });
    for (auto& resource : inputs) {
        auto& type     = vert.get_type(resource.type_id);
        auto base_size = 4u;
        auto location =
            vert.get_decoration(resource.id, spv::DecorationLocation);
        vk::Format format = vk::Format::eUndefined;
        switch (type.basetype) {
            case spirv_cross::SPIRType::Float:
                base_size = 4;
                switch (type.vecsize) {
                    case 1:
                        format = vk::Format::eR32Sfloat;
                        break;
                    case 2:
                        format = vk::Format::eR32G32Sfloat;
                        break;
                    case 3:
                        format = vk::Format::eR32G32B32Sfloat;
                        break;
                    case 4:
                        format = vk::Format::eR32G32B32A32Sfloat;
                        break;
                    default:
                        break;
                }
                break;
            case spirv_cross::SPIRType::Int:
                base_size = 4;
                switch (type.vecsize) {
                    case 1:
                        format = vk::Format::eR32Sint;
                        break;
                    case 2:
                        format = vk::Format::eR32G32Sint;
                        break;
                    case 3:
                        format = vk::Format::eR32G32B32Sint;
                        break;
                    case 4:
                        format = vk::Format::eR32G32B32A32Sint;
                        break;
                    default:
                        break;
                }
                break;
            case spirv_cross::SPIRType::UInt:
                base_size = 4;
                switch (type.vecsize) {
                    case 1:
                        format = vk::Format::eR32Uint;
                        break;
                    case 2:
                        format = vk::Format::eR32G32Uint;
                        break;
                    case 3:
                        format = vk::Format::eR32G32B32Uint;
                        break;
                    case 4:
                        format = vk::Format::eR32G32B32A32Uint;
                        break;
                    default:
                        break;
                }
                break;
            case spirv_cross::SPIRType::Double:
                base_size = 8;
                switch (type.vecsize) {
                    case 1:
                        format = vk::Format::eR64Sfloat;
                        break;
                    case 2:
                        format = vk::Format::eR64G64Sfloat;
                        break;
                    case 3:
                        format = vk::Format::eR64G64B64Sfloat;
                        break;
                    case 4:
                        format = vk::Format::eR64G64B64A64Sfloat;
                        break;
                    default:
                        break;
                }
                break;
            case spirv_cross::SPIRType::Int64:
                base_size = 8;
                switch (type.vecsize) {
                    case 1:
                        format = vk::Format::eR64Sint;
                        break;
                    case 2:
                        format = vk::Format::eR64G64Sint;
                        break;
                    case 3:
                        format = vk::Format::eR64G64B64Sint;
                        break;
                    case 4:
                        format = vk::Format::eR64G64B64A64Sint;
                        break;
                    default:
                        break;
                }
                break;
            case spirv_cross::SPIRType::UInt64:
                base_size = 8;
                switch (type.vecsize) {
                    case 1:
                        format = vk::Format::eR64Uint;
                        break;
                    case 2:
                        format = vk::Format::eR64G64Uint;
                        break;
                    case 3:
                        format = vk::Format::eR64G64B64Uint;
                        break;
                    case 4:
                        format = vk::Format::eR64G64B64A64Uint;
                        break;
                    default:
                        break;
                }
                break;
            default:
                break;
        }
        for (int i = 0; i < type.columns; i++) {
            bindings.push_back(vk::VertexInputAttributeDescription()
                                   .setBinding(0)
                                   .setLocation(location + i)
                                   .setFormat(format)
                                   .setOffset(offset));
            offset += base_size * type.vecsize;
        }
    }
    return bindings;
}

template <typename T, size_t B = 0>
std::vector<vk::VertexInputAttributeDescription> get_vertex_input_attributes() {
    std::vector<vk::VertexInputAttributeDescription> bindings;
    T t;
    pfr::for_each_field<
        T>(std::move(t), [&t, &bindings](auto&& field, auto index) {
        uint32_t offset =
            ((size_t) reinterpret_cast<void*>(&field) -
             (size_t) reinterpret_cast<void*>(&t));
        vk::Format format = vk::Format::eUndefined;
        using Field       = std::decay_t<decltype(field)>;
        if constexpr (std::is_same_v<Field, float>) {
            format = vk::Format::eR32Sfloat;
        } else if constexpr (std::is_same_v<Field, glm::vec2>) {
            format = vk::Format::eR32G32Sfloat;
        } else if constexpr (std::is_same_v<Field, glm::vec3>) {
            format = vk::Format::eR32G32B32Sfloat;
        } else if constexpr (std::is_same_v<Field, glm::vec4>) {
            format = vk::Format::eR32G32B32A32Sfloat;
        } else if constexpr (std::is_same_v<Field, int>) {
            format = vk::Format::eR32Sint;
        } else if constexpr (std::is_same_v<Field, glm::ivec2>) {
            format = vk::Format::eR32G32Sint;
        } else if constexpr (std::is_same_v<Field, glm::ivec3>) {
            format = vk::Format::eR32G32B32Sint;
        } else if constexpr (std::is_same_v<Field, glm::ivec4>) {
            format = vk::Format::eR32G32B32A32Sint;
        } else if constexpr (std::is_same_v<Field, uint32_t>) {
            format = vk::Format::eR32Uint;
        } else if constexpr (std::is_same_v<Field, glm::uvec2>) {
            format = vk::Format::eR32G32Uint;
        } else if constexpr (std::is_same_v<Field, glm::uvec3>) {
            format = vk::Format::eR32G32B32Uint;
        } else if constexpr (std::is_same_v<Field, glm::uvec4>) {
            format = vk::Format::eR32G32B32A32Uint;
        }
        if (format == vk::Format::eUndefined) {
            spdlog::warn(
                "Unsupported member type for vertex input binding, this will "
                "be ignored."
            );
            return;
        }
        bindings.push_back(vk::VertexInputAttributeDescription()
                               .setBinding(B)
                               .setLocation(index)
                               .setFormat(format)
                               .setOffset(offset));
    });
    return bindings;
}

struct test {
    float x;
    float y;
    float z;
};

using namespace epix;

struct Vertex {
    glm::vec3 pos;
    glm::mat4 model;
    int x;
};

struct TestPipeline {
    Device device;
    Swapchain swapchain;
    vk::RenderPass render_pass;
    vk::Pipeline pipeline;
    vk::PipelineLayout layout;
    std::vector<vk::DescriptorSetLayout> set_layouts;

    void create_render_pass() {
        vk::AttachmentDescription color_attachment;
        color_attachment.setFormat(swapchain.surface_format.format);
        color_attachment.setSamples(vk::SampleCountFlagBits::e1);
        color_attachment.setLoadOp(vk::AttachmentLoadOp::eLoad);
        color_attachment.setStoreOp(vk::AttachmentStoreOp::eStore);
        color_attachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
        color_attachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
        color_attachment.setInitialLayout(
            vk::ImageLayout::eColorAttachmentOptimal
        );
        color_attachment.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal
        );
        vk::AttachmentReference color_attachment_ref;
        color_attachment_ref.setAttachment(0);
        color_attachment_ref.setLayout(vk::ImageLayout::eColorAttachmentOptimal
        );
        vk::SubpassDescription subpass;
        subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
        subpass.setColorAttachments(color_attachment_ref);
        vk::SubpassDependency dependency;
        dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL);
        dependency.setDstSubpass(0);
        dependency.setSrcStageMask(
            vk::PipelineStageFlagBits::eColorAttachmentOutput
        );
        dependency.setDstStageMask(
            vk::PipelineStageFlagBits::eColorAttachmentOutput
        );
        dependency.setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
        dependency.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
        dependency.setDependencyFlags(vk::DependencyFlagBits::eByRegion);
        vk::RenderPassCreateInfo render_pass_info;
        render_pass_info.setAttachments(color_attachment);
        render_pass_info.setSubpasses(subpass);
        render_pass_info.setDependencies(dependency);
        render_pass = device.createRenderPass(render_pass_info);
    }
    void create_layout() {
        vk::PipelineLayoutCreateInfo layout_info;
        auto source_vert = std::vector<uint32_t>(
            vertex_spv, vertex_spv + sizeof(vertex_spv) / sizeof(uint32_t)
        );
        vk::ShaderModuleCreateInfo vert_info;
        vert_info.setCode(source_vert);
        spirv_cross::CompilerGLSL vert(source_vert);
        std::vector<std::vector<vk::DescriptorSetLayoutBinding>> bindings;
        get_shader_resource_bindings(
            bindings, vert, vk::ShaderStageFlagBits::eVertex
        );
        vk::PushConstantRange range;
        if (get_push_constant_ranges(
                range, vert, vk::ShaderStageFlagBits::eVertex
            )) {
            layout_info.setPushConstantRanges(range);
        }
        auto& layouts = set_layouts;
        layouts.reserve(bindings.size());
        for (auto& binding : bindings) {
            vk::DescriptorSetLayoutCreateInfo layout_info;
            layout_info.setBindings(binding);
            layouts.push_back(device.createDescriptorSetLayout(layout_info));
        }
        layout_info.setSetLayouts(layouts);
        layout = device.createPipelineLayout(layout_info);
    }
    void create_pipeline() {
        using namespace epix::render::vulkan2::util;
        auto vert_source = std::vector<uint32_t>(
            vertex_spv, vertex_spv + sizeof(vertex_spv) / sizeof(uint32_t)
        );
        auto vert_module = device.createShaderModule(
            vk::ShaderModuleCreateInfo().setCode(vert_source)
        );
        spirv_cross::CompilerGLSL vert(vert_source);

        auto pipeline_info = vk::GraphicsPipelineCreateInfo();
        auto stages        = default_shader_stages(
            vk::ShaderStageFlagBits::eVertex, vert_module
        );
        pipeline_info.setStages(stages);
        pipeline_info.setLayout(layout);
        vk::PipelineViewportStateCreateInfo viewport_state;
        auto view_scissors = default_viewport_scissor(
            &viewport_state, swapchain.others->extent, 1
        );
        pipeline_info.setPViewportState(&viewport_state);
        vk::PipelineInputAssemblyStateCreateInfo input_assembly;
        input_assembly.setTopology(vk::PrimitiveTopology::eTriangleList);
        pipeline_info.setPInputAssemblyState(&input_assembly);
        vk::PipelineRasterizationStateCreateInfo rasterization;
        default_rasterization(&rasterization);
        pipeline_info.setPRasterizationState(&rasterization);
        vk::PipelineMultisampleStateCreateInfo multisample;
        default_multisample(&multisample);
        pipeline_info.setPMultisampleState(&multisample);
        vk::PipelineDepthStencilStateCreateInfo depth_stencil;
        default_depth_stencil(&depth_stencil);
        pipeline_info.setPDepthStencilState(&depth_stencil);
        auto color_blend_attachments = default_blend_attachments(1);
        vk::PipelineColorBlendStateCreateInfo color_blend;
        color_blend.setAttachments(color_blend_attachments);
        pipeline_info.setPColorBlendState(&color_blend);
        vk::PipelineDynamicStateCreateInfo dynamic_state;
        auto dynamic_states = default_dynamic_states(&dynamic_state);
        pipeline_info.setPDynamicState(&dynamic_state);
        vk::PipelineVertexInputStateCreateInfo vertex_input;
        auto attributes = get_vertex_input_attributes(vert);
        auto bindings   = std::vector<vk::VertexInputBindingDescription>{
            vk::VertexInputBindingDescription().setStride(sizeof(Vertex))
        };
        default_vertex_input(&vertex_input, bindings, attributes);
        pipeline_info.setPVertexInputState(&vertex_input);
        pipeline_info.setRenderPass(render_pass);
        pipeline =
            device.createGraphicsPipeline(vk::PipelineCache(), pipeline_info)
                .value;
        device.destroyShaderModule(vert_module);
    }

    void destroy() {
        device.destroyPipeline(pipeline);
        device.destroyPipelineLayout(layout);
        device.destroyRenderPass(render_pass);
        for (auto& layout : set_layouts) {
            device.destroyDescriptorSetLayout(layout);
        }
    }
};

void create_pipeline(
    Query<Get<Device, Swapchain>, With<epix::render::vulkan2::RenderContext>>
        query,
    Command cmd
) {
    if (!query) {
        return;
    }
    auto [device, swapchain] = query.single();
    TestPipeline pipeline;
    pipeline.device    = device;
    pipeline.swapchain = swapchain;
    pipeline.create_render_pass();
    pipeline.create_layout();
    pipeline.create_pipeline();
    cmd.spawn(pipeline);
}

void destroy_pipeline(Query<Get<TestPipeline>> query) {
    if (!query) {
        return;
    }
    auto [pipeline] = query.single();
    pipeline.destroy();
}

int main() {
    using namespace epix::app;
    using namespace epix::window;

    App app2 = App::create2();
    app2.add_plugin(WindowPlugin{});
    app2.get_plugin<WindowPlugin>()->primary_desc().set_vsync(false);
    app2.add_plugin(
        epix::render::vulkan2::RenderVKPlugin{}.set_debug_callback(true)
    );
    app2.add_plugin(epix::render::vulkan2::VulkanResManagerPlugin{});
    app2.add_plugin(epix::input::InputPlugin{});
    app2.add_system(epix::Startup, create_pipeline);
    app2.add_system(epix::Exit, destroy_pipeline);
    app2.run();

    std::vector<uint32_t> spirv(
        vertex_spv, vertex_spv + sizeof(vertex_spv) / sizeof(uint32_t)
    );
    spirv_cross::CompilerGLSL compiler(spirv);

    auto resources = compiler.get_shader_resources();

    for (auto& resource : resources.push_constant_buffers) {
        auto& buffer_type = compiler.get_type(resource.type_id);
        auto& buffer_name = compiler.get_name(resource.id);
        std::cout << "Push constant buffer: " << buffer_name << std::endl;
        for (auto& member : buffer_type.member_types) {
            auto& member_type = compiler.get_type(member);
            auto& member_name =
                compiler.get_member_name(resource.base_type_id, member);
            std::cout << "\tmember name: " << member_name << std::endl;
        }
    }
    for (auto& resource : resources.uniform_buffers) {
        auto& buffer_type = compiler.get_type(resource.type_id);
        auto& buffer_name = compiler.get_name(resource.id);
        auto set =
            compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        auto binding =
            compiler.get_decoration(resource.id, spv::DecorationBinding);
        auto array_size = buffer_type.array[0];
        std::cout << std::format(
                         "set = {}, binding = {}, Uniform buffer: {}, array "
                         "size literal = {}",
                         set, binding, buffer_name, array_size
                     )
                  << std::endl;
        for (size_t i = 0; i < buffer_type.member_types.size(); i++) {
            auto& member_type = compiler.get_type(buffer_type.member_types[i]);
            auto& member_name =
                compiler.get_member_name(resource.base_type_id, i);
            std::cout << "\tmember name: " << member_name << std::endl;
        }
    }
    for (auto& resource : resources.storage_buffers) {
        auto& buffer_type = compiler.get_type(resource.type_id);
        auto& buffer_name = compiler.get_name(resource.id);
        auto set =
            compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        auto binding =
            compiler.get_decoration(resource.id, spv::DecorationBinding);
        auto array_size = buffer_type.array[0];
        std::cout << std::format(
                         "set = {}, binding = {}, Storage buffer: {}, array "
                         "size literal = {}",
                         set, binding, buffer_name, array_size
                     )
                  << std::endl;
        for (size_t i = 0; i < buffer_type.member_types.size(); i++) {
            auto& member_type = compiler.get_type(buffer_type.member_types[i]);
            auto& member_name =
                compiler.get_member_name(resource.base_type_id, i);
            std::cout << "\tmember name: " << member_name << std::endl;
        }
    }
    for (auto& resource : resources.sampled_images) {
        auto& image_type = compiler.get_type(resource.type_id);
        auto& image_name = compiler.get_name(resource.id);
        auto set =
            compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        auto binding =
            compiler.get_decoration(resource.id, spv::DecorationBinding);
        auto array_size = image_type.array[0];
        std::cout
            << std::format(
                   "set = {}, binding = {}, Sampled image: {}, array size = {}",
                   set, binding, image_name, array_size
               )
            << std::endl;
    }

    auto attributes = get_vertex_input_attributes<test>();
    for (auto& attribute : attributes) {
        std::cout << "Binding: " << attribute.binding << std::endl;
        std::cout << "Location: " << attribute.location << std::endl;
        std::cout << "Format: " << vk::to_string(attribute.format) << std::endl;
        std::cout << "Offset: " << attribute.offset << std::endl;
    }
    std::cout << "--------------------------------" << std::endl;
    auto attributes2 = get_vertex_input_attributes(compiler);
    for (auto& attribute : attributes2) {
        std::cout << "Binding: " << attribute.binding << std::endl;
        std::cout << "Location: " << attribute.location << std::endl;
        std::cout << "Format: " << vk::to_string(attribute.format) << std::endl;
        std::cout << "Offset: " << attribute.offset << std::endl;
    }
}