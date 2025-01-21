#include "epix/rdvk.h"

namespace epix::render::vulkan2 {
using namespace epix::render::vulkan2::backend;
EPIX_API PipelineBase::PipelineBase(Device& device)
    : device(device),
      func_rasterization_state([]() {
          return vk::PipelineRasterizationStateCreateInfo()
              .setDepthClampEnable(false)
              .setRasterizerDiscardEnable(false)
              .setPolygonMode(vk::PolygonMode::eFill)
              .setLineWidth(1.0f)
              .setCullMode(vk::CullModeFlagBits::eNone)
              .setFrontFace(vk::FrontFace::eCounterClockwise)
              .setDepthBiasEnable(false)
              .setDepthBiasConstantFactor(0.0f)
              .setDepthBiasClamp(0.0f)
              .setDepthBiasSlopeFactor(0.0f);
      }),
      func_multisample_state([]() {
          return vk::PipelineMultisampleStateCreateInfo()
              .setSampleShadingEnable(false)
              .setRasterizationSamples(vk::SampleCountFlagBits::e1)
              .setMinSampleShading(1.0f)
              .setPSampleMask(nullptr)
              .setAlphaToCoverageEnable(false)
              .setAlphaToOneEnable(false);
      }),
      func_depth_stencil_state([]() {
          return vk::PipelineDepthStencilStateCreateInfo()
              .setDepthTestEnable(true)
              .setDepthWriteEnable(true)
              .setDepthCompareOp(vk::CompareOp::eLess)
              .setDepthBoundsTestEnable(false)
              .setStencilTestEnable(false);
      }),
      func_color_blend_attachments([]() {
          return std::vector<vk::PipelineColorBlendAttachmentState>{
              vk::PipelineColorBlendAttachmentState()
                  .setColorWriteMask(
                      vk::ColorComponentFlagBits::eR |
                      vk::ColorComponentFlagBits::eG |
                      vk::ColorComponentFlagBits::eB |
                      vk::ColorComponentFlagBits::eA
                  )
                  .setBlendEnable(true)
                  .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
                  .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
                  .setColorBlendOp(vk::BlendOp::eAdd)
                  .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
                  .setDstAlphaBlendFactor(vk::BlendFactor::eZero)
                  .setAlphaBlendOp(vk::BlendOp::eAdd)
          };
      }),
      func_color_blend_state([]() {
          return vk::PipelineColorBlendStateCreateInfo()
              .setLogicOpEnable(false)
              .setLogicOp(vk::LogicOp::eCopy)
              .setBlendConstants({0.0f, 0.0f, 0.0f, 0.0f});
      }),
      func_dynamic_states([]() {
          return std::vector<vk::DynamicState>{
              vk::DynamicState::eViewport, vk::DynamicState::eScissor
          };
      }) {}
EPIX_API void PipelineBase::create_render_pass() {
    if (!func_create_render_pass) {
        throw std::runtime_error("No render pass creation function provided");
    }
    render_pass = func_create_render_pass(device);
}
EPIX_API void PipelineBase::create_descriptor_pool() {
    if (!func_create_descriptor_pool) {
        throw std::runtime_error("No descriptor pool creation function provided"
        );
    }
    descriptor_pool = func_create_descriptor_pool(device);
}
EPIX_API void PipelineBase::create_layout() {
    vk::PipelineLayoutCreateInfo layout_info;

    std::vector<std::vector<vk::DescriptorSetLayoutBinding>> bindings;
    vk::PushConstantRange range;
    bool has_push_constants = false;
    for (auto& [stage, source] : shader_sources) {
        spirv_cross::CompilerGLSL compiler(source);
        epix::render::vulkan2::util::get_shader_resource_bindings(
            bindings, compiler, stage
        );
        bool current_has_push_constants =
            epix::render::vulkan2::util::get_push_constant_ranges(
                range, compiler, stage
            );
        has_push_constants |= current_has_push_constants;
        if (current_has_push_constants) push_constant_stage |= stage;
    }
    if (has_push_constants) {
        layout_info.setPushConstantRanges(range);
    }
    auto& set_layouts = descriptor_set_layouts;
    set_layouts.reserve(bindings.size());
    for (auto& binding : bindings) {
        vk::DescriptorSetLayoutCreateInfo layout_info;
        layout_info.setBindings(binding);
        set_layouts.push_back(device.createDescriptorSetLayout(layout_info));
    }
    layout_info.setSetLayouts(set_layouts);
    pipeline_layout = device.createPipelineLayout(layout_info);
}
EPIX_API void PipelineBase::create_pipeline(uint32_t subpass) {
    if (!func_vertex_input_bindings) {
        spdlog::error("No vertex input bindings function provided");
        throw std::runtime_error("No vertex input bindings function provided");
    }
    vk::GraphicsPipelineCreateInfo pipeline_info;
    pipeline_info.setLayout(pipeline_layout);
    pipeline_info.setRenderPass(render_pass);
    pipeline_info.setSubpass(subpass);

    std::vector<vk::ShaderModule> modules;
    for (auto& [stage, source] : shader_sources) {
        modules.push_back(device.createShaderModule(
            vk::ShaderModuleCreateInfo().setCode(source)
        ));
    }
    std::vector<vk::PipelineShaderStageCreateInfo> stages;
    for (int i = 0; i < modules.size(); i++) {
        stages.push_back(vk::PipelineShaderStageCreateInfo()
                             .setStage(shader_sources[i].first)
                             .setModule(modules[i])
                             .setPName("main"));
    }
    pipeline_info.setStages(stages);

    vk::PipelineVertexInputStateCreateInfo vertex_input_state;
    std::vector<vk::VertexInputBindingDescription> bindings;
    if (func_vertex_input_bindings) {
        bindings = func_vertex_input_bindings();
    }
    std::vector<vk::VertexInputAttributeDescription> attributes;
    if (func_vertex_input_attributes) {
        attributes = func_vertex_input_attributes();
    } else {
        auto&& vert_source = std::find_if(
            shader_sources.begin(), shader_sources.end(),
            [](auto& pair) {
                return pair.first == vk::ShaderStageFlagBits::eVertex;
            }
        );
        if (vert_source == shader_sources.end()) {
            spdlog::error("No vertex shader provided");
            throw std::runtime_error("No vertex shader provided");
        }
        spirv_cross::CompilerGLSL compiler(vert_source->second);
        attributes = util::get_vertex_input_attributes(compiler);
    }
    vertex_input_state.setVertexBindingDescriptions(bindings);
    vertex_input_state.setVertexAttributeDescriptions(attributes);
    pipeline_info.setPVertexInputState(&vertex_input_state);

    vk::PipelineInputAssemblyStateCreateInfo input_assembly_state;
    if (func_input_assembly_state) {
        input_assembly_state = func_input_assembly_state();
    } else {
        input_assembly_state = vk::PipelineInputAssemblyStateCreateInfo()
                                   .setTopology(default_topology)
                                   .setPrimitiveRestartEnable(false);
    }
    pipeline_info.setPInputAssemblyState(&input_assembly_state);

    vk::PipelineRasterizationStateCreateInfo rasterization_state =
        func_rasterization_state();
    pipeline_info.setPRasterizationState(&rasterization_state);

    vk::PipelineViewportStateCreateInfo viewport_state;
    vk::Viewport viewport;
    vk::Rect2D scissor;
    viewport_state.setViewportCount(1);
    viewport_state.setPViewports(&viewport);
    viewport_state.setScissorCount(1);
    viewport_state.setPScissors(&scissor);
    pipeline_info.setPViewportState(&viewport_state);

    vk::PipelineMultisampleStateCreateInfo multisample_state =
        func_multisample_state();
    pipeline_info.setPMultisampleState(&multisample_state);

    vk::PipelineDepthStencilStateCreateInfo depth_stencil_state =
        func_depth_stencil_state();
    pipeline_info.setPDepthStencilState(&depth_stencil_state);

    vk::PipelineColorBlendStateCreateInfo color_blend_state =
        func_color_blend_state();
    std::vector<vk::PipelineColorBlendAttachmentState> attachments;
    if (func_color_blend_attachments &&
        color_blend_state.attachmentCount == 0 &&
        color_blend_state.pAttachments == nullptr) {
        attachments = func_color_blend_attachments();
        color_blend_state.setAttachments(attachments);
    }
    pipeline_info.setPColorBlendState(&color_blend_state);

    auto dynamic_states = func_dynamic_states();
    vk::PipelineDynamicStateCreateInfo dynamic_state;
    dynamic_state.setDynamicStates(dynamic_states);
    pipeline_info.setPDynamicState(&dynamic_state);

    pipeline = device.createGraphicsPipeline({}, pipeline_info).value;

    for (auto& module : modules) {
        device.destroyShaderModule(module);
    }
}
EPIX_API void PipelineBase::destroy() {
    for (auto& layout : descriptor_set_layouts) {
        device.destroyDescriptorSetLayout(layout);
    }
    device.destroyPipelineLayout(pipeline_layout);
    device.destroyPipeline(pipeline);
    device.destroyRenderPass(render_pass);
    device.destroyDescriptorPool(descriptor_pool);
}
EPIX_API void PipelineBase::create() {
    if (!func_create_render_pass) {
        spdlog::error("No render pass creation function provided");
        throw std::runtime_error("No render pass creation function provided");
    }
    if (!func_create_descriptor_pool) {
        spdlog::error("No descriptor pool creation function provided");
        throw std::runtime_error("No descriptor pool creation function provided"
        );
    }
    if (shader_sources.size() == 0) {
        spdlog::error("No shaders provided");
        throw std::runtime_error("No shaders provided");
    }
    bool has_vertex_shader =
        std::ranges::any_of(shader_sources, [](auto& pair) {
            return pair.first == vk::ShaderStageFlagBits::eVertex;
        });
    bool has_fragment_shader =
        std::ranges::any_of(shader_sources, [](auto& pair) {
            return pair.first == vk::ShaderStageFlagBits::eFragment;
        });
    if (!has_vertex_shader) {
        spdlog::error("No vertex shader provided");
        // this will not throw in case this is intended
    }
    if (!has_fragment_shader) {
        spdlog::error("No fragment shader provided");
        // this will not throw in case this is intended
    }

    create_render_pass();
    create_descriptor_pool();
    create_layout();
    create_pipeline();
}
EPIX_API PipelineBase& PipelineBase::set_render_pass(
    std::function<backend::RenderPass(backend::Device&)> func
) {
    func_create_render_pass = func;
    return *this;
}
EPIX_API PipelineBase& PipelineBase::set_descriptor_pool(
    std::function<backend::DescriptorPool(backend::Device&)> func
) {
    func_create_descriptor_pool = func;
    return *this;
}
EPIX_API PipelineBase& PipelineBase::set_vertex_attributes(
    std::function<std::vector<vk::VertexInputAttributeDescription>()> func
) {
    func_vertex_input_attributes = func;
    return *this;
}
EPIX_API PipelineBase& PipelineBase::set_vertex_bindings(
    std::function<std::vector<vk::VertexInputBindingDescription>()> func
) {
    func_vertex_input_bindings = func;
    return *this;
}
EPIX_API PipelineBase& PipelineBase::set_input_assembly_state(
    std::function<vk::PipelineInputAssemblyStateCreateInfo()> func
) {
    func_input_assembly_state = func;
    return *this;
}
EPIX_API PipelineBase& PipelineBase::set_default_topology(
    vk::PrimitiveTopology topology
) {
    default_topology = topology;
    return *this;
}
EPIX_API PipelineBase& PipelineBase::set_rasterization_state(
    std::function<vk::PipelineRasterizationStateCreateInfo()> func
) {
    func_rasterization_state = func;
    return *this;
}
EPIX_API PipelineBase& PipelineBase::set_multisample_state(
    std::function<vk::PipelineMultisampleStateCreateInfo()> func
) {
    func_multisample_state = func;
    return *this;
}
EPIX_API PipelineBase& PipelineBase::set_depth_stencil_state(
    std::function<vk::PipelineDepthStencilStateCreateInfo()> func
) {
    func_depth_stencil_state = func;
    return *this;
}
EPIX_API PipelineBase& PipelineBase::set_color_blend_attachments(
    std::function<std::vector<vk::PipelineColorBlendAttachmentState>()> func
) {
    func_color_blend_attachments = func;
    return *this;
}
EPIX_API PipelineBase& PipelineBase::set_color_blend_state(
    std::function<vk::PipelineColorBlendStateCreateInfo()> func
) {
    func_color_blend_state = func;
    return *this;
}
EPIX_API PipelineBase& PipelineBase::set_dynamic_states(
    std::function<std::vector<vk::DynamicState>()> func
) {
    func_dynamic_states = func;
    return *this;
}
}  // namespace epix::render::vulkan2