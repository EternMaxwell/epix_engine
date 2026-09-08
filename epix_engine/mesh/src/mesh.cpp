

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
        for (const auto& [id, attribute_data] : source_attributes->value().get()) {
            cloned_attributes.emplace(id, MeshAttributeData{
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
    for (const auto& [id, attribute_data] : stored_attributes->get()) {
        layout.insert_or_assign(id, attribute_data.attribute);
    }
    return layout;
}

MeshVertexBufferLayoutRef Mesh::get_mesh_vertex_buffer_layout(
    MeshVertexBufferLayouts& mesh_vertex_buffer_layouts) const {
    // Bevy Mesh::get_mesh_vertex_buffer_layout: attributes are packed in ID
    // order, while raw shader locations are their iteration indices. Each
    // specialized pipeline subsequently remaps the requested IDs.
    std::vector<MeshVertexAttributeId> attribute_ids;
    std::vector<VertexAttribute> attributes;
    std::uint64_t accumulated_offset = 0;
    std::uint32_t shader_location    = 0;
    const auto stored_attributes     = _attributes.as_ref();
    if (!stored_attributes) throw_access_error(stored_attributes.error());
    for (const auto& [id, attribute_data] : stored_attributes->get()) {
        attribute_ids.push_back(id);
        attributes.push_back(VertexAttribute{
            .offset          = accumulated_offset,
            .format          = attribute_data.attribute.format,
            .shader_location = shader_location++,
        });
        accumulated_offset += vertex_format_size(attribute_data.attribute.format);
    }
    return mesh_vertex_buffer_layouts.insert(MeshVertexBufferLayout{
        std::move(attribute_ids),
        VertexBufferLayout{
            .array_stride = accumulated_offset,
            .step_mode    = wgpu::VertexStepMode::eVertex,
            .attributes   = std::move(attributes),
        },
    });
}

std::optional<std::reference_wrapper<const ecs::untyped_vector>> Mesh::attribute(
    const MeshVertexAttribute& descriptor) const {
    return attribute(descriptor.id);
}

std::optional<std::reference_wrapper<const ecs::untyped_vector>> Mesh::attribute(MeshVertexAttributeId id) const {
    auto result = try_attribute_option(id);
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<std::reference_wrapper<const ecs::untyped_vector>, MeshAccessError> Mesh::try_attribute(
    const MeshVertexAttribute& descriptor) const {
    return try_attribute(descriptor.id);
}

std::expected<std::reference_wrapper<const ecs::untyped_vector>, MeshAccessError> Mesh::try_attribute(
    MeshVertexAttributeId id) const {
    auto result = try_attribute_option(id);
    if (!result) return std::unexpected(result.error());
    if (!*result) return std::unexpected(MeshAccessError::NotFound);
    return result->value();
}

std::expected<std::optional<std::reference_wrapper<const ecs::untyped_vector>>, MeshAccessError>
Mesh::try_attribute_option(const MeshVertexAttribute& descriptor) const {
    return try_attribute_option(descriptor.id);
}

std::expected<std::optional<std::reference_wrapper<const ecs::untyped_vector>>, MeshAccessError>
Mesh::try_attribute_option(MeshVertexAttributeId id) const {
    const auto stored_attributes = _attributes.as_ref();
    if (!stored_attributes) return std::unexpected(stored_attributes.error());
    const auto it = stored_attributes->get().find(id);
    if (it == stored_attributes->get().end()) return std::nullopt;
    return std::optional{std::cref(it->second.data)};
}

std::optional<std::reference_wrapper<ecs::untyped_vector>> Mesh::attribute_mut(const MeshVertexAttribute& descriptor) {
    return attribute_mut(descriptor.id);
}

std::optional<std::reference_wrapper<ecs::untyped_vector>> Mesh::attribute_mut(MeshVertexAttributeId id) {
    auto result = try_attribute_mut_option(id);
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<std::reference_wrapper<ecs::untyped_vector>, MeshAccessError> Mesh::try_attribute_mut(
    const MeshVertexAttribute& descriptor) {
    return try_attribute_mut(descriptor.id);
}

std::expected<std::reference_wrapper<ecs::untyped_vector>, MeshAccessError> Mesh::try_attribute_mut(
    MeshVertexAttributeId id) {
    auto result = try_attribute_mut_option(id);
    if (!result) return std::unexpected(result.error());
    if (!*result) return std::unexpected(MeshAccessError::NotFound);
    return result->value();
}

std::expected<std::optional<std::reference_wrapper<ecs::untyped_vector>>, MeshAccessError>
Mesh::try_attribute_mut_option(const MeshVertexAttribute& descriptor) {
    return try_attribute_mut_option(descriptor.id);
}

std::expected<std::optional<std::reference_wrapper<ecs::untyped_vector>>, MeshAccessError>
Mesh::try_attribute_mut_option(MeshVertexAttributeId id) {
    auto stored_attributes = _attributes.as_mut();
    if (!stored_attributes) return std::unexpected(stored_attributes.error());
    auto it = stored_attributes->get().find(id);
    if (it == stored_attributes->get().end()) return std::nullopt;
    return std::optional{std::ref(it->second.data)};
}

std::optional<ecs::untyped_vector> Mesh::remove_attribute(const MeshVertexAttribute& descriptor) {
    return remove_attribute(descriptor.id);
}

std::optional<ecs::untyped_vector> Mesh::remove_attribute(MeshVertexAttributeId id) {
    auto stored_attributes = _attributes.as_mut();
    if (!stored_attributes) throw_access_error(stored_attributes.error());
    auto it = stored_attributes->get().find(id);
    if (it == stored_attributes->get().end()) return std::nullopt;
    ecs::untyped_vector data = std::move(it->second.data);
    stored_attributes->get().erase(it);
    return data;
}

std::expected<ecs::untyped_vector, MeshAccessError> Mesh::try_remove_attribute(const MeshVertexAttribute& descriptor) {
    return try_remove_attribute(descriptor.id);
}

std::expected<ecs::untyped_vector, MeshAccessError> Mesh::try_remove_attribute(MeshVertexAttributeId id) {
    auto stored_attributes = _attributes.as_mut();
    if (!stored_attributes) return std::unexpected(stored_attributes.error());
    auto it = stored_attributes->get().find(id);
    if (it == stored_attributes->get().end()) return std::unexpected(MeshAccessError::NotFound);
    ecs::untyped_vector data = std::move(it->second.data);
    stored_attributes->get().erase(it);
    return data;
}

std::expected<Mesh, MeshAccessError> Mesh::try_with_removed_attribute(const MeshVertexAttribute& descriptor) && {
    auto result = try_remove_attribute(descriptor);
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::try_with_removed_attribute(MeshVertexAttributeId id) && {
    auto result = try_remove_attribute(id);
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

bool Mesh::contains_attribute(const MeshVertexAttribute& descriptor) const { return contains_attribute(descriptor.id); }

bool Mesh::contains_attribute(MeshVertexAttributeId id) const {
    auto result = try_contains_attribute(id);
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<bool, MeshAccessError> Mesh::try_contains_attribute(const MeshVertexAttribute& descriptor) const {
    return try_contains_attribute(descriptor.id);
}

std::expected<bool, MeshAccessError> Mesh::try_contains_attribute(MeshVertexAttributeId id) const {
    const auto stored_attributes = _attributes.as_ref();
    if (!stored_attributes) return std::unexpected(stored_attributes.error());
    return stored_attributes->get().contains(id);
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
