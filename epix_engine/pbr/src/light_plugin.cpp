#include <epix/pbr/light_plugin.hpp>

#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/pbr/light_systems.hpp>
#include <epix/transform.hpp>

namespace epix::pbr {

void LightPlugin::attach(epix::app::App& app) {
    // Bevy's four initialised resources. Every one of the types already exists in this
    // port; the plugin is what inserts them into an app.
    app.world_mut().init_resource<GlobalVisibleClusterableObjects>();
    app.world_mut().init_resource<GlobalAmbientLight>();
    app.world_mut().init_resource<DirectionalLightShadowMap>();
    app.world_mut().init_resource<PointLightShadowMap>();

    // Bevy also attaches two ambiguity assertions here - one on
    // `UpdateDirectionalLightCascades`, one on `CheckLightVisibility` - and one more
    // between the directional and spot frustum updates. This scheduler has no
    // `ambiguous_with`, so those three are the only part of Bevy's `build` that stays
    // unwritten; each suppresses a diagnostic rather than changing behaviour.
    app.configure_sets(epix::app::ScheduleInfo(epix::app::PostUpdate),
                       epix::ecs::sets(SimulationLightSystems::AddClusters,
                                       SimulationLightSystems::AssignLightsToClusters,
                                       SimulationLightSystems::UpdateDirectionalLightCascades,
                                       SimulationLightSystems::UpdateLightFrusta,
                                       SimulationLightSystems::CheckLightVisibility));

    const auto post_update = epix::app::ScheduleInfo(epix::app::PostUpdate);

    // A system's function pointer is itself a set label in this scheduler
    // (`SystemSetLabel(func_)`, `ecs/system.hpp:209-211`), which is how Bevy's
    // system-to-system orderings are expressed below.
    auto system_label = [](auto* function) { return epix::ecs::SystemSetLabel(function); };

    // `validate_shadow_map_size.before(build_directional_light_cascades)`
    app.add_systems(post_update, epix::ecs::into(validate_shadow_map_size)
                                     .before(system_label(build_directional_light_cascades))
                                     .set_name("validate shadow map size"));

    // `add_clusters` in `AddClusters`, after `CameraUpdateSystems`.
    app.add_systems(post_update,
                    epix::ecs::into(add_clusters)
                        .in_set(SimulationLightSystems::AddClusters)
                        .after(epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                        .set_name("add clusters"));

    // `assign_objects_to_clusters` in `AssignLightsToClusters`, after transform
    // propagation, visibility and camera update.
    app.add_systems(post_update,
                    epix::ecs::into(assign_objects_to_clusters)
                        .in_set(SimulationLightSystems::AssignLightsToClusters)
                        .after(epix::transform::TransformSystems::Propagate)
                        .after(epix::camera::VisibilitySystems::CheckVisibility)
                        .after(epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                        .set_name("assign objects to clusters"));

    // `clear_directional_light_cascades`, after propagation and camera update.
    app.add_systems(post_update,
                    epix::ecs::into(clear_directional_light_cascades)
                        .in_set(SimulationLightSystems::UpdateDirectionalLightCascades)
                        .after(epix::transform::TransformSystems::Propagate)
                        .after(epix::camera::CameraUpdateSystems::CameraUpdateSystem)
                        .set_name("clear directional light cascades"));

    // `build_directional_light_cascades` in the same set, and specifically after the
    // clear: building first would wipe the cascades in the same frame.
    app.add_systems(post_update,
                    epix::ecs::into(build_directional_light_cascades)
                        .in_set(SimulationLightSystems::UpdateDirectionalLightCascades)
                        .after(system_label(clear_directional_light_cascades))
                        .set_name("build directional light cascades"));

    // The three frustum updates, all in `UpdateLightFrusta`.
    app.add_systems(post_update,
                    epix::ecs::into(update_directional_light_frusta)
                        .in_set(SimulationLightSystems::UpdateLightFrusta)
                        .after(epix::camera::VisibilitySystems::CheckVisibility)
                        .after(epix::transform::TransformSystems::Propagate)
                        .after(SimulationLightSystems::UpdateDirectionalLightCascades)
                        .set_name("update directional light frusta"));
    app.add_systems(post_update,
                    epix::ecs::into(update_point_light_frusta)
                        .in_set(SimulationLightSystems::UpdateLightFrusta)
                        .after(epix::transform::TransformSystems::Propagate)
                        .after(SimulationLightSystems::AssignLightsToClusters)
                        .set_name("update point light frusta"));
    app.add_systems(post_update,
                    epix::ecs::into(update_spot_light_frusta)
                        .in_set(SimulationLightSystems::UpdateLightFrusta)
                        .after(epix::transform::TransformSystems::Propagate)
                        .after(SimulationLightSystems::AssignLightsToClusters)
                        .set_name("update spot light frusta"));

    // The two mesh-visibility checks. Bevy runs them after the frusta and after
    // visibility, and before hidden entities are marked: lights can see entities no
    // camera sees, so that shadows are cast for what is off screen.
    app.add_systems(post_update,
                    epix::ecs::into(check_dir_light_mesh_visibility)
                        .in_set(SimulationLightSystems::CheckLightVisibility)
                        .after(epix::camera::VisibilitySystems::CalculateBounds)
                        .after(epix::transform::TransformSystems::Propagate)
                        .after(SimulationLightSystems::UpdateLightFrusta)
                        .after(epix::camera::VisibilitySystems::CheckVisibility)
                        .before(epix::camera::VisibilitySystems::MarkNewlyHiddenEntitiesInvisible)
                        .set_name("check dir light mesh visibility"));
    app.add_systems(post_update,
                    epix::ecs::into(check_point_light_mesh_visibility)
                        .in_set(SimulationLightSystems::CheckLightVisibility)
                        .after(epix::camera::VisibilitySystems::CalculateBounds)
                        .after(epix::transform::TransformSystems::Propagate)
                        .after(SimulationLightSystems::UpdateLightFrusta)
                        .after(epix::camera::VisibilitySystems::CheckVisibility)
                        .before(epix::camera::VisibilitySystems::MarkNewlyHiddenEntitiesInvisible)
                        .set_name("check point light mesh visibility"));
}

}  // namespace epix::pbr
