

#include <algorithm>
#include <cstring>
#include <epix/mesh/mesh.hpp>
#include <epix/render/mesh/render_asset.hpp>

using namespace epix;
using namespace epix::render::mesh;

void epix::render::mesh::inherit_weights(
    ecs::Query<ecs::Item<const ecs::Children&, const ::epix::mesh::MorphWeights&>,
               ecs::Filter<ecs::Without<::epix::mesh::Mesh3d>, ecs::Modified<::epix::mesh::MorphWeights>>> morph_nodes,
    ecs::Query<ecs::Item<ecs::Mut<::epix::mesh::MeshMorphWeights>>, ecs::With<::epix::mesh::Mesh3d>> morph_primitives) {
    for (auto&& [children, parent_weights] : morph_nodes.iter()) {
        for (const ecs::Entity child : children.entities()) {
            if (auto primitive = morph_primitives.get(child)) {
                auto&& [child_weights] = *primitive;
                auto& weights          = child_weights.get_mut();
                weights.clear_weights();
                weights.extend_weights(parent_weights.weights());
            }
        }
    }
}

void epix::render::mesh::MorphPlugin::attach(app::App& app) {
    app.configure_sets(app::PostUpdate, ecs::sets(::epix::mesh::InheritWeightSystems{}));
    app.add_systems(
        app::PostUpdate,
        ecs::into(inherit_weights).in_set(::epix::mesh::InheritWeightSystems{}).set_name("inherit morph weights"));
}
