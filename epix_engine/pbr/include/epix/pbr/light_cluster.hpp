#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <glm/glm.hpp>
#include <unordered_set>
#include <vector>
#include <epix/common.hpp>
#endif

/** @brief The light-side clustered-forward definitions (Bevy
 *  `bevy_light/src/cluster/mod.rs`). Bevy keeps these in its `bevy_light` crate
 *  and the GPU buffers in `bevy_pbr`; Epix has one `pbr` module, so both live
 *  here, with the GPU side in `epix/pbr/cluster.hpp`. */
namespace epix::pbr {

/** @brief What the device and the view allow (Bevy `GlobalClusterSettings`,
 *  `cluster/mod.rs:38-43`). */
EPIX_EXPORT struct GlobalClusterSettings {
    /** @brief Whether the device offers the three storage-buffer bindings
     *  clustered forward needs (`cluster.rs:41-46`). */
    bool supports_storage_buffers = false;
    /** @brief Whether clustered decals are usable on this device. */
    bool clustered_decals_are_usable = false;
    /** @brief The largest number of clusterable objects a uniform buffer holds. */
    std::size_t max_uniform_buffer_clusterable_objects = 0;
    /** @brief The maximum number of indices a view's cluster bindings hold. */
    std::size_t view_cluster_bindings_max_indices = 0;
};

/** @brief How the far Z plane of the furthest depth slice is chosen (Bevy
 *  `ClusterFarZMode`, `cluster/mod.rs:49-57`). */
EPIX_EXPORT enum class ClusterFarZMode {
    /** @brief Derive it from the currently visible objects, which uses clusters
     *  better but costs CPU time and more indices. */
    MaxClusterableObjectRange,
    /** @brief A constant far Z. */
    Constant,
};

/** @brief The depth-slicing configuration (Bevy `ClusterZConfig`,
 *  `cluster/mod.rs:62-67`). */
EPIX_EXPORT struct ClusterZConfig {
    /** @brief Far Z plane of the first depth slice. */
    float first_slice_depth = 0.0f;
    /** @brief How the furthest slice's far Z is evaluated. */
    ClusterFarZMode far_z_mode = ClusterFarZMode::MaxClusterableObjectRange;
    /** @brief The value used when `far_z_mode` is `Constant`. */
    float constant_far_z = 0.0f;
};

/** @brief A view's clustering strategy (Bevy `ClusterConfig`,
 *  `cluster/mod.rs:72-99`). */
EPIX_EXPORT struct ClusterConfig {
    /** @brief Which strategy the view uses. */
    enum class Kind {
        /** @brief No cluster calculation for this view. */
        None,
        /** @brief One single cluster. */
        Single,
        /** @brief Explicit X/Y/Z counts with a dynamic-resizing option. */
        XYZ,
        /** @brief A fixed number of Z slices with X/Y derived to give square
         *  clusters, with a dynamic-resizing option. */
        FixedZ,
    } kind = Kind::None;

    /** @brief `XYZ` dimensions (`ClusterConfig::XYZ::dimensions`). */
    glm::uvec3 dimensions{1, 1, 1};
    /** @brief `FixedZ` total cluster count. */
    std::uint32_t total = 0;
    /** @brief `FixedZ` slice count. */
    std::uint32_t z_slices = 0;
    /** @brief The depth-slicing configuration for `XYZ` and `FixedZ`. */
    ClusterZConfig z_config{};
    /** @brief Whether X/Y may resize to stay inside the index limit. */
    bool dynamic_resizing = false;
};

/** @brief The number of each kind of clusterable object in one cluster (Bevy
 *  `ClusterableObjectCounts`, `cluster/mod.rs:135-146`). Reflection probes and
 *  irradiance volumes are not clustered when fewer than three storage buffers are
 *  available. */
EPIX_EXPORT struct ClusterableObjectCounts {
    std::uint32_t point_lights = 0;
    std::uint32_t spot_lights = 0;
    std::uint32_t reflection_probes = 0;
    std::uint32_t irradiance_volumes = 0;
    std::uint32_t decals = 0;
};

/** @brief The clusterable objects visible to one view, with their counts (Bevy
 *  `VisibleClusterableObjects`, `cluster/mod.rs:119-123`). */
EPIX_EXPORT struct VisibleClusterableObjects {
    std::vector<ecs::Entity> entities;
    ClusterableObjectCounts counts{};
};

/** @brief A view's cluster layout and the objects it must assign (Bevy
 *  `Clusters`, `cluster/mod.rs:101-112`). */
EPIX_EXPORT struct Clusters {
    /** @brief Tile size in pixels. */
    glm::uvec2 tile_size{0, 0};
    /** @brief Number of clusters in X / Y / Z in the view frustum. */
    glm::uvec3 dimensions{1, 1, 1};
    /** @brief Distance to the far plane of the first depth slice. Named
     *  `near_plane`/`far_plane` rather than Bevy's `near`/`far`, because those
     *  two are macros in the Windows SDK and a macro expands a whole token -
     *  the same reason the camera spells its accessors `get_far()`. */
    float near_plane = 0.0f;
    float far_plane = 0.0f;
    /** @brief One entry per cluster: the objects that fall in it. */
    std::vector<VisibleClusterableObjects> clusterable_objects;
};

/** @brief The clusterable objects visible to any view (Bevy
 *  `GlobalVisibleClusterableObjects`, `cluster/mod.rs:126-128`). */
EPIX_EXPORT struct GlobalVisibleClusterableObjects {
    std::unordered_set<ecs::Entity> entities;
};

}  // namespace epix::pbr
