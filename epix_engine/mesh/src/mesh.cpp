

#include <epix/assets.hpp>
#include <epix/mesh.hpp>

using namespace epix::mesh;

MeshAttributeLayout Mesh::attribute_layout() const {
    MeshAttributeLayout layout;
    layout.primitive_type = primitive_type;
    for (const auto& [slot, attribute_data] : _attributes) {
        layout.insert_or_assign(slot, attribute_data.attribute);
    }
    return layout;
}

std::optional<epix::camera::Aabb> Mesh::compute_aabb() const {
    const auto attribute = get_attribute(ATTRIBUTE_POSITION);
    if (!attribute || attribute->get().data.type_info() != epix::meta::type_info::of<glm::vec3>()) return std::nullopt;
    const auto positions = attribute->get().data.cspan_as<glm::vec3>();
    if (positions.empty()) return std::nullopt;
    glm::vec3 minimum = positions.front();
    glm::vec3 maximum = positions.front();
    for (const glm::vec3& position : positions) {
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    return epix::camera::Aabb::from_min_max(minimum, maximum);
}

std::expected<std::reference_wrapper<const MeshAttributeData>, MeshError> Mesh::get_attribute(
    const MeshAttribute& attribute) const {
    auto it = _attributes.find(attribute.slot);
    if (it == _attributes.end()) {
        return std::unexpected(MeshError::SlotNotFound);
    } else if (it->second.attribute.name != attribute.name) {
        return std::unexpected(MeshError::NameMismatch);
    } else if (it->second.attribute.format != attribute.format) {
        return std::unexpected(MeshError::TypeMismatch);
    }
    return std::cref(it->second);
}

std::expected<std::reference_wrapper<const MeshAttributeData>, MeshError> Mesh::get_attribute(std::size_t slot) const {
    if (auto it = _attributes.find(slot); it != _attributes.end()) {
        return std::cref(it->second);
    }
    return std::unexpected(MeshError::SlotNotFound);
}

std::expected<std::reference_wrapper<MeshAttributeData>, MeshError> Mesh::get_attribute_mut(
    const MeshAttribute& attribute) {
    auto it = _attributes.find(attribute.slot);
    if (it == _attributes.end()) {
        return std::unexpected(MeshError::SlotNotFound);
    } else if (it->second.attribute.name != attribute.name) {
        return std::unexpected(MeshError::NameMismatch);
    } else if (it->second.attribute.format != attribute.format) {
        return std::unexpected(MeshError::TypeMismatch);
    }
    return std::ref(it->second);
}

std::expected<std::reference_wrapper<MeshAttributeData>, MeshError> Mesh::get_attribute_mut(std::size_t slot) {
    if (auto it = _attributes.find(slot); it != _attributes.end()) {
        return std::ref(it->second);
    }
    return std::unexpected(MeshError::SlotNotFound);
}

std::expected<MeshAttributeData, MeshError> Mesh::remove_attribute(const MeshAttribute& attribute) {
    auto it = _attributes.find(attribute.slot);
    if (it == _attributes.end()) {
        return std::unexpected(MeshError::SlotNotFound);
    } else if (it->second.attribute.name != attribute.name) {
        return std::unexpected(MeshError::NameMismatch);
    } else if (it->second.attribute.format != attribute.format) {
        return std::unexpected(MeshError::TypeMismatch);
    }
    MeshAttributeData data = std::move(it->second);
    _attributes.erase(it);
    return data;
}
std::expected<MeshAttributeData, MeshError> Mesh::remove_attribute(std::size_t slot) {
    if (auto it = _attributes.find(slot); it != _attributes.end()) {
        MeshAttributeData data = std::move(it->second);
        _attributes.erase(it);
        return data;
    }
    return std::unexpected(MeshError::SlotNotFound);
}

void MeshPlugin::attach(app::App& app) { assets::app_register_asset<Mesh>(app); }
