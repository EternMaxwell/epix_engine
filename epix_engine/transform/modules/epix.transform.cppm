/**
 * @file epix.transform.cppm
 * @brief Transform module for 3D spatial transformations
 */

export module epix.transform;

#include <epix/core.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

export namespace epix::transform {
    // Transform template (float or double)
    template <typename T = float>
    struct TransformT {
        glm::vec<3, T> translation{0.0f, 0.0f, 0.0f};
        glm::qua<T> rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec<3, T> scaler{1.0f, 1.0f, 1.0f};

        static TransformT identity();
        static TransformT from_matrix(const glm::mat4& matrix);
        static TransformT from_translation(const glm::vec3& translation);
        static TransformT from_xyz(float x, float y, float z);
        static TransformT from_rotation(const glm::quat& rotation);
        static TransformT from_scale(const glm::vec3& scale);

        auto&& look_at(this auto&& t, const glm::vec3& target, const glm::vec3& up);
        auto&& look_to(this auto&& t, const glm::vec3& direction, const glm::vec3& up);

        glm::mat4 to_matrix(this const TransformT& t);

        glm::vec3 local_x(this const TransformT& t);
        glm::vec3 local_y(this const TransformT& t);
        glm::vec3 local_z(this const TransformT& t);

        auto&& rotate(this auto&& t, const glm::quat& rotation);
        auto&& rotate(this auto&& t, const glm::vec3& axis, float angle);
        auto&& rotate_x(this auto&& t, float angle);
        auto&& rotate_y(this auto&& t, float angle);
        auto&& rotate_z(this auto&& t, float angle);
        
        auto&& rotate_local(this auto&& t, const glm::quat& rotation);
        auto&& rotate_local(this auto&& t, const glm::vec3& axis, float angle);
        auto&& rotate_local_x(this auto&& t, float angle);
        auto&& rotate_local_y(this auto&& t, float angle);
        auto&& rotate_local_z(this auto&& t, float angle);
        
        auto&& translate(this auto&& t, const glm::vec3& translation);
        auto&& translate(this auto&& t, float x, float y, float z);
        auto&& scale(this auto&& t, const glm::vec3& scale);
        auto&& scale(this auto&& t, float x, float y, float z);
    };
    
    using Transform = TransformT<float>;
    using TransformD = TransformT<double>;
    
    // Global transform computed from hierarchy
    template <typename T = float>
    struct GlobalTransformT {
        glm::mat<4, 4, T> matrix{1.0f};
        
        GlobalTransformT() = default;
        GlobalTransformT(const glm::mat<4, 4, T>& m) : matrix(m) {}
        
        glm::vec<3, T> translation() const;
        glm::qua<T> rotation() const;
        glm::vec<3, T> scale() const;
    };
    
    using GlobalTransform = GlobalTransformT<float>;
    using GlobalTransformD = GlobalTransformT<double>;
    
    // Transform plugin
    struct TransformPlugin {
        void build(epix::App& app);
    };
    
}  // namespace epix::transform
