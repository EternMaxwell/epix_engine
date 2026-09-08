#include <epix/camera/mesh.hpp>

#include <stdexcept>

std::optional<epix::camera::Aabb> epix::camera::MeshAabb<epix::mesh::Mesh>::compute_aabb(
    const epix::mesh::Mesh& mesh) {
    const auto position = mesh.try_attribute_option(epix::mesh::Mesh::ATTRIBUTE_POSITION);
    if (!position) {
        if (position.error() == epix::mesh::MeshAccessError::ExtractedToRenderWorld) {
            throw std::logic_error("mesh data has already been extracted to the render world");
        }
        return std::nullopt;
    }
    if (!*position || position->value().get().type_info() != epix::meta::type_info::of<glm::vec3>()) {
        return std::nullopt;
    }
    const auto positions = position->value().get().cspan_as<glm::vec3>();
    if (positions.empty()) return std::nullopt;
    glm::vec3 minimum = positions.front();
    glm::vec3 maximum = positions.front();
    for (const glm::vec3& position_value : positions) {
        minimum = glm::min(minimum, position_value);
        maximum = glm::max(maximum, position_value);
    }
    return Aabb::from_min_max(minimum, maximum);
}
