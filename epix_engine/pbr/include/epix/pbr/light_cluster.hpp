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
#include <utility>
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
 *  `cluster/mod.rs:72-99`).
 *
 *  Bevy's `Default` is `FixedZ { total: 4096, z_slices: 24, z_config:
 *  ClusterZConfig::default(), dynamic_resizing: true }` - 24 depth slices with
 *  square clusters and at most 4096 clusters, using the maximum light distance as
 *  the far depth and a first slice reaching 5.0 (`cluster/mod.rs:230-241`) - and
 *  those are the defaults here. */
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
    } kind = Kind::FixedZ;

    /** @brief `XYZ` dimensions (`ClusterConfig::XYZ::dimensions`). */
    glm::uvec3 dimensions{1, 1, 1};
    /** @brief `FixedZ` total cluster count. */
    std::uint32_t total = 4096;
    /** @brief `FixedZ` slice count. */
    std::uint32_t z_slices = 24;
    /** @brief The depth-slicing configuration for `XYZ` and `FixedZ`. */
    ClusterZConfig z_config{};
    /** @brief Whether X/Y may resize to stay inside the index limit. */
    bool dynamic_resizing = true;

    /** @brief The cluster dimensions a view of this screen size gets (Bevy
     *  `ClusterConfig::dimensions_for_screen_size`, `cluster/mod.rs:244-280`).
     *
     *  The `FixedZ` case is the interesting one: it divides the total by the
     *  slice count to get the clusters per layer, then solves for the X and Y
     *  that give square clusters at this aspect ratio - `y =
     *  sqrt(per_layer / ratio)`, `x = y * ratio` - truncating both to integers.
     *  If either truncates to zero the layer is forced into a single row or
     *  column, which is how a tiny total still yields a valid configuration. A
     *  total below the slice count is clamped to it first, with Bevy warning. */
    glm::uvec3 dimensions_for_screen_size(glm::uvec2 screen_size) const {
        switch (kind) {
            case Kind::None: return glm::uvec3(0u);
            case Kind::Single: return glm::uvec3(1u);
            case Kind::XYZ: return dimensions;
            case Kind::FixedZ: break;
        }
        if (screen_size.x == 0 || screen_size.y == 0) {
            throw std::invalid_argument("Failed to calculate aspect ratio for Cluster: screen dimensions must be " \
                                        "positive, non-zero values");
        }
        const float aspect_ratio = static_cast<float>(screen_size.x) / static_cast<float>(screen_size.y);
        std::uint32_t slices     = z_slices;
        if (total < slices) slices = total;
        const float per_layer = static_cast<float>(total) / static_cast<float>(slices);

        const float y_float = std::sqrt(per_layer / aspect_ratio);
        std::uint32_t x     = static_cast<std::uint32_t>(y_float * aspect_ratio);
        std::uint32_t y     = static_cast<std::uint32_t>(y_float);

        // Check the extremes, where truncation would leave no clusters at all.
        if (x == 0) {
            x = 1;
            y = static_cast<std::uint32_t>(per_layer);
        }
        if (y == 0) {
            x = static_cast<std::uint32_t>(per_layer);
            y = 1;
        }
        return glm::uvec3(x, y, slices);
    }

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

    /** @brief Set the tile size and dimensions for a screen size (Bevy
     *  `Clusters::update`, `cluster/mod.rs:315-333`).
     *
     *  The tile size is computed first, as whole pixels covering the requested
     *  grid, and the dimensions are then *recomputed* from that tile size rather
     *  than kept - so a screen that does not divide evenly yields fewer clusters
     *  than requested, never fractional ones. Both are floored at one, and the z
     *  count is the requested one. */
    void update(glm::uvec2 screen_size, glm::uvec3 requested_dimensions) {
        const glm::vec2 requested_xy{requested_dimensions.x, requested_dimensions.y};
        const glm::vec2 screen{screen_size};
        tile_size = glm::max(glm::uvec2(glm::ceil(screen / requested_xy)), glm::uvec2(1u));

        const glm::uvec3 derived{glm::ceil(screen / glm::vec2(tile_size)), requested_dimensions.z};
        dimensions = glm::max(derived, glm::uvec3(1u));
    }

    /** @brief Reset to a state with no clusters (Bevy `Clusters::clear`,
     *  `cluster/mod.rs:334-340`). Note the dimensions become zero, where
     *  `update` would floor them at one. */
    void clear() {
        tile_size  = glm::uvec2(1u);
        dimensions = glm::uvec3(0u);
        near_plane = 0.0f;
        far_plane  = 0.0f;
        clusterable_objects.clear();
    }
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
    /** @brief The render layers the object is visible through. Bevy's default is
     *  layer 0, which is distinct from belonging to none. */
    camera::RenderLayers render_layers = camera::RenderLayers::layer(0);

    /** @brief The sphere the object occupies (Bevy
     *  `ClusterableObjectAssignmentData::sphere`, `assign.rs:45-52`): centred on
     *  the transform's translation, with the range as its radius. */
    camera::Sphere sphere() const {
        return camera::Sphere{.center = glm::vec3(transform[3]), .radius = range};
    }
};

/** @brief An orthographic view's view-space near plane, derived from its
 *  reverse-Z projection matrix (Bevy `assign.rs:352-361`).
 *
 *  Bevy derives rather than reads it: for glam's reverse-Z orthographic matrix
 *  the third column's z is `r * far` and the diagonal's z is `r` with
 *  `r = 1 / (far - near)`, so `(m32 - 1) / m22` recovers `near`. */
EPIX_EXPORT inline float orthographic_first_slice_depth(const glm::mat4& clip_from_view) {
    return (clip_from_view[3][2] - 1.0f) / clip_from_view[2][2];
}

/** @brief How many cluster indices a view's clusterable objects are expected to
 *  need (Bevy `assign_objects_to_clusters`'s dynamic-resizing estimate,
 *  `bevy_light/src/cluster/assign.rs:377-432`).
 *
 *  Each object in view contributes the product of three things: its extent in
 *  NDC scaled to the cluster grid in x and y, its exact slice count in z, and an
 *  overlap allowance of one extra cluster per axis side - except where the
 *  object's bound already reaches the NDC edge, where the extra would fall
 *  outside the view and is not counted. Bevy notes this overestimates index
 *  counts by at most 50% when the object's whole range is in view, and more when
 *  only part of it is.
 *
 *  The frustum test is a predicate because it is the caller's, which keeps this
 *  arithmetic verifiable on its own; Bevy performs the same test inline and skips
 *  objects outside the view. The result is compared against the view's cluster
 *  index limit by the caller. */
template <typename InView>
    requires std::invocable<InView&, const camera::Sphere&>
EPIX_EXPORT float estimate_cluster_index_count(const std::vector<ClusterableObjectAssignmentData>& clusterable_objects,
                                               const glm::mat4& view_from_world, glm::vec3 view_from_world_scale,
                                               const glm::mat4& clip_from_view, glm::vec2 cluster_factors,
                                               glm::uvec3 cluster_dimensions, bool is_orthographic,
                                               InView&& in_view) {
    float estimate = 0.0f;
    for (const ClusterableObjectAssignmentData& object : clusterable_objects) {
        const camera::Sphere object_sphere = object.sphere();
        if (!in_view(object_sphere)) continue;

        const auto [aabb_min, aabb_max] = cluster_space_clusterable_object_aabb(
            view_from_world, view_from_world_scale, clip_from_view, object_sphere);

        // The slice count in z is exact, since the slices are not adjusted here.
        const std::uint32_t z_min =
            view_z_to_z_slice(cluster_factors, cluster_dimensions.z, aabb_min.z, is_orthographic);
        const std::uint32_t z_max =
            view_z_to_z_slice(cluster_factors, cluster_dimensions.z, aabb_max.z, is_orthographic);
        const auto z_count = static_cast<float>(std::max(z_min, z_max) - std::min(z_min, z_max) + 1);

        // In x and y the extent is scaled from [-1, 1] to the cluster grid, in
        // floats so that a large initial tile size does not overestimate.
        const glm::vec2 xy_count = (glm::vec2(aabb_max) - glm::vec2(aabb_min)) * 0.5f *
                                   glm::vec2(cluster_dimensions.x, cluster_dimensions.y);

        // One extra cluster per side, except where the object already reaches
        // the edge of the view.
        const float x_overlap = (aabb_min.x <= -1.0f ? 0.0f : 1.0f) + (aabb_max.x >= 1.0f ? 0.0f : 1.0f);
        const float y_overlap = (aabb_min.y <= -1.0f ? 0.0f : 1.0f) + (aabb_max.y >= 1.0f ? 0.0f : 1.0f);
        estimate += (xy_count.x + x_overlap) * (xy_count.y + y_overlap) * z_count;
    }
    return estimate;
}

/** @brief Shrink a view's X/Y cluster counts so its indices fit the limit (Bevy
 *  `assign_objects_to_clusters`'s dynamic-resizing branch,
 *  `bevy_light/src/cluster/assign.rs:434-451`).
 *
 *  The estimate is conservative, so Bevy scales both axes by the square root of
 *  the ratio between the limit and the estimate rather than trying to solve
 *  exactly, floors the result and clamps it at one. The z count is untouched: z
 *  slices are not adjusted. */
EPIX_EXPORT inline glm::uvec3 reduce_cluster_dimensions_for_index_limit(glm::uvec3 dimensions, float index_estimate,
                                                                       std::size_t max_indices) {
    if (index_estimate <= static_cast<float>(max_indices)) return dimensions;
    const float index_ratio = static_cast<float>(max_indices) / index_estimate;
    const float xy_ratio    = std::sqrt(index_ratio);
    return glm::uvec3(
        std::max(static_cast<std::uint32_t>(std::floor(static_cast<float>(dimensions.x) * xy_ratio)), 1u),
        std::max(static_cast<std::uint32_t>(std::floor(static_cast<float>(dimensions.y) * xy_ratio)), 1u),
        dimensions.z);
}

/** @brief An orthographic view's x and y cluster planes in view space (Bevy
 *  `assign_objects_to_clusters`, `assign.rs:484-503`).
 *
 *  Each plane is placed at the view-space position its slice boundary unprojects
 *  to at the near plane, with an axis-aligned normal and that position as its d.
 *  The x boundaries run from -1 to 1 across the view, but the y ones are
 *  *inverted* - `1 - y / slices` - because NDC y grows upward while the plane
 *  index grows downward. There are `dimensions + 1` planes per axis, the last
 *  being the far edge. */
EPIX_EXPORT inline std::pair<std::vector<camera::HalfSpace>, std::vector<camera::HalfSpace>>
orthographic_cluster_xy_planes(const glm::mat4& view_from_clip, glm::uvec3 dimensions) {
    std::vector<camera::HalfSpace> x_planes;
    x_planes.reserve(dimensions.x + 1);
    const float x_slices = static_cast<float>(dimensions.x);
    for (std::uint32_t x = 0; x <= dimensions.x; ++x) {
        const float x_pos  = (static_cast<float>(x) / x_slices) * 2.0f - 1.0f;
        const float view_x = clip_to_view(view_from_clip, glm::vec4(x_pos, 0.0f, 1.0f, 1.0f)).x;
        x_planes.push_back(camera::HalfSpace::from_normal_d(glm::vec4(1.0f, 0.0f, 0.0f, view_x)));
    }

    std::vector<camera::HalfSpace> y_planes;
    y_planes.reserve(dimensions.y + 1);
    const float y_slices = static_cast<float>(dimensions.y);
    for (std::uint32_t y = 0; y <= dimensions.y; ++y) {
        // Inverted: the top of the view is the first plane.
        const float y_pos  = (1.0f - static_cast<float>(y) / y_slices) * 2.0f - 1.0f;
        const float view_y = clip_to_view(view_from_clip, glm::vec4(0.0f, y_pos, 1.0f, 1.0f)).y;
        y_planes.push_back(camera::HalfSpace::from_normal_d(glm::vec4(0.0f, 1.0f, 0.0f, view_y)));
    }
    return {x_planes, y_planes};
}

/** @brief A perspective view's x and y cluster planes in view space (Bevy
 *  `assign_objects_to_clusters`, `assign.rs:504-525`).
 *
 *  Unlike the orthographic case these are not axis-aligned. Each boundary
 *  unprojects two points on the near plane - the bottom and top of the view for
 *  an x boundary, the left and right for a y boundary - and the plane is the one
 *  through them and the eye, found by crossing them. The y boundaries are
 *  inverted here too, for the same reason as the orthographic ones. */
EPIX_EXPORT inline std::pair<std::vector<camera::HalfSpace>, std::vector<camera::HalfSpace>>
perspective_cluster_xy_planes(const glm::mat4& view_from_clip, glm::uvec3 dimensions) {
    std::vector<camera::HalfSpace> x_planes;
    x_planes.reserve(dimensions.x + 1);
    const float x_slices = static_cast<float>(dimensions.x);
    for (std::uint32_t x = 0; x <= dimensions.x; ++x) {
        const float x_pos = (static_cast<float>(x) / x_slices) * 2.0f - 1.0f;
        const glm::vec3 bottom = glm::vec3(clip_to_view(view_from_clip, glm::vec4(x_pos, -1.0f, 1.0f, 1.0f)));
        const glm::vec3 top    = glm::vec3(clip_to_view(view_from_clip, glm::vec4(x_pos, 1.0f, 1.0f, 1.0f)));
        const glm::vec3 normal = glm::cross(bottom, top);
        x_planes.push_back(camera::HalfSpace::from_normal_d(glm::vec4(normal, glm::dot(bottom, normal))));
    }

    std::vector<camera::HalfSpace> y_planes;
    y_planes.reserve(dimensions.y + 1);
    const float y_slices = static_cast<float>(dimensions.y);
    for (std::uint32_t y = 0; y <= dimensions.y; ++y) {
        const float y_pos = (1.0f - static_cast<float>(y) / y_slices) * 2.0f - 1.0f;
        const glm::vec3 left  = glm::vec3(clip_to_view(view_from_clip, glm::vec4(-1.0f, y_pos, 1.0f, 1.0f)));
        const glm::vec3 right = glm::vec3(clip_to_view(view_from_clip, glm::vec4(1.0f, y_pos, 1.0f, 1.0f)));
        const glm::vec3 normal = glm::cross(right, left);
        y_planes.push_back(camera::HalfSpace::from_normal_d(glm::vec4(normal, glm::dot(right, normal))));
    }
    return {x_planes, y_planes};
}

/** @brief A view's z cluster planes in view space (Bevy
 *  `assign_objects_to_clusters`, `assign.rs:528-534`).
 *
 *  The boundaries come from `z_slice_to_view_z`, and each plane carries the
 *  normal `-Z` with `d = -view_z`, so under Bevy's `n . x = d` convention a point
 *  at that view z lies on it. There are `z_slices + 1` planes, the first at 0 (or
 *  `-near` for an orthographic view) and the last at `-far`. */
EPIX_EXPORT inline std::vector<camera::HalfSpace> cluster_z_planes(float first_slice_depth, float far_z,
                                                                  std::uint32_t z_slices, bool is_orthographic) {
    std::vector<camera::HalfSpace> z_planes;
    z_planes.reserve(z_slices + 1);
    for (std::uint32_t z = 0; z <= z_slices; ++z) {
        const float view_z = z_slice_to_view_z(first_slice_depth, far_z, z_slices, z, is_orthographic);
        z_planes.push_back(camera::HalfSpace::from_normal_d(glm::vec4(0.0f, 0.0f, -1.0f, -view_z)));
    }
    return z_planes;
}

/** @brief Where an object's centre falls and how far its bounds reach in the
 *  cluster grid, before the sphere is refined against the cluster planes (Bevy
 *  `assign_objects_to_clusters`, `bevy_light/src/cluster/assign.rs:570-641`). */
EPIX_EXPORT struct ClusterAssignmentBounds {
    /** @brief The first and last cluster, in each axis, that the object's
     *  cluster-space bounds reach. */
    glm::uvec3 min_cluster{0u};
    glm::uvec3 max_cluster{0u};
    /** @brief The z slice containing the object's centre, or nothing when the
     *  centre projects past the far plane. */
    std::optional<std::uint32_t> z_center;
    /** @brief The y row containing the object's centre. The two off-screen cases
     *  are *not* symmetric: a centre above the view gives nothing, while one
     *  below it gives `dimensions.y + 1`, a sentinel past the last row, because
     *  NDC y grows upward while the rows grow downward. */
    std::optional<std::uint32_t> y_center;
    /** @brief The object's sphere in view space: its centre transformed, and its
     *  radius scaled by the largest axis scale, so a non-uniform scale cannot
     *  shrink it. */
    camera::Sphere view_sphere{};
};

/** @brief Compute an object's cluster range and centre rows from its
 *  cluster-space bounds (Bevy `assign.rs:570-641`).
 *
 *  The range comes from `ndc_position_to_cluster` applied to both corners of the
 *  object's bounds, then ordered so the minimum is the smaller of the two - the
 *  projection can invert them. */
EPIX_EXPORT inline ClusterAssignmentBounds cluster_assignment_bounds(
    const camera::Sphere& object_sphere, const glm::mat4& view_from_world, float view_from_world_scale_max,
    const glm::mat4& clip_from_view, glm::vec2 cluster_factors, glm::uvec3 cluster_dimensions, bool is_orthographic,
    glm::vec3 bounds_ndc_min, glm::vec3 bounds_ndc_max) {
    ClusterAssignmentBounds bounds;

    const glm::uvec3 corner_min = ndc_position_to_cluster(cluster_dimensions, cluster_factors, is_orthographic,
                                                          bounds_ndc_min, bounds_ndc_min.z);
    const glm::uvec3 corner_max = ndc_position_to_cluster(cluster_dimensions, cluster_factors, is_orthographic,
                                                          bounds_ndc_max, bounds_ndc_max.z);
    bounds.min_cluster          = glm::min(corner_min, corner_max);
    bounds.max_cluster          = glm::max(corner_min, corner_max);

    bounds.view_sphere = camera::Sphere{
        .center = glm::vec3(view_from_world * glm::vec4(object_sphere.center, 1.0f)),
        .radius = object_sphere.radius * view_from_world_scale_max};

    const glm::vec4 center_clip = clip_from_view * glm::vec4(bounds.view_sphere.center, 1.0f);
    const glm::vec3 center_ndc  = glm::vec3(center_clip) / center_clip.w;
    const glm::uvec3 center_cluster =
        ndc_position_to_cluster(cluster_dimensions, cluster_factors, is_orthographic, center_ndc, bounds.view_sphere.center.z);

    if (center_ndc.z <= 1.0f) bounds.z_center = center_cluster.z;
    if (center_ndc.y > 1.0f) {
        bounds.y_center = std::nullopt;
    } else if (center_ndc.y < -1.0f) {
        bounds.y_center = cluster_dimensions.y + 1;  // the sentinel past the last row
    } else {
        bounds.y_center = center_cluster.y;
    }
    return bounds;
}

/** @brief The cluster index a (x, y, z) coordinate maps to (Bevy's
 *  `(y * dimensions.x + min_x) * dimensions.z + z`, `assign.rs:712-714`): y
 *  outermost, then x, then z innermost. */
EPIX_EXPORT inline std::size_t cluster_index(glm::uvec3 dimensions, std::uint32_t x, std::uint32_t y,
                                             std::uint32_t z) noexcept {
    return (static_cast<std::size_t>(y) * dimensions.x + x) * dimensions.z + z;
}

/** @brief What one (z, y) pair of the refinement contributes (Bevy
 *  `assign_objects_to_clusters`'s inner loops, `assign.rs:642-711`). */
EPIX_EXPORT struct ClusterXRange {
    /** @brief False when the refined sphere misses this z slice or y row
     *  entirely, in which case the x range is meaningless. */
    bool reached = false;
    /** @brief The first and last x clusters the refined sphere reaches. */
    std::uint32_t min_x = 0;
    std::uint32_t max_x = 0;
    /** @brief The sphere after both refinements; this is the sphere the x scan
     *  tests against. */
    camera::Sphere y_object{};
};

/** @brief Refine the object's view-space sphere into one (z, y) cell and scan
 *  for the x clusters it reaches (Bevy `assign.rs:642-711`).
 *
 *  This is the Iterative Sphere Refinement of Persson et al, *Practical
 *  Clustered Shading* (Just Cause 3), which Bevy cites here. The z and y steps
 *  each intersect the sphere with the plane of the cell it is entering and take
 *  the *circle* of intersection as the new sphere: the plane nearer the object
 *  has the larger circle, so when the object's centre is beyond the cell the
 *  nearer plane of the pair is used. The refinement matters because a sphere
 *  under perspective is not a sphere - it stretches and warps - so assuming its
 *  widest projection is centre-plus-radius is wrong.
 *
 *  The x scan then walks inwards from both ends of the object's cluster range,
 *  stopping at the first plane the refined sphere actually reaches on each side
 *  (`distance + radius > 0`). */
EPIX_EXPORT inline ClusterXRange refine_sphere_and_scan_x(
    glm::uvec3 min_cluster, glm::uvec3 max_cluster, std::uint32_t z, std::uint32_t y,
    const std::optional<std::uint32_t>& z_center, const std::optional<std::uint32_t>& y_center,
    const camera::Sphere& view_sphere, const std::vector<camera::HalfSpace>& x_planes,
    const std::vector<camera::HalfSpace>& y_planes, const std::vector<camera::HalfSpace>& z_planes,
    bool is_orthographic) {
    ClusterXRange range;

    camera::Sphere z_object = view_sphere;
    if (!z_center.has_value() || z != *z_center) {
        // The nearer plane of the pair has the larger intersection circle.
        const std::size_t plane_index = (z_center.has_value() && z < *z_center) ? z + 1 : z;
        const auto projected          = project_to_plane_z(z_object, z_planes[plane_index]);
        if (!projected.has_value()) return range;
        z_object = *projected;
    }

    camera::Sphere y_object = z_object;
    if (!y_center.has_value() || y != *y_center) {
        const std::size_t plane_index = (y_center.has_value() && y < *y_center) ? y + 1 : y;
        const auto projected = project_to_plane_y(y_object, y_planes[plane_index], is_orthographic);
        if (!projected.has_value()) return range;
        y_object = *projected;
    }

    // Walk in from the left while the sphere does not reach the next plane.
    std::uint32_t min_x = min_cluster.x;
    while (min_x < max_cluster.x &&
           -get_distance_x(x_planes[min_x + 1], y_object.center, is_orthographic) + y_object.radius <= 0.0f) {
        ++min_x;
    }
    // And in from the right.
    std::uint32_t max_x = max_cluster.x;
    while (max_x > min_x &&
           get_distance_x(x_planes[max_x], y_object.center, is_orthographic) + y_object.radius <= 0.0f) {
        --max_x;
    }

    range.reached = true;
    range.min_x   = min_x;
    range.max_x   = max_x;
    range.y_object = y_object;
    return range;
}

/** @brief The bounding sphere of a cluster's AABB (Bevy `assign.rs:739-742`):
 *  centred on the box, with the length of the box's half extents as its radius. */
EPIX_EXPORT inline camera::Sphere cluster_bounding_sphere(const camera::Aabb& aabb) {
    return camera::Sphere{.center = aabb.center, .radius = glm::length(aabb.half_extents)};
}

/** @brief The three ways a spot light can be culled from a cluster (Bevy
 *  `assign.rs:747-765`, based on Bart Wronski's *cull that cone*). */
EPIX_EXPORT struct SpotLightClusterCull {
    /** @brief The cluster sphere lies outside the light's cone. */
    bool angle_cull = false;
    /** @brief The cluster is further along the light's axis than its range
     *  reaches. */
    bool front_cull = false;
    /** @brief The cluster is behind the light. */
    bool back_cull = false;

    /** @brief Whether any of the three culls applies, i.e. the light does not
     *  affect the cluster. */
    bool culled() const noexcept { return angle_cull || front_cull || back_cull; }
};

/** @brief Test a spot light's cone against a cluster's bounding sphere (Bevy
 *  `assign.rs:747-765`).
 *
 *  `angle_cos * sqrt(dist² - v1²) - v1 * angle_sin` is the distance from the cone
 *  surface to the cluster along the closest point, so it culls when it exceeds
 *  the cluster's radius. `v1` is the cluster centre's offset along the light
 *  direction: past the range is a front cull, and behind the light is a back
 *  cull. The range passed in is already scaled by the view's largest axis scale,
 *  as Bevy scales it. */
EPIX_EXPORT inline SpotLightClusterCull spot_light_cluster_cull(glm::vec3 light_center, float light_range_scaled,
                                                               glm::vec3 light_direction, float angle_sin,
                                                               float angle_cos, glm::vec3 cluster_center,
                                                               float cluster_radius) {
    const glm::vec3 offset   = light_center - cluster_center;
    const float distance_sq  = glm::dot(offset, offset);
    const float v1_len       = glm::dot(offset, light_direction);

    SpotLightClusterCull cull;
    const float distance_closest_point =
        angle_cos * std::sqrt(distance_sq - v1_len * v1_len) - v1_len * angle_sin;
    cull.angle_cull = distance_closest_point > cluster_radius;
    cull.front_cull = v1_len > cluster_radius + light_range_scaled;
    cull.back_cull  = v1_len < -cluster_radius;
    return cull;
}

/** @brief Record that an object affects a cluster: push its entity and bump the
 *  counter for its kind (Bevy `assign.rs:766-838`).
 *
 *  This is the only place the per-cluster counts are incremented, and the kind
 *  decides which counter - a point light increments `point_lights`, a spot light
 *  `spot_lights`, and the probes, volumes and decals their own. The counts are
 *  what `prepare_lights` writes into the offsets and counts buffer, so a missing
 *  or doubled increment here shifts every later cluster's indices. */
EPIX_EXPORT inline void insert_object_into_cluster(VisibleClusterableObjects& cluster, ecs::Entity entity,
                                                  const ClusterableObjectType& object_type) {
    cluster.entities.push_back(entity);
    switch (object_type.kind) {
        case ClusterableObjectType::Kind::PointLight: cluster.counts.point_lights += 1; break;
        case ClusterableObjectType::Kind::SpotLight: cluster.counts.spot_lights += 1; break;
        case ClusterableObjectType::Kind::ReflectionProbe: cluster.counts.reflection_probes += 1; break;
        case ClusterableObjectType::Kind::IrradianceVolume: cluster.counts.irradiance_volumes += 1; break;
        case ClusterableObjectType::Kind::Decal: cluster.counts.decals += 1; break;
    }
}

/** @brief Record an object across a row of x clusters at one (y, z) (Bevy
 *  `assign.rs:780-789` and the other insertion arms).
 *
 *  Bevy repeats the same loop for every kind - point lights, spot lights,
 *  reflection probes, irradiance volumes and decals all affect every cluster
 *  their sphere reaches, with only the counting differing - advancing the flat
 *  index by the z count each step, which walks along x in the layout
 *  `cluster_index` defines. The probes' and decals' loops carry a TODO about
 *  culling them more aggressively with their oriented boxes. */
EPIX_EXPORT inline void insert_object_into_cluster_row(std::vector<VisibleClusterableObjects>& clusters,
                                                      glm::uvec3 dimensions, std::uint32_t y, std::uint32_t z,
                                                      std::uint32_t min_x, std::uint32_t max_x, ecs::Entity entity,
                                                      const ClusterableObjectType& object_type) {
    std::size_t index = cluster_index(dimensions, min_x, y, z);
    for (std::uint32_t x = min_x; x <= max_x; ++x) {
        insert_object_into_cluster(clusters[index], entity, object_type);
        index += dimensions.z;
    }
}

/** @brief What the uniform-buffer cap did to a view's clusterable objects (Bevy
 *  `assign_objects_to_clusters`, `bevy_light/src/cluster/assign.rs:262-294`). */
EPIX_EXPORT struct UniformClusterableObjectLimit {
    /** @brief The objects that remain, at most `max_objects`. */
    std::vector<ClusterableObjectAssignmentData> objects;
    /** @brief Whether more objects were in view than the cap allows, which Bevy
     *  reports once with a warning. */
    bool exceeded = false;
};

/** @brief Cap a view's clusterable objects at what a uniform buffer can hold
 *  (Bevy `assign.rs:262-294`).
 *
 *  Only objects the view frustum intersects are kept, and Bevy stops collecting
 *  one past the cap rather than counting the lot - that extra object is there
 *  purely to tell whether the cap was exceeded, so objects outside the view
 *  never trigger the warning. The survivors are then truncated to the cap.
 *
 *  This path exists because a uniform buffer cannot grow: on a device without
 *  the three storage bindings clustered forward wants, the clusterable objects
 *  live in a fixed buffer of `MAX_UNIFORM_BUFFER_CLUSTERABLE_OBJECTS`, and
 *  anything past it is dropped. The frustum test is a predicate, as in the index
 *  estimate, so the capping behaviour can be verified on its own. */
template <typename InView>
    requires std::invocable<InView&, const camera::Sphere&>
EPIX_EXPORT UniformClusterableObjectLimit limit_clusterable_objects_for_uniform_buffer(
    std::vector<ClusterableObjectAssignmentData> objects, InView&& in_view, std::size_t max_objects) {
    UniformClusterableObjectLimit limited;
    limited.objects.reserve(std::min(objects.size(), max_objects + 1));

    std::size_t in_view_count = 0;
    for (ClusterableObjectAssignmentData& object : objects) {
        // One past the cap is kept: it is what reveals the cap was exceeded.
        if (in_view_count == max_objects + 1) break;
        if (!in_view(object.sphere())) continue;
        ++in_view_count;
        limited.objects.push_back(std::move(object));
    }

    // The retained length is what Bevy warns about, so objects outside the view
    // never contribute to it.
    limited.exceeded = limited.objects.size() > max_objects;
    if (limited.objects.size() > max_objects) limited.objects.resize(max_objects);
    return limited;
}

/** @brief The furthest depth a view's clusters reach (Bevy
 *  `assign_objects_to_clusters`, `bevy_light/src/cluster/assign.rs:337-350`).
 *
 *  A constant mode is taken as given; the maximum-range mode measures each
 *  object's view-space depth - negated, because view space looks down -Z - plus
 *  its range scaled by the view's z scale, and takes the largest. With no objects
 *  the result is zero.
 *
 *  Note the row: glam's `Mat4::row(2)` is the third *row*, while a glm matrix is
 *  column-major, so the row is assembled from the fourth element of each column
 *  here. Reading `view_from_world[2]` would silently use a column instead. */
EPIX_EXPORT inline float view_far_z(const ClusterFarZMode& mode,
                                    const std::vector<ClusterableObjectAssignmentData>& clusterable_objects,
                                    const glm::mat4& view_from_world, glm::vec3 view_from_world_scale) {
    if (mode.kind == ClusterFarZMode::Kind::Constant) return mode.constant;

    const glm::vec4 row{view_from_world[0][2], view_from_world[1][2], view_from_world[2][2], view_from_world[3][2]};
    float far_z = 0.0f;
    for (const ClusterableObjectAssignmentData& object : clusterable_objects) {
        const glm::vec3 translation = glm::vec3(object.transform[3]);
        const float depth = -glm::dot(row, glm::vec4(translation, 1.0f)) + object.range * view_from_world_scale.z;
        far_z             = std::max(far_z, depth);
    }
    return far_z;
}

/** @brief The depth of the first cluster slice (Bevy `assign_objects_to_clusters`,
 *  `assign.rs:351-369`).
 *
 *  An orthographic view derives it from its reverse-Z projection matrix instead of
 *  reading the configuration. A perspective view with a single slice has to reach
 *  as far as the objects do, so its first slice is the larger of the configured
 *  depth and the far plane; with more slices the configured depth stands. The
 *  result is then scaled by the view's z scale - and the far plane is raised to
 *  meet it, so the first slice can never be beyond the far plane. */
EPIX_EXPORT inline float first_slice_depth(const ClusterConfig& config, bool is_orthographic,
                                           std::uint32_t cluster_dimensions_z, const glm::mat4& clip_from_view,
                                           glm::vec3 view_from_world_scale, float far_z) {
    float depth = 0.0f;
    if (is_orthographic) {
        depth = orthographic_first_slice_depth(clip_from_view);
    } else if (cluster_dimensions_z == 1) {
        depth = std::max(config.first_slice_depth(), far_z);
    } else {
        depth = config.first_slice_depth();
    }
    return depth * view_from_world_scale.z;
}

/** @brief The far plane a view's clusters actually use, which is at least as far
 *  as the first slice (Bevy `assign.rs:369`): clustering misbehaves if the first
 *  slice reaches past the far plane, so the far plane is raised to meet it. */
EPIX_EXPORT inline float far_z_at_least_first_slice_depth(float far_z, float first_slice_depth) {
    return std::max(far_z, first_slice_depth);
}

/** @brief The clusterable objects visible to any view (Bevy
 *  `GlobalVisibleClusterableObjects`, `cluster/mod.rs:126-128`). */
EPIX_EXPORT struct GlobalVisibleClusterableObjects {
    std::unordered_set<ecs::Entity> entities;
};

/** @brief Assignment data for a point light (Bevy `assign_objects_to_clusters`,
 *  `assign.rs:170-188`).
 *
 *  The transform is reduced to a translation: a point light's reach is a sphere, so
 *  nothing about its rotation or scale can change what it covers, and Bevy builds a
 *  fresh `GlobalTransform::from_translation` rather than keeping the entity's - its
 *  own comment calls the ignored scale a TODO. A spot light cannot do the same,
 *  which is the asymmetry between the two: its cone direction lives in the
 *  transform, so flattening it would leave every spot light pointing the same way. */
EPIX_EXPORT inline ClusterableObjectAssignmentData point_light_assignment_data(
    ecs::Entity entity, glm::vec3 translation, float range, bool shadows_enabled, bool volumetric,
    const camera::RenderLayers& render_layers) {
    ClusterableObjectAssignmentData data;
    data.entity        = entity;
    data.transform     = glm::translate(glm::mat4(1.0f), translation);
    data.range         = range;
    data.object_type   = ClusterableObjectType{
        .kind = ClusterableObjectType::Kind::PointLight, .shadows_enabled = shadows_enabled, .volumetric = volumetric};
    data.render_layers = render_layers;
    return data;
}

/** @brief Assignment data for a spot light (Bevy `assign_objects_to_clusters`,
 *  `assign.rs:189-215`), which keeps the entity's full transform so the cone's
 *  direction survives, and carries the outer angle the cone test needs. */
EPIX_EXPORT inline ClusterableObjectAssignmentData spot_light_assignment_data(
    ecs::Entity entity, const glm::mat4& world_from_light, float range, float outer_angle, bool shadows_enabled,
    bool volumetric, const camera::RenderLayers& render_layers) {
    ClusterableObjectAssignmentData data;
    data.entity      = entity;
    data.transform   = world_from_light;
    data.range       = range;
    data.object_type = ClusterableObjectType{.kind = ClusterableObjectType::Kind::SpotLight,
                                             .shadows_enabled = shadows_enabled,
                                             .volumetric      = volumetric,
                                             .outer_angle     = outer_angle};
    data.render_layers = render_layers;
    return data;
}

/** @brief Cut a view's clusterable objects down to what the uniform path can hold
 *  (Bevy `assign_objects_to_clusters`, `assign.rs:245-295`).
 *
 *  Only used when the object count exceeds the uniform limit and the device has no
 *  storage buffers; the caller decides that. What happens then is a two-step
 *  reduction that is easy to get subtly wrong:
 *
 *  First the objects are sorted by the same key the offsets list uses - type, then
 *  shadow and volumetric flags, then entity - so the ones that survive the cut are
 *  the ones a clustered lookup should prefer. Then they are retained only if they
 *  intersect at least one view's frustum, since an object no view can see costs a
 *  slot for nothing.
 *
 *  The subtlety is the count: the retain keeps going one object *past* the limit,
 *  because the caller warns when the result still exceeds it, and a count capped at
 *  the limit could not tell "exactly at capacity" from "over it". Truncation
 *  happens after that check, so the extra object only ever serves to set the flag -
 *  `warning_emitted` is set rather than a message logged here. */
EPIX_EXPORT inline std::size_t limit_clusterable_objects_to_uniform_capacity(
    std::vector<ClusterableObjectAssignmentData>& objects, const std::vector<camera::Frustum>& view_frusta,
    std::size_t max_uniform_buffer_clusterable_objects, bool* warning_emitted) {
    std::sort(objects.begin(), objects.end(),
              [](const ClusterableObjectAssignmentData& lhs, const ClusterableObjectAssignmentData& rhs) {
                  return std::tuple{lhs.object_type.ordering(), lhs.entity} <
                         std::tuple{rhs.object_type.ordering(), rhs.entity};
              });

    std::size_t kept_in_view = 0;
    objects.erase(std::remove_if(objects.begin(), objects.end(),
                                 [&](const ClusterableObjectAssignmentData& object) {
                                     // One past the limit is kept, so the caller can
                                     // tell that the limit was exceeded.
                                     if (kept_in_view == max_uniform_buffer_clusterable_objects + 1) return true;

                                     const camera::Sphere sphere = object.sphere();
                                     bool in_view                = false;
                                     for (const camera::Frustum& frustum : view_frusta) {
                                         in_view = in_view || frustum.intersects_sphere(sphere, true);
                                     }
                                     if (in_view) ++kept_in_view;
                                     return !in_view;
                                 }),
                  objects.end());

    if (objects.size() > max_uniform_buffer_clusterable_objects && warning_emitted != nullptr) {
        *warning_emitted = true;
    }
    if (objects.size() > max_uniform_buffer_clusterable_objects) {
        objects.resize(max_uniform_buffer_clusterable_objects);
    }
    return objects.size();
}

/** @brief The bounding sphere of one cluster, computed on first use and cached
 *  (Bevy `assign_objects_to_clusters`, `assign.rs:475-477,722-745`).
 *
 *  Bevy fills a slot per cluster with `None` before the object loop and computes
 *  each cluster's sphere the first time a spot light's cone tests against it,
 *  because most clusters are never tested - only the ones a cone reaches - so
 *  computing all of them up front would be work thrown away. The cache is per view
 *  and indexed by the same cluster index the assignment uses.
 *
 *  The computation is a callable so the caching is verifiable on its own: a test
 *  can count how many times it runs. Getting the cache wrong is quiet - a slot
 *  shared between indices would reuse a neighbouring cluster's bounds, which would
 *  cull a light that should light that cluster. */
template <typename Compute>
requires std::invocable<Compute&>
EPIX_EXPORT inline const camera::Sphere& cached_cluster_sphere(std::vector<std::optional<camera::Sphere>>& cache,
                                                              std::size_t cluster_index, Compute&& compute) {
    std::optional<camera::Sphere>& slot = cache[cluster_index];
    if (!slot.has_value()) {
        slot = compute();
    }
    return *slot;
}

}  // namespace epix::pbr
