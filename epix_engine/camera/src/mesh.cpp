#include <epix/camera/mesh.hpp>
#include <stdexcept>

std::optional<epix::camera::Aabb> epix::camera::MeshAabb<epix::mesh::Mesh>::compute_aabb(const epix::mesh::Mesh& mesh) {
    const auto position = mesh.try_attribute_option(epix::mesh::Mesh::ATTRIBUTE_POSITION);
    if (!position) {
        if (position.error() == epix::mesh::MeshAccessError::ExtractedToRenderWorld) {
            throw std::logic_error("mesh data has already been extracted to the render world");
        }
        return std::nullopt;
    }
    if (!*position) return std::nullopt;
    const auto* positions = position->value().get().as_float3();
    if (!positions || positions->empty()) return std::nullopt;
    glm::vec3 minimum{positions->front()[0], positions->front()[1], positions->front()[2]};
    glm::vec3 maximum = minimum;
    for (const auto& components : *positions) {
        const glm::vec3 position_value{components[0], components[1], components[2]};
        minimum = glm::min(minimum, position_value);
        maximum = glm::max(maximum, position_value);
    }
    return Aabb::from_min_max(minimum, maximum);
}
