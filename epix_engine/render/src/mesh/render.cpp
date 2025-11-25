#include "epix/mesh/render.hpp"
#include "epix/render/assets.hpp"
#include "epix/render/extract.hpp"

namespace epix::mesh {
void extract_mesh2d(ResMut<RenderMesh2dInstances> render_meshes,
                    ResMut<MeshUniforms> mesh_uniforms,
                    Extract<Query<Item<Entity, const transform::GlobalTransform&, const Mesh2d&>>> meshes) {
    mesh_uniforms->uniforms.clear();
    for (auto&& [entity, transform, mesh] : meshes.iter()) {
        render_meshes->emplace(entity, RenderMesh2d{
                                           .mesh           = mesh.handle.id(),
                                           .uniform_offset = mesh_uniforms->uniforms.size(),
                                       });
        mesh_uniforms->uniforms.push_back(MeshUniform{
            .model = transform.matrix,
        });
    }
}
void upload_mesh_uniforms(Res<nvrhi::DeviceHandle> device, ResMut<MeshUniforms> mesh_uniforms) {
    size_t total_bytes = mesh_uniforms->uniforms.size() * sizeof(MeshUniform);
    if (total_bytes == 0) return;
    if (!mesh_uniforms->buffer || mesh_uniforms->buffer->getDesc().byteSize < total_bytes) {
        mesh_uniforms->buffer = device.get()->createBuffer(nvrhi::BufferDesc()
                                                               .setByteSize(total_bytes)
                                                               .setCanHaveRawViews(true)
                                                               .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                                               .setKeepInitialState(true)
                                                               .setDebugName("Mesh Storage Buffer"));
    }
    auto cmd_list = device.get()->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
    cmd_list->open();
    cmd_list->writeBuffer(mesh_uniforms->buffer, mesh_uniforms->uniforms.data(),
                          mesh_uniforms->uniforms.size() * sizeof(MeshUniform));
    cmd_list->close();
    device.get_mut()->executeCommandList(cmd_list);
}
void MeshRenderPlugin::build(epix::App& app) {
    app.add_plugins(render::assets::ExtractAssetPlugin<Mesh>{});
    app.add_plugins(mesh::MeshPipelinePlugin{});
    auto& render_app = app.sub_app_mut(render::Render);
    render_app.world_mut().init_resource<MeshUniforms>();
    render_app.add_systems(render::ExtractSchedule, into(extract_mesh2d).set_name("extract mesh2d"));
    render_app.add_systems(
        render::Render,
        into(upload_mesh_uniforms).set_name("upload mesh uniforms").in_set(render::RenderSet::PrepareResources));
    render_app.world_mut().init_resource<RenderMesh2dInstances>();
}
void MeshRenderPlugin::finish(epix::App& app) { app.world_mut().init_resource<MeshPipeline>(); }
}  // namespace epix::mesh