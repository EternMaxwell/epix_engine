#include <epix/core_graph.hpp>
#include <epix/core_graph/core2d.hpp>
#include <epix/render/pipeline.hpp>
#include <epix/render/render_phase.hpp>

#include "epix/mesh/material.hpp"
#include "epix/mesh/render.hpp"

namespace epix::mesh {
void extract_color_materials2d(ResMut<MeshColorMaterials> color_materials,
                               Extract<Query<Item<Entity, const ColorMaterial&>, With<Mesh2d>>> query) {
    color_materials->clear();
    for (auto&& [entity, color_material] : query.iter()) {
        color_materials->emplace(entity, color_material);
    }
}
struct NoMaterialMeshes : std::vector<Entity> {};
void extract_no_materials2d(ResMut<NoMaterialMeshes> no_material_meshes,
                            Extract<Query<Item<Entity>, With<Mesh2d, NoMaterial>>> query) {
    no_material_meshes->clear();
    for (auto&& [entity] : query.iter()) {
        no_material_meshes->emplace_back(entity);
    }
}
void queue_color_material_mesh_2d(Res<MeshColorMaterials> color_materials,
                                  Res<RenderMesh2dInstances> render_meshes,
                                  ResMut<MeshPipeline> mesh_pipeline,
                                  ResMut<render::PipelineServer> pipeline_server,
                                  Res<render::assets::RenderAssets<Mesh>> gpu_meshes,
                                  ResMut<render::render_phase::DrawFunctions<render::core_2d::Opaque2D>> draw_functions,
                                  Query<Item<render::render_phase::RenderPhase<render::core_2d::Opaque2D>&>,
                                        With<render::camera::ExtractedCamera, render::view::ViewTarget>> views) {
    for (auto&& [phase] : views.iter()) {
        for (auto&& [entity, color_material] : *color_materials) {
            if (!render_meshes->contains(entity)) {
                spdlog::warn("Entity {:#x} has ColorMaterial but no RenderMesh2d. Skipping.", entity.index);
                continue;
            }
            auto&& render_mesh = render_meshes->at(entity);
            auto* gpu_mesh     = gpu_meshes->try_get(render_mesh.mesh);
            if (!gpu_mesh) {
                spdlog::warn("Entity {:#x} has no GPU mesh. Skipping.", entity.index);
                continue;
            }
            auto pipeline_id = mesh_pipeline->specialize(*pipeline_server, gpu_mesh->attribute_layout());
            if (!pipeline_id.has_value()) {
                spdlog::warn(
                    "Failed to specialize pipeline for color materialed mesh entity {:#x} with mesh layout: \n{}. "
                    "Skipping.",
                    entity.index, gpu_mesh->attribute_layout().to_string());
                continue;
            }
            phase.add(render::core_2d::Opaque2D{
                .id          = entity,
                .pipeline_id = pipeline_id.value(),
                .draw_func   = render::render_phase::get_or_add_render_commands<
                      render::core_2d::Opaque2D, render::render_phase::SetItemPipeline,
                      render::view::BindViewUniform<0>::Command, mesh::BindMeshUniform<1>::Command, BindGpuMesh,
                      BindColorPushConstants, render::render_phase::SetGraphicsState, PushColorMaterial, DrawMesh>(
                    *draw_functions),
                .batch_count = 1,
            });
        }
    }
}
void queue_no_material_mesh_2d(Res<NoMaterialMeshes> no_material_meshes,
                               Res<RenderMesh2dInstances> render_meshes,
                               ResMut<MeshPipeline> mesh_pipeline,
                               ResMut<render::PipelineServer> pipeline_server,
                               Res<render::assets::RenderAssets<Mesh>> gpu_meshes,
                               ResMut<render::render_phase::DrawFunctions<render::core_2d::Opaque2D>> draw_functions,
                               Query<Item<render::render_phase::RenderPhase<render::core_2d::Opaque2D>&>,
                                     With<render::camera::ExtractedCamera, render::view::ViewTarget>> views) {
    for (auto&& [phase] : views.iter()) {
        for (auto& entity : *no_material_meshes) {
            if (!render_meshes->contains(entity)) {
                spdlog::warn("Entity {:#x} has NoMaterial but no RenderMesh2d. Skipping.", entity.index);
                continue;
            }
            auto&& render_mesh = render_meshes->at(entity);
            auto* gpu_mesh     = gpu_meshes->try_get(render_mesh.mesh);
            if (!gpu_mesh) {
                spdlog::warn("Entity {:#x} has no GPU mesh. Skipping.", entity.index);
                continue;
            }
            auto pipeline_id = mesh_pipeline->specialize(*pipeline_server, gpu_mesh->attribute_layout());
            if (!pipeline_id.has_value()) {
                spdlog::warn(
                    "Failed to specialize pipeline for no materialed mesh entity {:#x} with mesh layout: \n{}. "
                    "Skipping.",
                    entity.index, gpu_mesh->attribute_layout().to_string());
                continue;
            }
            phase.add(render::core_2d::Opaque2D{
                .id          = entity,
                .pipeline_id = pipeline_id.value(),
                .draw_func   = render::render_phase::get_or_add_render_commands<
                      render::core_2d::Opaque2D, render::render_phase::SetItemPipeline,
                      render::view::BindViewUniform<0>::Command, mesh::BindMeshUniform<1>::Command, BindGpuMesh,
                      render::render_phase::SetGraphicsState, DrawMesh>(*draw_functions),
                .batch_count = 1,
            });
        }
    }
}
void ColorMaterialPlugin::build(epix::App& app) {
    auto& render_app = app.sub_app_mut(render::Render);
    render_app.world_mut().init_resource<MeshColorMaterials>();
    render_app.world_mut().init_resource<NoMaterialMeshes>();
    render_app.add_systems(render::ExtractSchedule,
                           into(extract_color_materials2d, extract_no_materials2d)
                               .set_names(std::array{"extract color materials 2d", "extract no materials 2d"}));
    render_app.add_systems(render::Render,
                           into(queue_color_material_mesh_2d, queue_no_material_mesh_2d)
                               .set_names(std::array{"queue color material mesh 2d", "queue no material mesh 2d"})
                               .in_set(render::RenderSet::Queue));
}
}  // namespace epix::mesh