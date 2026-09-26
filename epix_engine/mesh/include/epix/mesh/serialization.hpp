#pragma once


#ifndef EPIX_CXX_MODULE
#include <zpp_bits.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <epix/common.hpp>
#endif
#include <epix/mesh/mesh.hpp>

namespace epix::mesh {

namespace detail {

/** @brief Serializable owning counterpart of Bevy's crate-private
 * `SerializedMeshVertexAttribute`. */
struct SerializedMeshVertexAttribute {
   private:
    friend zpp::bits::access;
    using serialize = zpp::bits::members<3>;

   public:
    std::string name;
    MeshVertexAttributeId id;
    wgpu::VertexFormat format = wgpu::VertexFormat::eFloat32;
};

/** @brief Serializable counterpart of Bevy's crate-private
 * `SerializedMeshAttributeData`. */
struct SerializedMeshAttributeData {
   private:
    friend zpp::bits::access;
    using serialize = zpp::bits::members<2>;

   public:
    SerializedMeshVertexAttribute attribute;
    VertexAttributeValues::Variant values;
};

using SerializedIndices = std::variant<std::vector<std::uint16_t>, std::vector<std::uint32_t>>;

}  // namespace detail

class EPIX_API MeshDeserializer;

/** @brief A lossy, version-local representation of a `Mesh` suitable for
 * short-term transfer between processes (Bevy `SerializedMesh`).
 *
 * Only primitive topology, vertex attributes, and indices are retained. Asset
 * usage and the final AABB are deliberately reconstructed from `Mesh` defaults.
 * Custom attributes are ignored unless registered with `MeshDeserializer`.
 */
EPIX_EXPORT class EPIX_API SerializedMesh {
   public:
    /** @brief Consume a mesh and retain its serializable fields. Throws if its
     * CPU payload has already been extracted to the render world. */
    static SerializedMesh from_mesh(Mesh mesh);

    /** @brief Consume this transfer representation and reconstruct a mesh with
     * the default built-in attribute whitelist. */
    Mesh into_mesh() &&;

   private:
    friend class MeshDeserializer;
    friend zpp::bits::access;
    friend EPIX_API std::expected<std::vector<std::byte>, std::errc> serialize_serialized_mesh(const SerializedMesh& mesh);
    friend EPIX_API std::expected<SerializedMesh, std::errc> deserialize_serialized_mesh(std::span<const std::byte> bytes);
    using serialize = zpp::bits::members<3>;

    SerializedMesh() = default;

    wgpu::PrimitiveTopology primitive_topology_ = wgpu::PrimitiveTopology::eTriangleList;
    std::vector<std::pair<MeshVertexAttributeId, detail::SerializedMeshAttributeData>> attributes_;
    std::optional<detail::SerializedIndices> indices_;
};

/** @brief Options used when reconstructing a mesh from `SerializedMesh`
 * (Bevy `MeshDeserializer`). */
EPIX_EXPORT class EPIX_API MeshDeserializer {
   public:
    /** @brief Construct a deserializer containing all eight built-in mesh
     * attributes. */
    MeshDeserializer();

    /** @brief Register or replace the descriptor accepted for `name`.
     * Unregistered custom attributes and registrations whose IDs do not match
     * the serialized descriptor are ignored during deserialization. */
    MeshDeserializer& add_custom_vertex_attribute(std::string_view name, MeshVertexAttribute attribute);

    /** @brief Consume a transfer representation and reconstruct its mesh. */
    Mesh deserialize(SerializedMesh serialized_mesh) const;

   private:
    std::unordered_map<std::string, MeshVertexAttribute> custom_vertex_attributes_;
};

/** @brief Encode a `SerializedMesh` with Epix's project serialization backend. */
EPIX_EXPORT EPIX_API std::expected<std::vector<std::byte>, std::errc> serialize_serialized_mesh(const SerializedMesh& mesh);

/** @brief Decode a `SerializedMesh` encoded by `serialize_serialized_mesh`. */
EPIX_EXPORT EPIX_API std::expected<SerializedMesh, std::errc> deserialize_serialized_mesh(std::span<const std::byte> bytes);

/** @brief Vector overload for `deserialize_serialized_mesh`. */
EPIX_EXPORT inline std::expected<SerializedMesh, std::errc> deserialize_serialized_mesh(
    const std::vector<std::byte>& bytes) {
    return deserialize_serialized_mesh(std::span<const std::byte>{bytes.data(), bytes.size()});
}

}  // namespace epix::mesh
