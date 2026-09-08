#include <epix/sprite.hpp>

#include <epix/camera.hpp>
#include <epix/mesh.hpp>

#include <spdlog/spdlog.h>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

void sprite::calculate_bounds_2d(
    Commands commands,
    Res<assets::Assets<mesh::Mesh>> meshes,
    Query<Item<Entity, const mesh::Mesh2d&>,
          Filter<Without<camera::Aabb>, Without<camera::NoFrustumCulling>, Without<camera::NoAutoAabb>>>
        new_mesh_aabb,
    Query<Item<Ref<mesh::Mesh2d>, Mut<camera::Aabb>>,
          Filter<Or<assets::AssetChanged<mesh::Mesh2d>, Modified<mesh::Mesh2d>>,
                 Without<camera::NoFrustumCulling>,
                 Without<camera::NoAutoAabb>>>
        update_mesh_aabb) {
    for (auto&& [entity, mesh2d] : new_mesh_aabb.iter()) {
        if (const auto mesh_asset = meshes->get(mesh2d.handle.id())) {
            if (const auto aabb = camera::MeshAabb<mesh::Mesh>::compute_aabb(mesh_asset->get())) {
                commands.entity(entity).insert_if_new(*aabb);
            }
        }
    }

    for (auto&& [mesh2d, old_aabb] : update_mesh_aabb.iter()) {
        if (const auto mesh_asset = meshes->get(mesh2d->handle.id())) {
            if (const auto aabb = camera::MeshAabb<mesh::Mesh>::compute_aabb(mesh_asset->get());
                aabb && (glm::any(glm::notEqual(old_aabb->center, aabb->center)) ||
                         glm::any(glm::notEqual(old_aabb->half_extents, aabb->half_extents)))) {
                old_aabb.get_mut() = *aabb;
            }
        }
    }
}

void sprite::SpritePlugin::attach(App& app) {
    spdlog::debug("[sprite] Attaching SpritePlugin.");
    app.world_mut().register_required_components<sprite::Sprite, transform::Transform>();
    app.world_mut().register_required_components<sprite::Sprite, camera::Visibility>();
    app.world_mut().register_required_components_with<sprite::Sprite>(
        [] { return camera::VisibilityClass{meta::type_index(meta::type_id<sprite::Sprite>())}; });
    app.add_systems(app::PostUpdate, into(sprite::calculate_bounds_2d)
                                         .in_set(camera::VisibilitySystems::CalculateBounds)
                                         .before(camera::VisibilitySystems::CheckVisibility)
                                         .set_name("calculate bounds 2d"));
}
