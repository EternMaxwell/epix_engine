#pragma once

#ifndef EPIX_CXX_MODULE
#include <cmath>
#include <cstddef>
#include <epix/ecs.hpp>
#include <glm/glm.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief The cascade bounds for a directional light's shadow map (Bevy
 *  `calculate_cascade_bounds`, `bevy_light/src/cascade.rs:39-54`): the bounds are
 *  laid out geometrically between the first cascade's far bound and the shadow
 *  maximum distance, with a single cascade simply reaching the maximum. */
EPIX_EXPORT inline std::vector<float> calculate_cascade_bounds(std::size_t num_cascades, float nearest_bound,
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

/** @brief Builds a `CascadeShadowConfig`, validating first (Bevy
 *  `CascadeShadowConfigBuilder`, `cascade.rs:57-156`).
 *
 *  Bevy asserts on invalid input; Epix's established convention for that is a
 *  typed exception, so the same five conditions throw `std::invalid_argument`
 *  with Bevy's wording. */
EPIX_EXPORT struct CascadeShadowConfigBuilder {
    /** @brief The number of cascades. Bevy uses one on WebGL2 without WebGPU and
     *  four everywhere else; Epix targets the four-cascade case. */
    std::size_t num_cascades = 4;
    /** @brief The distance to the near boundary of the first cascade. */
    float minimum_distance = 0.1f;
    /** @brief The maximum shadow distance. */
    float maximum_distance = 150.0f;
    /** @brief The distance to the far boundary of the first cascade. */
    float first_cascade_far_bound = 10.0f;
    /** @brief The proportion of overlap each cascade has with the previous one. */
    float overlap_proportion = 0.2f;
};

/** @brief A directional light's cascade configuration (Bevy
 *  `CascadeShadowConfig`, `cascade.rs:24-37`). */
EPIX_EXPORT struct CascadeShadowConfig {
    /** @brief The positive distance to the far boundary of each cascade. */
    std::vector<float> bounds;
    /** @brief The proportion of overlap each cascade has with the previous one. */
    float overlap_proportion = 0.0f;
    /** @brief The positive distance to the near boundary of the first cascade. */
    float minimum_distance = 0.0f;
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
        .bounds = calculate_cascade_bounds(builder.num_cascades, builder.first_cascade_far_bound,
                                           builder.maximum_distance),
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

}  // namespace epix::pbr
