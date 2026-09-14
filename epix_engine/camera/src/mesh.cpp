#include <epix/camera/mesh.hpp>
#include <ranges>

std::optional<epix::camera::Aabb> epix::camera::MeshAabb<epix::mesh::Mesh>::compute_aabb(const epix::mesh::Mesh& mesh) {
    // Use the precomputed extents, exactly as Bevy prefers them.
    if (mesh.final_aabb) return Aabb::from_min_max(mesh.final_aabb->min, mesh.final_aabb->max);

    // Bevy's `let Ok(VertexAttributeValues::Float32x3(values)) = mesh.try_attribute(..) else {
    // return None }` turns every access failure - a missing attribute, a value that is not
    // `float3`, or data already extracted to the render world - into "no bounds" rather than an
    // error, so this returns no value instead of throwing.
    const auto position = mesh.try_attribute_option(epix::mesh::Mesh::ATTRIBUTE_POSITION);
    if (!position || !*position) return std::nullopt;
    const auto* positions = position->value().get().as_float3();
    if (positions == nullptr) return std::nullopt;

    return Aabb::enclosing(*positions |
                           std::views::transform([](const epix::mesh::VertexAttributeValues::Float32x3Value& value) {
                               return glm::vec3{value[0], value[1], value[2]};
                           }));
}
