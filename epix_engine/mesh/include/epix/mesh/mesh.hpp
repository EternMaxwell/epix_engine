#pragma once

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets/render_asset_usages.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/transform.hpp>
#include <expected>
#include <format>
#include <functional>
#include <glm/glm.hpp>
#include <limits>
#include <map>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif
#include <epix/mesh/direction.hpp>
#include <epix/mesh/vertex_attribute_values.hpp>
#include <epix/mesh/vertex_buffer_layout.hpp>

namespace epix::mesh {
struct EPIX_API Mesh;
class EPIX_API SerializedMesh;
class EPIX_API MeshDeserializer;

namespace detail {
/** @brief Rust's `assert!(condition, message)`: it panics in every build.
 *
 *  C++'s `assert` disappears under `NDEBUG`, so a release build would sail past
 *  these preconditions instead of failing the way Bevy does. */
inline void assert_precondition(bool condition, std::string_view message) {
    if (!condition) throw std::logic_error(std::string(message));
}
}  // namespace detail

/** @brief C++ counterpart of Bevy's `MeshBuilder` trait. */
template <typename T>
concept MeshBuilder = requires(const T& builder) {
    { builder.build() } -> std::same_as<Mesh>;
};

/** @brief C++ counterpart of Bevy's `Meshable` trait. */
template <typename T>
concept Meshable = requires(const T& shape) {
    { shape.mesh() } -> MeshBuilder;
};

constexpr std::size_t vertex_format_size(wgpu::VertexFormat format) noexcept {
    switch (format) {
        case wgpu::VertexFormat::eUint8:
        case wgpu::VertexFormat::eSint8:
        case wgpu::VertexFormat::eUnorm8:
        case wgpu::VertexFormat::eSnorm8:
            return 1;
        case wgpu::VertexFormat::eUint8x2:
        case wgpu::VertexFormat::eSint8x2:
        case wgpu::VertexFormat::eUnorm8x2:
        case wgpu::VertexFormat::eSnorm8x2:
        case wgpu::VertexFormat::eUint16:
        case wgpu::VertexFormat::eSint16:
        case wgpu::VertexFormat::eUnorm16:
        case wgpu::VertexFormat::eSnorm16:
        case wgpu::VertexFormat::eFloat16:
            return 2;
        case wgpu::VertexFormat::eUint8x4:
        case wgpu::VertexFormat::eSint8x4:
        case wgpu::VertexFormat::eUnorm8x4:
        case wgpu::VertexFormat::eSnorm8x4:
        case wgpu::VertexFormat::eUint16x2:
        case wgpu::VertexFormat::eSint16x2:
        case wgpu::VertexFormat::eUnorm16x2:
        case wgpu::VertexFormat::eSnorm16x2:
        case wgpu::VertexFormat::eFloat16x2:
        case wgpu::VertexFormat::eFloat32:
        case wgpu::VertexFormat::eUint32:
        case wgpu::VertexFormat::eSint32:
        case wgpu::VertexFormat::eUnorm10_10_10_2:
        case wgpu::VertexFormat::eUnorm8x4BGRA:
            return 4;
        case wgpu::VertexFormat::eUint16x4:
        case wgpu::VertexFormat::eSint16x4:
        case wgpu::VertexFormat::eUnorm16x4:
        case wgpu::VertexFormat::eSnorm16x4:
        case wgpu::VertexFormat::eFloat16x4:
        case wgpu::VertexFormat::eFloat32x2:
        case wgpu::VertexFormat::eUint32x2:
        case wgpu::VertexFormat::eSint32x2:
            return 8;
        case wgpu::VertexFormat::eFloat32x3:
        case wgpu::VertexFormat::eUint32x3:
        case wgpu::VertexFormat::eSint32x3:
            return 12;
        case wgpu::VertexFormat::eFloat32x4:
        case wgpu::VertexFormat::eUint32x4:
        case wgpu::VertexFormat::eSint32x4:
            return 16;
        default:
            return 0;
    }
}
/** @brief Error from accessing mesh vertex attributes or indices. */
EPIX_EXPORT enum class MeshAccessError {
    /** @brief The mesh payload was transferred to the render world. */
    ExtractedToRenderWorld,
    /** @brief The requested vertex or index data is absent. */
    NotFound,
};

/** @brief Return Bevy's display text for a mesh access error. */
EPIX_EXPORT constexpr std::string_view to_string(MeshAccessError error) noexcept {
    switch (error) {
        case MeshAccessError::ExtractedToRenderWorld:
            return "The mesh vertex/index data has been extracted to the RenderWorld (via `Mesh::asset_usage`)";
        case MeshAccessError::NotFound:
            return "The requested mesh data wasn't found in this mesh";
    }
    std::unreachable();
}

EPIX_EXPORT namespace mesh_winding_invert_error {
    /** @brief Winding inversion is unsupported for the mesh topology. */
    struct WrongTopology {
        bool operator==(const WrongTopology&) const = default;
    };
    /** @brief The index count is not a complete set of topology primitives. */
    struct AbruptIndicesEnd {
        bool operator==(const AbruptIndicesEnd&) const = default;
    };
}  // namespace mesh_winding_invert_error

/** @brief Error returned while inverting a mesh's index winding. */
EPIX_EXPORT struct MeshWindingInvertError : std::variant<mesh_winding_invert_error::WrongTopology,
                                                         mesh_winding_invert_error::AbruptIndicesEnd,
                                                         MeshAccessError> {
    using Base = std::
        variant<mesh_winding_invert_error::WrongTopology, mesh_winding_invert_error::AbruptIndicesEnd, MeshAccessError>;
    using Base::Base;

    std::string to_string() const {
        return std::visit(
            [](const auto& error) -> std::string {
                using Error = std::remove_cvref_t<decltype(error)>;
                if constexpr (std::same_as<Error, mesh_winding_invert_error::WrongTopology>) {
                    return "Mesh winding inversion does not work for primitive topology `PointList`";
                } else if constexpr (std::same_as<Error, mesh_winding_invert_error::AbruptIndicesEnd>) {
                    return "Indices weren't in chunks according to topology";
                } else {
                    return "Mesh access error: " + std::string(epix::mesh::to_string(error));
                }
            },
            static_cast<const Base&>(*this));
    }
};

EPIX_EXPORT namespace mesh_triangles_error {
    /** @brief The mesh is neither a triangle list nor a triangle strip. */
    struct WrongTopology {
        bool operator==(const WrongTopology&) const = default;
    };
    /** @brief Position data is not stored as Float32x3 values. */
    struct PositionsFormat {
        bool operator==(const PositionsFormat&) const = default;
    };
    /** @brief An index references a vertex that does not exist. */
    struct BadIndices {
        bool operator==(const BadIndices&) const = default;
    };
}  // namespace mesh_triangles_error

/** @brief Three vertices forming a 3D triangle (Bevy `Triangle3d`).
 *
 * Epix uses GLM directly as its math layer, so the primitive required by
 * `Mesh::triangles` is exposed by the mesh module.
 */
struct EPIX_API Triangle3dMeshBuilder;

/** @brief Three vertices forming a 3D triangle (Bevy `Triangle3d`). */
EPIX_EXPORT struct EPIX_API Triangle3d {
    /** @brief Bevy's default triangle `[+Y * 0.5, (-0.5, -0.5, 0.0), (0.5, -0.5, 0.0)]`. */
    std::array<glm::vec3, 3> vertices{glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(-0.5f, -0.5f, 0.0f),
                                      glm::vec3(0.5f, -0.5f, 0.0f)};

    constexpr Triangle3d() = default;
    constexpr Triangle3d(glm::vec3 a, glm::vec3 b, glm::vec3 c) : vertices{a, b, c} {}
    /** @brief Face normal, or the reason there is none (Bevy `Triangle3d::normal`).
     *
     *  A degenerate triangle - zero, infinite or `NaN` cross product - is reported
     *  as the error Bevy's `Dir3::new` produces for it, not as a zero vector. */
    std::expected<Dir3, InvalidDirectionError> normal() const noexcept;
    /** @brief Degenerate when the `ab x ac` cross product is shorter than `10e-7`. */
    bool is_degenerate() const noexcept;
    bool is_acute() const noexcept;
    bool is_obtuse() const noexcept;
    glm::vec3 centroid() const noexcept;
    std::pair<glm::vec3, glm::vec3> largest_side() const noexcept;
    glm::vec3 circumcenter() const noexcept;
    /** @brief Reverse the winding by swapping the first and last vertices. */
    void reverse() noexcept { std::swap(vertices[0], vertices[2]); }
    Triangle3d reversed() const noexcept {
        Triangle3d value = *this;
        value.reverse();
        return value;
    }
    /** @brief Build a triangle mesh from this shape (defined in `primitives.hpp`). */
    Triangle3dMeshBuilder mesh() const noexcept;

    bool operator==(const Triangle3d&) const = default;
};

/** @brief Error returned while iterating over a mesh's triangles. */
EPIX_EXPORT struct MeshTrianglesError : std::variant<mesh_triangles_error::WrongTopology,
                                                     mesh_triangles_error::PositionsFormat,
                                                     mesh_triangles_error::BadIndices,
                                                     MeshAccessError> {
    using Base = std::variant<mesh_triangles_error::WrongTopology,
                              mesh_triangles_error::PositionsFormat,
                              mesh_triangles_error::BadIndices,
                              MeshAccessError>;
    using Base::Base;

    std::string to_string() const {
        return std::visit(
            [](const auto& error) -> std::string {
                using Error = std::remove_cvref_t<decltype(error)>;
                if constexpr (std::same_as<Error, mesh_triangles_error::WrongTopology>) {
                    return "Source mesh does not have primitive topology TriangleList or TriangleStrip";
                } else if constexpr (std::same_as<Error, mesh_triangles_error::PositionsFormat>) {
                    return "Source mesh position data is not Float32x3";
                } else if constexpr (std::same_as<Error, mesh_triangles_error::BadIndices>) {
                    return "Face index data references vertices that do not exist";
                } else {
                    return "mesh access error: " + std::string(epix::mesh::to_string(error));
                }
            },
            static_cast<const Base&>(*this));
    }
};
/** @brief Describes one named vertex attribute and its stable mesh ID. */
EPIX_EXPORT struct MeshVertexAttribute {
    std::string_view name;
    MeshVertexAttributeId id;
    wgpu::VertexFormat format;

    operator MeshVertexAttributeId() const noexcept { return id; }

    VertexAttributeDescriptor at_shader_location(std::uint32_t shader_location) const noexcept {
        return VertexAttributeDescriptor{
            .shader_location = shader_location,
            .id              = id,
            .name            = name,
        };
    }

    bool operator==(const MeshVertexAttribute&) const noexcept = default;
};
/** @brief Ordered map of ID to attribute describing the complete vertex layout. */
EPIX_EXPORT struct MeshAttributeLayout : std::map<MeshVertexAttributeId, MeshVertexAttribute> {
    wgpu::PrimitiveTopology primitive_type = wgpu::PrimitiveTopology::eTriangleList;

    bool operator==(const MeshAttributeLayout& other) const noexcept {
        return primitive_type == other.primitive_type &&
               static_cast<const std::map<MeshVertexAttributeId, MeshVertexAttribute>&>(*this) ==
                   static_cast<const std::map<MeshVertexAttributeId, MeshVertexAttribute>&>(other);
    }

    bool contains_attribute(const MeshVertexAttribute& attribute) const noexcept {
        auto it = this->find(attribute.id);
        return it != this->end() && it->second == attribute;
    }
    /** @brief Add or replace an attribute in the layout. */
    void add_attribute(const MeshVertexAttribute& attribute) { this->insert_or_assign(attribute.id, attribute); }
    /** @brief Get a const reference to an attribute by descriptor. */
    std::optional<std::reference_wrapper<const MeshVertexAttribute>> get_attribute(
        const MeshVertexAttribute& attribute) const {
        auto it = this->find(attribute.id);
        if (it != this->end() && it->second == attribute) {
            return std::cref(it->second);
        }
        return std::nullopt;
    }
    /** @brief Get a const reference to an attribute by ID. */
    std::optional<std::reference_wrapper<const MeshVertexAttribute>> get_attribute(MeshVertexAttributeId id) const {
        auto it = this->find(id);
        if (it != this->end()) {
            return std::cref(it->second);
        }
        return std::nullopt;
    }
    /** @brief Get a mutable reference to an attribute by descriptor. */
    std::optional<std::reference_wrapper<MeshVertexAttribute>> get_attribute_mut(const MeshVertexAttribute& attribute) {
        auto it = this->find(attribute.id);
        if (it != this->end() && it->second == attribute) {
            return std::ref(it->second);
        }
        return std::nullopt;
    }
    /** @brief Get a mutable reference to an attribute by ID. */
    std::optional<std::reference_wrapper<MeshVertexAttribute>> get_attribute_mut(MeshVertexAttributeId id) {
        auto it = this->find(id);
        if (it != this->end()) {
            return std::ref(it->second);
        }
        return std::nullopt;
    }
    /** @brief Get a human-readable string representation of this layout. */
    std::string to_string() const {
        std::stringstream ss;
        std::println(ss, "MeshAttributeLayout {{ primitive_type = {}", wgpu::to_string(primitive_type));
        for (const auto& [id, attribute] : *this) {
            std::println(ss, "  Id {}: Name='{}', Format={}", id.value, attribute.name,
                         wgpu::to_string(attribute.format));
        }
        std::println(ss, "}}");
        return ss.str();
    }
};
EPIX_EXPORT namespace mesh_merge_error {
    /** @brief The two meshes store the same attribute ID with different types. */
    struct IncompatibleVertexAttributes {
        /** @brief The attribute this mesh stores under that ID. */
        MeshVertexAttribute self_attribute;
        /** @brief The attribute the other mesh stores under that ID, if it has one. */
        std::optional<MeshVertexAttribute> other_attribute;

        bool operator==(const IncompatibleVertexAttributes&) const = default;
    };
    /** @brief The two meshes use different primitive topologies. */
    struct IncompatiblePrimitiveTopology {
        /** @brief The topology of the mesh being merged into. */
        wgpu::PrimitiveTopology self_primitive_topology = wgpu::PrimitiveTopology::eTriangleList;
        /** @brief The topology of the mesh being merged in. */
        wgpu::PrimitiveTopology other_primitive_topology = wgpu::PrimitiveTopology::eTriangleList;

        bool operator==(const IncompatiblePrimitiveTopology&) const = default;
    };
}  // namespace mesh_merge_error

/** @brief Error returned while merging one mesh into another (Bevy
 * `MeshMergeError`). */
EPIX_EXPORT struct MeshMergeError : std::variant<mesh_merge_error::IncompatibleVertexAttributes,
                                                 mesh_merge_error::IncompatiblePrimitiveTopology,
                                                 MeshAccessError> {
    using Base = std::variant<mesh_merge_error::IncompatibleVertexAttributes,
                              mesh_merge_error::IncompatiblePrimitiveTopology,
                              MeshAccessError>;
    using Base::Base;

    /** @brief Return Bevy's `Display` text for this merge error. */
    std::string to_string() const {
        return std::visit(
            [](const auto& error) -> std::string {
                using Error = std::remove_cvref_t<decltype(error)>;
                if constexpr (std::same_as<Error, mesh_merge_error::IncompatibleVertexAttributes>) {
                    return std::format("Incompatible vertex attribute types: {} and {}", error.self_attribute.name,
                                       error.other_attribute ? error.other_attribute->name : std::string_view{"None"});
                } else if constexpr (std::same_as<Error, mesh_merge_error::IncompatiblePrimitiveTopology>) {
                    return std::format("Incompatible primitive topologies: {} and {}",
                                       wgpu::to_string(error.self_primitive_topology),
                                       wgpu::to_string(error.other_primitive_topology));
                } else {
                    return "Mesh access error: " + std::string(epix::mesh::to_string(error));
                }
            },
            static_cast<const Base&>(*this));
    }
};

/** @brief An index buffer stored as either 16-bit or 32-bit values (Bevy
 * `Indices`). */
EPIX_EXPORT class Indices {
   public:
    explicit Indices(std::vector<std::uint16_t> values) : values_(std::move(values)) {}
    explicit Indices(std::vector<std::uint32_t> values) : values_(std::move(values)) {}

    /** @brief Lazily iterate over every index as `std::size_t`. */
    auto iter() const {
        return std::views::iota(std::size_t{0}, len()) | std::views::transform([this](std::size_t index) {
                   return std::visit([index](const auto& values) { return static_cast<std::size_t>(values[index]); },
                                     values_);
               });
    }

    /** @brief Return the number of indices. */
    std::size_t len() const noexcept {
        return std::visit([](const auto& values) { return values.size(); }, values_);
    }
    /** @brief Return whether the index buffer is empty. */
    bool is_empty() const noexcept {
        return std::visit([](const auto& values) { return values.empty(); }, values_);
    }

    /** @brief Append an index, promoting U16 storage to U32 when necessary. */
    void push(std::uint32_t index) { extend(std::views::single(index)); }

    /** @brief Append a range of indices, promoting U16 storage to U32 when
     * necessary. */
    template <std::ranges::input_range Range>
        requires std::same_as<std::ranges::range_value_t<Range>, std::uint32_t>
    void extend(Range&& range) {
        auto iterator = std::ranges::begin(range);
        auto sentinel = std::ranges::end(range);
        if (auto* values = std::get_if<std::vector<std::uint32_t>>(&values_)) {
            if constexpr (std::ranges::sized_range<Range>) {
                values->reserve(values->size() + static_cast<std::size_t>(std::ranges::size(range)));
            }
            for (; iterator != sentinel; ++iterator) values->push_back(static_cast<std::uint32_t>(*iterator));
            return;
        }

        auto* values = std::get_if<std::vector<std::uint16_t>>(&values_);
        if constexpr (std::ranges::sized_range<Range>) {
            values->reserve(values->size() + static_cast<std::size_t>(std::ranges::size(range)));
        }
        for (; iterator != sentinel; ++iterator) {
            const auto index = static_cast<std::uint32_t>(*iterator);
            if (index <= std::numeric_limits<std::uint16_t>::max()) {
                values->push_back(static_cast<std::uint16_t>(index));
                continue;
            }

            std::vector<std::uint32_t> promoted;
            promoted.reserve(values->size() + 1);
            promoted.insert(promoted.end(), values->begin(), values->end());
            promoted.push_back(index);
            for (++iterator; iterator != sentinel; ++iterator) {
                promoted.push_back(static_cast<std::uint32_t>(*iterator));
            }
            values_ = std::move(promoted);
            return;
        }
    }

    /** @brief Return the WebGPU index format matching the active alternative. */
    explicit operator wgpu::IndexFormat() const noexcept {
        return std::holds_alternative<std::vector<std::uint16_t>>(values_) ? wgpu::IndexFormat::eUint16
                                                                           : wgpu::IndexFormat::eUint32;
    }

    const std::vector<std::uint16_t>* as_u16() const noexcept {
        return std::get_if<std::vector<std::uint16_t>>(&values_);
    }
    std::vector<std::uint16_t>* as_u16() noexcept { return std::get_if<std::vector<std::uint16_t>>(&values_); }
    const std::vector<std::uint32_t>* as_u32() const noexcept {
        return std::get_if<std::vector<std::uint32_t>>(&values_);
    }
    std::vector<std::uint32_t>* as_u32() noexcept { return std::get_if<std::vector<std::uint32_t>>(&values_); }

    bool operator==(const Indices&) const = default;

   private:
    std::variant<std::vector<std::uint16_t>, std::vector<std::uint32_t>> values_;
};

namespace detail {
struct EPIX_API MeshTriangleAt {
    const std::vector<VertexAttributeValues::Float32x3Value>* vertices;
    const Indices* indices;
    wgpu::PrimitiveTopology topology;

    std::optional<Triangle3d> operator()(std::size_t triangle_index) const;
};

struct HasMeshTriangle {
    bool operator()(const std::optional<Triangle3d>& triangle) const noexcept { return triangle.has_value(); }
};

struct UnwrapMeshTriangle {
    Triangle3d operator()(std::optional<Triangle3d> triangle) const { return *std::move(triangle); }
};

inline auto mesh_triangles_view(const std::vector<VertexAttributeValues::Float32x3Value>& vertices,
                                const Indices& indices,
                                wgpu::PrimitiveTopology topology,
                                std::size_t triangle_count) {
    return std::views::iota(std::size_t{0}, triangle_count) |
           std::views::transform(MeshTriangleAt{&vertices, &indices, topology}) |
           std::views::filter(HasMeshTriangle{}) | std::views::transform(UnwrapMeshTriangle{});
}

}  // namespace detail

/** @brief Lazy range returned by `Mesh::triangles`. */
EPIX_EXPORT using MeshTriangles = std::invoke_result_t<decltype(detail::mesh_triangles_view),
                                                       const std::vector<VertexAttributeValues::Float32x3Value>&,
                                                       const Indices&,
                                                       wgpu::PrimitiveTopology,
                                                       std::size_t>;

namespace detail {

/** @brief Internal descriptor/value pair matching Bevy's crate-private `MeshAttributeData`. */
struct MeshAttributeData {
    MeshVertexAttribute attribute;
    VertexAttributeValues values;

    bool operator==(const MeshAttributeData&) const = default;
};

struct ExtractedToRenderWorld {
    bool operator==(const ExtractedToRenderWorld&) const = default;
};

struct ConstMeshAttributeRefs {
    auto operator()(const MeshAttributeData& data) const {
        return std::pair<const MeshVertexAttribute&, const VertexAttributeValues&>{data.attribute, data.values};
    }
};

struct MeshAttributeRefs {
    auto operator()(MeshAttributeData& data) const {
        return std::pair<const MeshVertexAttribute&, VertexAttributeValues&>{data.attribute, data.values};
    }
};

/** @brief Storage that distinguishes present, absent, and extracted mesh data.
 *
 * This is the C++ counterpart of Bevy 0.18's private
 * `MeshExtractableData<T>` state machine.
 */
template <typename T>
class MeshExtractableData {
   public:
    static MeshExtractableData data(T value) {
        MeshExtractableData result;
        result._value.template emplace<1>(std::move(value));
        return result;
    }

    static MeshExtractableData extracted_to_render_world() {
        MeshExtractableData result;
        result._value.template emplace<2>();
        return result;
    }

    std::expected<std::reference_wrapper<const T>, MeshAccessError> as_ref() const {
        if (const auto* value = std::get_if<T>(&_value)) return std::cref(*value);
        return std::unexpected(std::holds_alternative<ExtractedToRenderWorld>(_value)
                                   ? MeshAccessError::ExtractedToRenderWorld
                                   : MeshAccessError::NotFound);
    }

    std::expected<std::optional<std::reference_wrapper<const T>>, MeshAccessError> as_ref_option() const {
        if (const auto* value = std::get_if<T>(&_value)) return std::optional{std::cref(*value)};
        if (std::holds_alternative<ExtractedToRenderWorld>(_value)) {
            return std::unexpected(MeshAccessError::ExtractedToRenderWorld);
        }
        return std::nullopt;
    }

    std::expected<std::reference_wrapper<T>, MeshAccessError> as_mut() {
        if (auto* value = std::get_if<T>(&_value)) return std::ref(*value);
        return std::unexpected(std::holds_alternative<ExtractedToRenderWorld>(_value)
                                   ? MeshAccessError::ExtractedToRenderWorld
                                   : MeshAccessError::NotFound);
    }

    std::expected<std::optional<std::reference_wrapper<T>>, MeshAccessError> as_mut_option() {
        if (auto* value = std::get_if<T>(&_value)) return std::optional{std::ref(*value)};
        if (std::holds_alternative<ExtractedToRenderWorld>(_value)) {
            return std::unexpected(MeshAccessError::ExtractedToRenderWorld);
        }
        return std::nullopt;
    }

    std::expected<MeshExtractableData, MeshAccessError> extract() {
        if (std::holds_alternative<ExtractedToRenderWorld>(_value)) {
            return std::unexpected(MeshAccessError::ExtractedToRenderWorld);
        }
        MeshExtractableData extracted = std::move(*this);
        _value.template emplace<2>();
        return extracted;
    }

    std::expected<std::optional<T>, MeshAccessError> replace(std::optional<T> value) {
        if (std::holds_alternative<ExtractedToRenderWorld>(_value)) {
            return std::unexpected(MeshAccessError::ExtractedToRenderWorld);
        }
        std::optional<T> previous;
        if (auto* current = std::get_if<T>(&_value)) previous.emplace(std::move(*current));
        if (value) {
            _value.template emplace<1>(std::move(*value));
        } else {
            _value.template emplace<0>();
        }
        return previous;
    }

    bool operator==(const MeshExtractableData&) const = default;

   private:
    std::variant<std::monostate, T, ExtractedToRenderWorld> _value;
};
}  // namespace detail

/** @brief Axis-aligned box defined by its minimum and maximum extent (Bevy
 * `bevy_math::bounding::Aabb3d`).
 *
 * Bevy keeps this type in `bevy_math`, which Epix has no standalone equivalent of, so the surface
 * its mesh and camera code consumes lives here, as the minimal shape inputs do (M19a). The rest of
 * `bevy_math::bounding` - the `BoundingVolume` trait, `from_point_cloud`, `bounding_sphere`,
 * `closest_point` and the ray casts - has no consumer in these modules and is out of scope. */
EPIX_EXPORT struct Aabb3d {
    /** @brief The minimum point of the box. */
    glm::vec3 min{0.0f};
    /** @brief The maximum point of the box. */
    glm::vec3 max{0.0f};

    /** @brief Construct from a minimum and maximum extent (Bevy `Aabb3d::from_min_max`). */
    static Aabb3d from_min_max(glm::vec3 minimum, glm::vec3 maximum) noexcept {
        return {.min = minimum, .max = maximum};
    }
    /** @brief The box's center (Bevy `BoundingVolume::center`). */
    glm::vec3 center() const noexcept { return 0.5f * (min + max); }
    /** @brief Half the box's size (Bevy `BoundingVolume::half_size`). */
    glm::vec3 half_size() const noexcept { return 0.5f * (max - min); }

    bool operator==(const Aabb3d&) const = default;
};

/** @brief CPU-side mesh asset storing vertex attributes and optional index data.
 *
 * Use insert_attribute/with_inserted_attribute to add vertex data and
 * insert_indices/with_inserted_indices to add index data. Standard attribute constants
 * (ATTRIBUTE_POSITION, etc.) are provided.
 */
EPIX_EXPORT struct EPIX_API Mesh {
   private:
    using AttributeMap = std::map<MeshVertexAttributeId, detail::MeshAttributeData>;

   public:
    static inline const MeshVertexAttribute ATTRIBUTE_POSITION{"Vertex_Position", MeshVertexAttributeId{0},
                                                               wgpu::VertexFormat::eFloat32x3};
    static inline const MeshVertexAttribute ATTRIBUTE_NORMAL{"Vertex_Normal", MeshVertexAttributeId{1},
                                                             wgpu::VertexFormat::eFloat32x3};
    static inline const MeshVertexAttribute ATTRIBUTE_UV_0{"Vertex_Uv", MeshVertexAttributeId{2},
                                                           wgpu::VertexFormat::eFloat32x2};
    static inline const MeshVertexAttribute ATTRIBUTE_UV_1{"Vertex_Uv_1", MeshVertexAttributeId{3},
                                                           wgpu::VertexFormat::eFloat32x2};
    static inline const MeshVertexAttribute ATTRIBUTE_TANGENT{"Vertex_Tangent", MeshVertexAttributeId{4},
                                                              wgpu::VertexFormat::eFloat32x4};
    static inline const MeshVertexAttribute ATTRIBUTE_COLOR{"Vertex_Color", MeshVertexAttributeId{5},
                                                            wgpu::VertexFormat::eFloat32x4};
    static inline const MeshVertexAttribute ATTRIBUTE_JOINT_WEIGHT{"Vertex_JointWeight", MeshVertexAttributeId{6},
                                                                   wgpu::VertexFormat::eFloat32x4};
    static inline const MeshVertexAttribute ATTRIBUTE_JOINT_INDEX{"Vertex_JointIndex", MeshVertexAttributeId{7},
                                                                  wgpu::VertexFormat::eUint16x4};
    static constexpr std::uint64_t FIRST_AVAILABLE_CUSTOM_ATTRIBUTE = 8;

   public:
    Mesh(wgpu::PrimitiveTopology primitive_type, assets::RenderAssetUsages asset_usage) noexcept
        : asset_usage(asset_usage),
          primitive_type(primitive_type),
          _attributes(detail::MeshExtractableData<AttributeMap>::data({})) {}
    Mesh(const Mesh&);
    Mesh(Mesh&&) = default;
    Mesh& operator=(const Mesh&);
    Mesh& operator=(Mesh&&)            = default;
    bool operator==(const Mesh&) const = default;
    template <MeshBuilder Builder>
    Mesh(const Builder& builder) : Mesh(builder.build()) {}
    template <Meshable Shape>
    Mesh(const Shape& shape) : Mesh(shape.mesh().build()) {}

    /** @brief Worlds in which this mesh's asset data is retained. */
    assets::RenderAssetUsages asset_usage;
    /** @brief Precomputed min and max extents of the mesh position data, used mainly for
     * constructing `Aabb`s for frustum culling.
     *
     * Set if and when the mesh is extracted to the GPU (Bevy `Mesh::final_aabb`). */
    std::optional<Aabb3d> final_aabb;

    /** @brief Get the primitive topology (Bevy `Mesh::primitive_topology`,
     *  `bevy_mesh/src/mesh.rs:356`). Bevy's field is private and only `Mesh::new`
     *  writes it, so there is no setter here either. */
    wgpu::PrimitiveTopology primitive_topology() const noexcept { return primitive_type; }
    /** @brief Fallible attribute iteration (Bevy `Mesh::try_attributes`). */
    auto try_attributes() const {
        using View        = decltype(std::views::values(std::declval<const AttributeMap&>()) |
                                     std::views::transform(detail::ConstMeshAttributeRefs{}));
        const auto values = _attributes.as_ref();
        if (!values) return std::expected<View, MeshAccessError>{std::unexpected(values.error())};
        return std::expected<View, MeshAccessError>{std::views::values(values->get()) |
                                                    std::views::transform(detail::ConstMeshAttributeRefs{})};
    }
    /** @brief Iterate over all attributes. Throws after render-world extraction. */
    auto attributes() const {
        auto result = try_attributes();
        if (!result) throw_access_error(result.error());
        return std::move(result).value();
    }
    /** @brief Fallible mutable attribute iteration (Bevy `Mesh::try_attributes_mut`). */
    auto try_attributes_mut() {
        using View  = decltype(std::views::values(std::declval<AttributeMap&>()) |
                               std::views::transform(detail::MeshAttributeRefs{}));
        auto values = _attributes.as_mut();
        if (!values) return std::expected<View, MeshAccessError>{std::unexpected(values.error())};
        return std::expected<View, MeshAccessError>{std::views::values(values->get()) |
                                                    std::views::transform(detail::MeshAttributeRefs{})};
    }
    /** @brief Iterate mutably over all attributes. Throws after render-world extraction. */
    auto attributes_mut() {
        auto result = try_attributes_mut();
        if (!result) throw_access_error(result.error());
        return std::move(result).value();
    }

    /** @brief Build a MeshAttributeLayout from the current attributes. */
    MeshAttributeLayout attribute_layout() const;

    /** @brief Get this mesh's interleaved vertex-buffer layout, interning it in
     * `mesh_vertex_buffer_layouts` (Bevy `Mesh::get_mesh_vertex_buffer_layout`).
     * The attribute IDs are in ascending ID order and each attribute's offset
     * accumulates the previous attribute sizes. */
    MeshVertexBufferLayoutRef get_mesh_vertex_buffer_layout(MeshVertexBufferLayouts& mesh_vertex_buffer_layouts) const;

    /** @brief Size of a single vertex in bytes: the sum of every attribute's
     * format size (Bevy `Mesh::get_vertex_size`). */
    std::uint64_t get_vertex_size() const;
    /** @brief Size required for the vertex buffer in bytes (Bevy
     * `Mesh::get_vertex_buffer_size`). */
    std::size_t get_vertex_buffer_size() const;
    /** @brief The vertex data packed into Bevy's interleaved vertex-buffer form:
     * the attributes of each vertex in ascending attribute-ID order, truncated to
     * the smallest attribute length (Bevy `Mesh::create_packed_vertex_buffer_data`). */
    std::vector<std::uint8_t> create_packed_vertex_buffer_data() const;
    /** @brief Writes the packed vertex data into `slice`, which must hold at least
     * `get_vertex_buffer_size()` bytes (Bevy `Mesh::write_packed_vertex_buffer_data`). */
    void write_packed_vertex_buffer_data(std::span<std::uint8_t> slice) const;

    /** @brief Insert or replace semantically tagged attribute values. */
    void insert_attribute(MeshVertexAttribute attribute, VertexAttributeValues values) {
        auto result = try_insert_attribute(std::move(attribute), std::move(values));
        if (!result) throw_access_error(result.error());
    }
    /** @brief Fallible insertion. Format mismatch remains a programmer error, as in Bevy. */
    std::expected<void, MeshAccessError> try_insert_attribute(MeshVertexAttribute attribute,
                                                              VertexAttributeValues values) {
        if (values.format() != attribute.format) {
            throw std::invalid_argument(std::format(
                "Failed to insert attribute. Invalid attribute format for {}. Given format is {} but expected {}",
                attribute.name, wgpu::to_string(values.format()), wgpu::to_string(attribute.format)));
        }
        detail::MeshAttributeData attribute_data{
            .attribute = attribute,
            .values    = std::move(values),
        };
        auto attributes = _attributes.as_mut();
        if (!attributes) return std::unexpected(attributes.error());
        attributes->get().insert_or_assign(attribute.id, std::move(attribute_data));
        return {};
    }
    /** @brief C++ range adaptation for Bevy's `Into<VertexAttributeValues>` input. */
    template <std::ranges::input_range Range>
        requires std::constructible_from<VertexAttributeValues, std::vector<std::ranges::range_value_t<Range>>>
    void insert_attribute(MeshVertexAttribute attribute, Range&& values) {
        insert_attribute(std::move(attribute),
                         VertexAttributeValues{std::ranges::to<std::vector<std::ranges::range_value_t<Range>>>(
                             std::forward<Range>(values))});
    }
    template <std::ranges::input_range Range>
        requires std::constructible_from<VertexAttributeValues, std::vector<std::ranges::range_value_t<Range>>>
    std::expected<void, MeshAccessError> try_insert_attribute(MeshVertexAttribute attribute, Range&& values) {
        return try_insert_attribute(
            std::move(attribute), VertexAttributeValues{std::ranges::to<std::vector<std::ranges::range_value_t<Range>>>(
                                      std::forward<Range>(values))});
    }
    /** @brief Builder-style attribute insertion (Bevy `with_inserted_attribute`). */
    Mesh with_inserted_attribute(MeshVertexAttribute attribute, VertexAttributeValues values) && {
        insert_attribute(std::move(attribute), std::move(values));
        return std::move(*this);
    }
    template <std::ranges::input_range Range>
        requires std::constructible_from<VertexAttributeValues, std::vector<std::ranges::range_value_t<Range>>>
    Mesh with_inserted_attribute(MeshVertexAttribute attribute, Range&& values) && {
        insert_attribute(std::move(attribute), std::forward<Range>(values));
        return std::move(*this);
    }
    template <typename Values>
        requires requires(Mesh& mesh, MeshVertexAttribute descriptor, Values&& values) {
            mesh.try_insert_attribute(std::move(descriptor), std::forward<Values>(values));
        }
    std::expected<Mesh, MeshAccessError> try_with_inserted_attribute(MeshVertexAttribute attribute,
                                                                     Values&& values) && {
        auto result = try_insert_attribute(std::move(attribute), std::forward<Values>(values));
        if (!result) return std::unexpected(result.error());
        return std::move(*this);
    }
    std::optional<std::reference_wrapper<const VertexAttributeValues>> attribute(
        const MeshVertexAttribute& attribute) const;
    std::optional<std::reference_wrapper<const VertexAttributeValues>> attribute(MeshVertexAttributeId id) const;
    std::expected<std::reference_wrapper<const VertexAttributeValues>, MeshAccessError> try_attribute(
        const MeshVertexAttribute& attribute) const;
    std::expected<std::reference_wrapper<const VertexAttributeValues>, MeshAccessError> try_attribute(
        MeshVertexAttributeId id) const;
    std::expected<std::optional<std::reference_wrapper<const VertexAttributeValues>>, MeshAccessError>
    try_attribute_option(const MeshVertexAttribute& attribute) const;
    std::expected<std::optional<std::reference_wrapper<const VertexAttributeValues>>, MeshAccessError>
    try_attribute_option(MeshVertexAttributeId id) const;

    std::optional<std::reference_wrapper<VertexAttributeValues>> attribute_mut(const MeshVertexAttribute& attribute);
    std::optional<std::reference_wrapper<VertexAttributeValues>> attribute_mut(MeshVertexAttributeId id);
    std::expected<std::reference_wrapper<VertexAttributeValues>, MeshAccessError> try_attribute_mut(
        const MeshVertexAttribute& attribute);
    std::expected<std::reference_wrapper<VertexAttributeValues>, MeshAccessError> try_attribute_mut(
        MeshVertexAttributeId id);
    std::expected<std::optional<std::reference_wrapper<VertexAttributeValues>>, MeshAccessError>
    try_attribute_mut_option(const MeshVertexAttribute& attribute);
    std::expected<std::optional<std::reference_wrapper<VertexAttributeValues>>, MeshAccessError>
    try_attribute_mut_option(MeshVertexAttributeId id);

    std::optional<VertexAttributeValues> remove_attribute(const MeshVertexAttribute& attribute);
    std::optional<VertexAttributeValues> remove_attribute(MeshVertexAttributeId id);
    std::expected<VertexAttributeValues, MeshAccessError> try_remove_attribute(const MeshVertexAttribute& attribute);
    std::expected<VertexAttributeValues, MeshAccessError> try_remove_attribute(MeshVertexAttributeId id);
    auto&& with_removed_attribute(this auto&& self, const MeshVertexAttribute& attribute) {
        self.remove_attribute(attribute);
        return std::forward<decltype(self)>(self);
    }
    auto&& with_removed_attribute(this auto&& self, MeshVertexAttributeId id) {
        self.remove_attribute(id);
        return std::forward<decltype(self)>(self);
    }
    std::expected<Mesh, MeshAccessError> try_with_removed_attribute(const MeshVertexAttribute& attribute) &&;
    std::expected<Mesh, MeshAccessError> try_with_removed_attribute(MeshVertexAttributeId id) &&;

    bool contains_attribute(const MeshVertexAttribute& attribute) const;
    bool contains_attribute(MeshVertexAttributeId id) const;
    std::expected<bool, MeshAccessError> try_contains_attribute(const MeshVertexAttribute& attribute) const;
    std::expected<bool, MeshAccessError> try_contains_attribute(MeshVertexAttributeId id) const;

    /** @brief Insert indices, replacing any existing ones. */
    void insert_indices(Indices indices) {
        auto result = try_insert_indices(std::move(indices));
        if (!result) throw_access_error(result.error());
    }
    std::expected<void, MeshAccessError> try_insert_indices(Indices indices) {
        auto replaced = _indices.replace(std::optional<Indices>{std::move(indices)});
        if (!replaced) return std::unexpected(replaced.error());
        return {};
    }
    /** @brief Builder-style insert indices (Bevy `with_inserted_indices`). */
    Mesh with_inserted_indices(Indices indices) && {
        insert_indices(std::move(indices));
        return std::move(*this);
    }
    std::expected<Mesh, MeshAccessError> try_with_inserted_indices(Indices indices) && {
        auto result = try_insert_indices(std::move(indices));
        if (!result) return std::unexpected(result.error());
        return std::move(*this);
    }

    std::optional<std::reference_wrapper<const Indices>> indices() const;
    std::expected<std::reference_wrapper<const Indices>, MeshAccessError> try_indices() const;
    std::expected<std::optional<std::reference_wrapper<const Indices>>, MeshAccessError> try_indices_option() const;
    std::optional<std::reference_wrapper<Indices>> indices_mut();
    std::expected<std::reference_wrapper<Indices>, MeshAccessError> try_indices_mut();
    std::expected<std::optional<std::reference_wrapper<Indices>>, MeshAccessError> try_indices_mut_option();

    std::optional<Indices> remove_indices();
    std::expected<std::optional<Indices>, MeshAccessError> try_remove_indices();
    auto&& with_removed_indices(this auto&& self) {
        self.remove_indices();
        return std::forward<decltype(self)>(self);
    }
    std::expected<Mesh, MeshAccessError> try_with_removed_indices() &&;

    /** @brief Duplicate attributes according to the index buffer and remove the indices. */
    void duplicate_vertices();
    std::expected<void, MeshAccessError> try_duplicate_vertices();
    Mesh with_duplicated_vertices() &&;
    std::expected<Mesh, MeshAccessError> try_with_duplicated_vertices() &&;

    /** @brief Reverse the index winding according to the primitive topology. */
    std::expected<void, MeshWindingInvertError> invert_winding();
    std::expected<Mesh, MeshWindingInvertError> with_inverted_winding() &&;

    /** @brief Insert the mesh's normals: smooth when the mesh is indexed, flat when it is not
     * (Bevy `Mesh::compute_normals`). Throws after render-world extraction. */
    void compute_normals();
    /** @brief Fallible `compute_normals` (Bevy `Mesh::try_compute_normals`). */
    std::expected<void, MeshAccessError> try_compute_normals();

    /** @brief Insert one flat (per-face) normal per vertex; requires an unindexed `TriangleList`
     * (Bevy `Mesh::compute_flat_normals`). Throws after render-world extraction. */
    void compute_flat_normals();
    /** @brief Fallible `compute_flat_normals` (Bevy `Mesh::try_compute_flat_normals`). */
    std::expected<void, MeshAccessError> try_compute_flat_normals();

    /** @brief Insert normals smoothed over shared vertices, weighted by the corner angles of the
     * connected triangles (Bevy `Mesh::compute_smooth_normals`). */
    void compute_smooth_normals();
    /** @brief Fallible `compute_smooth_normals` (Bevy `Mesh::try_compute_smooth_normals`). */
    std::expected<void, MeshAccessError> try_compute_smooth_normals();

    /** @brief Insert normals smoothed over shared vertices, weighted by the area of each
     * connected triangle (Bevy `Mesh::compute_area_weighted_normals`). */
    void compute_area_weighted_normals();
    /** @brief Fallible `compute_area_weighted_normals` (Bevy
     * `Mesh::try_compute_area_weighted_normals`). */
    std::expected<void, MeshAccessError> try_compute_area_weighted_normals();

    /** @brief Insert normals smoothed over shared vertices with a custom per-triangle weight
     * (Bevy `Mesh::compute_custom_smooth_normals`). Throws after render-world extraction.
     *
     * `per_triangle` receives a triangle's three vertex indices, the mesh's positions and the
     * running normal sums, which is the C++ form of Bevy's
     * `FnMut([usize; 3], &[[f32; 3]], &mut [Vec3])`. */
    template <typename PerTriangle>
    void compute_custom_smooth_normals(PerTriangle&& per_triangle) {
        auto result = try_compute_custom_smooth_normals(std::forward<PerTriangle>(per_triangle));
        if (!result) throw_access_error(result.error());
    }
    /** @brief Fallible `compute_custom_smooth_normals` (Bevy
     * `Mesh::try_compute_custom_smooth_normals`). */
    template <typename PerTriangle>
    std::expected<void, MeshAccessError> try_compute_custom_smooth_normals(PerTriangle&& per_triangle) {
        detail::assert_precondition(primitive_type == wgpu::PrimitiveTopology::eTriangleList,
                                    "smooth normals can only be computed on `TriangleList`s");
        auto stored_indices = try_indices_option();
        if (!stored_indices) return std::unexpected(stored_indices.error());
        detail::assert_precondition(stored_indices->has_value(),
                                    "smooth normals can only be computed on indexed meshes");

        auto position_data = try_attribute(ATTRIBUTE_POSITION);
        if (!position_data) return std::unexpected(position_data.error());
        const auto* positions = position_data->get().as_float3();
        detail::assert_precondition(positions != nullptr,
                                    "`Mesh::ATTRIBUTE_POSITION` vertex attributes should be of type `float3`");

        std::vector<glm::vec3> normals(positions->size(), glm::vec3{0.0f});

        const std::vector<std::size_t> raw_indices =
            stored_indices->value().get().iter() | std::ranges::to<std::vector>();
        for (std::size_t offset = 0; offset + 3 <= raw_indices.size(); offset += 3) {
            per_triangle(std::array{raw_indices[offset], raw_indices[offset + 1], raw_indices[offset + 2]}, *positions,
                         normals);
        }

        for (auto& normal : normals) normal = detail::normalize_or_zero(normal);
        return try_insert_attribute(ATTRIBUTE_NORMAL, std::move(normals));
    }

    /** @brief Consume the mesh and return it with computed normals (Bevy
     * `Mesh::with_computed_normals`). Throws after render-world extraction. */
    Mesh with_computed_normals() &&;
    /** @brief Fallible `with_computed_normals` (Bevy `Mesh::try_with_computed_normals`). */
    std::expected<Mesh, MeshAccessError> try_with_computed_normals() &&;

    /** @brief Consume the mesh and return it with flat normals (Bevy
     * `Mesh::with_computed_flat_normals`). */
    Mesh with_computed_flat_normals() &&;
    /** @brief Fallible `with_computed_flat_normals` (Bevy
     * `Mesh::try_with_computed_flat_normals`). */
    std::expected<Mesh, MeshAccessError> try_with_computed_flat_normals() &&;

    /** @brief Consume the mesh and return it with smooth normals (Bevy
     * `Mesh::with_computed_smooth_normals`). */
    Mesh with_computed_smooth_normals() &&;
    /** @brief Fallible `with_computed_smooth_normals` (Bevy
     * `Mesh::try_with_computed_smooth_normals`). */
    std::expected<Mesh, MeshAccessError> try_with_computed_smooth_normals() &&;

    /** @brief Consume the mesh and return it with area-weighted normals (Bevy
     * `Mesh::with_computed_area_weighted_normals`). */
    Mesh with_computed_area_weighted_normals() &&;
    /** @brief Fallible `with_computed_area_weighted_normals` (Bevy
     * `Mesh::try_with_computed_area_weighted_normals`). */
    std::expected<Mesh, MeshAccessError> try_with_computed_area_weighted_normals() &&;

    /** @brief Lazily iterate indexed triangle-list or triangle-strip faces. */
    std::expected<MeshTriangles, MeshTrianglesError> triangles() const;

    /** @brief Return the raw index-buffer bytes, or no value for a non-indexed
     * mesh (Bevy `Mesh::get_index_buffer_bytes`). */
    std::optional<std::span<const std::uint8_t>> get_index_buffer_bytes() const;

    /** @brief Append `other`'s vertex and index data to this mesh (Bevy
     * `Mesh::merge`).
     *
     * Attributes of `other` that this mesh does not have are ignored, and the
     * indices of `other` are offset past this mesh's last vertex. Both meshes
     * must share a primitive topology, and every attribute ID must have the same
     * type in both. Throws after render-world extraction, exactly as Bevy's
     * `count_vertices` panics there. */
    std::expected<void, MeshMergeError> merge(const Mesh& other);

    /** @brief Translate every vertex position by `translation` (Bevy
     * `Mesh::translate_by`). Throws after render-world extraction. */
    void translate_by(glm::vec3 translation);
    /** @brief Fallible `translate_by` (Bevy `Mesh::try_translate_by`). */
    std::expected<void, MeshAccessError> try_translate_by(glm::vec3 translation);
    /** @brief Consume this mesh and return a translated copy (Bevy
     * `Mesh::translated_by`). */
    Mesh translated_by(glm::vec3 translation) &&;
    /** @brief Fallible `translated_by` (Bevy `Mesh::try_translated_by`). */
    std::expected<Mesh, MeshAccessError> try_translated_by(glm::vec3 translation) &&;

    /** @brief Scale every vertex position, normal and tangent by `scale` (Bevy
     * `Mesh::scale_by`). Throws after render-world extraction. */
    void scale_by(glm::vec3 scale);
    /** @brief Fallible `scale_by` (Bevy `Mesh::try_scale_by`). */
    std::expected<void, MeshAccessError> try_scale_by(glm::vec3 scale);
    /** @brief Consume this mesh and return a scaled copy (Bevy
     * `Mesh::scaled_by`). */
    Mesh scaled_by(glm::vec3 scale) &&;
    /** @brief Fallible `scaled_by` (Bevy `Mesh::try_scaled_by`). */
    std::expected<Mesh, MeshAccessError> try_scaled_by(glm::vec3 scale) &&;

    /** @brief Rotate every vertex position, normal and tangent by `rotation` (Bevy
     * `Mesh::rotate_by`). Throws after render-world extraction. */
    void rotate_by(glm::quat rotation);
    /** @brief Fallible `rotate_by` (Bevy `Mesh::try_rotate_by`). */
    std::expected<void, MeshAccessError> try_rotate_by(glm::quat rotation);
    /** @brief Consume this mesh and return a rotated copy (Bevy `Mesh::rotated_by`). */
    Mesh rotated_by(glm::quat rotation) &&;
    /** @brief Fallible `rotated_by` (Bevy `Mesh::try_rotated_by`). */
    std::expected<Mesh, MeshAccessError> try_rotated_by(glm::quat rotation) &&;

    /** @brief Transform every vertex position, normal and tangent by `transform` (Bevy
     * `Mesh::transform_by`). Throws after render-world extraction. */
    void transform_by(const epix::transform::Transform& transform);
    /** @brief Fallible `transform_by` (Bevy `Mesh::try_transform_by`). */
    std::expected<void, MeshAccessError> try_transform_by(const epix::transform::Transform& transform);
    /** @brief Consume this mesh and return a transformed copy (Bevy
     * `Mesh::transformed_by`). */
    Mesh transformed_by(const epix::transform::Transform& transform) &&;
    /** @brief Fallible `transformed_by` (Bevy `Mesh::try_transformed_by`). */
    std::expected<Mesh, MeshAccessError> try_transformed_by(const epix::transform::Transform& transform) &&;

    /** @brief Normalize the joint weights so that each vertex's four weights sum to 1 (Bevy
     * `Mesh::normalize_joint_weights`). Throws after render-world extraction. */
    void normalize_joint_weights();
    /** @brief Fallible `normalize_joint_weights` (Bevy `Mesh::try_normalize_joint_weights`). */
    std::expected<void, MeshAccessError> try_normalize_joint_weights();

    /** @brief Whether this mesh has a morph-target image. Throws after render-world extraction. */
    bool has_morph_targets() const;
    /** @brief Fallible form of `has_morph_targets`. */
    std::expected<bool, MeshAccessError> try_has_morph_targets() const;

    /** @brief Set the image containing this mesh's morph-target deltas. */
    void set_morph_targets(assets::Handle<image::Image> morph_targets);
    /** @brief Fallible form of `set_morph_targets`. */
    std::expected<void, MeshAccessError> try_set_morph_targets(assets::Handle<image::Image> morph_targets);
    /** @brief Retrieve the morph-target image, or no value when absent. */
    std::optional<std::reference_wrapper<const assets::Handle<image::Image>>> morph_targets() const;
    /** @brief Retrieve the required morph-target image, reporting absence or extraction. */
    std::expected<std::reference_wrapper<const assets::Handle<image::Image>>, MeshAccessError> try_morph_targets()
        const;
    /** @brief Consume this mesh and set its morph-target image. */
    Mesh with_morph_targets(assets::Handle<image::Image> morph_targets) &&;
    /** @brief Fallible consuming form of `with_morph_targets`. */
    std::expected<Mesh, MeshAccessError> try_with_morph_targets(assets::Handle<image::Image> morph_targets) &&;

    /** @brief Set names corresponding in order to the morph-target image layers. */
    void set_morph_target_names(std::vector<std::string> names);
    /** @brief Fallible form of `set_morph_target_names`. */
    std::expected<void, MeshAccessError> try_set_morph_target_names(std::vector<std::string> names);
    /** @brief Consume this mesh and set its morph-target names. */
    Mesh with_morph_target_names(std::vector<std::string> names) &&;
    /** @brief Fallible consuming form of `with_morph_target_names`. */
    std::expected<Mesh, MeshAccessError> try_with_morph_target_names(std::vector<std::string> names) &&;
    /** @brief Retrieve the morph-target names, or no value when absent. */
    std::optional<std::span<const std::string>> morph_target_names() const;
    /** @brief Fallible form of `morph_target_names`. */
    std::expected<std::optional<std::span<const std::string>>, MeshAccessError> try_morph_target_names() const;

    /** @brief Count vertices (not indices) in the mesh. */
    std::size_t count_vertices() const {
        std::optional<std::size_t> count;
        const auto stored_attributes = _attributes.as_ref();
        if (!stored_attributes) throw_access_error(stored_attributes.error());
        for (const auto& entry : stored_attributes->get()) {
            const auto& attribute_data = entry.second;
            std::size_t attribute_count = attribute_data.values.len();
            if (count.has_value() && attribute_count != *count) {
                spdlog::warn("Mesh::count_vertices(): {} has a different vertex count ({}) than other attributes ({}) in this "
                             "mesh, all attributes will be truncated to match the smallest.",
                             attribute_data.attribute.name, attribute_count, *count);
            }
            count = count.has_value() ? std::min(*count, attribute_count) : attribute_count;
        }
        return count.value_or(0);
    };

    /** @brief Move the vertex/index payload into a render-world copy while
     * retaining this asset's metadata (Bevy `Mesh::take_gpu_data`). */
    std::expected<Mesh, MeshAccessError> take_gpu_data();

   private:
    friend class SerializedMesh;
    friend class MeshDeserializer;

    [[noreturn]] static void throw_access_error(MeshAccessError error);

    wgpu::PrimitiveTopology primitive_type;
    detail::MeshExtractableData<AttributeMap> _attributes;
    detail::MeshExtractableData<Indices> _indices;
    detail::MeshExtractableData<assets::Handle<image::Image>> _morph_targets;
    detail::MeshExtractableData<std::vector<std::string>> _morph_target_names;
};
/** @brief Create a circle mesh centered at origin with given radius.
 * @param segment_count Number of line segments; auto-calculated if not provided.
 */
EPIX_EXPORT EPIX_API Mesh make_circle(float radius,
                                      std::optional<glm::vec4> color             = std::nullopt,
                                      std::optional<std::uint32_t> segment_count = std::nullopt);
/** @brief Create a box mesh on the XY plane. */
EPIX_EXPORT EPIX_API Mesh make_box2d(float width, float height, std::optional<glm::vec4> color = std::nullopt);
/** @brief Create a box mesh on the XY plane with UV coordinates. */
EPIX_EXPORT EPIX_API Mesh make_box2d_uv(float width,
                                        float height,
                                        glm::vec4 uv_rect                     = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                                        std::optional<glm::vec4> vertex_color = std::nullopt);

/** @brief Plugin that registers mesh asset loading and GPU upload systems. */
EPIX_EXPORT struct EPIX_API MeshPlugin {
    void attach(app::App& app);
};
}  // namespace epix::mesh
