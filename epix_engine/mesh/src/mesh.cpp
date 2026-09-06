

#include <epix/assets.hpp>
#include <epix/mesh.hpp>

using namespace epix::mesh;
namespace ecs = epix::ecs;

Mesh::Mesh(const Mesh& other) : asset_usage(other.asset_usage), primitive_type(other.primitive_type) {
    const auto source_attributes = other._attributes.as_ref_option();
    if (!source_attributes) {
        _attributes = detail::MeshExtractableData<AttributeMap>::extracted_to_render_world();
    } else if (*source_attributes) {
        AttributeMap cloned_attributes;
        for (const auto& [slot, attribute_data] : source_attributes->value().get()) {
            cloned_attributes.emplace(slot, MeshAttributeData{
                                                .attribute = attribute_data.attribute,
                                                .data      = attribute_data.data.clone(),
                                            });
        }
        _attributes = detail::MeshExtractableData<AttributeMap>::data(std::move(cloned_attributes));
    }

    const auto source_indices = other._indices.as_ref_option();
    if (!source_indices) {
        _indices = detail::MeshExtractableData<MeshIndices>::extracted_to_render_world();
    } else if (*source_indices) {
        _indices =
            detail::MeshExtractableData<MeshIndices>::data(MeshIndices(source_indices->value().get().data.clone()));
    }
}

Mesh& Mesh::operator=(const Mesh& other) {
    if (this == std::addressof(other)) return *this;
    Mesh copy(other);
    *this = std::move(copy);
    return *this;
}

MeshAttributeLayout Mesh::attribute_layout() const {
    MeshAttributeLayout layout;
    layout.primitive_type        = primitive_type;
    const auto stored_attributes = _attributes.as_ref();
    if (!stored_attributes) throw_access_error(stored_attributes.error());
    for (const auto& [slot, attribute_data] : stored_attributes->get()) {
        layout.insert_or_assign(slot, attribute_data.attribute);
    }
    return layout;
}

MeshVertexBufferLayoutRef Mesh::get_mesh_vertex_buffer_layout(
    MeshVertexBufferLayouts& mesh_vertex_buffer_layouts) const {
    // Bevy Mesh::get_mesh_vertex_buffer_layout: the layout's attributes are
    // built in attribute-id (Bevy) / slot (Epix) order with accumulated byte
    // offsets and a stride equal to the total size.
    std::vector<MeshVertexAttributeId> attribute_ids;
    std::vector<VertexAttributeDescriptor> attributes;
    std::uint64_t accumulated_offset = 0;
    const auto stored_attributes     = _attributes.as_ref();
    if (!stored_attributes) throw_access_error(stored_attributes.error());
    for (const auto& [slot, attribute_data] : stored_attributes->get()) {
        attribute_ids.push_back(MeshVertexAttributeId{static_cast<std::uint64_t>(slot)});
        attributes.push_back(VertexAttributeDescriptor{
            .offset          = accumulated_offset,
            .format          = attribute_data.attribute.format,
            .shader_location = static_cast<std::uint32_t>(slot),
        });
        accumulated_offset += vertex_format_size(attribute_data.attribute.format);
    }
    return mesh_vertex_buffer_layouts.insert(MeshVertexBufferLayout{
        .attribute_ids = std::move(attribute_ids),
        .layout        = VertexBufferLayout{.array_stride = accumulated_offset,
                                            .step_mode    = wgpu::VertexStepMode::eVertex,
                                            .attributes   = std::move(attributes)},
    });
}

std::optional<epix::camera::Aabb> Mesh::compute_aabb() const {
    const auto position = try_attribute_option(ATTRIBUTE_POSITION);
    if (!position) throw_access_error(position.error());
    if (!*position || position->value().get().type_info() != epix::meta::type_info::of<glm::vec3>()) {
        return std::nullopt;
    }
    const auto positions = position->value().get().cspan_as<glm::vec3>();
    if (positions.empty()) return std::nullopt;
    glm::vec3 minimum = positions.front();
    glm::vec3 maximum = positions.front();
    for (const glm::vec3& position : positions) {
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
    }
    return epix::camera::Aabb::from_min_max(minimum, maximum);
}

std::optional<std::reference_wrapper<const ecs::untyped_vector>> Mesh::attribute(
    const MeshAttribute& descriptor) const {
    return attribute(descriptor.slot);
}

std::optional<std::reference_wrapper<const ecs::untyped_vector>> Mesh::attribute(std::size_t slot) const {
    auto result = try_attribute_option(slot);
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<std::reference_wrapper<const ecs::untyped_vector>, MeshAccessError> Mesh::try_attribute(
    const MeshAttribute& descriptor) const {
    return try_attribute(descriptor.slot);
}

std::expected<std::reference_wrapper<const ecs::untyped_vector>, MeshAccessError> Mesh::try_attribute(
    std::size_t slot) const {
    auto result = try_attribute_option(slot);
    if (!result) return std::unexpected(result.error());
    if (!*result) return std::unexpected(MeshAccessError::NotFound);
    return result->value();
}

std::expected<std::optional<std::reference_wrapper<const ecs::untyped_vector>>, MeshAccessError>
Mesh::try_attribute_option(const MeshAttribute& descriptor) const {
    return try_attribute_option(descriptor.slot);
}

std::expected<std::optional<std::reference_wrapper<const ecs::untyped_vector>>, MeshAccessError>
Mesh::try_attribute_option(std::size_t slot) const {
    const auto stored_attributes = _attributes.as_ref();
    if (!stored_attributes) return std::unexpected(stored_attributes.error());
    const auto it = stored_attributes->get().find(slot);
    if (it == stored_attributes->get().end()) return std::nullopt;
    return std::optional{std::cref(it->second.data)};
}

std::optional<std::reference_wrapper<ecs::untyped_vector>> Mesh::attribute_mut(const MeshAttribute& descriptor) {
    return attribute_mut(descriptor.slot);
}

std::optional<std::reference_wrapper<ecs::untyped_vector>> Mesh::attribute_mut(std::size_t slot) {
    auto result = try_attribute_mut_option(slot);
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<std::reference_wrapper<ecs::untyped_vector>, MeshAccessError> Mesh::try_attribute_mut(
    const MeshAttribute& descriptor) {
    return try_attribute_mut(descriptor.slot);
}

std::expected<std::reference_wrapper<ecs::untyped_vector>, MeshAccessError> Mesh::try_attribute_mut(std::size_t slot) {
    auto result = try_attribute_mut_option(slot);
    if (!result) return std::unexpected(result.error());
    if (!*result) return std::unexpected(MeshAccessError::NotFound);
    return result->value();
}

std::expected<std::optional<std::reference_wrapper<ecs::untyped_vector>>, MeshAccessError>
Mesh::try_attribute_mut_option(const MeshAttribute& descriptor) {
    return try_attribute_mut_option(descriptor.slot);
}

std::expected<std::optional<std::reference_wrapper<ecs::untyped_vector>>, MeshAccessError>
Mesh::try_attribute_mut_option(std::size_t slot) {
    auto stored_attributes = _attributes.as_mut();
    if (!stored_attributes) return std::unexpected(stored_attributes.error());
    auto it = stored_attributes->get().find(slot);
    if (it == stored_attributes->get().end()) return std::nullopt;
    return std::optional{std::ref(it->second.data)};
}

std::optional<ecs::untyped_vector> Mesh::remove_attribute(const MeshAttribute& descriptor) {
    return remove_attribute(descriptor.slot);
}

std::optional<ecs::untyped_vector> Mesh::remove_attribute(std::size_t slot) {
    auto stored_attributes = _attributes.as_mut();
    if (!stored_attributes) throw_access_error(stored_attributes.error());
    auto it = stored_attributes->get().find(slot);
    if (it == stored_attributes->get().end()) return std::nullopt;
    ecs::untyped_vector data = std::move(it->second.data);
    stored_attributes->get().erase(it);
    return data;
}

std::expected<ecs::untyped_vector, MeshAccessError> Mesh::try_remove_attribute(const MeshAttribute& descriptor) {
    return try_remove_attribute(descriptor.slot);
}

std::expected<ecs::untyped_vector, MeshAccessError> Mesh::try_remove_attribute(std::size_t slot) {
    auto stored_attributes = _attributes.as_mut();
    if (!stored_attributes) return std::unexpected(stored_attributes.error());
    auto it = stored_attributes->get().find(slot);
    if (it == stored_attributes->get().end()) return std::unexpected(MeshAccessError::NotFound);
    ecs::untyped_vector data = std::move(it->second.data);
    stored_attributes->get().erase(it);
    return data;
}

std::expected<Mesh, MeshAccessError> Mesh::try_with_removed_attribute(const MeshAttribute& descriptor) && {
    auto result = try_remove_attribute(descriptor);
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::try_with_removed_attribute(std::size_t slot) && {
    auto result = try_remove_attribute(slot);
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

bool Mesh::contains_attribute(const MeshAttribute& descriptor) const { return contains_attribute(descriptor.slot); }

bool Mesh::contains_attribute(std::size_t slot) const {
    auto result = try_contains_attribute(slot);
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<bool, MeshAccessError> Mesh::try_contains_attribute(const MeshAttribute& descriptor) const {
    return try_contains_attribute(descriptor.slot);
}

std::expected<bool, MeshAccessError> Mesh::try_contains_attribute(std::size_t slot) const {
    const auto stored_attributes = _attributes.as_ref();
    if (!stored_attributes) return std::unexpected(stored_attributes.error());
    return stored_attributes->get().contains(slot);
}

std::optional<std::reference_wrapper<const MeshIndices>> Mesh::indices() const {
    auto result = try_indices_option();
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<std::reference_wrapper<const MeshIndices>, MeshAccessError> Mesh::try_indices() const {
    return _indices.as_ref();
}

std::expected<std::optional<std::reference_wrapper<const MeshIndices>>, MeshAccessError> Mesh::try_indices_option()
    const {
    return _indices.as_ref_option();
}

std::optional<std::reference_wrapper<MeshIndices>> Mesh::indices_mut() {
    auto result = try_indices_mut_option();
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<std::reference_wrapper<MeshIndices>, MeshAccessError> Mesh::try_indices_mut() {
    return _indices.as_mut();
}

std::expected<std::optional<std::reference_wrapper<MeshIndices>>, MeshAccessError> Mesh::try_indices_mut_option() {
    return _indices.as_mut_option();
}

std::optional<MeshIndices> Mesh::remove_indices() {
    auto result = try_remove_indices();
    if (!result) throw_access_error(result.error());
    return std::move(result).value();
}

std::expected<std::optional<MeshIndices>, MeshAccessError> Mesh::try_remove_indices() {
    return _indices.replace(std::nullopt);
}

std::expected<Mesh, MeshAccessError> Mesh::try_with_removed_indices() && {
    auto result = try_remove_indices();
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::take_gpu_data() {
    auto attributes = _attributes.extract();
    if (!attributes) return std::unexpected(attributes.error());
    auto indices = _indices.extract();
    if (!indices) return std::unexpected(indices.error());

    Mesh extracted{primitive_type, asset_usage};
    extracted._attributes = std::move(attributes).value();
    extracted._indices    = std::move(indices).value();
    return extracted;
}

[[noreturn]] void Mesh::throw_access_error(MeshAccessError error) {
    if (error == MeshAccessError::ExtractedToRenderWorld) {
        throw std::logic_error(
            "Mesh has been extracted to RenderWorld. To access vertex attributes, asset_usage must include MAIN_WORLD");
    }
    throw std::out_of_range("The requested mesh data was not found");
}

void MeshPlugin::attach(app::App& app) { assets::app_register_asset<Mesh>(app); }
