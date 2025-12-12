#include "epix/text/render.hpp"
#include "nvrhi/nvrhi.h"

using namespace epix;
using namespace epix::text;

namespace shaders {
#include "shaders/shader.frag.h"
#include "shaders/shader.vert.h"
}  // namespace shaders

assets::AssetId<render::Shader> vertex_shader =
    assets::AssetId<render::Shader>(uuids::uuid::from_string("c1c79399-9efe-476a-9dd2-446cff97859f").value());
assets::AssetId<render::Shader> fragment_shader =
    assets::AssetId<render::Shader>(uuids::uuid::from_string("16be62b9-1f8b-4cda-bfef-43e682dfa8d4").value());

namespace epix::text {
TextPipeline::TextPipeline(World& world) {
    auto device           = world.resource<nvrhi::DeviceHandle>();
    auto& view_layout     = world.resource<render::view::ViewUniformBindingLayout>();
    auto input_attributes = std::array{
        nvrhi::VertexAttributeDesc{
            .name          = "text position",
            .format        = nvrhi::Format::RGB32_FLOAT,
            .bufferIndex   = 0,
            .offset        = 0,
            .elementStride = sizeof(glm::vec3),
        },
        nvrhi::VertexAttributeDesc{
            .name          = "text uv0",
            .format        = nvrhi::Format::RG32_FLOAT,
            .bufferIndex   = 1,
            .offset        = 0,
            .elementStride = sizeof(glm::vec2),
        },
    };
    auto input_layout =
        device->createInputLayout(input_attributes.data(), static_cast<uint32_t>(input_attributes.size()), nullptr);

    auto text_layout = device->createBindingLayout(
        nvrhi::BindingLayoutDesc()
            .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
            .addItem(nvrhi::BindingLayoutItem::Sampler(1))
            .addItem(nvrhi::BindingLayoutItem::PushConstants(2, sizeof(glm::mat4) + sizeof(glm::vec4)))
            .setVisibility(nvrhi::ShaderType::All)
            .setBindingOffsets(nvrhi::VulkanBindingOffsets{0, 0, 0, 0}));

    render::RenderPipelineDesc desc;
    desc.setPrimType(nvrhi::PrimitiveType::TriangleList);
    desc.setRenderState(
        nvrhi::RenderState()
            .setDepthStencilState(nvrhi::DepthStencilState().setDepthTestEnable(true).setDepthWriteEnable(true))
            .setBlendState(nvrhi::BlendState().setRenderTarget(0, nvrhi::BlendState::RenderTarget()
                                                                      .setBlendEnable(true)
                                                                      .setBlendOp(nvrhi::BlendOp::Add)
                                                                      .setBlendOpAlpha(nvrhi::BlendOp::Add)
                                                                      .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                                                                      .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                                                                      .setSrcBlendAlpha(nvrhi::BlendFactor::One)
                                                                      .setDestBlendAlpha(nvrhi::BlendFactor::Zero)))
            .setRasterState(
                nvrhi::RasterState().setCullMode(nvrhi::RasterCullMode::None).setFrontCounterClockwise(true)));
    desc.setInputLayout(input_layout);
    desc.addBindingLayout(view_layout.layout);
    desc.addBindingLayout(text_layout);

    desc.setVertexShader({.shader = vertex_shader, .debugName = "text_vertex_shader", .entryName = "main"});
    desc.setFragmentShader({.shader = fragment_shader, .debugName = "text_fragment_shader", .entryName = "main"});

    auto& pipeline_server = world.resource_mut<render::PipelineServer>();
    pipeline_id_          = pipeline_server.queue_render_pipeline(desc);
}
void TextPipelinePlugin::build(epix::App& app) {
    auto& render_app = app.sub_app_mut(render::Render);
    render_app.world_mut().init_resource<TextPipeline>();
    app.add_systems(
        Startup,
        into([](ResMut<assets::Assets<render::Shader>> shaders) {
            // Insert generated SPIR-V shaders into the engine's shader assets
            auto res = shaders->insert(
                vertex_shader,
                render::Shader::spirv(std::vector<uint32_t>(
                    shaders::shader_vert, shaders::shader_vert + sizeof(shaders::shader_vert) / sizeof(uint32_t))));
            res = shaders->insert(
                fragment_shader,
                render::Shader::spirv(std::vector<uint32_t>(
                    shaders::shader_frag, shaders::shader_frag + sizeof(shaders::shader_frag) / sizeof(uint32_t))));
        }).set_name("insert text shaders"));
}
}  // namespace epix::text