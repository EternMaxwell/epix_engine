#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cmath>
#include <cstddef>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#endif

namespace epix::pbr {

/** @brief The cascade bounds for a directional light's shadow map (Bevy
 *  `calculate_cascade_bounds`, `bevy_light/src/cascade.rs:39-54`): the bounds are
 *  laid out geometrically between the first cascade's far bound and the shadow
 *  maximum distance, with a single cascade simply reaching the maximum. */
EPIX_EXPORT inline std::vector<float> calculate_cascade_bounds(std::size_t num_cascades,
                                                               float nearest_bound,
                                                               float shadow_maximum_distance) {
    if (num_cascades == 1) return {shadow_maximum_distance};

    const float base = std::pow(shadow_maximum_distance / nearest_bound, 1.0f / static_cast<float>(num_cascades - 1));

    std::vector<float> bounds;
    bounds.reserve(num_cascades);
    for (std::size_t index = 0; index < num_cascades; ++index) {
        bounds.push_back(nearest_bound * std::pow(base, static_cast<float>(index)));
    }
    return bounds;
}

/** @brief The builder's defaults (Bevy `CascadeShadowConfigBuilder::default`,
 *  `bevy_light/src/cascade.rs:133-155`), named so that both the builder and a
 *  default-constructed `CascadeShadowConfig` come from one source.
 *
 *  Bevy uses one cascade on WebGL2 without WebGPU and four everywhere else; Epix
 *  targets the four-cascade case. */
EPIX_EXPORT inline constexpr std::size_t DEFAULT_CASCADE_COUNT        = 4;
EPIX_EXPORT inline constexpr float DEFAULT_CASCADE_MINIMUM_DISTANCE   = 0.1f;
EPIX_EXPORT inline constexpr float DEFAULT_CASCADE_MAXIMUM_DISTANCE   = 150.0f;
EPIX_EXPORT inline constexpr float DEFAULT_CASCADE_FIRST_FAR_BOUND    = 10.0f;
EPIX_EXPORT inline constexpr float DEFAULT_CASCADE_OVERLAP_PROPORTION = 0.2f;

/** @brief Builds a `CascadeShadowConfig`, validating first (Bevy
 *  `CascadeShadowConfigBuilder`, `cascade.rs:57-156`).
 *
 *  Bevy asserts on invalid input; Epix's established convention for that is a
 *  typed exception, so the same five conditions throw `std::invalid_argument`
 *  with Bevy's wording. */
EPIX_EXPORT struct CascadeShadowConfigBuilder {
    /** @brief The number of cascades. */
    std::size_t num_cascades = DEFAULT_CASCADE_COUNT;
    /** @brief The distance to the near boundary of the first cascade. */
    float minimum_distance = DEFAULT_CASCADE_MINIMUM_DISTANCE;
    /** @brief The maximum shadow distance. */
    float maximum_distance = DEFAULT_CASCADE_MAXIMUM_DISTANCE;
    /** @brief The distance to the far boundary of the first cascade. */
    float first_cascade_far_bound = DEFAULT_CASCADE_FIRST_FAR_BOUND;
    /** @brief The proportion of overlap each cascade has with the previous one. */
    float overlap_proportion = DEFAULT_CASCADE_OVERLAP_PROPORTION;
};

/** @brief A directional light's cascade configuration (Bevy
 *  `CascadeShadowConfig`, `cascade.rs:24-37`).
 *
 *  Bevy's `Default` builds from the default builder, which a C++ aggregate
 *  cannot express by calling a function declared below it - so the bounds are
 *  defaulted from the same named defaults the builder uses, and the two cannot
 *  drift apart. */
EPIX_EXPORT struct CascadeShadowConfig {
    /** @brief The positive distance to the far boundary of each cascade. */
    std::vector<float> bounds = calculate_cascade_bounds(
        DEFAULT_CASCADE_COUNT, DEFAULT_CASCADE_FIRST_FAR_BOUND, DEFAULT_CASCADE_MAXIMUM_DISTANCE);
    /** @brief The proportion of overlap each cascade has with the previous one. */
    float overlap_proportion = DEFAULT_CASCADE_OVERLAP_PROPORTION;
    /** @brief The positive distance to the near boundary of the first cascade. */
    float minimum_distance = DEFAULT_CASCADE_MINIMUM_DISTANCE;
};

EPIX_EXPORT inline CascadeShadowConfig build_cascade_shadow_config(const CascadeShadowConfigBuilder& builder) {
    if (builder.num_cascades == 0) {
        throw std::invalid_argument("num_cascades must be positive, but was " + std::to_string(builder.num_cascades));
    }
    if (builder.minimum_distance < 0.0f) {
        throw std::invalid_argument("maximum_distance must be non-negative, but was " +
                                    std::to_string(builder.minimum_distance));
    }
    if (builder.num_cascades != 1 && builder.minimum_distance >= builder.first_cascade_far_bound) {
        throw std::invalid_argument("minimum_distance must be less than first_cascade_far_bound, but was " +
                                    std::to_string(builder.minimum_distance));
    }
    if (builder.maximum_distance <= builder.minimum_distance) {
        throw std::invalid_argument("maximum_distance must be greater than minimum_distance, but was " +
                                    std::to_string(builder.maximum_distance));
    }
    if (!(builder.overlap_proportion >= 0.0f && builder.overlap_proportion < 1.0f)) {
        throw std::invalid_argument("overlap_proportion must be in [0.0, 1.0) but was " +
                                    std::to_string(builder.overlap_proportion));
    }

    return CascadeShadowConfig{
        .bounds =
            calculate_cascade_bounds(builder.num_cascades, builder.first_cascade_far_bound, builder.maximum_distance),
        .overlap_proportion = builder.overlap_proportion,
        .minimum_distance   = builder.minimum_distance,
    };
}

/** @brief `CascadeShadowConfig::default()` builds from the default builder
 *  (Bevy `cascade.rs:33-37`). */
EPIX_EXPORT inline CascadeShadowConfig default_cascade_shadow_config() {
    return build_cascade_shadow_config(CascadeShadowConfigBuilder{});
}

/** @brief One cascade of a directional light's shadow map (Bevy `Cascade`,
 *  `bevy_light/src/cascade.rs:169-184`). */
EPIX_EXPORT struct Cascade {
    /** @brief The cascade's view transform in the world. */
    glm::mat4 world_from_cascade{1.0f};
    /** @brief The cascade's projection. */
    glm::mat4 clip_from_cascade{1.0f};
    /** @brief The combined transform, which shadow-maps the cascade. */
    glm::mat4 clip_from_world{1.0f};
    /** @brief The cascade's texel size in world units, used to scale shadow
     *  normal biases. */
    float texel_size = 0.0f;
};

/** @brief A directional light's cascades, keyed by the view they were built for
 *  (Bevy `Cascades`, `cascade.rs:164-168`, whose map is Bevy's
 *  `EntityHashMap<Vec<Cascade>>`). It is a component of the light entity, and
 *  `build_directional_light_cascades` fills it per visible view. */
EPIX_EXPORT struct Cascades {
    std::unordered_map<ecs::Entity, std::vector<Cascade>> cascades;
};

/** @brief The near and far bound of each cascade (Bevy `cascade.rs:232-248`).
 *
 *  The first cascade starts at the configuration's minimum distance; each later
 *  one starts at the previous far bound scaled by `1 - overlap_proportion`, which
 *  is what makes consecutive cascades overlap. */
EPIX_EXPORT inline std::vector<std::pair<float, float>> cascade_bounds(const CascadeShadowConfig& config) {
    const float overlap_factor = 1.0f - config.overlap_proportion;

    std::vector<std::pair<float, float>> bounds;
    bounds.reserve(config.bounds.size());
    float near_bound = config.minimum_distance;
    for (float far_bound : config.bounds) {
        bounds.emplace_back(near_bound, far_bound);
        near_bound = overlap_factor * far_bound;
    }
    return bounds;
}

/** @brief One cascade from a slice of the camera frustum (Bevy `calculate_cascade`,
 *  `cascade.rs:261-333`).
 *
 *  The corners must be ordered bottom-right, top-right, top-left, bottom-left for
 *  the near plane, then the same order for the far plane.
 *
 *  Three details exist purely for shadow stability, and Bevy's comments say so:
 *  the cascade's diameter is the larger of the body and far-plane diagonals
 *  *ceiled to an integer*, so the texel size is an exact multiple of a power of
 *  two; the near-plane centre is snapped to integer multiples of that texel size so
 *  it is exactly representable; and the inverse is formed directly rather than by
 *  inverting `world_from_cascade`, which would risk numerical instability in a
 *  matrix that must stay stable frame to frame. The projection is a right-handed
 *  orthographic one, reverse-Z, which is where it differs from the reference
 *  material Bevy cites. */
EPIX_EXPORT inline Cascade calculate_cascade(const std::array<glm::vec3, 8>& frustum_corners,
                                             float cascade_texture_size,
                                             const glm::mat4& world_from_light,
                                             const glm::mat4& light_from_camera) {
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    for (const glm::vec3& corner : frustum_corners) {
        const glm::vec3 in_light = glm::vec3(light_from_camera * glm::vec4(corner, 1.0f));
        minimum                  = glm::min(minimum, in_light);
        maximum                  = glm::max(maximum, in_light);
    }

    // The diagonals are measured on the original camera-space corners, because the
    // light-space ones can differ slightly in floating point. lengths are squared
    // (glm has no length_squared, so they are the dot product with themselves).
    const glm::vec3 body_a         = frustum_corners[0] - frustum_corners[6];
    const glm::vec3 far_a          = frustum_corners[4] - frustum_corners[6];
    const float body_diagonal      = glm::dot(body_a, body_a);
    const float far_diagonal       = glm::dot(far_a, far_a);
    const float cascade_diameter   = std::ceil(std::sqrt(std::max(body_diagonal, far_diagonal)));
    const float cascade_texel_size = cascade_diameter / cascade_texture_size;

    // max.z is the near plane for a right-handed y-up view.
    const glm::vec3 near_plane_center{
        std::floor(0.5f * (minimum.x + maximum.x) / cascade_texel_size) * cascade_texel_size,
        std::floor(0.5f * (minimum.y + maximum.y) / cascade_texel_size) * cascade_texel_size, maximum.z};

    glm::mat4 cascade_from_world = glm::transpose(world_from_light);
    cascade_from_world[3]        = glm::vec4(-near_plane_center, 1.0f);

    glm::mat4 world_from_cascade = world_from_light;
    world_from_cascade[3]        = glm::vec4(glm::vec3(world_from_light * glm::vec4(near_plane_center, 1.0f)), 1.0f);

    glm::mat4 clip_from_cascade{0.0f};
    clip_from_cascade[0][0] = 2.0f / cascade_diameter;
    clip_from_cascade[1][1] = 2.0f / cascade_diameter;
    clip_from_cascade[2][2] = 1.0f / (maximum.z - minimum.z);
    clip_from_cascade[3][2] = 1.0f;
    clip_from_cascade[3][3] = 1.0f;

    return Cascade{.world_from_cascade = world_from_cascade,
                   .clip_from_cascade  = clip_from_cascade,
                   .clip_from_world    = clip_from_cascade * cascade_from_world,
                   .texel_size         = cascade_texel_size};
}

/** @brief The cascades a directional light builds for one view (Bevy
 *  `build_directional_light_cascades`, `cascade.rs:216-251`).
 *
 *  `slice_corners` holds the camera-frustum corners of each cascade's slice, in
 *  the order `calculate_cascade` expects, which the caller gets from its
 *  projection at the bounds `cascade_bounds` returns - kept as an input so the
 *  assembly here does not depend on a projection type.
 *
 *  The light matrix is built from the light's **rotation alone**. Bevy's comment
 *  is emphatic about it: the matrix must have an orthogonal upper-left 3x3 and
 *  zero translation for the shadows to be numerically stable, and nothing stops a
 *  user putting scale or translation on a light's transform, so the transform's
 *  matrix is deliberately not used. */
EPIX_EXPORT inline std::vector<Cascade> build_view_cascades(const std::vector<std::array<glm::vec3, 8>>& slice_corners,
                                                            float cascade_texture_size,
                                                            const glm::quat& light_rotation,
                                                            const glm::mat4& view_to_world) {
    const glm::mat4 world_from_light  = glm::mat4_cast(light_rotation);
    const glm::mat4 light_from_camera = glm::transpose(world_from_light) * view_to_world;

    std::vector<Cascade> cascades;
    cascades.reserve(slice_corners.size());
    for (const std::array<glm::vec3, 8>& corners : slice_corners) {
        cascades.push_back(calculate_cascade(corners, cascade_texture_size, world_from_light, light_from_camera));
    }
    return cascades;
}

}  // namespace epix::pbr
