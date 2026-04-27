module;
#ifndef EPIX_IMPORT_STD
#include <cstdint>
#endif

export module epix.extension.fallingsand:helpers;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import glm;
import epix.render;
import epix.transform;

namespace epix::ext::fallingsand {

/**
 * @brief Convert a viewport-relative cursor position to 2-D world space.
 *
 * @param relative_pos  Normalised position in [-1, 1] for each axis (NDC origin at viewport
 * centre).
 * @param camera        Camera component holding the computed projection matrix.
 * @param cam_transform Transform of the camera entity.
 */
export inline glm::vec2 relative_to_world(glm::vec2 relative_pos,
                                          const render::camera::Camera& camera,
                                          const transform::Transform& cam_transform) {
    float ndc_x = relative_pos.x * 2.0f;
    float ndc_y = relative_pos.y * 2.0f;

    glm::mat4 proj_matrix = camera.computed.projection;
    glm::mat4 view_matrix = glm::inverse(cam_transform.to_matrix());
    glm::mat4 vp_inv      = glm::inverse(proj_matrix * view_matrix);

    glm::vec4 world = vp_inv * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
    return glm::vec2(world.x / world.w, world.y / world.w);
}

}  // namespace epix::ext::fallingsand
