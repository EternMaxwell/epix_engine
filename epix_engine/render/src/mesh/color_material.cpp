#include <epix/core_graph.hpp>
#include <epix/core_graph/core2d.hpp>
#include <epix/render/pipeline.hpp>
#include <epix/render/render_phase.hpp>
#include <unordered_map>

#include "epix/mesh/material.hpp"
#include "epix/mesh/mesh.hpp"
#include "epix/mesh/render.hpp"
#include "nvrhi/nvrhi.h"

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
struct ImageMaterialMeshes : std::unordered_map<Entity, ImageMaterial> {};
void extract_image_materials2d(ResMut<ImageMaterialMeshes> image_material_meshes,
                               Extract<Query<Item<Entity, const ImageMaterial&>, With<Mesh2d>>> query) {
    image_material_meshes->clear();
    for (auto&& [entity, image_material] : query.iter()) {
        image_material_meshes->emplace(entity, image_material);
    }
}
template <render::render_phase::PhaseItem P>
struct BindImage {
    nvrhi::BindingSetHandle binding_set;
    nvrhi::BindingLayoutHandle image_layout;
    void prepare(World& world) {
        auto& mesh_pipeline = world.resource<MeshPipeline>();
        image_layout        = mesh_pipeline.texture_layout();
    }
    bool render(const P& item,
                Item<> entity_item,
                std::optional<Item<>> view_item,
                ParamSet<Res<ImageMaterialMeshes>,
                         ResMut<render::assets::RenderAssets<image::Image>>,
                         Res<nvrhi::DeviceHandle>,
                         Res<render::DefaultSampler>> params,
                render::render_phase::DrawContext& ctx) {
        auto&& [image_material_meshes, image_assets, device, default_sampler] = params.get();
        auto entity                                                           = item.entity();
        if (!image_material_meshes->contains(entity)) {
            spdlog::warn("Entity {:#x} has no ImageMaterial. Skipping.", entity.index);
            return false;
        }
        auto&& image_material = image_material_meshes->at(entity);
        auto* image           = image_assets->try_get(image_material.image);
        if (!image) return false;

        binding_set =
            device.get()->createBindingSet(nvrhi::BindingSetDesc()
                                               .addItem(nvrhi::BindingSetItem::Texture_SRV(0, *image))
                                               .addItem(nvrhi::BindingSetItem::Sampler(1, default_sampler->handle)),
                                           image_layout);

        ctx.graphics_state.bindings.resize(3);
        ctx.graphics_state.bindings[2] = binding_set;
        return true;
    }
};
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
            {
                static MeshAttributeLayout expected_layout = []() {
                    MeshAttributeLayout layout;
                    layout.add_attribute(Mesh::ATTRIBUTE_POSITION);
                    return layout;
                }();
                if (gpu_mesh->attribute_layout() != expected_layout) {
                    spdlog::warn(
                        "Entity {:#x} has ColorMaterial but its mesh layout is incompatible. Expected layout:\n{}, got "
                        "layout:\n{}. Skipping.",
                        entity.index, expected_layout.to_string(), gpu_mesh->attribute_layout().to_string());
                    continue;
                }
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
                      PushColorMaterial, DrawMesh>(*draw_functions),
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
            {
                static MeshAttributeLayout expected_layout = []() {
                    MeshAttributeLayout layout;
                    layout.add_attribute(Mesh::ATTRIBUTE_POSITION);
                    layout.add_attribute(Mesh::ATTRIBUTE_COLOR);
                    return layout;
                }();
                if (gpu_mesh->attribute_layout() != expected_layout) {
                    spdlog::warn(
                        "Entity {:#x} has NoMaterial but its mesh layout is incompatible. Expected layout:\n{}, got "
                        "layout:\n{}. Skipping.",
                        entity.index, expected_layout.to_string(), gpu_mesh->attribute_layout().to_string());
                    continue;
                }
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
                      DrawMesh>(*draw_functions),
                .batch_count = 1,
            });
        }
    }
}
void queue_image_material_mesh_2d(Res<ImageMaterialMeshes> image_material_meshes,
                                  Res<RenderMesh2dInstances> render_meshes,
                                  ResMut<MeshPipeline> mesh_pipeline,
                                  ResMut<render::PipelineServer> pipeline_server,
                                  Res<render::assets::RenderAssets<Mesh>> gpu_meshes,
                                  ResMut<render::render_phase::DrawFunctions<render::core_2d::Opaque2D>> draw_functions,
                                  Query<Item<render::render_phase::RenderPhase<render::core_2d::Opaque2D>&>,
                                        With<render::camera::ExtractedCamera, render::view::ViewTarget>> views) {
    for (auto&& [phase] : views.iter()) {
        for (auto&& [entity, image_material] : *image_material_meshes) {
            if (!render_meshes->contains(entity)) {
                spdlog::warn("Entity {:#x} has ImageMaterial but no RenderMesh2d. Skipping.", entity.index);
                continue;
            }
            auto&& render_mesh = render_meshes->at(entity);
            auto* gpu_mesh     = gpu_meshes->try_get(render_mesh.mesh);
            if (!gpu_mesh) {
                spdlog::warn("Entity {:#x} has no GPU mesh. Skipping.", entity.index);
                continue;
            }
            {
                static MeshAttributeLayout expected_layout1 = []() {
                    MeshAttributeLayout layout;
                    layout.add_attribute(Mesh::ATTRIBUTE_POSITION);
                    layout.add_attribute(Mesh::ATTRIBUTE_UV0);
                    return layout;
                }();
                static MeshAttributeLayout expected_layout2 = []() {
                    MeshAttributeLayout layout;
                    layout.add_attribute(Mesh::ATTRIBUTE_POSITION);
                    layout.add_attribute(Mesh::ATTRIBUTE_COLOR);
                    layout.add_attribute(Mesh::ATTRIBUTE_UV0);
                    return layout;
                }();
                if (gpu_mesh->attribute_layout() != expected_layout1 &&
                    gpu_mesh->attribute_layout() != expected_layout2) {
                    spdlog::warn(
                        "Entity {:#x} has ImageMaterial but its mesh layout is incompatible. Expected layouts:\n{} "
                        "OR\n{}, got layout:\n{}. Skipping.",
                        entity.index, expected_layout1.to_string(), expected_layout2.to_string(),
                        gpu_mesh->attribute_layout().to_string());
                    continue;
                }
            }
            auto pipeline_id = mesh_pipeline->specialize(*pipeline_server, gpu_mesh->attribute_layout());
            if (!pipeline_id.has_value()) {
                spdlog::warn(
                    "Failed to specialize pipeline for image materialed mesh entity {:#x} with mesh layout: \n{}. "
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
                      BindImage, DrawMesh>(*draw_functions),
                .batch_count = 1,
            });
        }
    }
}
void ColorMaterialPlugin::build(epix::App& app) {
    auto& render_app = app.sub_app_mut(render::Render);
    render_app.world_mut().init_resource<MeshColorMaterials>();
    render_app.world_mut().init_resource<NoMaterialMeshes>();
    render_app.world_mut().init_resource<ImageMaterialMeshes>();
    render_app.add_systems(render::ExtractSchedule,
                           into(extract_color_materials2d, extract_no_materials2d, extract_image_materials2d)
                               .set_names(std::array{"extract color materials 2d", "extract no materials 2d",
                                                     "extract image materials 2d"}));
    render_app.add_systems(render::Render,
                           into(queue_color_material_mesh_2d, queue_no_material_mesh_2d, queue_image_material_mesh_2d)
                               .set_names(std::array{"queue color material mesh 2d", "queue no material mesh 2d",
                                                     "queue image material mesh 2d"})
                               .in_set(render::RenderSet::Queue));
}
}  // namespace epix::mesh