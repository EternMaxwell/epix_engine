

#include <epix/assets.hpp>
#include <epix/mesh.hpp>
#include <unordered_set>

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
        _indices = detail::MeshExtractableData<Indices>::extracted_to_render_world();
    } else if (*source_indices) {
        _indices = detail::MeshExtractableData<Indices>::data(source_indices->value().get());
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

std::optional<std::reference_wrapper<const Indices>> Mesh::indices() const {
    auto result = try_indices_option();
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<std::reference_wrapper<const Indices>, MeshAccessError> Mesh::try_indices() const {
    return _indices.as_ref();
}

std::expected<std::optional<std::reference_wrapper<const Indices>>, MeshAccessError> Mesh::try_indices_option()
    const {
    return _indices.as_ref_option();
}

std::optional<std::reference_wrapper<Indices>> Mesh::indices_mut() {
    auto result = try_indices_mut_option();
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<std::reference_wrapper<Indices>, MeshAccessError> Mesh::try_indices_mut() {
    return _indices.as_mut();
}

std::expected<std::optional<std::reference_wrapper<Indices>>, MeshAccessError> Mesh::try_indices_mut_option() {
    return _indices.as_mut_option();
}

std::optional<Indices> Mesh::remove_indices() {
    auto result = try_remove_indices();
    if (!result) throw_access_error(result.error());
    return std::move(result).value();
}

std::expected<std::optional<Indices>, MeshAccessError> Mesh::try_remove_indices() {
    return _indices.replace(std::nullopt);
}

std::expected<Mesh, MeshAccessError> Mesh::try_with_removed_indices() && {
    auto result = try_remove_indices();
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

void Mesh::duplicate_vertices() {
    auto result = try_duplicate_vertices();
    if (!result) throw_access_error(result.error());
}

std::expected<void, MeshAccessError> Mesh::try_duplicate_vertices() {
    auto removed_indices = _indices.replace(std::nullopt);
    if (!removed_indices) return std::unexpected(removed_indices.error());
    if (!*removed_indices) return {};

    auto attributes = _attributes.as_mut();
    if (!attributes) return std::unexpected(attributes.error());

    for (auto& attribute : std::views::values(attributes->get())) {
        ecs::untyped_vector duplicated(attribute.data.type_info(), removed_indices->value().len());
        for (const auto index : removed_indices->value().iter()) {
            if (index >= attribute.data.size()) {
                throw std::out_of_range("Mesh index references a vertex that does not exist");
            }
            duplicated.push_back_from(attribute.data.cget(index));
        }
        attribute.data = std::move(duplicated);
    }
    return {};
}

Mesh Mesh::with_duplicated_vertices() && {
    duplicate_vertices();
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::try_with_duplicated_vertices() && {
    auto result = try_duplicate_vertices();
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

namespace {
template <typename Index>
std::expected<void, MeshWindingInvertError> invert_indices(std::vector<Index>& indices,
                                                           wgpu::PrimitiveTopology topology) {
    switch (topology) {
        case wgpu::PrimitiveTopology::eTriangleList:
            if (indices.size() % 3 != 0) {
                return std::unexpected(
                    MeshWindingInvertError{mesh_winding_invert_error::AbruptIndicesEnd{}});
            }
            for (std::size_t index = 0; index < indices.size(); index += 3) {
                std::swap(indices[index + 1], indices[index + 2]);
            }
            return {};
        case wgpu::PrimitiveTopology::eLineList:
            if (indices.size() % 2 != 0) {
                return std::unexpected(
                    MeshWindingInvertError{mesh_winding_invert_error::AbruptIndicesEnd{}});
            }
            std::ranges::reverse(indices);
            return {};
        case wgpu::PrimitiveTopology::eTriangleStrip:
        case wgpu::PrimitiveTopology::eLineStrip:
            std::ranges::reverse(indices);
            return {};
        default:
            return std::unexpected(MeshWindingInvertError{mesh_winding_invert_error::WrongTopology{}});
    }
}
}  // namespace

std::expected<void, MeshWindingInvertError> Mesh::invert_winding() {
    auto stored_indices = try_indices_mut_option();
    if (!stored_indices) {
        return std::unexpected(MeshWindingInvertError{stored_indices.error()});
    }
    if (!*stored_indices) return {};

    auto& indices = stored_indices->value().get();
    if (auto* values = indices.as_u16()) return invert_indices(*values, primitive_type);
    return invert_indices(*indices.as_u32(), primitive_type);
}

std::expected<Mesh, MeshWindingInvertError> Mesh::with_inverted_winding() && {
    auto result = invert_winding();
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

std::optional<std::span<const std::uint8_t>> Mesh::get_index_buffer_bytes() const {
    const auto stored_indices = indices();
    if (!stored_indices) return std::nullopt;
    if (const auto* values = stored_indices->get().as_u16()) {
        return std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(values->data()),
                                             values->size() * sizeof(std::uint16_t)};
    }
    const auto* values = stored_indices->get().as_u32();
    return std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(values->data()),
                                         values->size() * sizeof(std::uint32_t)};
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

namespace epix::mesh {

void mark_3d_meshes_as_changed_if_their_assets_changed(ecs::Query<ecs::Item<ecs::Mut<Mesh3d>>> meshes_3d,
                                                       ecs::EventReader<assets::AssetEvent<Mesh>> mesh_asset_events) {
    std::unordered_set<assets::AssetId<Mesh>> changed_meshes;
    for (const auto& event : mesh_asset_events.read()) {
        if (event.type == assets::AssetEvent<Mesh>::Type::Modified) changed_meshes.insert(event.id);
    }
    if (changed_meshes.empty()) return;

    for (auto&& [mesh_3d] : meshes_3d.iter()) {
        if (changed_meshes.contains(mesh_3d.get().handle.id())) {
            (void)mesh_3d.get_mut();
        }
    }
}

void MeshPlugin::attach(app::App& app) {
    assets::app_register_asset<Mesh>(app);
    app.add_systems(app::PostUpdate, ecs::into(mark_3d_meshes_as_changed_if_their_assets_changed)
                                         .after(assets::AssetSystems::WriteEvents)
                                         .set_name("mark 3d meshes changed after mesh asset changes"));
}

}  // namespace epix::mesh
