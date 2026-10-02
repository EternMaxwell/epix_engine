
#include <epix/render/color_grading.hpp>

namespace epix::render::view {

ColorGradingUniform to_uniform(const ColorGrading& component) {
    // GLM is column-major, matching Bevy/glam's Mat3 constructor.
    const glm::mat3 rgb_to_lms{
        glm::vec3(0.311692f, 0.0905138f, 0.00764433f),
        glm::vec3(0.652085f, 0.901341f, 0.0486554f),
        glm::vec3(0.0362225f, 0.00814478f, 0.943700f),
    };
    const glm::mat3 lms_to_rgb{
        glm::vec3(4.06305f, -0.40791f, -0.0118812f),
        glm::vec3(-2.93241f, 1.40437f, -0.0486532f),
        glm::vec3(-0.130646f, 0.00353630f, 1.0605344f),
    };
    const glm::vec2 xy =
        glm::vec2(0.31272f, 0.32903f) + glm::vec2(-component.global.temperature, component.global.tint);
    const glm::vec3 lms =
        glm::vec3(0.701634f, 1.15856f, -0.904175f) +
        (glm::vec3(-0.051461f, 0.045854f, 0.953127f) + glm::vec3(0.452749f, -0.296122f, -0.955206f) * xy.x) / xy.y;
    const glm::vec3 scale = glm::vec3(0.975538f, 1.01648f, 1.08475f) / lms;
    const glm::mat3 adjustment{
        glm::vec3(scale.x, 0.0f, 0.0f),
        glm::vec3(0.0f, scale.y, 0.0f),
        glm::vec3(0.0f, 0.0f, scale.z),
    };
    ColorGradingUniform uniform;
    uniform.balance = lms_to_rgb * adjustment * rgb_to_lms;
    uniform.saturation =
        glm::vec3(component.shadows.saturation, component.midtones.saturation, component.highlights.saturation);
    uniform.contrast =
        glm::vec3(component.shadows.contrast, component.midtones.contrast, component.highlights.contrast);
    uniform.gamma           = glm::vec3(component.shadows.gamma, component.midtones.gamma, component.highlights.gamma);
    uniform.gain            = glm::vec3(component.shadows.gain, component.midtones.gain, component.highlights.gain);
    uniform.lift            = glm::vec3(component.shadows.lift, component.midtones.lift, component.highlights.lift);
    uniform.midtone_range   = glm::vec2(component.global.midtones_range.first, component.global.midtones_range.second);
    uniform.exposure        = component.global.exposure;
    uniform.hue             = component.global.hue;
    uniform.post_saturation = component.global.post_saturation;
    return uniform;
}

}  // namespace epix::render::view
