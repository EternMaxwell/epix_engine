#include <epix/camera/mesh.hpp>
#include <epix/mesh/components.hpp>

namespace epix::mesh {

/** @brief Give every `Mesh3d` an `Aabb` and keep it current (Bevy `calculate_bounds`,
 *  `bevy_camera/src/visibility/mod.rs:451-486`).
 *
 *  **Placement note.** Bevy declares this in `bevy_camera::visibility`; this port puts it
 *  in `mesh` because the camera module cannot see `Mesh3d` without a cycle - `mesh` already
 *  includes `camera`, and `components.hpp` pulls in `camera/visibility.hpp` for
 *  `Visibility`/`VisibilityClass`. The behaviour is Bevy's; only the home differs, the same
 *  kind of placement difference `PBR-889`/`CAM-56` already record.
 *
 *  Like the port's `sprite::calculate_bounds_2d`, which is the direct template for this
 *  body, the update half compares before writing so an unchanged bound does not re-mark
 *  `Aabb` as modified. Bevy writes unconditionally; that difference is the 2D system's and
 *  is mirrored here so the two bounds systems behave alike rather than diverging from each
 *  other. */
void calculate_bounds(ecs::Commands commands, ecs::Res<assets::Assets<Mesh>> meshes,
                      ecs::Query<ecs::Item<ecs::Entity, const Mesh3d&>,
                                 ecs::Filter<ecs::Without<camera::Aabb>, ecs::Without<camera::NoFrustumCulling>,
                                             ecs::Without<camera::NoAutoAabb>>>
                          new_mesh_aabb,
                      ecs::Query<ecs::Item<ecs::Ref<Mesh3d>, ecs::Mut<camera::Aabb>>,
                                 ecs::Filter<ecs::Modified<Mesh3d>,
                                             ecs::Without<camera::NoFrustumCulling>,
                                             ecs::Without<camera::NoAutoAabb>>>
                          update_mesh_aabb) {
    for (auto&& [entity, mesh3d] : new_mesh_aabb.iter()) {
        if (const auto mesh_asset = meshes->get(mesh3d.handle.id())) {
            if (const auto aabb = camera::MeshAabb<Mesh>::compute_aabb(mesh_asset->get())) {
                // Bevy `commands.entity(entity).try_insert(aabb)`: an existing `Aabb` is
                // replaced, which is also what the 2D system does at sprite.cpp:151.
                commands.entity(entity).insert(*aabb);
            }
        }
    }

    for (auto&& [mesh3d, old_aabb] : update_mesh_aabb.iter()) {
        if (const auto mesh_asset = meshes->get(mesh3d->handle.id())) {
            if (const auto aabb = camera::MeshAabb<Mesh>::compute_aabb(mesh_asset->get()); aabb) {
                const auto& old = old_aabb.get();
                if (glm::any(glm::notEqual(old.center, aabb->center)) ||
                    glm::any(glm::notEqual(old.half_extents, aabb->half_extents))) {
                    old_aabb.get_mut() = *aabb;
                }
            }
        }
    }
}

}  // namespace epix::mesh
