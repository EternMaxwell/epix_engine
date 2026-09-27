#include <epix/pbr/cascade.hpp>

namespace epix::pbr {

std::vector<float> calculate_cascade_bounds(std::size_t num_cascades,
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

CascadeShadowConfig build_cascade_shadow_config(const CascadeShadowConfigBuilder& builder) {
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

std::vector<std::pair<float, float>> cascade_bounds(const CascadeShadowConfig& config) {
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

Cascade calculate_cascade(const std::array<glm::vec3, 8>& frustum_corners,
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

std::vector<Cascade> build_view_cascades(const std::vector<std::array<glm::vec3, 8>>& slice_corners,
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
