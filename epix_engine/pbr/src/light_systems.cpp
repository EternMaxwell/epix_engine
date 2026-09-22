#include <epix/pbr/light_systems.hpp>

#include <cstddef>
#include <glm/glm.hpp>

namespace epix::pbr {

void update_point_light_frusta(
    ecs::Res<GlobalVisibleClusterableObjects> global_lights,
    ecs::Query<ecs::Item<ecs::Entity, ecs::Ref<::epix::transform::GlobalTransform>, ecs::Ref<PointLight>,
                         ecs::Mut<::epix::camera::CubemapFrusta>>>
        lights) {
    const bool global_lights_changed = global_lights.is_modified();
    const auto& clustered          = global_lights.get().entities;

    for (auto&& [entity, transform, point_light, cubemap_frusta] : lights.iter()) {
        const bool light_changed = transform.is_modified() || point_light.is_modified();
        if (!should_update_point_light_frusta(light_changed, global_lights_changed,
                                              point_light.get().shadows_enabled, clustered.contains(entity))) {
            continue;
        }

        // Epix's GlobalTransform is a matrix, so the light's world position is its
        // translation column and its backward axis is the third column - the local
        // +Z of a right-handed transform. The light's scale and rotation are
        // deliberately not used, as in Bevy.
        const glm::mat4 world_from_light = transform.get().matrix;
        const glm::vec3 translation      = glm::vec3(world_from_light[3]);
        const glm::vec3 backward         = glm::vec3(world_from_light[2]);

        const auto frusta = point_light_cubemap_frusta(translation, backward, point_light.get().range,
                                                      point_light.get().shadow_map_near_z);
        auto& destination = (*cubemap_frusta).frusta;
        for (std::size_t face = 0; face < frusta.size(); ++face) {
            destination[face] = frusta[face];
        }
    }
}

void update_directional_light_frusta(
    ecs::Query<ecs::Item<const Cascades&, const DirectionalLight&, const ::epix::camera::ViewVisibility&,
                         ecs::Mut<::epix::camera::CascadesFrusta>>,
                ecs::Without<::epix::camera::Camera>>
        lights) {
    for (auto&& [cascades, directional_light, visibility, frusta] : lights.iter()) {
        if (!should_update_directional_cascade_frusta(directional_light.shadows_enabled, visibility.get())) {
            continue;
        }

        // Rebuilt whole, preserving the view keys, and with each cascade's near
        // plane pushed to infinity so it culls nothing in front of the light.
        camera::CascadesFrusta rebuilt;
        for (const auto& [view, view_cascades] : cascades.cascades) {
            auto& destination = rebuilt.frusta[view];
            destination.reserve(view_cascades.size());
            for (const Cascade& cascade : view_cascades) {
                destination.push_back(
                    directional_view_frustum(camera::Frustum::from_clip_from_world(cascade.clip_from_world)));
            }
        }
        *frusta = std::move(rebuilt);
    }
}

void clear_directional_light_cascades(ecs::Query<ecs::Item<const DirectionalLight&, ecs::Mut<Cascades>>> lights) {
    for (auto&& [directional_light, cascades] : lights.iter()) {
        if (!directional_light.shadows_enabled) continue;
        (*cascades).cascades.clear();
    }
}

void build_directional_light_cascades(
    ecs::Res<DirectionalLightShadowMap> shadow_map,
    ecs::Query<ecs::Item<ecs::Entity, const ::epix::transform::GlobalTransform&, const ::epix::camera::Projection&,
                         const ::epix::camera::Camera&>>
        views,
    ecs::Query<ecs::Item<const ::epix::transform::GlobalTransform&, const DirectionalLight&,
                         const CascadeShadowConfig&, ecs::Mut<Cascades>>>
        lights) {
    // The active views, with their projection and world matrix, collected first as
    // Bevy does - the light loop below needs each of them.
    struct ActiveView {
        ecs::Entity entity;
        const ::epix::camera::Projection* projection;
        glm::mat4 view_to_world;
    };
    std::vector<ActiveView> active_views;
    for (auto&& [entity, transform, projection, camera] : views.iter()) {
        if (!camera.is_active) continue;
        active_views.push_back(ActiveView{.entity       = entity,
                                          .projection   = &projection,
                                          .view_to_world = transform.matrix});
    }

    for (auto&& [transform, directional_light, config, cascades] : lights.iter()) {
        if (!directional_light.shadows_enabled) continue;

        // Rotation only, no translation, and with any scale stripped: the matrix
        // must be orthonormal for the shadows to be stable.
        const glm::mat3 basis = glm::mat3(transform.matrix);
        glm::mat3 rotation_basis;
        for (int column = 0; column < 3; ++column) {
            const glm::vec3 axis = basis[column];
            const float length   = glm::length(axis);
            rotation_basis[column] = length > 0.0f ? axis / length : axis;
        }
        const glm::quat light_rotation = glm::quat_cast(rotation_basis);
        const glm::mat4 world_from_light = glm::mat4_cast(light_rotation);

        const auto bounds = cascade_bounds(config);
        for (const ActiveView& view : active_views) {
            // Bevy zips the far bounds with near bounds that start at the configured
            // minimum distance and then follow the previous far bound scaled by the
            // overlap, which is what `cascade_bounds` returns.
            std::vector<std::array<glm::vec3, 8>> slices;
            slices.reserve(bounds.size());
            for (const auto& [near_bound, far_bound] : bounds) {
                // Negated because -Z is the camera's forward direction.
                slices.push_back(view.projection->get_frustum_corners(-near_bound, -far_bound));
            }

            (*cascades).cascades[view.entity] = build_view_cascades(
                slices, static_cast<float>(shadow_map.get().size), light_rotation, view.view_to_world);
        }
    }
}

}  // namespace epix::pbr
