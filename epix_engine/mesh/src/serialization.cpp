#include <array>
#include <epix/mesh/serialization.hpp>

namespace epix::mesh {
namespace {

VertexAttributeValues deserialize_attribute_values(VertexAttributeValues::Variant values) {
    return std::visit([]<typename Alternative>(Alternative values) { return VertexAttributeValues{std::move(values)}; },
                      std::move(values));
}

Indices deserialize_indices(detail::SerializedIndices indices) {
    return std::visit([]<typename Values>(Values values) { return Indices{std::move(values)}; }, std::move(indices));
}

detail::SerializedIndices serialize_indices(Indices indices) {
    if (auto* values = indices.as_u16()) return std::move(*values);
    return std::move(*indices.as_u32());
}

}  // namespace

SerializedMesh SerializedMesh::from_mesh(Mesh mesh) {
    SerializedMesh result;
    result.primitive_topology_ = mesh.primitive_type;

    auto attributes = mesh._attributes.replace(std::nullopt);
    if (!attributes) Mesh::throw_access_error(attributes.error());
    result.attributes_.reserve(attributes->value().size());
    for (auto& [id, data] : **attributes) {
        result.attributes_.emplace_back(id, detail::SerializedMeshAttributeData{
                                                .attribute = {.name   = std::string{data.attribute.name},
                                                              .id     = data.attribute.id,
                                                              .format = data.attribute.format},
                                                .values    = std::move(data.values.variant()),
                                            });
    }

    auto indices = mesh._indices.replace(std::nullopt);
    if (!indices) Mesh::throw_access_error(indices.error());
    if (*indices) result.indices_ = serialize_indices(std::move(**indices));
    return result;
}

Mesh SerializedMesh::into_mesh() && { return MeshDeserializer{}.deserialize(std::move(*this)); }

MeshDeserializer::MeshDeserializer() {
    constexpr std::array builtins{
        &Mesh::ATTRIBUTE_POSITION, &Mesh::ATTRIBUTE_NORMAL, &Mesh::ATTRIBUTE_UV_0,         &Mesh::ATTRIBUTE_UV_1,
        &Mesh::ATTRIBUTE_TANGENT,  &Mesh::ATTRIBUTE_COLOR,  &Mesh::ATTRIBUTE_JOINT_WEIGHT, &Mesh::ATTRIBUTE_JOINT_INDEX,
    };
    static_assert(builtins.size() == Mesh::FIRST_AVAILABLE_CUSTOM_ATTRIBUTE);
    for (const auto* attribute : builtins) {
        custom_vertex_attributes_.emplace(attribute->name, *attribute);
    }
}

MeshDeserializer& MeshDeserializer::add_custom_vertex_attribute(std::string_view name, MeshVertexAttribute attribute) {
    custom_vertex_attributes_.insert_or_assign(std::string{name}, attribute);
    return *this;
}

Mesh MeshDeserializer::deserialize(SerializedMesh serialized_mesh) const {
    constexpr auto default_usage = static_cast<assets::RenderAssetUsages>(assets::RenderAssetUsages::MAIN_WORLD |
                                                                          assets::RenderAssetUsages::RENDER_WORLD);
    Mesh result{serialized_mesh.primitive_topology_, default_usage};
    Mesh::AttributeMap attributes;
    for (auto& [id, serialized_data] : serialized_mesh.attributes_) {
        const auto found = custom_vertex_attributes_.find(serialized_data.attribute.name);
        if (found == custom_vertex_attributes_.end() || found->second.id != serialized_data.attribute.id) {
            spdlog::warn(
                "Deserialized mesh contains custom vertex attribute {{ name: {}, id: {}, format: {} }} that was not "
                "specified with MeshDeserializer::add_custom_vertex_attribute. Ignoring.",
                serialized_data.attribute.name, serialized_data.attribute.id.value,
                wgpu::to_string(serialized_data.attribute.format));
            continue;
        }

        attributes.insert_or_assign(
            id, detail::MeshAttributeData{.attribute = found->second,
                                          .values = deserialize_attribute_values(std::move(serialized_data.values))});
    }
    result._attributes = detail::MeshExtractableData<Mesh::AttributeMap>::data(std::move(attributes));
    if (serialized_mesh.indices_) {
        result._indices =
            detail::MeshExtractableData<Indices>::data(deserialize_indices(std::move(*serialized_mesh.indices_)));
    }
    return result;
}

std::expected<std::vector<std::byte>, std::errc> serialize_serialized_mesh(const SerializedMesh& mesh) {
    std::vector<std::byte> bytes;
    zpp::bits::out out{bytes};
    const auto result = out(mesh);
    if (zpp::bits::failure(result)) return std::unexpected(result);
    return bytes;
}

std::expected<SerializedMesh, std::errc> deserialize_serialized_mesh(std::span<const std::byte> bytes) {
    SerializedMesh mesh;
    zpp::bits::in in{bytes};
    const auto result = in(mesh);
    if (zpp::bits::failure(result)) return std::unexpected(result);
    return mesh;
}

}  // namespace epix::mesh
