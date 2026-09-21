#include <epix/transform.hpp>
#include <gtest/gtest.h>
#include <cmath>
#include <glm/glm.hpp>

/** @brief The transform's handedness: Bevy is right-handed with Y up and -Z
 *  forward, and the engine's camera follows it, so `look_at` must aim the local
 *  **-Z** axis at its target while +Z is the backward axis. glm's unqualified
 *  `quatLookAt` is left-handed when `GLM_FORCE_LEFT_HANDED` is defined, which is
 *  what this pins against. */
namespace {

TEST(Transform, LookAtIsRightHanded) {
    const glm::vec3 target{1.0f, 0.0f, 0.0f};
    const glm::vec3 up{0.0f, 1.0f, 0.0f};

    const auto oriented = epix::transform::Transform{}.look_at(target, up);

    // Forward is the local -Z axis, so it points at the target.
    const glm::vec3 forward = glm::normalize(oriented.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
    EXPECT_NEAR(forward.x, 1.0f, 1e-5f) << "forward points at the target";
    EXPECT_NEAR(forward.y, 0.0f, 1e-5f);
    EXPECT_NEAR(forward.z, 0.0f, 1e-5f);

    // The local +Z axis therefore points away from it, which is what Bevy calls
    // `back` - and what the light path reads for a spot light's direction.
    const glm::vec3 back = glm::normalize(oriented.rotation * glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_NEAR(back.x, -1.0f, 1e-5f) << "back points away from the target";
    EXPECT_NEAR(oriented.local_z().x, -1.0f, 1e-5f) << "and local_z is that same backward axis";

    // The up vector is honoured, and the basis stays right-handed.
    const glm::vec3 local_up = glm::normalize(oriented.rotation * glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(local_up.y, 1.0f, 1e-5f) << "up is preserved";
    const glm::vec3 right = glm::normalize(oriented.rotation * glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_NEAR(glm::dot(glm::cross(right, local_up), back), 1.0f, 1e-4f)
        << "right cross up is back, the right-handed relation";
}

/** @brief `look_to` takes the direction directly, with the same convention: the
 *  given direction is where the local -Z axis ends up pointing. */
TEST(Transform, LookToIsRightHanded) {
    const auto oriented = epix::transform::Transform{}.look_to(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 forward = glm::normalize(oriented.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
    EXPECT_NEAR(forward.z, -1.0f, 1e-5f) << "looking along -Z leaves the identity rotation";

    const auto sideways = epix::transform::Transform{}.look_to(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 reversed = glm::normalize(sideways.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
    EXPECT_NEAR(reversed.z, 1.0f, 1e-5f) << "looking along +Z turns the forward axis around";
}

}  // namespace
