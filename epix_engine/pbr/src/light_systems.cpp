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
    ecs::Query<ecs::Item<ecs::Ref<Cascades>, ecs::Ref<DirectionalLight>, ecs::Ref<::epix::camera::ViewVisibility>,
                         ecs::Mut<::epix::camera::CascadesFrusta>>,
                ecs::Without<::epix::camera::Camera>>
        lights) {
    for (auto&& [cascades, directional_light, visibility, frusta] : lights.iter()) {
        if (!should_update_directional_cascade_frusta(directional_light.get().shadows_enabled, visibility.get().get())) {
            continue;
        }

        // Rebuilt whole, preserving the view keys, and with each cascade's near
        // plane pushed to infinity so it culls nothing in front of the light.
        camera::CascadesFrusta rebuilt;
        for (const auto& [view, view_cascades] : cascades.get().cascades) {
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

}  // namespace epix::pbr
