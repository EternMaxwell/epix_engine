#include "epix/mesh/render.hpp"

using namespace epix;
using namespace epix::mesh;

namespace epix::mesh::shaders {
#include "shaders/mesh_pos.frag.h"
#include "shaders/mesh_pos.vert.h"
#include "shaders/mesh_pos_color.frag.h"
#include "shaders/mesh_pos_color.vert.h"
#include "shaders/mesh_pos_normal.frag.h"
#include "shaders/mesh_pos_normal.vert.h"
#include "shaders/mesh_pos_uv0.frag.h"
#include "shaders/mesh_pos_uv0.vert.h"
#include "shaders/mesh_pos_uv0_color.frag.h"
#include "shaders/mesh_pos_uv0_color.vert.h"
}  // namespace epix::mesh::shaders

void MeshPipelinePlugin::build(epix::App& app) {
    auto& render_app = app.sub_app_mut(render::Render);
    MeshPipeline pipeline;
    auto device           = render_app.world().resource<nvrhi::DeviceHandle>();
    pipeline._view_layout = app.world().resource<render::view::ViewUniformBindingLayout>().layout;
    pipeline._mesh_layout =
        device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                        .addItem(nvrhi::BindingLayoutItem::RawBuffer_SRV(0))
                                        .setVisibility(nvrhi::ShaderType::Vertex)
                                        .setBindingOffsets(nvrhi::VulkanBindingOffsets{0, 0, 0, 0}));
    pipeline._texture_layout =
        device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                                        .addItem(nvrhi::BindingLayoutItem::Sampler(1))
                                        .setVisibility(nvrhi::ShaderType::Pixel)
                                        .setBindingOffsets(nvrhi::VulkanBindingOffsets{0, 0, 0, 0}));
    pipeline._push_constant_layout =
        device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                        .setVisibility(nvrhi::ShaderType::All)
                                        .addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(glm::vec4))));
    render_app.world_mut().insert_resource<MeshPipeline>(std::move(pipeline));
}
void MeshPipelinePlugin::finish(epix::App& app) {
    auto& assets   = app.world_mut().resource_mut<assets::Assets<render::Shader>>();
    auto& pipeline = app.sub_app_mut(render::Render).world_mut().resource_mut<MeshPipeline>();

    // Insert generated SPIR-V shaders into the engine's shader assets and
    // populate the pipeline-local shader maps so specialize() can look them up.
    // Use stable UUIDs for each shader variant.
    auto insert_shader = [&](const uuids::uuid& id, const uint32_t* data, size_t count) {
        auto res = assets.insert(assets::AssetId<render::Shader>(id),
                                 render::Shader::spirv(std::vector<uint32_t>(data, data + count)));
    };

    // Helper to compute array sizes at compile time
    auto arr_size = [](const uint32_t* arr, size_t bytes) { return bytes / sizeof(uint32_t); };

    using namespace epix::mesh::shaders;

    // Define UUIDs for each shader (vertex and fragment pairs)
    const uuids::uuid mesh_pos_vert_id = uuids::uuid::from_string("d623b121-16af-441e-bdea-5a71239b01fe").value();
    const uuids::uuid mesh_pos_frag_id = uuids::uuid::from_string("0fd804c7-0c94-44c9-96b7-e714568f23ce").value();

    const uuids::uuid mesh_pos_color_vert_id = uuids::uuid::from_string("66554311-d605-4d87-b14f-98f316b1dc43").value();
    const uuids::uuid mesh_pos_color_frag_id = uuids::uuid::from_string("c1548564-8fcc-4288-b3d8-29daac03eca2").value();

    const uuids::uuid mesh_pos_uv0_vert_id = uuids::uuid::from_string("0a6bb91f-4420-4ed7-aafe-9e7011eecdf0").value();
    const uuids::uuid mesh_pos_uv0_frag_id = uuids::uuid::from_string("68f4dfef-03e5-4376-b27f-cfb2e060e10d").value();

    const uuids::uuid mesh_pos_uv0_color_vert_id =
        uuids::uuid::from_string("2c8d5c0a-4aff-49df-b236-9bff6d7a4fae").value();
    const uuids::uuid mesh_pos_uv0_color_frag_id =
        uuids::uuid::from_string("872fe149-b219-4850-a7dd-8685270127ee").value();

    const uuids::uuid mesh_pos_normal_vert_id =
        uuids::uuid::from_string("aae5361f-a639-4655-9f70-02c541fc577f").value();
    const uuids::uuid mesh_pos_normal_frag_id =
        uuids::uuid::from_string("b8f409f8-3724-4282-a318-f6a400e86711").value();

    // Insert each shader into assets
    insert_shader(mesh_pos_vert_id, mesh_pos_vert, sizeof(mesh_pos_vert) / sizeof(uint32_t));
    insert_shader(mesh_pos_frag_id, mesh_pos_frag, sizeof(mesh_pos_frag) / sizeof(uint32_t));

    insert_shader(mesh_pos_color_vert_id, mesh_pos_color_vert, sizeof(mesh_pos_color_vert) / sizeof(uint32_t));
    insert_shader(mesh_pos_color_frag_id, mesh_pos_color_frag, sizeof(mesh_pos_color_frag) / sizeof(uint32_t));

    insert_shader(mesh_pos_uv0_vert_id, mesh_pos_uv0_vert, sizeof(mesh_pos_uv0_vert) / sizeof(uint32_t));
    insert_shader(mesh_pos_uv0_frag_id, mesh_pos_uv0_frag, sizeof(mesh_pos_uv0_frag) / sizeof(uint32_t));

    insert_shader(mesh_pos_uv0_color_vert_id, mesh_pos_uv0_color_vert,
                  sizeof(mesh_pos_uv0_color_vert) / sizeof(uint32_t));
    insert_shader(mesh_pos_uv0_color_frag_id, mesh_pos_uv0_color_frag,
                  sizeof(mesh_pos_uv0_color_frag) / sizeof(uint32_t));

    insert_shader(mesh_pos_normal_vert_id, mesh_pos_normal_vert, sizeof(mesh_pos_normal_vert) / sizeof(uint32_t));
    insert_shader(mesh_pos_normal_frag_id, mesh_pos_normal_frag, sizeof(mesh_pos_normal_frag) / sizeof(uint32_t));

    // Populate the pipeline shader maps for flag -> asset id
    // Flags from render.hpp: pos=1, color=2, normal=4, uv0=8, uv1=16
    pipeline.vertex_shaders.emplace(static_cast<uint8_t>(MeshPipeline::PipelineFlag::pos),
                                    assets::AssetId<render::Shader>(mesh_pos_vert_id));
    pipeline.fragment_shaders.emplace(static_cast<uint8_t>(MeshPipeline::PipelineFlag::pos),
                                      assets::AssetId<render::Shader>(mesh_pos_frag_id));

    pipeline.vertex_shaders.emplace(
        static_cast<uint8_t>(MeshPipeline::PipelineFlag::pos | MeshPipeline::PipelineFlag::color),
        assets::AssetId<render::Shader>(mesh_pos_color_vert_id));
    pipeline.fragment_shaders.emplace(
        static_cast<uint8_t>(MeshPipeline::PipelineFlag::pos | MeshPipeline::PipelineFlag::color),
        assets::AssetId<render::Shader>(mesh_pos_color_frag_id));

    pipeline.vertex_shaders.emplace(
        static_cast<uint8_t>(MeshPipeline::PipelineFlag::pos | MeshPipeline::PipelineFlag::uv0),
        assets::AssetId<render::Shader>(mesh_pos_uv0_vert_id));
    pipeline.fragment_shaders.emplace(
        static_cast<uint8_t>(MeshPipeline::PipelineFlag::pos | MeshPipeline::PipelineFlag::uv0),
        assets::AssetId<render::Shader>(mesh_pos_uv0_frag_id));

    pipeline.vertex_shaders.emplace(
        static_cast<uint8_t>(MeshPipeline::PipelineFlag::pos | MeshPipeline::PipelineFlag::uv0 |
                             MeshPipeline::PipelineFlag::color),
        assets::AssetId<render::Shader>(mesh_pos_uv0_color_vert_id));
    pipeline.fragment_shaders.emplace(
        static_cast<uint8_t>(MeshPipeline::PipelineFlag::pos | MeshPipeline::PipelineFlag::uv0 |
                             MeshPipeline::PipelineFlag::color),
        assets::AssetId<render::Shader>(mesh_pos_uv0_color_frag_id));

    pipeline.vertex_shaders.emplace(
        static_cast<uint8_t>(MeshPipeline::PipelineFlag::pos | MeshPipeline::PipelineFlag::normal),
        assets::AssetId<render::Shader>(mesh_pos_normal_vert_id));
    pipeline.fragment_shaders.emplace(
        static_cast<uint8_t>(MeshPipeline::PipelineFlag::pos | MeshPipeline::PipelineFlag::normal),
        assets::AssetId<render::Shader>(mesh_pos_normal_frag_id));
}

std::optional<render::RenderPipelineId> MeshPipeline::specialize(render::PipelineServer& pipeline_server,
                                                                 const MeshAttributeLayout& mesh_layout) {
    size_t flag = 0;
    if (mesh_layout.contains_attribute(Mesh::ATTRIBUTE_POSITION)) flag |= MeshPipeline::PipelineFlag::pos;
    if (mesh_layout.contains_attribute(Mesh::ATTRIBUTE_COLOR)) flag |= MeshPipeline::PipelineFlag::color;
    if (mesh_layout.contains_attribute(Mesh::ATTRIBUTE_NORMAL)) flag |= MeshPipeline::PipelineFlag::normal;
    if (mesh_layout.contains_attribute(Mesh::ATTRIBUTE_UV0)) flag |= MeshPipeline::PipelineFlag::uv0;
    if (mesh_layout.contains_attribute(Mesh::ATTRIBUTE_UV1)) flag |= MeshPipeline::PipelineFlag::uv1;
    size_t cache_flag = flag | (static_cast<size_t>(mesh_layout.primitive_type) << 8);
    if (auto cached_pipeline = _get_cached_pipeline(flag); cached_pipeline.has_value()) {
        return *cached_pipeline;
    }

    render::RenderPipelineDesc desc;
    desc.setPrimType(mesh_layout.primitive_type);
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
                nvrhi::RasterState().setCullMode(nvrhi::RasterCullMode::Back).setFrontCounterClockwise(true)));
    std::vector<nvrhi::VertexAttributeDesc> input_layouts;
    for (auto&& [slot, attribute] : mesh_layout) {
        input_layouts.push_back(nvrhi::VertexAttributeDesc()
                                    .setName("Position")
                                    .setFormat(attribute.format)
                                    .setOffset(0)
                                    .setElementStride(nvrhi::getFormatInfo(attribute.format).bytesPerBlock)
                                    .setBufferIndex(slot));
    }
    auto input_layout =
        pipeline_server.get_device()->createInputLayout(input_layouts.data(), (uint32_t)input_layouts.size(), nullptr);
    desc.setInputLayout(input_layout);
    desc.addBindingLayout(_view_layout);
    desc.addBindingLayout(_mesh_layout);
    // if no color, add a push constant
    if (!mesh_layout.contains_attribute(Mesh::ATTRIBUTE_COLOR)) {
        desc.addBindingLayout(_push_constant_layout);
    }
    if (mesh_layout.contains_attribute(Mesh::ATTRIBUTE_UV0)) {
        desc.addBindingLayout(_texture_layout);
    }
    std::optional<assets::AssetId<render::Shader>> vertex_shader   = _get_vertex_shader(flag);
    std::optional<assets::AssetId<render::Shader>> fragment_shader = _get_fragment_shader(flag);
    if (!vertex_shader || !fragment_shader) {
        spdlog::error("Mesh2dPipeline::specialize: missing shader for flag {}", flag);
        return std::nullopt;
    }
    desc.setVertexShader(render::ShaderInfo{
        .shader    = *vertex_shader,
        .debugName = "Mesh2dVertex",
    });
    desc.setFragmentShader(render::ShaderInfo{
        .shader    = *fragment_shader,
        .debugName = "Mesh2dFragment",
    });
    auto pipeline_id = pipeline_server.queue_render_pipeline(desc);
    _set_cached_pipeline(flag, pipeline_id);
    return pipeline_id;
}