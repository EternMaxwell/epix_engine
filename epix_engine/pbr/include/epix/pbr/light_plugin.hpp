#pragma once

#include <epix/app.hpp>
#include <epix/common.hpp>

namespace epix::pbr {

/** @brief Ordering sets for the light simulation (Bevy `SimulationLightSystems`,
 *  `bevy_light/src/lib.rs:272-288`).
 *
 *  Bevy notes on two of them that order ambiguities within the set are ignored: each
 *  `build_directional_light_cascades` run is independent of the others and operates on
 *  distinct entities, and the visibility checks assume their operations are
 *  irreversible during the frame. */
EPIX_EXPORT enum class SimulationLightSystems {
    AddClusters,
    AssignLightsToClusters,
    UpdateDirectionalLightCascades,
    UpdateLightFrusta,
    CheckLightVisibility,
};

/** @brief Registers light simulation: its resources, its ordering sets and its systems
 *  (Bevy `LightPlugin`, `bevy_light/src/lib.rs:132-205`).
 *
 *  Bevy's `build` registers ten systems. Nine of them exist in this port -
 *  `validate_shadow_map_size`, `build_directional_light_cascades`,
 *  `assign_objects_to_clusters`, the three frustum updates,
 *  `clear_directional_light_cascades`, and the two mesh-visibility checks - and they
 *  are wired here with Bevy's sets and orderings. **`add_clusters` is absent**, so its
 *  arm is left out rather than half-written; it is the first thing the next slice
 *  needs, and until it lands the cluster assignment has no producer. */
EPIX_EXPORT struct EPIX_API LightPlugin {
    void attach(epix::app::App& app);
};

}  // namespace epix::pbr
