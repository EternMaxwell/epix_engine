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
    // `UpdateDirectionalLightCascades`, one on `CheckLightVisibility` - because the
    // systems inside each set are deliberately independent. They suppress a diagnostic
    // rather than change behaviour, and this port has no ambiguity detector to suppress.
    app.configure_sets(epix::app::ScheduleInfo(epix::app::PostUpdate),
                       epix::ecs::sets(SimulationLightSystems::AddClusters,
                                       SimulationLightSystems::AssignLightsToClusters,
                                       SimulationLightSystems::UpdateDirectionalLightCascades,
                                       SimulationLightSystems::UpdateLightFrusta,
                                       SimulationLightSystems::CheckLightVisibility));

    const auto post_update = epix::app::ScheduleInfo(epix::app::PostUpdate);

    // Every registered system keeps Bevy's set membership, which is what orders the
    // simulation as a whole: clusters before assignment, assignment before the frusta,
    // the frusta before the visibility checks. Bevy's orderings *inside* those sets are
    // finer than this port's scheduler expresses today - it orders set to set, not
    // system to system - so each one is recorded here and marked owed on the row rather
    // than dropped silently.
    app.add_systems(post_update, epix::ecs::into(validate_shadow_map_size).set_name("validate shadow map size"));
    app.add_systems(post_update,
                    epix::ecs::into(build_directional_light_cascades)
                        .in_set(SimulationLightSystems::UpdateDirectionalLightCascades)
                        .set_name("build directional light cascades"));
    app.add_systems(post_update,
                    epix::ecs::into(clear_directional_light_cascades)
                        .in_set(SimulationLightSystems::UpdateDirectionalLightCascades)
                        .set_name("clear directional light cascades"));
    app.add_systems(post_update,
                    epix::ecs::into(assign_objects_to_clusters)
                        .in_set(SimulationLightSystems::AssignLightsToClusters)
                        .set_name("assign objects to clusters"));
    app.add_systems(post_update,
                    epix::ecs::into(update_directional_light_frusta)
                        .in_set(SimulationLightSystems::UpdateLightFrusta)
                        .set_name("update directional light frusta"));
    app.add_systems(post_update,
                    epix::ecs::into(update_point_light_frusta)
                        .in_set(SimulationLightSystems::UpdateLightFrusta)
                        .set_name("update point light frusta"));
    app.add_systems(post_update,
                    epix::ecs::into(update_spot_light_frusta)
                        .in_set(SimulationLightSystems::UpdateLightFrusta)
                        .set_name("update spot light frusta"));
    app.add_systems(post_update,
                    epix::ecs::into(check_dir_light_mesh_visibility)
                        .in_set(SimulationLightSystems::CheckLightVisibility)
                        .set_name("check dir light mesh visibility"));
    app.add_systems(post_update,
                    epix::ecs::into(check_point_light_mesh_visibility)
                        .in_set(SimulationLightSystems::CheckLightVisibility)
                        .set_name("check point light mesh visibility"));

    // Bevy's tenth system, `add_clusters`, is absent from this port, so its arm is
    // deliberately not written: without it nothing produces the clusters that
    // `assign_objects_to_clusters` consumes, and a half-written arm would say otherwise.
    //
    // Owed, from Bevy's `build` (`bevy_light/src/lib.rs:151-202`): `validate_shadow_map_size`
    // before `build_directional_light_cascades`; `build_directional_light_cascades` after
    // `clear_directional_light_cascades`; `clear_directional_light_cascades` and
    // `assign_objects_to_clusters` after `TransformSystems::Propagate` and
    // `CameraUpdateSystems::CameraUpdateSystem`; the three frusta after
    // `TransformSystems::Propagate`; the directional one after
    // `VisibilitySystems::CheckVisibility` and after `UpdateDirectionalLightCascades`;
    // the point and spot ones after `AssignLightsToClusters`; and the two mesh-visibility
    // checks after `VisibilitySystems::CalculateBounds`, `TransformSystems::Propagate`,
    // `UpdateLightFrusta` and `VisibilitySystems::CheckVisibility`, and before
    // `VisibilitySystems::MarkNewlyHiddenEntitiesInvisible`.
}

}  // namespace epix::pbr
