#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace epix::transform {
template <typename T = float>
struct TransformT {
    glm::vec<3, T> translation{0.0f, 0.0f, 0.0f};
    glm::qua<T> rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec<3, T> scaler{1.0f, 1.0f, 1.0f};

    static TransformT identity() { return TransformT(); }
    static TransformT from_matrix(const glm::mat4& matrix) {
        TransformT t;
        t.translation = glm::vec3(matrix[3]);
        t.scaler = glm::vec3(glm::length(matrix[0]), glm::length(matrix[1]),
                             glm::length(matrix[2]));
        // get the pure rotation matrix
        glm::mat3 rotationMatrix = glm::mat3(matrix);
        // normalize the rotation matrix
        rotationMatrix[0] /= t.scaler.x;
        rotationMatrix[1] /= t.scaler.y;
        rotationMatrix[2] /= t.scaler.z;
        t.rotation = glm::quat_cast(rotationMatrix);
        return t;
    }
    static TransformT from_translation(const glm::vec3& translation) {
        TransformT t;
        t.translation = translation;
        return t;
    }
    static TransformT from_xyz(float x, float y, float z) {
        return from_translation(glm::vec3(x, y, z));
    }
    static TransformT from_rotation(const glm::quat& rotation) {
        TransformT t;
        t.rotation = rotation;
        return t;
    }
    static TransformT from_scale(const glm::vec3& scale) {
        TransformT t;
        t.scaler = scale;
        return t;
    }

    auto&& look_at(this auto&& t,
                   const glm::vec3& target,
                   const glm::vec3& up) {
        glm::vec3 direction = glm::normalize(target - t.translation);
        t.rotation          = glm::quatLookAt(direction, up);
        return std::forward<decltype(t)>(t);
    }
    auto&& look_to(this auto&& t,
                   const glm::vec3& direction,
                   const glm::vec3& up) {
        t.rotation = glm::quatLookAt(direction, up);
        return std::forward<decltype(t)>(t);
    }

    glm::mat4 to_matrix(this const TransformT& t) {
        glm::mat3 rotate = glm::mat3_cast(t.rotation);
        glm::mat4 matrix(glm::vec4(rotate[0] * t.scaler.x, 0.0f),
                         glm::vec4(rotate[1] * t.scaler.y, 0.0f),
                         glm::vec4(rotate[2] * t.scaler.z, 0.0f),
                         glm::vec4(t.translation, 1.0f));
        return matrix;
    }

    glm::vec3 local_x(this const TransformT& t) {
        return glm::normalize(t.rotation * glm::vec3(1.0f, 0.0f, 0.0f));
    }
    glm::vec3 local_y(this const TransformT& t) {
        return glm::normalize(t.rotation * glm::vec3(0.0f, 1.0f, 0.0f));
    }
    glm::vec3 local_z(this const TransformT& t) {
        return glm::normalize(t.rotation * glm::vec3(0.0f, 0.0f, 1.0f));
    }

    // Adding rotation in the parent space
    auto&& rotate(this auto&& t, const glm::quat& rotation) {
        t.rotation = rotation * t.rotation;
        return std::forward<decltype(t)>(t);
    }
    auto&& rotate(this auto&& t, const glm::vec3& axis, float angle) {
        return t.rotate(glm::angleAxis(angle, axis));
    }
    auto&& rotate_x(this auto&& t, float angle) {
        return t.rotate(glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f)));
    }
    auto&& rotate_y(this auto&& t, float angle) {
        return t.rotate(glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f)));
    }
    auto&& rotate_z(this auto&& t, float angle) {
        return t.rotate(glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f)));
    }
    // Adding rotation in the local space
    auto&& rotate_local(this auto&& t, const glm::quat& rotation) {
        t.rotation = t.rotation * rotation;
        return std::forward<decltype(t)>(t);
    }
    auto&& rotate_local(this auto&& t, const glm::vec3& axis, float angle) {
        return t.rotate_local(glm::angleAxis(angle, axis));
    }
    auto&& rotate_local_x(this auto&& t, float angle) {
        return t.rotate_local(
            glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f)));
    }
    auto&& rotate_local_y(this auto&& t, float angle) {
        return t.rotate_local(
            glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f)));
    }
    auto&& rotate_local_z(this auto&& t, float angle) {
        return t.rotate_local(
            glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f)));
    }
    auto&& translate(this auto&& t, const glm::vec3& translation) {
        t.translation += translation;
        return std::forward<decltype(t)>(t);
    }
    auto&& translate(this auto&& t, float x, float y, float z) {
        return t.translate(glm::vec3(x, y, z));
    }
    auto&& scale(this auto&& t, const glm::vec3& scale) {
        t.scaler *= scale;
        return std::forward<decltype(t)>(t);
    }
    auto&& scale(this auto&& t, float x, float y, float z) {
        return t.scale(glm::vec3(x, y, z));
    }

    auto&& translate_around(this auto&& t,
                            const glm::vec3& point,
                            const glm::quat& rotation) {
        t.translation = point + rotation * (t.translation - point);
        return std::forward<decltype(t)>(t);
    }
    auto&& rotate_around(this auto&& t,
                         const glm::vec3& point,
                         const glm::quat& rotation) {
        return t.translate_around(point, rotation).rotate(rotation);
    }

    TransformT operator*(this TransformT t, const TransformT& other) {
        t.translation = other * t.translation;
        t.rotation    = other.rotation * t.rotation;
        t.scaler *= other.scaler;
        return t;
    }
    glm::vec3 operator*(this const TransformT& t, const glm::vec3& v) {
        return t.translation + t.rotation * (v * t.scaler);
    }

    TransformT mul_transform(this const TransformT& t,
                             const TransformT& other) {
        return t * other;
    }
    glm::vec3 mul_vec3(this const TransformT& t, const glm::vec3& v) {
        return t * v;
    }
};
using Transform  = TransformT<float>;
using DTransform = TransformT<double>;
struct GlobalTransform {
    glm::mat4 matrix{1.0f};
};
}  // namespace epix::transform