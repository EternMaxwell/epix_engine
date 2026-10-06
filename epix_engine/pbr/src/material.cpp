#include <epix/pbr/material.hpp>

#include <utility>

namespace epix::pbr {

assets::UntypedAssetId RenderMaterialInstances::mesh_material(render::sync_world::MainEntity entity) const {
    const auto found = instances.find(entity);
    if (found == instances.end()) return DUMMY_MESH_MATERIAL;
    return found->second.asset_id;
}

std::optional<MaterialBindingId> RenderMaterialBindings::get(const assets::UntypedAssetId& asset_id) const {
    const auto found = bindings.find(asset_id);
    if (found == bindings.end()) return std::nullopt;
    return found->second;
}

void late_sweep_material_instances(ecs::ResMut<RenderMaterialInstances> material_instances,
                                   app::Extract<ecs::RemovedComponents<mesh::Mesh3d>> removed_meshes_query) {
    const ecs::Tick last_change_tick = material_instances->current_change_tick;

    for (const ecs::Entity entity : removed_meshes_query.read()) {
        const render::sync_world::MainEntity main_entity{entity};
        const auto found = material_instances->instances.find(main_entity);
        if (found != material_instances->instances.end() && found->second.last_change_tick != last_change_tick) {
            material_instances->instances.erase(found);
        }
    }

    material_instances->current_change_tick = ecs::Tick{last_change_tick.get() + 1};
}

void init_material_pipeline(ecs::Commands commands, ecs::Res<MeshPipeline> mesh_pipeline) {
    commands.insert_resource(MaterialPipeline{.mesh_pipeline = *mesh_pipeline});
}

}  // namespace epix::pbr
