#include <spdlog/spdlog.h>

#include "epix/rdvk2/rdvk_utils.h"

namespace epix::render::vulkan2 {

EPIX_API void util::get_shader_resource_bindings(
    std::vector<std::vector<vk::DescriptorSetLayoutBinding>>& bindings,
    spirv_cross::CompilerGLSL& glsl,
    vk::ShaderStageFlags stage,
    uint32_t max_descriptor_count
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
        uint32_t count = type.array.size()
                             ? ((type.array_size_literal[0] && type.array[0])
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
        uint32_t count = type.array.size()
                             ? ((type.array_size_literal[0] && type.array[0])
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
        uint32_t count = type.array.size()
                             ? ((type.array_size_literal[0] && type.array[0])
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
        uint32_t count   = type.array.size()
                               ? ((type.array_size_literal[0] && type.array[0])
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
        uint32_t count   = type.array.size()
                               ? ((type.array_size_literal[0] && type.array[0])
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
        uint32_t count   = type.array.size()
                               ? ((type.array_size_literal[0] && type.array[0])
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
        uint32_t count = type.array.size()
                             ? ((type.array_size_literal[0] && type.array[0])
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

EPIX_API bool util::get_push_constant_ranges(
    vk::PushConstantRange& range,
    spirv_cross::CompilerGLSL& glsl,
    vk::ShaderStageFlags stage,
    uint32_t offset
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

EPIX_API std::vector<vk::VertexInputAttributeDescription>
util::get_vertex_input_attributes(spirv_cross::CompilerGLSL& vert) {
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
EPIX_API void util::default_blend_attachment(
    vk::PipelineColorBlendAttachmentState* state
) {
    *state =
        vk::PipelineColorBlendAttachmentState()
            .setColorWriteMask(
                vk::ColorComponentFlagBits::eR |
                vk::ColorComponentFlagBits::eG |
                vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
            )
            .setBlendEnable(true)
            .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
            .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
            .setColorBlendOp(vk::BlendOp::eAdd)
            .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
            .setDstAlphaBlendFactor(vk::BlendFactor::eZero)
            .setAlphaBlendOp(vk::BlendOp::eAdd);
}
EPIX_API [[nodiscard]] std::vector<vk::PipelineColorBlendAttachmentState>
util::default_blend_attachments(uint32_t count) {
    std::vector<vk::PipelineColorBlendAttachmentState> states(count);
    for (auto& state : states) {
        default_blend_attachment(&state);
    }
    return std::move(states);
}
EPIX_API void util::default_depth_stencil(
    vk::PipelineDepthStencilStateCreateInfo* state
) {
    *state = vk::PipelineDepthStencilStateCreateInfo()
                 .setDepthTestEnable(true)
                 .setDepthWriteEnable(true)
                 .setDepthCompareOp(vk::CompareOp::eLess)
                 .setDepthBoundsTestEnable(false)
                 .setStencilTestEnable(false);
}
EPIX_API [[nodiscard]] std::vector<vk::DynamicState>
util::default_dynamic_states(vk::PipelineDynamicStateCreateInfo* state) {
    auto states = std::vector<vk::DynamicState>{
        vk::DynamicState::eViewport, vk::DynamicState::eScissor
    };
    *state = vk::PipelineDynamicStateCreateInfo().setDynamicStates(states);
    return std::move(states);
}
EPIX_API void util::default_input_assembly(
    vk::PipelineInputAssemblyStateCreateInfo* state,
    vk::PrimitiveTopology topology
) {
    *state = vk::PipelineInputAssemblyStateCreateInfo()
                 .setTopology(topology)
                 .setPrimitiveRestartEnable(false);
}
EPIX_API void util::default_multisample(
    vk::PipelineMultisampleStateCreateInfo* state
) {
    *state = vk::PipelineMultisampleStateCreateInfo()
                 .setRasterizationSamples(vk::SampleCountFlagBits::e1)
                 .setSampleShadingEnable(false)
                 .setMinSampleShading(1.0f)
                 .setAlphaToCoverageEnable(false)
                 .setAlphaToOneEnable(false);
}
EPIX_API void util::default_rasterization(
    vk::PipelineRasterizationStateCreateInfo* state,
    bool cull,
    bool cull_back,
    bool ccw
) {
    *state = vk::PipelineRasterizationStateCreateInfo()
                 .setDepthClampEnable(false)
                 .setRasterizerDiscardEnable(false)
                 .setPolygonMode(vk::PolygonMode::eFill)
                 .setLineWidth(1.0f)
                 .setCullMode(
                     cull ? (cull_back ? vk::CullModeFlagBits::eBack
                                       : vk::CullModeFlagBits::eFront)
                          : vk::CullModeFlagBits::eNone
                 )
                 .setFrontFace(
                     ccw ? vk::FrontFace::eCounterClockwise
                         : vk::FrontFace::eClockwise
                 )
                 .setDepthBiasEnable(false)
                 .setDepthBiasConstantFactor(0.0f)
                 .setDepthBiasClamp(0.0f)
                 .setDepthBiasSlopeFactor(0.0f);
}
EPIX_API std::tuple<std::vector<vk::Viewport>, std::vector<vk::Rect2D>>
util::default_viewport_scissor(
    vk::PipelineViewportStateCreateInfo* state,
    vk::Extent2D extent,
    uint32_t count
) {
    std::vector<vk::Viewport> viewports(count);
    std::vector<vk::Rect2D> scissors(count);
    for (uint32_t i = 0; i < count; i++) {
        viewports[i] = vk::Viewport()
                           .setX(0.0f)
                           .setY(0.0f)
                           .setWidth(static_cast<float>(extent.width))
                           .setHeight(static_cast<float>(extent.height))
                           .setMinDepth(0.0f)
                           .setMaxDepth(1.0f);
        scissors[i] = vk::Rect2D().setOffset({0, 0}).setExtent(extent);
    }
    *state = vk::PipelineViewportStateCreateInfo()
                 .setViewports(viewports)
                 .setScissors(scissors);
    return {std::move(viewports), std::move(scissors)};
}
EPIX_API void util::default_tessellation(
    vk::PipelineTessellationStateCreateInfo* state
) {
    *state = vk::PipelineTessellationStateCreateInfo();
}
EPIX_API void util::default_vertex_input(
    vk::PipelineVertexInputStateCreateInfo* state,
    std::vector<vk::VertexInputBindingDescription>& bindings,
    std::vector<vk::VertexInputAttributeDescription>& attributes
) {
    *state = vk::PipelineVertexInputStateCreateInfo()
                 .setVertexBindingDescriptions(bindings)
                 .setVertexAttributeDescriptions(attributes);
}
}  // namespace epix::render::vulkan2