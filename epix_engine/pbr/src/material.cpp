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

}  // namespace epix::pbr
