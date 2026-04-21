module;
#ifndef EPIX_IMPORT_STD
#include <utility>
#endif

export module epix.transform;

export import glm;
import epix.core;
#ifdef EPIX_IMPORT_STD
import std;
#endif
export namespace epix::transform {
/** @brief Generic transform component with translation, rotation, and scale.
 *
 * All mutation methods return `*this` by forwarding reference, enabling
 * fluent chaining (e.g. `t.translate(...).rotate(...)`).
 * @tparam T Scalar type (float or double).
 */
template <typename T = float>
struct TransformT {
    glm::vec<3, T> translation{0.0f, 0.0f, 0.0f};
    glm::qua<T> rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec<3, T> scaler{1.0f, 1.0f, 1.0f};

    /** @brief Create an identity transform (no translation, rotation, or scale). */
    static TransformT identity() { return TransformT(); }
    /** @brief Decompose a 4x4 matrix into translation, rotation, and scale. */
    static TransformT from_matrix(const glm::mat4& matrix) {
        TransformT t;
        t.translation = glm::vec3(matrix[3]);
        t.scaler      = glm::vec3(glm::length(matrix[0]), glm::length(matrix[1]), glm::length(matrix[2]));
        // get the pure rotation matrix
        glm::mat3 rotationMatrix = glm::mat3(matrix);
        // normalize the rotation matrix
        rotationMatrix[0] /= t.scaler.x;
        rotationMatrix[1] /= t.scaler.y;
        rotationMatrix[2] /= t.scaler.z;
        t.rotation = glm::gtc::quat_cast(rotationMatrix);
        return t;
    }
    /** @brief Create a transform with only translation set. */
    static TransformT from_translation(const glm::vec3& translation) {
        TransformT t;
        t.translation = translation;
        return t;
    }
    /** @brief Create a transform from x, y, z translation. */
    static TransformT from_xyz(float x, float y, float z) { return from_translation(glm::vec3(x, y, z)); }
    /** @brief Create a transform with only rotation set. */
    static TransformT from_rotation(const glm::quat& rotation) {
        TransformT t;
        t.rotation = rotation;
        return t;
    }
    /** @brief Create a transform with only scale set. */
    static TransformT from_scale(const glm::vec3& scale) {
        TransformT t;
        t.scaler = scale;
        return t;
    }

    /** @brief Orient toward a target position. */
    auto&& look_at(this auto&& t, const glm::vec3& target, const glm::vec3& up) {
        glm::vec3 direction = glm::normalize(target - t.translation);
        t.rotation          = glm::gtc::quatLookAt(direction, up);
        return std::forward<decltype(t)>(t);
    }
    /** @brief Orient along a direction vector. */
    auto&& look_to(this auto&& t, const glm::vec3& direction, const glm::vec3& up) {
        t.rotation = glm::gtc::quatLookAt(direction, up);
        return std::forward<decltype(t)>(t);
    }

    /** @brief Compute the 4x4 model matrix (translation * rotation * scale). */
    glm::mat4 to_matrix(this const TransformT& t) {
        glm::mat3 rotate = glm::gtc::mat3_cast(t.rotation);
        glm::mat4 matrix(glm::vec4(rotate[0] * t.scaler.x, 0.0f), glm::vec4(rotate[1] * t.scaler.y, 0.0f),
                         glm::vec4(rotate[2] * t.scaler.z, 0.0f), glm::vec4(t.translation, 1.0f));
        return matrix;
    }

    /** @brief Local X axis direction (right). */
    glm::vec3 local_x(this const TransformT& t) { return glm::normalize(t.rotation * glm::vec3(1.0f, 0.0f, 0.0f)); }
    /** @brief Local Y axis direction (up). */
    glm::vec3 local_y(this const TransformT& t) { return glm::normalize(t.rotation * glm::vec3(0.0f, 1.0f, 0.0f)); }
    /** @brief Local Z axis direction (forward). */
    glm::vec3 local_z(this const TransformT& t) { return glm::normalize(t.rotation * glm::vec3(0.0f, 0.0f, 1.0f)); }

    /** @brief Apply a rotation in the parent (world) space. */
    auto&& rotate(this auto&& t, const glm::quat& rotation) {
        t.rotation = rotation * t.rotation;
        return std::forward<decltype(t)>(t);
    }
    /** @brief Apply a rotation around an axis by an angle (parent space). */
    auto&& rotate(this auto&& t, const glm::vec3& axis, float angle) {
        return t.rotate(glm::gtc::angleAxis(angle, axis));
    }
    /** @brief Rotate around the X axis in parent space. */
    auto&& rotate_x(this auto&& t, float angle) {
        return t.rotate(glm::gtc::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f)));
    }
    /** @brief Rotate around the Y axis in parent space. */
    auto&& rotate_y(this auto&& t, float angle) {
        return t.rotate(glm::gtc::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f)));
    }
    /** @brief Rotate around the Z axis in parent space. */
    auto&& rotate_z(this auto&& t, float angle) {
        return t.rotate(glm::gtc::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f)));
    }
    /** @brief Apply a rotation in the local (object) space. */
    auto&& rotate_local(this auto&& t, const glm::quat& rotation) {
        t.rotation = t.rotation * rotation;
        return std::forward<decltype(t)>(t);
    }
    /** @brief Rotate around an axis by an angle in local space. */
    auto&& rotate_local(this auto&& t, const glm::vec3& axis, float angle) {
        return t.rotate_local(glm::gtc::angleAxis(angle, axis));
    }
    /** @brief Rotate around the local X axis. */
    auto&& rotate_local_x(this auto&& t, float angle) {
        return t.rotate_local(glm::gtc::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f)));
    }
    /** @brief Rotate around the local Y axis. */
    auto&& rotate_local_y(this auto&& t, float angle) {
        return t.rotate_local(glm::gtc::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f)));
    }
    /** @brief Rotate around the local Z axis. */
    auto&& rotate_local_z(this auto&& t, float angle) {
        return t.rotate_local(glm::gtc::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f)));
    }
    /** @brief Add a translation vector. */
    auto&& translate(this auto&& t, const glm::vec3& translation) {
        t.translation += translation;
        return std::forward<decltype(t)>(t);
    }
    /** @brief Add a translation from x, y, z components. */
    auto&& translate(this auto&& t, float x, float y, float z) { return t.translate(glm::vec3(x, y, z)); }
    /** @brief Multiply current scale by a scale vector. */
    auto&& scale(this auto&& t, const glm::vec3& scale) {
        t.scaler *= scale;
        return std::forward<decltype(t)>(t);
    }
    /** @brief Multiply current scale by x, y, z components. */
    auto&& scale(this auto&& t, float x, float y, float z) { return t.scale(glm::vec3(x, y, z)); }

    /** @brief Translate the position by rotating around a point. */
    auto&& translate_around(this auto&& t, const glm::vec3& point, const glm::quat& rotation) {
        t.translation = point + rotation * (t.translation - point);
        return std::forward<decltype(t)>(t);
    }
    /** @brief Rotate the transform around a world-space point. */
    auto&& rotate_around(this auto&& t, const glm::vec3& point, const glm::quat& rotation) {
        return t.translate_around(point, rotation).rotate(rotation);
    }

    /** @brief Compose with another transform (applies other's transform on top). */
    TransformT operator*(this TransformT t, const TransformT& other) {
        t.translation = other * t.translation;
        t.rotation    = other.rotation * t.rotation;
        t.scaler *= other.scaler;
        return t;
    }
    /** @brief Transform a point by this transform (scale, rotate, translate). */
    glm::vec3 operator*(this const TransformT& t, const glm::vec3& v) {
        return t.translation + t.rotation * (v * t.scaler);
    }

    /** @brief Compose with another transform. */
    TransformT mul_transform(this const TransformT& t, const TransformT& other) { return t * other; }
    /** @brief Transform a point. */
    glm::vec3 mul_vec3(this const TransformT& t, const glm::vec3& v) { return t * v; }
};
/** @brief Float-precision transform alias. */
using Transform = TransformT<float>;
/** @brief Double-precision transform alias. */
using DTransform = TransformT<double>;
/** @brief Computed world-space transform matrix, derived from the entity hierarchy. */
struct GlobalTransform {
    glm::mat4 matrix{1.0f};
};

/** @brief System set labels for transform propagation. */
enum class TransformSets {
    CalculateGlobalTransform = 0,
};
/** @brief Plugin that registers transform propagation systems. */
struct TransformPlugin {
    void build(core::App& app);
};
}  // namespace transform