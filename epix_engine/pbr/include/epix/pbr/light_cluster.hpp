#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <glm/glm.hpp>
#include <optional>
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
    /** @brief Far Z plane of the first depth slice. Bevy's `Default` sets 5.0
     *  (`cluster/mod.rs:221-228`). */
    float first_slice_depth = 5.0f;
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

/** @brief A clustered decal: a 1×1×1 cube centred on its origin that projects
 *  images onto surfaces in its -Z direction (Bevy `ClusteredDecal`,
 *  `cluster/mod.rs:164-219`). Clustered decals need bindless textures, so they
 *  cannot be used on WebGL2 or WebGPU, and they work with forward or deferred
 *  rendering without a prepass.
 *
 *  Every texture is optional and defaults to none, as in Bevy. */
EPIX_EXPORT struct ClusteredDecal {
    /** @brief The base colour texture. It must be a 2D image, and if it has an
     *  alpha channel that channel is alpha-blended with the surface and with any
     *  other decals. Every decal image in a scene must share one sampler. */
    std::optional<assets::Handle<image::Image>> base_color_texture;
    /** @brief The normal map, combined with the surface's own normal map by the
     *  Whiteout method. It must be three-channel and in OpenGL format, i.e. the
     *  green channel points up. */
    std::optional<assets::Handle<image::Image>> normal_map_texture;
    /** @brief The metallic-roughness map, blended onto the surface using the
     *  base colour's alpha. Metallic is in blue and roughness in green, following
     *  glTF. */
    std::optional<assets::Handle<image::Image>> metallic_roughness_texture;
    /** @brief The emissive map, which makes the decal glow; it is blended by the
     *  alpha channel. */
    std::optional<assets::Handle<image::Image>> emissive_texture;
    /** @brief An application-specific tag, exposed to shaders for whatever use a
     *  custom shader has - for example restricting which surfaces the decal
     *  affects. */
    std::uint32_t tag = 0;
};

/** @brief The cluster factors a view's clusters use (Bevy
 *  `calculate_cluster_factors`, `bevy_light/src/cluster/assign.rs:863-878`).
 *
 *  For a perspective projection they are `(z_slices - 1) / ln(far / near)` and
 *  `ln(near)` times that, which is what lets the shader recover a view Z from a
 *  z-slice index; for an orthographic one the slices are linear, so they are
 *  `-near` and `z_slices / (near - far)`. These are the values that end up in
 *  `GpuLights::cluster_factors` (Bevy `mesh_types.wgsl`). */
EPIX_EXPORT inline glm::vec2 calculate_cluster_factors(float near_plane, float far_plane, float z_slices,
                                                      bool is_orthographic) {
    if (is_orthographic) return glm::vec2(-near_plane, z_slices / (-far_plane - -near_plane));
    const float z_slices_of_ln_zfar_over_znear = (z_slices - 1.0f) / std::log(far_plane / near_plane);
    return glm::vec2(z_slices_of_ln_zfar_over_znear, std::log(near_plane) * z_slices_of_ln_zfar_over_znear);
}

/** @brief The view Z at a z-slice boundary (Bevy `z_slice_to_view_z`,
 *  `assign.rs:950-967`), the inverse of `view_z_to_z_slice` in the same file.
 *
 *  Both are view-space Z, which is negative in front of the camera, so the
 *  values run from 0 (or `-near`) at the first slice to `-far` at the last. The
 *  perspective slices are geometric - each one multiplies by
 *  `(far / near)^(1 / (z_slices - 1))` - while the orthographic ones are evenly
 *  spaced. */
EPIX_EXPORT inline float z_slice_to_view_z(float near_plane, float far_plane, std::uint32_t z_slices,
                                           std::uint32_t z_slice, bool is_orthographic) {
    if (is_orthographic) {
        return -near_plane - (far_plane - near_plane) * static_cast<float>(z_slice) / static_cast<float>(z_slices);
    }
    if (z_slice == 0) return 0.0f;
    return -near_plane * std::pow(far_plane / near_plane, static_cast<float>(z_slice - 1) /
                                                          static_cast<float>(z_slices - 1));
}

/** @brief Where a view-space Z falls in the z-slices (Bevy `view_z_to_z_slice`,
 *  `assign.rs:1093-1109`).
 *
 *  The perspective branch is `ln(-view_z) * factors.x - factors.y + 1`, which is
 *  `pbr.wgsl`'s formula and must be kept in sync with it, so a slice boundary
 *  maps to its slice index rather than being an exact inverse of
 *  `z_slice_to_view_z`. The result is then clamped to `z_slices - 1`, because
 *  the far plane used for clustering may be closer than the furthest object. */
EPIX_EXPORT inline std::uint32_t view_z_to_z_slice(glm::vec2 cluster_factors, std::uint32_t z_slices, float view_z,
                                                  bool is_orthographic) {
    std::uint32_t z_slice = 0;
    if (is_orthographic) {
        z_slice = static_cast<std::uint32_t>(std::floor((view_z - cluster_factors.x) * cluster_factors.y));
    } else {
        z_slice = static_cast<std::uint32_t>(std::log(-view_z) * cluster_factors.x - cluster_factors.y + 1.0f);
    }
    return std::min(z_slice, z_slices - 1);
}

/** @brief Which cluster a fragment falls in (Bevy `ndc_position_to_cluster`,
 *  `assign.rs:969-988`).
 *
 *  The NDC position is flipped in Y and mapped to `[0, 1]`, scaled to the
 *  cluster grid and floored, and the z component comes from
 *  `view_z_to_z_slice`; the result is clamped to the grid, so a fragment exactly
 *  on the far edge lands in the last cluster rather than one past it. */
EPIX_EXPORT inline glm::uvec3 ndc_position_to_cluster(glm::uvec3 cluster_dimensions, glm::vec2 cluster_factors,
                                                      bool is_orthographic, glm::vec3 ndc_position, float view_z) {
    const glm::vec2 dimensions = glm::vec2(cluster_dimensions);
    const glm::vec2 frag_coord = glm::clamp(glm::vec2(ndc_position) * glm::vec2(0.5f, -0.5f) + glm::vec2(0.5f),
                                            glm::vec2(0.0f), glm::vec2(1.0f));
    const glm::vec2 xy = glm::floor(frag_coord * dimensions);

    const std::uint32_t z_slice =
        view_z_to_z_slice(cluster_factors, cluster_dimensions.z, view_z, is_orthographic);
    const glm::uvec3 cluster{static_cast<std::uint32_t>(xy.x), static_cast<std::uint32_t>(xy.y), z_slice};
    return glm::min(cluster, cluster_dimensions - glm::uvec3(1u));
}

/** @brief Where the line from `origin` to `p` crosses the view-space z plane
 *  (Bevy `line_intersection_to_z_plane`, `assign.rs:1086-1090`), used to find a
 *  cluster's corners on its near and far planes. */
EPIX_EXPORT inline glm::vec3 line_intersection_to_z_plane(glm::vec3 origin, glm::vec3 p, float z) {
    const glm::vec3 v = p - origin;
    const float t     = (z - origin.z) / v.z;
    return origin + t * v;
}

/** @brief Convert a clip-space point to view space, dividing by w (Bevy
 *  `clip_to_view`, `bevy_light/src/cluster/assign.rs:1111-1114`). */
EPIX_EXPORT inline glm::vec4 clip_to_view(const glm::mat4& view_from_clip, glm::vec4 clip) {
    const glm::vec4 view = view_from_clip * clip;
    return view / view.w;
}

/** @brief Convert a screen position to view space at a given NDC depth (Bevy
 *  `screen_to_view`, `assign.rs:1116-1125`).
 *
 *  The screen position is divided by the screen size, mapped to `[-1, 1]` with Y
 *  flipped - screen Y grows downward, NDC Y upward - and then unprojected with
 *  `view_from_clip`, which is the inverse of the view's projection. */
EPIX_EXPORT inline glm::vec4 screen_to_view(glm::vec2 screen_size, const glm::mat4& view_from_clip, glm::vec2 screen,
                                            float ndc_z) {
    const glm::vec2 tex_coord = screen / screen_size;
    const glm::vec4 clip{tex_coord.x * 2.0f - 1.0f, (1.0f - tex_coord.y) * 2.0f - 1.0f, ndc_z, 1.0f};
    return clip_to_view(view_from_clip, clip);
}

/** @brief The clusterable objects visible to any view (Bevy
 *  `GlobalVisibleClusterableObjects`, `cluster/mod.rs:126-128`). */
EPIX_EXPORT struct GlobalVisibleClusterableObjects {
    std::unordered_set<ecs::Entity> entities;
};

}  // namespace epix::pbr
