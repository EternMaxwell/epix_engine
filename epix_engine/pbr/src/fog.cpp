#include <epix/pbr/fog.hpp>

namespace epix::pbr {

float koschmieder(float visibility, float contrast_threshold) {
    return -std::log(contrast_threshold) / visibility;
}

float fog_exponential_density(float visibility, float contrast_threshold) {
    return koschmieder(visibility, contrast_threshold);
}

float fog_exponential_squared_density(float visibility, float contrast_threshold) {
    return std::sqrt(koschmieder(visibility, contrast_threshold) / visibility);
}

glm::vec3 fog_atmospheric_extinction(glm::vec3 extinction_rgb,
                                                        float extinction_alpha,
                                                        float visibility,
                                                        float contrast_threshold) {
    const float e = static_cast<float>(std::numbers::e);
    return glm::vec3(std::pow(1.0f - extinction_rgb.x, e), std::pow(1.0f - extinction_rgb.y, e),
                     std::pow(1.0f - extinction_rgb.z, e)) *
           koschmieder(visibility, contrast_threshold) * std::pow(extinction_alpha, e);
}

glm::vec3 fog_atmospheric_inscattering(glm::vec3 inscattering_rgb) {
    const float e = static_cast<float>(std::numbers::e);
    return glm::vec3(std::pow(inscattering_rgb.x, e), std::pow(inscattering_rgb.y, e), std::pow(inscattering_rgb.z, e));
}

GpuFog gpu_fog_from(const DistanceFog& fog) {
    GpuFog gpu;
    gpu.base_color                 = fog.color;
    gpu.directional_light_color    = fog.directional_light_color;
    gpu.directional_light_exponent = fog.directional_light_exponent;

    std::visit(
        [&](const auto& falloff) {
            using T = std::decay_t<decltype(falloff)>;
            if constexpr (std::is_same_v<T, FogFalloffLinear>) {
                gpu.mode = GPU_FOG_MODE_LINEAR;
                gpu.be   = glm::vec3(falloff.start, falloff.end, 0.0f);
            } else if constexpr (std::is_same_v<T, FogFalloffExponential>) {
                gpu.mode = GPU_FOG_MODE_EXPONENTIAL;
                gpu.be   = glm::vec3(falloff.density, 0.0f, 0.0f);
            } else if constexpr (std::is_same_v<T, FogFalloffExponentialSquared>) {
                gpu.mode = GPU_FOG_MODE_EXPONENTIAL_SQUARED;
                gpu.be   = glm::vec3(falloff.density, 0.0f, 0.0f);
            } else {
                gpu.mode = GPU_FOG_MODE_ATMOSPHERIC;
                gpu.be   = falloff.extinction;
                gpu.bi   = falloff.inscattering;
            }
        },
        fog.falloff.value);
    return gpu;
}

}  // namespace epix::pbr
