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
        using namespace epix::render::vulkan2::util;
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

void create_sampler(Query<
                    Get<epix::render::vulkan2::ResourceManager>,
                    With<epix::render::vulkan2::RenderContextResManager>> query
) {
    if (!query) {
        return;
    }
    auto [res_manager] = query.single();
    auto& device       = res_manager.device;
    vk::SamplerCreateInfo sampler_info;
    sampler_info.setMagFilter(vk::Filter::eLinear);
    sampler_info.setMinFilter(vk::Filter::eLinear);
    sampler_info.setAddressModeU(vk::SamplerAddressMode::eRepeat);
    sampler_info.setAddressModeV(vk::SamplerAddressMode::eRepeat);
    sampler_info.setAddressModeW(vk::SamplerAddressMode::eRepeat);
    sampler_info.setAnisotropyEnable(true);
    sampler_info.setMaxAnisotropy(16);
    sampler_info.setBorderColor(vk::BorderColor::eIntOpaqueBlack);
    sampler_info.setUnnormalizedCoordinates(false);
    sampler_info.setCompareEnable(false);
    sampler_info.setCompareOp(vk::CompareOp::eAlways);
    sampler_info.setMipmapMode(vk::SamplerMipmapMode::eLinear);
    sampler_info.setMipLodBias(0.0f);
    sampler_info.setMinLod(0.0f);
    sampler_info.setMaxLod(0.0f);
    res_manager.add_sampler("default", device.createSampler(sampler_info));
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
    app2.add_system(epix::Startup, create_pipeline, create_sampler);
    app2.add_system(epix::Exit, destroy_pipeline);
    app2.run();
}