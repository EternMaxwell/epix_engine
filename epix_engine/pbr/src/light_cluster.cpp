#include <epix/pbr/light_cluster.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace epix::pbr {

bool assign_object_to_view_clusters(std::vector<VisibleClusterableObjects>& clusters,
                                    const ViewClusterAssignment& view,
                                    const camera::Frustum& view_frustum,
                                    std::vector<std::optional<camera::Sphere>>& cluster_spheres,
                                    const ClusterableObjectAssignmentData& object) {
    const camera::Sphere object_sphere = object.sphere();
    // Bevy tests the render layers before anything else in the loop
    // (`assign.rs:539-543`), so an object on another layer never reaches this
    // view's clusters.
    if (!view.layers.intersects(object.render_layers)) return false;
    if (!view_frustum.intersects_sphere(object_sphere, true)) return false;

    const auto [aabb_min, aabb_max] = cluster_space_clusterable_object_aabb(
        view.view_from_world, glm::vec3(view.view_from_world_scale_max), view.clip_from_view, object_sphere);
    const auto [min_cluster, max_cluster] =
        object_cluster_range(view.dimensions, view.cluster_factors, view.is_orthographic, aabb_min, aabb_max);

    const camera::Sphere view_sphere =
        view_space_object_sphere(view.view_from_world, view.view_from_world_scale_max, object_sphere);

    const bool is_spot_light = object.object_type.kind == ClusterableObjectType::Kind::SpotLight;
    glm::vec3 light_direction{0.0f};
    float angle_sin = 0.0f;
    float angle_cos = 1.0f;
    if (is_spot_light) {
        // The cone's direction is the light's backward axis - the local +Z column of
        // its transform - taken into view space.
        light_direction =
            glm::normalize(glm::vec3(view.view_from_world * glm::vec4(glm::vec3(object.transform[2]), 0.0f)));
        angle_sin = std::sin(object.object_type.outer_angle);
        angle_cos = std::cos(object.object_type.outer_angle);
    }

    const glm::vec4 center_clip     = view.clip_from_view * glm::vec4(view_sphere.center, 1.0f);
    const glm::vec3 center_ndc      = glm::vec3(center_clip) / center_clip.w;
    const glm::uvec3 center_cluster = ndc_position_to_cluster(view.dimensions, view.cluster_factors,
                                                              view.is_orthographic, center_ndc, view_sphere.center.z);
    const auto [z_center, y_center] = ndc_position_to_cluster(center_cluster, center_ndc, view.dimensions);

    for (std::uint32_t z = min_cluster.z; z <= max_cluster.z; ++z) {
        for (std::uint32_t y = min_cluster.y; y <= max_cluster.y; ++y) {
            const ClusterXRange range =
                refine_sphere_and_scan_x(min_cluster, max_cluster, z, y, z_center, y_center, view_sphere, view.x_planes,
                                         view.y_planes, view.z_planes, view.is_orthographic);
            if (!range.reached) continue;

            if (!is_spot_light) {
                // Every cluster the refined sphere reaches is affected.
                insert_object_into_cluster_row(clusters, view.dimensions, y, z, range.min_x, range.max_x, object.entity,
                                               object.object_type);
                continue;
            }

            for (std::uint32_t x = range.min_x; x <= range.max_x; ++x) {
                const std::size_t index              = cluster_index(view.dimensions, x, y, z);
                const camera::Sphere& cluster_sphere = cached_cluster_sphere(cluster_spheres, index, [&] {
                    return cluster_bounding_sphere(compute_aabb_for_cluster(
                        view.first_slice_depth, view.far_z, view.tile_size, view.screen_size, view.view_from_clip,
                        view.is_orthographic, view.dimensions, glm::uvec3(x, y, z)));
                });

                const SpotLightClusterCull cull = spot_light_cluster_cull(
                    view_sphere.center, object.range * view.view_from_world_scale_max, light_direction, angle_sin,
                    angle_cos, cluster_sphere.center, cluster_sphere.radius);
                if (!cull.culled()) {
                    insert_object_into_cluster(clusters[index], object.entity, object.object_type);
                }
            }
        }
    }
    return true;
}

camera::Aabb compute_aabb_for_cluster(float z_near,
                                      float z_far,
                                      glm::vec2 tile_size,
                                      glm::vec2 screen_size,
                                      const glm::mat4& view_from_clip,
                                      bool is_orthographic,
                                      glm::uvec3 cluster_dimensions,
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

std::pair<glm::vec3, glm::vec3> cluster_space_clusterable_object_aabb(
    const glm::mat4& view_from_world,
    glm::vec3 view_from_world_scale,
    const glm::mat4& clip_from_view,
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
    const std::array<glm::vec4, 4> corners{
        glm::vec4(view_min.x, view_min.y, view_min.z, 1.0f), glm::vec4(view_min.x, view_min.y, view_max.z, 1.0f),
        glm::vec4(view_max.x, view_max.y, view_min.z, 1.0f), glm::vec4(view_max.x, view_max.y, view_max.z, 1.0f)};

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

glm::vec2 calculate_cluster_factors(float near_plane, float far_plane, float z_slices, bool is_orthographic) {
    if (is_orthographic) return glm::vec2(-near_plane, z_slices / (-far_plane - -near_plane));
    const float z_slices_of_ln_zfar_over_znear = (z_slices - 1.0f) / std::log(far_plane / near_plane);
    return glm::vec2(z_slices_of_ln_zfar_over_znear, std::log(near_plane) * z_slices_of_ln_zfar_over_znear);
}

float z_slice_to_view_z(
    float near_plane, float far_plane, std::uint32_t z_slices, std::uint32_t z_slice, bool is_orthographic) {
    if (is_orthographic) {
        return -near_plane - (far_plane - near_plane) * static_cast<float>(z_slice) / static_cast<float>(z_slices);
    }
    if (z_slice == 0) return 0.0f;
    return -near_plane *
           std::pow(far_plane / near_plane, static_cast<float>(z_slice - 1) / static_cast<float>(z_slices - 1));
}

std::uint32_t view_z_to_z_slice(glm::vec2 cluster_factors,
                                std::uint32_t z_slices,
                                float view_z,
                                bool is_orthographic) {
    std::uint32_t z_slice = 0;
    if (is_orthographic) {
        z_slice = static_cast<std::uint32_t>(std::floor((view_z - cluster_factors.x) * cluster_factors.y));
    } else {
        z_slice = static_cast<std::uint32_t>(std::log(-view_z) * cluster_factors.x - cluster_factors.y + 1.0f);
    }
    return std::min(z_slice, z_slices - 1);
}

glm::uvec3 ndc_position_to_cluster(glm::uvec3 cluster_dimensions,
                                   glm::vec2 cluster_factors,
                                   bool is_orthographic,
                                   glm::vec3 ndc_position,
                                   float view_z) {
    const glm::vec2 dimensions = glm::vec2(cluster_dimensions);
    const glm::vec2 frag_coord = glm::clamp(glm::vec2(ndc_position) * glm::vec2(0.5f, -0.5f) + glm::vec2(0.5f),
                                            glm::vec2(0.0f), glm::vec2(1.0f));
    const glm::vec2 xy         = glm::floor(frag_coord * dimensions);

    const std::uint32_t z_slice = view_z_to_z_slice(cluster_factors, cluster_dimensions.z, view_z, is_orthographic);
    const glm::uvec3 cluster{static_cast<std::uint32_t>(xy.x), static_cast<std::uint32_t>(xy.y), z_slice};
    return glm::min(cluster, cluster_dimensions - glm::uvec3(1u));
}

std::pair<std::vector<camera::HalfSpace>, std::vector<camera::HalfSpace>> orthographic_cluster_xy_planes(
    const glm::mat4& view_from_clip, glm::uvec3 dimensions) {
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

std::pair<std::vector<camera::HalfSpace>, std::vector<camera::HalfSpace>> perspective_cluster_xy_planes(
    const glm::mat4& view_from_clip, glm::uvec3 dimensions) {
    std::vector<camera::HalfSpace> x_planes;
    x_planes.reserve(dimensions.x + 1);
    const float x_slices = static_cast<float>(dimensions.x);
    for (std::uint32_t x = 0; x <= dimensions.x; ++x) {
        const float x_pos      = (static_cast<float>(x) / x_slices) * 2.0f - 1.0f;
        const glm::vec3 bottom = glm::vec3(clip_to_view(view_from_clip, glm::vec4(x_pos, -1.0f, 1.0f, 1.0f)));
        const glm::vec3 top    = glm::vec3(clip_to_view(view_from_clip, glm::vec4(x_pos, 1.0f, 1.0f, 1.0f)));
        const glm::vec3 normal = glm::cross(bottom, top);
        x_planes.push_back(camera::HalfSpace::from_normal_d(glm::vec4(normal, glm::dot(bottom, normal))));
    }

    std::vector<camera::HalfSpace> y_planes;
    y_planes.reserve(dimensions.y + 1);
    const float y_slices = static_cast<float>(dimensions.y);
    for (std::uint32_t y = 0; y <= dimensions.y; ++y) {
        const float y_pos      = (1.0f - static_cast<float>(y) / y_slices) * 2.0f - 1.0f;
        const glm::vec3 left   = glm::vec3(clip_to_view(view_from_clip, glm::vec4(-1.0f, y_pos, 1.0f, 1.0f)));
        const glm::vec3 right  = glm::vec3(clip_to_view(view_from_clip, glm::vec4(1.0f, y_pos, 1.0f, 1.0f)));
        const glm::vec3 normal = glm::cross(right, left);
        y_planes.push_back(camera::HalfSpace::from_normal_d(glm::vec4(normal, glm::dot(right, normal))));
    }
    return {x_planes, y_planes};
}

float get_distance_x(const camera::HalfSpace& plane, glm::vec3 point, bool is_orthographic) {
    if (is_orthographic) return point.x - plane.d();
    return glm::dot(glm::vec2(plane.normal_d().x, plane.normal_d().z), glm::vec2(point.x, point.z));
}

std::optional<camera::Sphere> project_to_plane_z(const camera::Sphere& z_object, const camera::HalfSpace& z_plane) {
    const float z                 = z_plane.d() / z_plane.normal_d().z;
    const float distance_to_plane = z - z_object.center.z;
    if (std::abs(distance_to_plane) > z_object.radius) return std::nullopt;
    return camera::Sphere{
        .center = glm::vec3(z_object.center.x, z_object.center.y, z),
        .radius = std::sqrt(z_object.radius * z_object.radius - distance_to_plane * distance_to_plane)};
}

std::optional<camera::Sphere> project_to_plane_y(const camera::Sphere& y_object,
                                                const camera::HalfSpace& y_plane,
                                                bool is_orthographic) {
    const float distance_to_plane = is_orthographic ? y_plane.d() - y_object.center.y
                                                    : -glm::dot(glm::vec2(y_object.center.y, y_object.center.z),
                                                                glm::vec2(y_plane.normal_d().y, y_plane.normal_d().z));
    if (std::abs(distance_to_plane) > y_object.radius) return std::nullopt;
    return camera::Sphere{
        .center = y_object.center + distance_to_plane * y_plane.normal(),
        .radius = std::sqrt(y_object.radius * y_object.radius - distance_to_plane * distance_to_plane)};
}

glm::uvec3 reduce_cluster_dimensions_for_index_limit(glm::uvec3 dimensions,
                                                     float index_estimate,
                                                     std::size_t max_indices) {
    if (index_estimate <= static_cast<float>(max_indices)) return dimensions;
    const float index_ratio = static_cast<float>(max_indices) / index_estimate;
    const float xy_ratio    = std::sqrt(index_ratio);
    return glm::uvec3(std::max(static_cast<std::uint32_t>(std::floor(static_cast<float>(dimensions.x) * xy_ratio)), 1u),
                      std::max(static_cast<std::uint32_t>(std::floor(static_cast<float>(dimensions.y) * xy_ratio)), 1u),
                      dimensions.z);
}

std::vector<camera::HalfSpace> cluster_z_planes(float first_slice_depth,
                                                float far_z,
                                                std::uint32_t z_slices,
                                                bool is_orthographic) {
    std::vector<camera::HalfSpace> z_planes;
    z_planes.reserve(z_slices + 1);
    for (std::uint32_t z = 0; z <= z_slices; ++z) {
        const float view_z = z_slice_to_view_z(first_slice_depth, far_z, z_slices, z, is_orthographic);
        z_planes.push_back(camera::HalfSpace::from_normal_d(glm::vec4(0.0f, 0.0f, -1.0f, -view_z)));
    }
    return z_planes;
}

ClusterXRange refine_sphere_and_scan_x(glm::uvec3 min_cluster,
                                       glm::uvec3 max_cluster,
                                       std::uint32_t z,
                                       std::uint32_t y,
                                       const std::optional<std::uint32_t>& z_center,
                                       const std::optional<std::uint32_t>& y_center,
                                       const camera::Sphere& view_sphere,
                                       const std::vector<camera::HalfSpace>& x_planes,
                                       const std::vector<camera::HalfSpace>& y_planes,
                                       const std::vector<camera::HalfSpace>& z_planes,
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
        const auto projected          = project_to_plane_y(y_object, y_planes[plane_index], is_orthographic);
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

    range.reached  = true;
    range.min_x    = min_x;
    range.max_x    = max_x;
    range.y_object = y_object;
    return range;
}

void insert_object_into_cluster_row(std::vector<VisibleClusterableObjects>& clusters,
                                    glm::uvec3 dimensions,
                                    std::uint32_t y,
                                    std::uint32_t z,
                                    std::uint32_t min_x,
                                    std::uint32_t max_x,
                                    ecs::Entity entity,
                                    const ClusterableObjectType& object_type) {
    std::size_t index = cluster_index(dimensions, min_x, y, z);
    for (std::uint32_t x = min_x; x <= max_x; ++x) {
        insert_object_into_cluster(clusters[index], entity, object_type);
        index += dimensions.z;
    }
}

ClusterAssignmentBounds cluster_assignment_bounds(const camera::Sphere& object_sphere,
                                                  const glm::mat4& view_from_world,
                                                  float view_from_world_scale_max,
                                                  const glm::mat4& clip_from_view,
                                                  glm::vec2 cluster_factors,
                                                  glm::uvec3 cluster_dimensions,
                                                  bool is_orthographic,
                                                  glm::vec3 bounds_ndc_min,
                                                  glm::vec3 bounds_ndc_max) {
    ClusterAssignmentBounds bounds;

    const glm::uvec3 corner_min =
        ndc_position_to_cluster(cluster_dimensions, cluster_factors, is_orthographic, bounds_ndc_min, bounds_ndc_min.z);
    const glm::uvec3 corner_max =
        ndc_position_to_cluster(cluster_dimensions, cluster_factors, is_orthographic, bounds_ndc_max, bounds_ndc_max.z);
    bounds.min_cluster = glm::min(corner_min, corner_max);
    bounds.max_cluster = glm::max(corner_min, corner_max);

    bounds.view_sphere = camera::Sphere{.center = glm::vec3(view_from_world * glm::vec4(object_sphere.center, 1.0f)),
                                        .radius = object_sphere.radius * view_from_world_scale_max};

    const glm::vec4 center_clip     = clip_from_view * glm::vec4(bounds.view_sphere.center, 1.0f);
    const glm::vec3 center_ndc      = glm::vec3(center_clip) / center_clip.w;
    const glm::uvec3 center_cluster = ndc_position_to_cluster(cluster_dimensions, cluster_factors, is_orthographic,
                                                              center_ndc, bounds.view_sphere.center.z);

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

void insert_object_into_cluster(VisibleClusterableObjects& cluster,
                                ecs::Entity entity,
                                const ClusterableObjectType& object_type) {
    cluster.entities.push_back(entity);
    switch (object_type.kind) {
        case ClusterableObjectType::Kind::PointLight:
            cluster.counts.point_lights += 1;
            break;
        case ClusterableObjectType::Kind::SpotLight:
            cluster.counts.spot_lights += 1;
            break;
        case ClusterableObjectType::Kind::ReflectionProbe:
            cluster.counts.reflection_probes += 1;
            break;
        case ClusterableObjectType::Kind::IrradianceVolume:
            cluster.counts.irradiance_volumes += 1;
            break;
        case ClusterableObjectType::Kind::Decal:
            cluster.counts.decals += 1;
            break;
    }
}

SpotLightClusterCull spot_light_cluster_cull(glm::vec3 light_center,
                                             float light_range_scaled,
                                             glm::vec3 light_direction,
                                             float angle_sin,
                                             float angle_cos,
                                             glm::vec3 cluster_center,
                                             float cluster_radius) {
    const glm::vec3 offset  = light_center - cluster_center;
    const float distance_sq = glm::dot(offset, offset);
    const float v1_len      = glm::dot(offset, light_direction);

    SpotLightClusterCull cull;
    const float distance_closest_point = angle_cos * std::sqrt(distance_sq - v1_len * v1_len) - v1_len * angle_sin;
    cull.angle_cull                    = distance_closest_point > cluster_radius;
    cull.front_cull                    = v1_len > cluster_radius + light_range_scaled;
    cull.back_cull                     = v1_len < -cluster_radius;
    return cull;
}

float view_far_z(const ClusterFarZMode& mode,
                 const std::vector<ClusterableObjectAssignmentData>& clusterable_objects,
                 const glm::mat4& view_from_world,
                 glm::vec3 view_from_world_scale) {
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

float first_slice_depth(const ClusterConfig& config,
                        bool is_orthographic,
                        std::uint32_t cluster_dimensions_z,
                        const glm::mat4& clip_from_view,
                        glm::vec3 view_from_world_scale,
                        float far_z) {
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

ClusterableObjectAssignmentData point_light_assignment_data(ecs::Entity entity,
                                                            glm::vec3 translation,
                                                            float range,
                                                            bool shadows_enabled,
                                                            bool volumetric,
                                                            const camera::RenderLayers& render_layers) {
    ClusterableObjectAssignmentData data;
    data.entity      = entity;
    data.transform   = glm::translate(glm::mat4(1.0f), translation);
    data.range       = range;
    data.object_type = ClusterableObjectType{
        .kind = ClusterableObjectType::Kind::PointLight, .shadows_enabled = shadows_enabled, .volumetric = volumetric};
    data.render_layers = render_layers;
    return data;
}

ClusterableObjectAssignmentData spot_light_assignment_data(ecs::Entity entity,
                                                           const glm::mat4& world_from_light,
                                                           float range,
                                                           float outer_angle,
                                                           bool shadows_enabled,
                                                           bool volumetric,
                                                           const camera::RenderLayers& render_layers) {
    ClusterableObjectAssignmentData data;
    data.entity        = entity;
    data.transform     = world_from_light;
    data.range         = range;
    data.object_type   = ClusterableObjectType{.kind            = ClusterableObjectType::Kind::SpotLight,
                                               .shadows_enabled = shadows_enabled,
                                               .volumetric      = volumetric,
                                               .outer_angle     = outer_angle};
    data.render_layers = render_layers;
    return data;
}

std::pair<std::optional<std::uint32_t>, std::optional<std::uint32_t>> ndc_position_to_cluster(
    glm::uvec3 cluster_coordinates, glm::vec3 object_center_ndc, glm::uvec3 dimensions) {
    const std::optional<std::uint32_t> z_center =
        object_center_ndc.z <= 1.0f ? std::optional<std::uint32_t>{cluster_coordinates.z} : std::nullopt;

    std::optional<std::uint32_t> y_center;
    if (object_center_ndc.y > 1.0f) {
        y_center = std::nullopt;
    } else if (object_center_ndc.y < -1.0f) {
        y_center = dimensions.y + 1;
    } else {
        y_center = cluster_coordinates.y;
    }
    return {z_center, y_center};
}

ClusterableObjectAssignmentData light_probe_assignment_data(ecs::Entity entity,
                                                           const glm::mat4& world_from_probe,
                                                           bool is_reflection_probe) {
    ClusterableObjectAssignmentData data;
    data.entity    = entity;
    data.transform = world_from_probe;
    data.range     = transform_scale_length(world_from_probe);
    data.object_type =
        ClusterableObjectType{.kind = is_reflection_probe ? ClusterableObjectType::Kind::ReflectionProbe
                                                          : ClusterableObjectType::Kind::IrradianceVolume};
    return data;
}

ClusterableObjectAssignmentData decal_assignment_data(ecs::Entity entity, const glm::mat4& world_from_decal) {
    ClusterableObjectAssignmentData data;
    data.entity      = entity;
    data.transform   = world_from_decal;
    data.range       = transform_scale_length(world_from_decal);
    data.object_type = ClusterableObjectType{.kind = ClusterableObjectType::Kind::Decal};
    return data;
}

ClusterFarZMode ClusterFarZMode::constant_far(float far_z_value) noexcept {
    ClusterFarZMode mode;
    mode.kind     = Kind::Constant;
    mode.constant = far_z_value;
    return mode;
}

glm::uvec3 ClusterConfig::dimensions_for_screen_size(glm::uvec2 screen_size) const {
    switch (kind) {
        case Kind::None:
            return glm::uvec3(0u);
        case Kind::Single:
            return glm::uvec3(1u);
        case Kind::XYZ:
            return dimensions;
        case Kind::FixedZ:
            break;
    }
    if (screen_size.x == 0 || screen_size.y == 0) {
        throw std::invalid_argument(
            "Failed to calculate aspect ratio for Cluster: screen dimensions must be "
            "positive, non-zero values");
    }
    const float aspect_ratio = static_cast<float>(screen_size.x) / static_cast<float>(screen_size.y);
    std::uint32_t slices     = z_slices;
    if (total < slices) {
        // Bevy warns before clamping (cluster/mod.rs:257-260).
        spdlog::warn("[pbr] ClusterConfig has more z-slices than total clusters!");
        slices = total;
    }
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

float ClusterConfig::first_slice_depth() const noexcept {
    switch (kind) {
        case Kind::None:
        case Kind::Single:
            return 0.0f;
        case Kind::XYZ:
        case Kind::FixedZ:
            return z_config.first_slice_depth;
    }
    return 0.0f;
}

ClusterFarZMode ClusterConfig::far_z_mode() const noexcept {
    switch (kind) {
        case Kind::None:
            return ClusterFarZMode::constant_far(0.0f);
        case Kind::Single:
            return ClusterFarZMode{};
        case Kind::XYZ:
        case Kind::FixedZ:
            return z_config.far_z_mode;
    }
    return ClusterFarZMode{};
}

bool ClusterConfig::may_resize_dynamically() const noexcept {
    switch (kind) {
        case Kind::None:
        case Kind::Single:
            return false;
        case Kind::XYZ:
        case Kind::FixedZ:
            return dynamic_resizing;
    }
    return false;
}

void Clusters::update(glm::uvec2 screen_size, glm::uvec3 requested_dimensions) {
    const glm::vec2 requested_xy{requested_dimensions.x, requested_dimensions.y};
    const glm::vec2 screen{screen_size};
    tile_size = glm::max(glm::uvec2(glm::ceil(screen / requested_xy)), glm::uvec2(1u));

    const glm::uvec3 derived{glm::ceil(screen / glm::vec2(tile_size)), requested_dimensions.z};
    dimensions = glm::max(derived, glm::uvec3(1u));

    // Maximum 4096 clusters, because that is what the uniform buffer holds
    // (Bevy's `debug_assert!`, `cluster/mod.rs:331-332`).
    assert(dimensions.x * dimensions.y * dimensions.z <= 4096);
}

void Clusters::clear() {
    tile_size  = glm::uvec2(1u);
    dimensions = glm::uvec3(0u);
    near_plane = 0.0f;
    far_plane  = 0.0f;
    clusterable_objects.clear();
}

std::tuple<std::uint8_t, bool, bool> ClusterableObjectType::ordering() const noexcept {
    switch (kind) {
        case Kind::PointLight:
            return {0u, !shadows_enabled, !volumetric};
        case Kind::SpotLight:
            return {1u, !shadows_enabled, !volumetric};
        case Kind::ReflectionProbe:
            return {2u, false, false};
        case Kind::IrradianceVolume:
            return {3u, false, false};
        case Kind::Decal:
            return {4u, false, false};
    }
    return {0u, false, false};
}

}  // namespace epix::pbr
