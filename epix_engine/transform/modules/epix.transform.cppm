/**
 * @file epix.transform.cppm
 * @brief Transform module for spatial transformations
 */

export module epix.transform;

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

// Module imports
#include <epix/core.hpp>

export namespace epix::transform {
    // Transform component
    struct Transform {
        glm::vec3 translation = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
        
        Transform() = default;
        Transform(glm::vec3 pos) : translation(pos) {}
        Transform(glm::vec3 pos, glm::quat rot) : translation(pos), rotation(rot) {}
        Transform(glm::vec3 pos, glm::quat rot, glm::vec3 scl) : translation(pos), rotation(rot), scale(scl) {}
        
        glm::mat4 compute_matrix() const {
            glm::mat4 mat = glm::mat4(1.0f);
            mat = glm::translate(mat, translation);
            mat = mat * glm::mat4_cast(rotation);
            mat = glm::scale(mat, scale);
            return mat;
        }
        
        glm::vec3 forward() const {
            return rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        }
        
        glm::vec3 right() const {
            return rotation * glm::vec3(1.0f, 0.0f, 0.0f);
        }
        
        glm::vec3 up() const {
            return rotation * glm::vec3(0.0f, 1.0f, 0.0f);
        }
        
        void look_at(glm::vec3 target, glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f)) {
            glm::mat4 view = glm::lookAt(translation, target, up);
            rotation = glm::quat_cast(glm::inverse(view));
        }
    };
    
    // Global transform (computed from hierarchy)
    struct GlobalTransform {
        glm::mat4 matrix = glm::mat4(1.0f);
        
        GlobalTransform() = default;
        GlobalTransform(const glm::mat4& mat) : matrix(mat) {}
        
        glm::vec3 translation() const {
            return glm::vec3(matrix[3]);
        }
        
        glm::quat rotation() const {
            return glm::quat_cast(matrix);
        }
        
        glm::vec3 scale() const {
            return glm::vec3(
                glm::length(glm::vec3(matrix[0])),
                glm::length(glm::vec3(matrix[1])),
                glm::length(glm::vec3(matrix[2]))
            );
        }
    };
    
    // Transform plugin
    struct TransformPlugin {
        void build(epix::App& app);
    };
}  // namespace epix::transform
