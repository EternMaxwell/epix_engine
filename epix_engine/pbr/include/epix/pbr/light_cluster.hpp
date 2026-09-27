#pragma once

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/camera/visibility.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <glm/glm.hpp>
#include <limits>
#include <optional>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>
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
EPIX_EXPORT struct EPIX_API ClusterFarZMode {
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
    static ClusterFarZMode constant_far(float far_z_value) noexcept;
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
EPIX_EXPORT struct EPIX_API ClusterConfig {
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
    glm::uvec3 dimensions_for_screen_size(glm::uvec2 screen_size) const;

    /** @brief The depth of the first slice, in view space (Bevy
     *  `ClusterConfig::first_slice_depth`, `cluster/mod.rs:282-289`): zero for
     *  the strategies that have no z configuration. */
    float first_slice_depth() const noexcept;

    /** @brief How the furthest slice's far Z is chosen (Bevy
     *  `ClusterConfig::far_z_mode`, `cluster/mod.rs:291-299`): a constant zero
     *  when clustering is off, the visible range for a single cluster, otherwise
     *  the view's own configuration. */
    ClusterFarZMode far_z_mode() const noexcept;

    /** @brief Whether the view resizes its X/Y clusters to stay inside the index
     *  limit (Bevy `ClusterConfig::dynamic_resizing`, `cluster/mod.rs:301-311`);
     *  false for the strategies that have no such setting. */
    bool may_resize_dynamically() const noexcept;
};

/** @brief The number of each kind of clusterable object in one cluster (Bevy
 *  `ClusterableObjectCounts`, `cluster/mod.rs:135-146`). Reflection probes and
 *  irradiance volumes are not clustered when fewer than three storage buffers are
 *  available. */
EPIX_EXPORT struct ClusterableObjectCounts {
    std::uint32_t point_lights       = 0;
    std::uint32_t spot_lights        = 0;
    std::uint32_t reflection_probes  = 0;
    std::uint32_t irradiance_volumes = 0;
    std::uint32_t decals             = 0;
};

/** @brief The clusterable objects visible to one view, with their counts (Bevy
 *  `VisibleClusterableObjects`, `cluster/mod.rs:119-123`). */
EPIX_EXPORT struct VisibleClusterableObjects {
    std::vector<ecs::Entity> entities;
    ClusterableObjectCounts counts{};
};

/** @brief A view's cluster layout and the objects it must assign (Bevy
 *  `Clusters`, `cluster/mod.rs:101-112`). */
EPIX_EXPORT struct EPIX_API Clusters {
    /** @brief Tile size in pixels. */
    glm::uvec2 tile_size{0, 0};
    /** @brief Number of clusters in X / Y / Z in the view frustum. */
    glm::uvec3 dimensions{1, 1, 1};
    /** @brief Distance to the far plane of the first depth slice. Named
     *  `near_plane`/`far_plane` rather than Bevy's `near`/`far`, because those
     *  two are macros in the Windows SDK and a macro expands a whole token -
     *  the same reason the camera spells its accessors `get_far()`. */
    float near_plane = 0.0f;
    float far_plane  = 0.0f;
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
    void update(glm::uvec2 screen_size, glm::uvec3 requested_dimensions);

    /** @brief Reset to a state with no clusters (Bevy `Clusters::clear`,
     *  `cluster/mod.rs:334-340`). Note the dimensions become zero, where
     *  `update` would floor them at one. */
    void clear();
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
EPIX_EXPORT EPIX_API glm::vec2 calculate_cluster_factors(float near_plane,
                                                        float far_plane,
                                                        float z_slices,
                                                        bool is_orthographic);

/** @brief The view Z at a z-slice boundary (Bevy `z_slice_to_view_z`,
 *  `assign.rs:950-967`), the inverse of `view_z_to_z_slice` in the same file.
 *
 *  Both are view-space Z, which is negative in front of the camera, so the
 *  values run from 0 (or `-near`) at the first slice to `-far` at the last. The
 *  perspective slices are geometric - each one multiplies by
 *  `(far / near)^(1 / (z_slices - 1))` - while the orthographic ones are evenly
 *  spaced. */
EPIX_EXPORT EPIX_API float z_slice_to_view_z(
    float near_plane, float far_plane, std::uint32_t z_slices, std::uint32_t z_slice, bool is_orthographic);

/** @brief Where a view-space Z falls in the z-slices (Bevy `view_z_to_z_slice`,
 *  `assign.rs:1093-1109`).
 *
 *  The perspective branch is `ln(-view_z) * factors.x - factors.y + 1`, which is
 *  `pbr.wgsl`'s formula and must be kept in sync with it, so a slice boundary
 *  maps to its slice index rather than being an exact inverse of
 *  `z_slice_to_view_z`. The result is then clamped to `z_slices - 1`, because
 *  the far plane used for clustering may be closer than the furthest object. */
EPIX_EXPORT EPIX_API std::uint32_t view_z_to_z_slice(glm::vec2 cluster_factors,
                                                    std::uint32_t z_slices,
                                                    float view_z,
                                                    bool is_orthographic);

/** @brief Which cluster a fragment falls in (Bevy `ndc_position_to_cluster`,
 *  `assign.rs:969-988`).
 *
 *  The NDC position is flipped in Y and mapped to `[0, 1]`, scaled to the
 *  cluster grid and floored, and the z component comes from
 *  `view_z_to_z_slice`; the result is clamped to the grid, so a fragment exactly
 *  on the far edge lands in the last cluster rather than one past it. */
EPIX_EXPORT EPIX_API glm::uvec3 ndc_position_to_cluster(glm::uvec3 cluster_dimensions,
                                                       glm::vec2 cluster_factors,
                                                       bool is_orthographic,
                                                       glm::vec3 ndc_position,
                                                       float view_z);

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
EPIX_EXPORT inline glm::vec4 screen_to_view(glm::vec2 screen_size,
                                            const glm::mat4& view_from_clip,
                                            glm::vec2 screen,
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
EPIX_EXPORT EPIX_API camera::Aabb compute_aabb_for_cluster(float z_near,
                                                           float z_far,
                                                           glm::vec2 tile_size,
                                                           glm::vec2 screen_size,
                                                           const glm::mat4& view_from_clip,
                                                           bool is_orthographic,
                                                           glm::uvec3 cluster_dimensions,
                                                           glm::uvec3 ijk);

/** @brief The signed distance from a point to an x-plane (Bevy `get_distance_x`,
 *  `bevy_light/src/cluster/assign.rs:1128-1138`).
 *
 *  Bevy's note is the reason this is a function: for an x-plane the normal's y
 *  and the plane's d are both zero, so the general point-to-plane formula
 *  reduces to the dot product of the normal's x and z with the point's. The
 *  orthographic case is simpler still - the distance is just the x difference. */
EPIX_EXPORT EPIX_API float get_distance_x(const camera::HalfSpace& plane, glm::vec3 point, bool is_orthographic);

/** @brief Project a sphere onto a z-plane, or nothing when it does not reach it
 *  (Bevy `project_to_plane_z`, `assign.rs:1141-1160`).
 *
 *  The projected sphere keeps the centre's x and y, takes the plane's z, and its
 *  radius shrinks by Pythagoras: `sqrt(r² - distance²)`. The `std::nullopt` case
 *  is what tells the caller the object does not intersect that plane. */
EPIX_EXPORT EPIX_API std::optional<camera::Sphere> project_to_plane_z(const camera::Sphere& z_object,
                                                                     const camera::HalfSpace& z_plane);

/** @brief Project a sphere onto a y-plane, or nothing when it does not reach it
 *  (Bevy `project_to_plane_y`, `assign.rs:1163-1181`).
 *
 *  Unlike the z case the centre moves: it is displaced along the plane's normal
 *  by the signed distance, and the radius shrinks the same way. */
EPIX_EXPORT EPIX_API std::optional<camera::Sphere> project_to_plane_y(const camera::Sphere& y_object,
                                                                     const camera::HalfSpace& y_plane,
                                                                     bool is_orthographic);

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
EPIX_EXPORT EPIX_API std::pair<glm::vec3, glm::vec3> cluster_space_clusterable_object_aabb(
    const glm::mat4& view_from_world,
    glm::vec3 view_from_world_scale,
    const glm::mat4& clip_from_view,
    const camera::Sphere& clusterable_object_sphere);

/** @brief What kind of clusterable object this is, with the type-specific data
 *  the assignment needs (Bevy `ClusterableObjectType`,
 *  `bevy_light/src/cluster/assign.rs:57-95`). */
EPIX_EXPORT struct EPIX_API ClusterableObjectType {
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
    std::tuple<std::uint8_t, bool, bool> ordering() const noexcept;
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
    camera::Sphere sphere() const { return camera::Sphere{.center = glm::vec3(transform[3]), .radius = range}; }
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
EPIX_EXPORT template <typename InView>
    requires std::invocable<InView&, const camera::Sphere&> &&
                 std::convertible_to<std::invoke_result_t<InView&, const camera::Sphere&>, bool>
float estimate_cluster_index_count(const std::vector<ClusterableObjectAssignmentData>& clusterable_objects,
                                               const glm::mat4& view_from_world,
                                               glm::vec3 view_from_world_scale,
                                               const glm::mat4& clip_from_view,
                                               glm::vec2 cluster_factors,
                                               glm::uvec3 cluster_dimensions,
                                               bool is_orthographic,
                                               InView&& in_view) {
    float estimate = 0.0f;
    for (const ClusterableObjectAssignmentData& object : clusterable_objects) {
        const camera::Sphere object_sphere = object.sphere();
        if (!in_view(object_sphere)) continue;

        const auto [aabb_min, aabb_max] = cluster_space_clusterable_object_aabb(view_from_world, view_from_world_scale,
                                                                                clip_from_view, object_sphere);

        // The slice count in z is exact, since the slices are not adjusted here.
        const std::uint32_t z_min =
            view_z_to_z_slice(cluster_factors, cluster_dimensions.z, aabb_min.z, is_orthographic);
        const std::uint32_t z_max =
            view_z_to_z_slice(cluster_factors, cluster_dimensions.z, aabb_max.z, is_orthographic);
        const auto z_count = static_cast<float>(std::max(z_min, z_max) - std::min(z_min, z_max) + 1);

        // In x and y the extent is scaled from [-1, 1] to the cluster grid, in
        // floats so that a large initial tile size does not overestimate.
        const glm::vec2 xy_count =
            (glm::vec2(aabb_max) - glm::vec2(aabb_min)) * 0.5f * glm::vec2(cluster_dimensions.x, cluster_dimensions.y);

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
EPIX_EXPORT EPIX_API glm::uvec3 reduce_cluster_dimensions_for_index_limit(glm::uvec3 dimensions,
                                                                         float index_estimate,
                                                                         std::size_t max_indices);

/** @brief An orthographic view's x and y cluster planes in view space (Bevy
 *  `assign_objects_to_clusters`, `assign.rs:484-503`).
 *
 *  Each plane is placed at the view-space position its slice boundary unprojects
 *  to at the near plane, with an axis-aligned normal and that position as its d.
 *  The x boundaries run from -1 to 1 across the view, but the y ones are
 *  *inverted* - `1 - y / slices` - because NDC y grows upward while the plane
 *  index grows downward. There are `dimensions + 1` planes per axis, the last
 *  being the far edge. */
EPIX_EXPORT EPIX_API std::pair<std::vector<camera::HalfSpace>, std::vector<camera::HalfSpace>>
orthographic_cluster_xy_planes(const glm::mat4& view_from_clip, glm::uvec3 dimensions);

/** @brief A perspective view's x and y cluster planes in view space (Bevy
 *  `assign_objects_to_clusters`, `assign.rs:504-525`).
 *
 *  Unlike the orthographic case these are not axis-aligned. Each boundary
 *  unprojects two points on the near plane - the bottom and top of the view for
 *  an x boundary, the left and right for a y boundary - and the plane is the one
 *  through them and the eye, found by crossing them. The y boundaries are
 *  inverted here too, for the same reason as the orthographic ones. */
EPIX_EXPORT EPIX_API std::pair<std::vector<camera::HalfSpace>, std::vector<camera::HalfSpace>>
perspective_cluster_xy_planes(const glm::mat4& view_from_clip, glm::uvec3 dimensions);

/** @brief A view's z cluster planes in view space (Bevy
 *  `assign_objects_to_clusters`, `assign.rs:528-534`).
 *
 *  The boundaries come from `z_slice_to_view_z`, and each plane carries the
 *  normal `-Z` with `d = -view_z`, so under Bevy's `n . x = d` convention a point
 *  at that view z lies on it. There are `z_slices + 1` planes, the first at 0 (or
 *  `-near` for an orthographic view) and the last at `-far`. */
EPIX_EXPORT EPIX_API std::vector<camera::HalfSpace> cluster_z_planes(float first_slice_depth,
                                                                    float far_z,
                                                                    std::uint32_t z_slices,
                                                                    bool is_orthographic);

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
EPIX_EXPORT EPIX_API ClusterAssignmentBounds cluster_assignment_bounds(const camera::Sphere& object_sphere,
                                                                      const glm::mat4& view_from_world,
                                                                      float view_from_world_scale_max,
                                                                      const glm::mat4& clip_from_view,
                                                                      glm::vec2 cluster_factors,
                                                                      glm::uvec3 cluster_dimensions,
                                                                      bool is_orthographic,
                                                                      glm::vec3 bounds_ndc_min,
                                                                      glm::vec3 bounds_ndc_max);

/** @brief The cluster index a (x, y, z) coordinate maps to (Bevy's
 *  `(y * dimensions.x + min_x) * dimensions.z + z`, `assign.rs:712-714`): y
 *  outermost, then x, then z innermost. */
EPIX_EXPORT inline std::size_t cluster_index(glm::uvec3 dimensions,
                                             std::uint32_t x,
                                             std::uint32_t y,
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
EPIX_EXPORT EPIX_API ClusterXRange refine_sphere_and_scan_x(glm::uvec3 min_cluster,
                                                           glm::uvec3 max_cluster,
                                                           std::uint32_t z,
                                                           std::uint32_t y,
                                                           const std::optional<std::uint32_t>& z_center,
                                                           const std::optional<std::uint32_t>& y_center,
                                                           const camera::Sphere& view_sphere,
                                                           const std::vector<camera::HalfSpace>& x_planes,
                                                           const std::vector<camera::HalfSpace>& y_planes,
                                                           const std::vector<camera::HalfSpace>& z_planes,
                                                           bool is_orthographic);

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
EPIX_EXPORT EPIX_API SpotLightClusterCull spot_light_cluster_cull(glm::vec3 light_center,
                                                                 float light_range_scaled,
                                                                 glm::vec3 light_direction,
                                                                 float angle_sin,
                                                                 float angle_cos,
                                                                 glm::vec3 cluster_center,
                                                                 float cluster_radius);

/** @brief Record that an object affects a cluster: push its entity and bump the
 *  counter for its kind (Bevy `assign.rs:766-838`).
 *
 *  This is the only place the per-cluster counts are incremented, and the kind
 *  decides which counter - a point light increments `point_lights`, a spot light
 *  `spot_lights`, and the probes, volumes and decals their own. The counts are
 *  what `prepare_lights` writes into the offsets and counts buffer, so a missing
 *  or doubled increment here shifts every later cluster's indices. */
EPIX_EXPORT EPIX_API void insert_object_into_cluster(VisibleClusterableObjects& cluster,
                                                    ecs::Entity entity,
                                                    const ClusterableObjectType& object_type);

/** @brief Record an object across a row of x clusters at one (y, z) (Bevy
 *  `assign.rs:780-789` and the other insertion arms).
 *
 *  Bevy repeats the same loop for every kind - point lights, spot lights,
 *  reflection probes, irradiance volumes and decals all affect every cluster
 *  their sphere reaches, with only the counting differing - advancing the flat
 *  index by the z count each step, which walks along x in the layout
 *  `cluster_index` defines. The probes' and decals' loops carry a TODO about
 *  culling them more aggressively with their oriented boxes. */
EPIX_EXPORT EPIX_API void insert_object_into_cluster_row(std::vector<VisibleClusterableObjects>& clusters,
                                                        glm::uvec3 dimensions,
                                                        std::uint32_t y,
                                                        std::uint32_t z,
                                                        std::uint32_t min_x,
                                                        std::uint32_t max_x,
                                                        ecs::Entity entity,
                                                        const ClusterableObjectType& object_type);

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
EPIX_EXPORT template <typename InView>
    requires std::invocable<InView&, camera::Sphere> &&
                 std::convertible_to<std::invoke_result_t<InView&, camera::Sphere>, bool>
UniformClusterableObjectLimit limit_clusterable_objects_for_uniform_buffer(
    std::vector<ClusterableObjectAssignmentData> objects, InView&& in_view, std::size_t max_objects) {
    // Sorted first, so the objects that survive the cap are the ones a clustered
    // lookup should prefer: Bevy uses `(object_type.ordering(), entity)`, the same
    // key the offsets list is built from. Without this the cap would keep whichever
    // objects happened to come first, which is not the same list.
    std::sort(objects.begin(), objects.end(),
              [](const ClusterableObjectAssignmentData& lhs, const ClusterableObjectAssignmentData& rhs) {
                  return std::tuple{lhs.object_type.ordering(), lhs.entity} <
                         std::tuple{rhs.object_type.ordering(), rhs.entity};
              });

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
EPIX_EXPORT EPIX_API float view_far_z(const ClusterFarZMode& mode,
                                      const std::vector<ClusterableObjectAssignmentData>& clusterable_objects,
                                      const glm::mat4& view_from_world,
                                      glm::vec3 view_from_world_scale);

/** @brief The depth of the first cluster slice (Bevy `assign_objects_to_clusters`,
 *  `assign.rs:351-369`).
 *
 *  An orthographic view derives it from its reverse-Z projection matrix instead of
 *  reading the configuration. A perspective view with a single slice has to reach
 *  as far as the objects do, so its first slice is the larger of the configured
 *  depth and the far plane; with more slices the configured depth stands. The
 *  result is then scaled by the view's z scale - and the far plane is raised to
 *  meet it, so the first slice can never be beyond the far plane. */
EPIX_EXPORT EPIX_API float first_slice_depth(const ClusterConfig& config,
                                            bool is_orthographic,
                                            std::uint32_t cluster_dimensions_z,
                                            const glm::mat4& clip_from_view,
                                            glm::vec3 view_from_world_scale,
                                            float far_z);

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
EPIX_EXPORT EPIX_API ClusterableObjectAssignmentData point_light_assignment_data(
    ecs::Entity entity,
    glm::vec3 translation,
    float range,
    bool shadows_enabled,
    bool volumetric,
    const camera::RenderLayers& render_layers);

/** @brief Assignment data for a spot light (Bevy `assign_objects_to_clusters`,
 *  `assign.rs:189-215`), which keeps the entity's full transform so the cone's
 *  direction survives, and carries the outer angle the cone test needs. */
EPIX_EXPORT EPIX_API ClusterableObjectAssignmentData spot_light_assignment_data(
    ecs::Entity entity,
    const glm::mat4& world_from_light,
    float range,
    float outer_angle,
    bool shadows_enabled,
    bool volumetric,
    const camera::RenderLayers& render_layers);

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
EPIX_EXPORT template <typename Compute>
    requires std::invocable<Compute&> &&
                 std::convertible_to<std::invoke_result_t<Compute&>, camera::Sphere>
inline const camera::Sphere& cached_cluster_sphere(std::vector<std::optional<camera::Sphere>>& cache,
                                                               std::size_t cluster_index,
                                                               Compute&& compute) {
    std::optional<camera::Sphere>& slot = cache[cluster_index];
    if (!slot.has_value()) {
        slot = compute();
    }
    return *slot;
}

/** @brief The cluster an object's centre falls in, in the form the refinement
 *  wants (Bevy `assign_objects_to_clusters`, `assign.rs:630-641`).
 *
 *  `z_center` is the object's z cluster when its centre is in front of the far
 *  plane, and nothing otherwise. `y_center` is stranger: nothing when the centre is
 *  *above* the view, but `dimensions.y + 1` - a value no real y can equal - when it
 *  is *below*. Both forms mean the same thing to the caller, which only asks
 *  whether a slice holds the centre, so the asymmetry reads as an accident in Bevy
 *  rather than a distinction. It is reproduced as written: choosing which form was
 *  intended would be a divergence, and this is the kind of thing that looks like a
 *  bug to "fix". */
EPIX_EXPORT EPIX_API std::pair<std::optional<std::uint32_t>, std::optional<std::uint32_t>> ndc_position_to_cluster(
    glm::uvec3 cluster_coordinates, glm::vec3 object_center_ndc, glm::uvec3 dimensions);

/** @brief An object's sphere in view space (Bevy `assign_objects_to_clusters`,
 *  `assign.rs:594-599`).
 *
 *  The centre is taken through the view matrix, and the radius is scaled by the
 *  view's **largest** scale component rather than by one axis or by an average:
 *  under a non-uniform view scale the object's extent is bounded by the biggest
 *  stretch, so using that keeps the cluster range a superset of what the object
 *  really covers. Using a smaller factor would thin the range and drop clusters the
 *  object reaches. */
EPIX_EXPORT inline camera::Sphere view_space_object_sphere(const glm::mat4& view_from_world,
                                                           float view_from_world_scale_max,
                                                           const camera::Sphere& object_sphere) {
    return camera::Sphere{.center = glm::vec3(view_from_world * glm::vec4(object_sphere.center, 1.0f)),
                          .radius = object_sphere.radius * view_from_world_scale_max};
}

/** @brief The ordered cluster range an object's bounds cover (Bevy
 *  `assign_objects_to_clusters`, `assign.rs:570-585`).
 *
 *  Both corners are converted independently and then swapped component-wise,
 *  because which corner lands nearer the view's origin is not fixed: the y axis is
 *  flipped on the way into cluster space, so an AABB's maximum y corner usually
 *  converts to a *lower* cluster row than its minimum. Bevy orders the pair here,
 *  and the loops that follow treat it as a range. */
EPIX_EXPORT inline std::pair<glm::uvec3, glm::uvec3> object_cluster_range(
    glm::uvec3 dimensions, glm::vec2 cluster_factors, bool is_orthographic, glm::vec3 aabb_min, glm::vec3 aabb_max) {
    const glm::uvec3 min_cluster =
        ndc_position_to_cluster(dimensions, cluster_factors, is_orthographic, aabb_min, aabb_min.z);
    const glm::uvec3 max_cluster =
        ndc_position_to_cluster(dimensions, cluster_factors, is_orthographic, aabb_max, aabb_max.z);
    return {glm::min(min_cluster, max_cluster), glm::max(min_cluster, max_cluster)};
}

/** @brief The per-view data the assignment loop needs (Bevy keeps these as locals
 *  inside `assign_objects_to_clusters`, built once per view before the object
 *  loop). */
EPIX_EXPORT struct ViewClusterAssignment {
    glm::uvec3 dimensions{};
    glm::vec2 tile_size{};
    glm::vec2 screen_size{};
    glm::vec2 cluster_factors{};
    glm::mat4 view_from_world{1.0f};
    glm::mat4 view_from_clip{1.0f};
    glm::mat4 clip_from_view{1.0f};
    /** @brief The largest component of the view's scale, which bounds an object's
     *  extent under a non-uniform view. */
    float view_from_world_scale_max = 1.0f;
    bool is_orthographic            = false;
    float first_slice_depth         = 0.0f;
    float far_z                     = 0.0f;
    /** @brief The view's render layers; a clusterable object only reaches a view
     *  whose layers it overlaps (Bevy tests `view_layers.intersects(...)` inside
     *  the assignment loop, `assign.rs:541`). */
    camera::RenderLayers layers = camera::RenderLayers::layer(0);
    /** @brief `dimensions + 1` planes per axis, bounding each slice. */
    std::vector<camera::HalfSpace> x_planes;
    std::vector<camera::HalfSpace> y_planes;
    std::vector<camera::HalfSpace> z_planes;
};

/** @brief Assign one object to the clusters of one view (Bevy
 *  `assign_objects_to_clusters`, `assign.rs:540-840`).
 *
 *  Returns false when the object's sphere misses the view frustum entirely, in
 *  which case nothing was assigned and the caller should not record the object as
 *  visible to that view - Bevy adds it to the global and per-view visible sets up
 *  there, behind the same test, with a note that an object intersecting the frustum
 *  must therefore be visible.
 *
 *  The shape of the rest is: the object's bounds in cluster space give a cluster
 *  range; its view-space sphere and the cluster it centres in drive the iterative
 *  refinement, which walks each (z, y) cell and returns the x clusters the refined
 *  sphere reaches; and every reached cluster gets the object. Spot lights are the
 *  exception - each cluster they reach is tested against the light's cone first,
 *  using that cluster's bounding sphere, which is why the cache exists. */
EPIX_EXPORT EPIX_API bool assign_object_to_view_clusters(std::vector<VisibleClusterableObjects>& clusters,
                                                        const ViewClusterAssignment& view,
                                                        const camera::Frustum& view_frustum,
                                                        std::vector<std::optional<camera::Sphere>>& cluster_spheres,
                                                        const ClusterableObjectAssignmentData& object);

/** @brief The length of a transform matrix's scale (Bevy
 *  `GlobalTransform::scale().length()`, reached for probes through
 *  `radius_vec3a(Vec3A::ONE)`).
 *
 *  A probe's and a decal's range is this length, so a uniformly scaled probe of 2
 *  covers `sqrt(12)` - the length of the scale *vector* - rather than twice as far,
 *  and an unscaled transform, whose scale is (1,1,1), has a range of `sqrt(3)`
 *  rather than one. Taking the largest component, or treating an unscaled object as
 *  range one, would both look like simplifications and both be divergences. */
EPIX_EXPORT inline float transform_scale_length(const glm::mat4& world_from_local) {
    const glm::vec3 scale{glm::length(glm::vec3(world_from_local[0])), glm::length(glm::vec3(world_from_local[1])),
                          glm::length(glm::vec3(world_from_local[2]))};
    return glm::length(scale);
}

/** @brief Assignment data for a light probe (Bevy `assign_objects_to_clusters`,
 *  `assign.rs:210-230`).
 *
 *  Probes are only gathered when the device has storage buffers: a uniform cannot
 *  hold their indices, and the platforms without storage buffers typically lack the
 *  bindless textures probes need anyway. A probe carrying an environment map is a
 *  reflection probe; otherwise it is an irradiance volume. Its range is its scale
 *  length, and its layers are the default. */
EPIX_EXPORT EPIX_API ClusterableObjectAssignmentData light_probe_assignment_data(ecs::Entity entity,
                                                                                const glm::mat4& world_from_probe,
                                                                                bool is_reflection_probe);

/** @brief Assignment data for a clustered decal (Bevy `assign_objects_to_clusters`,
 *  `assign.rs:232-243`). Only gathered when the platform supports clustered decals,
 *  and its range is its scale length like a probe's. */
EPIX_EXPORT EPIX_API ClusterableObjectAssignmentData decal_assignment_data(ecs::Entity entity,
                                                                          const glm::mat4& world_from_decal);

}  // namespace epix::pbr
