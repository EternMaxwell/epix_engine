#include "epix/assets.hpp"
#include "epix/mesh/mesh.hpp"

using namespace epix;
using namespace epix::mesh;

MeshAttributeLayout Mesh::attribute_layout() const {
    MeshAttributeLayout layout;
    for (const auto& [slot, attribute_data] : _attributes) {
        layout.insert_or_assign(slot, attribute_data.attribute);
    }
    return layout;
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

std::expected<std::reference_wrapper<const MeshAttributeData>, MeshError> Mesh::get_attribute(size_t slot) const {
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

std::expected<std::reference_wrapper<MeshAttributeData>, MeshError> Mesh::get_attribute_mut(size_t slot) {
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
std::expected<MeshAttributeData, MeshError> Mesh::remove_attribute(size_t slot) {
    if (auto it = _attributes.find(slot); it != _attributes.end()) {
        MeshAttributeData data = std::move(it->second);
        _attributes.erase(it);
        return data;
    }
    return std::unexpected(MeshError::SlotNotFound);
}

void MeshPlugin::build(epix::App& app) { app.plugin_mut<assets::AssetPlugin>().register_asset<Mesh>(); }