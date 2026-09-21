#pragma once

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <epix/assets.hpp>
#include <epix/camera/visibility.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <tuple>
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
 *  `ClusterFarZMode`, `cluster/mod.rs:49-57`).
 *
 *  Bevy's `Constant` variant carries its value, which a C++ enum cannot, so the
 *  value lives beside the kind here rather than in `ClusterZConfig`; setting
 *  `kind` to `Constant` without `constant` leaves a zero far plane. */
EPIX_EXPORT struct ClusterFarZMode {
    enum class Kind {
        /** @brief Derive it from the currently visible objects, which uses
         *  clusters better but costs CPU time and more indices. */
        MaxClusterableObjectRange,
        /** @brief A constant far Z, carried in `constant`. */
        Constant,
    } kind = Kind::MaxClusterableObjectRange;

    /** @brief The far Z used when `kind` is `Constant`. */
    float constant = 0.0f;

    /** @brief Bevy's `ClusterFarZMode::Constant(far)`. */
    static ClusterFarZMode constant_far(float far_z_value) noexcept {
        ClusterFarZMode mode;
        mode.kind     = Kind::Constant;
        mode.constant = far_z_value;
        return mode;
    }
};

/** @brief The depth-slicing configuration (Bevy `ClusterZConfig`,
 *  `cluster/mod.rs:62-67`). */
EPIX_EXPORT struct ClusterZConfig {
    /** @brief Far Z plane of the first depth slice. Bevy's `Default` sets 5.0
     *  (`cluster/mod.rs:221-228`). */
    float first_slice_depth = 5.0f;
    /** @brief How the furthest slice's far Z is evaluated. */
    ClusterFarZMode far_z_mode{};
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

    /** @brief The depth of the first slice, in view space (Bevy
     *  `ClusterConfig::first_slice_depth`, `cluster/mod.rs:282-289`): zero for
     *  the strategies that have no z configuration. */
    float first_slice_depth() const noexcept {
        switch (kind) {
            case Kind::None:
            case Kind::Single: return 0.0f;
            case Kind::XYZ:
            case Kind::FixedZ: return z_config.first_slice_depth;
        }
        return 0.0f;
    }

    /** @brief How the furthest slice's far Z is chosen (Bevy
     *  `ClusterConfig::far_z_mode`, `cluster/mod.rs:291-299`): a constant zero
     *  when clustering is off, the visible range for a single cluster, otherwise
     *  the view's own configuration. */
    ClusterFarZMode far_z_mode() const noexcept {
        switch (kind) {
            case Kind::None: return ClusterFarZMode::constant_far(0.0f);
            case Kind::Single: return ClusterFarZMode{};
            case Kind::XYZ:
            case Kind::FixedZ: return z_config.far_z_mode;
        }
        return ClusterFarZMode{};
    }

    /** @brief Whether the view resizes its X/Y clusters to stay inside the index
     *  limit (Bevy `ClusterConfig::dynamic_resizing`, `cluster/mod.rs:301-311`);
     *  false for the strategies that have no such setting. */
    bool may_resize_dynamically() const noexcept {
        switch (kind) {
            case Kind::None:
            case Kind::Single: return false;
            case Kind::XYZ:
            case Kind::FixedZ: return dynamic_resizing;
        }
        return false;
    }
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

/** @brief The view-space bounds of one cluster (Bevy `compute_aabb_for_cluster`,
 *  `bevy_light/src/cluster/assign.rs:880-947`).
 *
 *  The cluster's screen tile is unprojected at the near plane and then cut by
 *  the cluster's near and far planes. The two projection kinds differ in how
 *  those planes are found: an orthographic view uses linear depth slicing, while
 *  a perspective one takes them geometrically - the first cluster starting at
 *  z 0 and the rest at `-near * (far / near)^((k - 1) / (slices - 1))`. The four
 *  intersections with the near and far planes are then bounded, which is why the
 *  corners are computed by intersecting lines from the origin rather than by
 *  scaling the near-plane corners. */
EPIX_EXPORT inline camera::Aabb compute_aabb_for_cluster(float z_near, float z_far, glm::vec2 tile_size,
                                                        glm::vec2 screen_size, const glm::mat4& view_from_clip,
                                                        bool is_orthographic, glm::uvec3 cluster_dimensions,
                                                        glm::uvec3 ijk) {
    const glm::vec3 index = glm::vec3(ijk);
    const glm::vec2 p_min = glm::vec2(index) * tile_size;
    const glm::vec2 p_max = p_min + tile_size;

    glm::vec3 cluster_min{0.0f};
    glm::vec3 cluster_max{0.0f};
    if (is_orthographic) {
        glm::vec3 minimum = glm::vec3(screen_to_view(screen_size, view_from_clip, p_min, 0.0f));
        glm::vec3 maximum = glm::vec3(screen_to_view(screen_size, view_from_clip, p_max, 0.0f));

        const float slices = static_cast<float>(cluster_dimensions.z);
        minimum.z          = -z_near + (z_near - z_far) * index.z / slices;
        maximum.z          = -z_near + (z_near - z_far) * (index.z + 1.0f) / slices;

        cluster_min = glm::min(minimum, maximum);
        cluster_max = glm::max(minimum, maximum);
    } else {
        const glm::vec3 minimum = glm::vec3(screen_to_view(screen_size, view_from_clip, p_min, 1.0f));
        const glm::vec3 maximum = glm::vec3(screen_to_view(screen_size, view_from_clip, p_max, 1.0f));

        const float z_far_over_z_near = -z_far / -z_near;
        const float slices            = static_cast<float>(cluster_dimensions.z);
        const float cluster_near =
            index.z == 0.0f ? 0.0f : -z_near * std::pow(z_far_over_z_near, (index.z - 1.0f) / (slices - 1.0f));
        const float cluster_far =
            cluster_dimensions.z == 1 ? -z_far : -z_near * std::pow(z_far_over_z_near, index.z / (slices - 1.0f));

        const glm::vec3 minimum_near = line_intersection_to_z_plane(glm::vec3(0.0f), minimum, cluster_near);
        const glm::vec3 minimum_far  = line_intersection_to_z_plane(glm::vec3(0.0f), minimum, cluster_far);
        const glm::vec3 maximum_near = line_intersection_to_z_plane(glm::vec3(0.0f), maximum, cluster_near);
        const glm::vec3 maximum_far  = line_intersection_to_z_plane(glm::vec3(0.0f), maximum, cluster_far);

        cluster_min = glm::min(glm::min(minimum_near, minimum_far), glm::min(maximum_near, maximum_far));
        cluster_max = glm::max(glm::max(minimum_near, minimum_far), glm::max(maximum_near, maximum_far));
    }

    return camera::Aabb::from_min_max(cluster_min, cluster_max);
}

/** @brief The signed distance from a point to an x-plane (Bevy `get_distance_x`,
 *  `bevy_light/src/cluster/assign.rs:1128-1138`).
 *
 *  Bevy's note is the reason this is a function: for an x-plane the normal's y
 *  and the plane's d are both zero, so the general point-to-plane formula
 *  reduces to the dot product of the normal's x and z with the point's. The
 *  orthographic case is simpler still - the distance is just the x difference. */
EPIX_EXPORT inline float get_distance_x(const camera::HalfSpace& plane, glm::vec3 point, bool is_orthographic) {
    if (is_orthographic) return point.x - plane.d();
    return glm::dot(glm::vec2(plane.normal_d.x, plane.normal_d.z), glm::vec2(point.x, point.z));
}

/** @brief Project a sphere onto a z-plane, or nothing when it does not reach it
 *  (Bevy `project_to_plane_z`, `assign.rs:1141-1160`).
 *
 *  The projected sphere keeps the centre's x and y, takes the plane's z, and its
 *  radius shrinks by Pythagoras: `sqrt(r² - distance²)`. The `std::nullopt` case
 *  is what tells the caller the object does not intersect that plane. */
EPIX_EXPORT inline std::optional<camera::Sphere> project_to_plane_z(const camera::Sphere& z_object,
                                                                   const camera::HalfSpace& z_plane) {
    const float z                  = z_plane.d() / z_plane.normal_d.z;
    const float distance_to_plane  = z - z_object.center.z;
    if (std::abs(distance_to_plane) > z_object.radius) return std::nullopt;
    return camera::Sphere{.center = glm::vec3(z_object.center.x, z_object.center.y, z),
                          .radius = std::sqrt(z_object.radius * z_object.radius -
                                              distance_to_plane * distance_to_plane)};
}

/** @brief Project a sphere onto a y-plane, or nothing when it does not reach it
 *  (Bevy `project_to_plane_y`, `assign.rs:1163-1181`).
 *
 *  Unlike the z case the centre moves: it is displaced along the plane's normal
 *  by the signed distance, and the radius shrinks the same way. */
EPIX_EXPORT inline std::optional<camera::Sphere> project_to_plane_y(const camera::Sphere& y_object,
                                                                   const camera::HalfSpace& y_plane,
                                                                   bool is_orthographic) {
    const float distance_to_plane =
        is_orthographic ? y_plane.d() - y_object.center.y
                        : -glm::dot(glm::vec2(y_object.center.y, y_object.center.z),
                                    glm::vec2(y_plane.normal_d.y, y_plane.normal_d.z));
    if (std::abs(distance_to_plane) > y_object.radius) return std::nullopt;
    return camera::Sphere{
        .center = y_object.center + distance_to_plane * y_plane.normal(),
        .radius = std::sqrt(y_object.radius * y_object.radius - distance_to_plane * distance_to_plane)};
}

/** @brief A clusterable object's bounds in the space clusters are indexed in
 *  (Bevy `cluster_space_clusterable_object_aabb`, `assign.rs:995-1083`).
 *
 *  Returns a minimum and maximum whose X and Y are NDC in `[-1, 1]` and whose Z
 *  is left in *view* space, unadjusted - Bevy packs the view Z back in after
 *  clamping the projected coordinates, because the z-range test is done in view
 *  space.
 *
 *  Two details are the whole reason this is more than a projection. View z is
 *  clamped in front of the camera first, since behind it (or at exactly zero)
 *  perspective flips the axes and the min/max reasoning breaks. And all four
 *  combinations of the min/max x/y with the min/max z are projected, because
 *  under perspective the point with the greatest z can have the *smaller* screen
 *  position, so projecting only the two corners would miss the true bounds. */
EPIX_EXPORT inline std::pair<glm::vec3, glm::vec3> cluster_space_clusterable_object_aabb(
    const glm::mat4& view_from_world, glm::vec3 view_from_world_scale, const glm::mat4& clip_from_view,
    const camera::Sphere& clusterable_object_sphere) {
    const camera::Aabb view_aabb{
        .center       = glm::vec3(view_from_world * glm::vec4(clusterable_object_sphere.center, 1.0f)),
        .half_extents = clusterable_object_sphere.radius * glm::abs(view_from_world_scale)};
    glm::vec3 view_min = view_aabb.min();
    glm::vec3 view_max = view_aabb.max();

    // Constrain view z to be negative, i.e. in front of the camera.
    const float closest = -std::numeric_limits<float>::min();
    view_min.z          = std::min(view_min.z, closest);
    view_max.z          = std::min(view_max.z, closest);

    // The four combinations of the x/y bounds with the near and far z, since
    // perspective can invert which one projects smaller.
    const std::array<glm::vec4, 4> corners{glm::vec4(view_min.x, view_min.y, view_min.z, 1.0f),
                                           glm::vec4(view_min.x, view_min.y, view_max.z, 1.0f),
                                           glm::vec4(view_max.x, view_max.y, view_min.z, 1.0f),
                                           glm::vec4(view_max.x, view_max.y, view_max.z, 1.0f)};

    glm::vec3 ndc_min{std::numeric_limits<float>::infinity()};
    glm::vec3 ndc_max{-std::numeric_limits<float>::infinity()};
    for (const glm::vec4& corner : corners) {
        const glm::vec4 clip = clip_from_view * corner;
        const glm::vec3 ndc  = glm::vec3(clip) / clip.w;
        ndc_min              = glm::min(ndc_min, ndc);
        ndc_max              = glm::max(ndc_max, ndc);
    }

    // Clamp X and Y to NDC, and pack the *unadjusted* view Z back in.
    const glm::vec2 clamped_min = glm::clamp(glm::vec2(ndc_min), glm::vec2(-1.0f), glm::vec2(1.0f));
    const glm::vec2 clamped_max = glm::clamp(glm::vec2(ndc_max), glm::vec2(-1.0f), glm::vec2(1.0f));
    return {glm::vec3(clamped_min, view_min.z), glm::vec3(clamped_max, view_max.z)};
}

/** @brief What kind of clusterable object this is, with the type-specific data
 *  the assignment needs (Bevy `ClusterableObjectType`,
 *  `bevy_light/src/cluster/assign.rs:57-95`). */
EPIX_EXPORT struct ClusterableObjectType {
    enum class Kind {
        PointLight,
        SpotLight,
        ReflectionProbe,
        IrradianceVolume,
        Decal,
    } kind = Kind::PointLight;

    /** @brief Whether this light casts shadows; used for sorting the light
     *  list. */
    bool shadows_enabled = false;
    /** @brief Whether this light interacts with volumetrics; also used for
     *  sorting. */
    bool volumetric = false;
    /** @brief The outer angle of a spot light's cone, in radians. */
    float outer_angle = 0.0f;

    /** @brief The sort key that decides the order indices to clusterable objects
     *  are stored in the offsets and counts list (Bevy
     *  `ClusterableObjectType::ordering`, `assign.rs:105-120`).
     *
     *  Bevy sorts first by type - point lights, then spot lights, then
     *  reflection probes, irradiance volumes and decals - and then, for lights,
     *  by whether shadows are enabled and whether volumetrics are, both
     *  *enabled first*. The booleans are therefore negated: ascending order puts
     *  `false` first, so `!shadows_enabled` is what makes a shadow-casting light
     *  sort before one that does not cast. Inverting either negation reverses
     *  the light list. */
    std::tuple<std::uint8_t, bool, bool> ordering() const noexcept {
        switch (kind) {
            case Kind::PointLight: return {0u, !shadows_enabled, !volumetric};
            case Kind::SpotLight: return {1u, !shadows_enabled, !volumetric};
            case Kind::ReflectionProbe: return {2u, false, false};
            case Kind::IrradianceVolume: return {3u, false, false};
            case Kind::Decal: return {4u, false, false};
        }
        return {0u, false, false};
    }
};

/** @brief One clusterable object, ready to be assigned to clusters (Bevy
 *  `ClusterableObjectAssignmentData`, `assign.rs:35-52`).
 *
 *  Bevy stores the entity's `GlobalTransform`, though only its translation is
 *  ever used for the sphere - its own TODO notes that scale is ignored; the
 *  transform is kept because a spot light's direction comes from it. */
EPIX_EXPORT struct ClusterableObjectAssignmentData {
    ecs::Entity entity{};
    /** @brief The object's global transform. Only the translation is used for the
     *  sphere; Bevy reconstructs a point light's transform from its translation
     *  alone, which is noted there as a TODO about ignoring scale. */
    glm::mat4 transform{1.0f};
    /** @brief The object's range, which becomes the sphere's radius. */
    float range = 0.0f;
    ClusterableObjectType object_type{};
    /** @brief The render layers the object is visible through. */
    std::uint32_t render_layers = 0;

    /** @brief The sphere the object occupies (Bevy
     *  `ClusterableObjectAssignmentData::sphere`, `assign.rs:45-52`): centred on
     *  the transform's translation, with the range as its radius. */
    camera::Sphere sphere() const {
        return camera::Sphere{.center = glm::vec3(transform[3]), .radius = range};
    }
};

/** @brief The clusterable objects visible to any view (Bevy
 *  `GlobalVisibleClusterableObjects`, `cluster/mod.rs:126-128`). */
EPIX_EXPORT struct GlobalVisibleClusterableObjects {
    std::unordered_set<ecs::Entity> entities;
};

}  // namespace epix::pbr
