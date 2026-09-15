

#include <array>
#include <cassert>
#include <cmath>
#include <epix/assets.hpp>
#include <epix/mesh.hpp>
#include <limits>
#include <span>
#include <unordered_set>
#include <vector>

using namespace epix::mesh;
namespace ecs = epix::ecs;

std::optional<Triangle3d> detail::MeshTriangleAt::operator()(std::size_t triangle_index) const {
    const auto index_at = [this](std::size_t index) -> std::size_t {
        if (const auto* values = indices->as_u16()) return (*values)[index];
        return (*indices->as_u32())[index];
    };

    const auto first_index = topology == wgpu::PrimitiveTopology::eTriangleList ? triangle_index * 3 : triangle_index;
    std::array triangle_indices{
        index_at(first_index),
        index_at(first_index + 1),
        index_at(first_index + 2),
    };
    if (topology == wgpu::PrimitiveTopology::eTriangleStrip && triangle_index % 2 != 0) {
        std::swap(triangle_indices[0], triangle_indices[1]);
    }
    if (std::ranges::any_of(triangle_indices, [this](std::size_t index) { return index >= vertices->size(); })) {
        return std::nullopt;
    }

    Triangle3d triangle;
    for (std::size_t vertex_index = 0; vertex_index < triangle.vertices.size(); ++vertex_index) {
        const auto& components          = (*vertices)[triangle_indices[vertex_index]];
        triangle.vertices[vertex_index] = glm::vec3{components[0], components[1], components[2]};
    }
    return triangle;
}

Mesh::Mesh(const Mesh& other)
    : asset_usage(other.asset_usage), final_aabb(other.final_aabb), primitive_type(other.primitive_type) {
    const auto source_attributes = other._attributes.as_ref_option();
    if (!source_attributes) {
        _attributes = detail::MeshExtractableData<AttributeMap>::extracted_to_render_world();
    } else if (*source_attributes) {
        _attributes = detail::MeshExtractableData<AttributeMap>::data(source_attributes->value().get());
    }

    const auto source_indices = other._indices.as_ref_option();
    if (!source_indices) {
        _indices = detail::MeshExtractableData<Indices>::extracted_to_render_world();
    } else if (*source_indices) {
        _indices = detail::MeshExtractableData<Indices>::data(source_indices->value().get());
    }

    const auto source_morph_targets = other._morph_targets.as_ref_option();
    if (!source_morph_targets) {
        _morph_targets = detail::MeshExtractableData<assets::Handle<image::Image>>::extracted_to_render_world();
    } else if (*source_morph_targets) {
        _morph_targets =
            detail::MeshExtractableData<assets::Handle<image::Image>>::data(source_morph_targets->value().get());
    }

    const auto source_morph_target_names = other._morph_target_names.as_ref_option();
    if (!source_morph_target_names) {
        _morph_target_names = detail::MeshExtractableData<std::vector<std::string>>::extracted_to_render_world();
    } else if (*source_morph_target_names) {
        _morph_target_names =
            detail::MeshExtractableData<std::vector<std::string>>::data(source_morph_target_names->value().get());
    }
}

Mesh& Mesh::operator=(const Mesh& other) {
    if (this == std::addressof(other)) return *this;
    Mesh copy(other);
    *this = std::move(copy);
    return *this;
}

std::expected<MeshTriangles, MeshTrianglesError> Mesh::triangles() const {
    const auto position_data = try_attribute(ATTRIBUTE_POSITION);
    if (!position_data) return std::unexpected(MeshTrianglesError{position_data.error()});

    const auto* vertices = position_data->get().as_float3();
    if (!vertices) {
        return std::unexpected(MeshTrianglesError{mesh_triangles_error::PositionsFormat{}});
    }

    const auto index_data = try_indices();
    if (!index_data) return std::unexpected(MeshTrianglesError{index_data.error()});

    if (primitive_type != wgpu::PrimitiveTopology::eTriangleList &&
        primitive_type != wgpu::PrimitiveTopology::eTriangleStrip) {
        return std::unexpected(MeshTrianglesError{mesh_triangles_error::WrongTopology{}});
    }

    const auto triangle_count = primitive_type == wgpu::PrimitiveTopology::eTriangleList ? index_data->get().len() / 3
                                : index_data->get().len() >= 3                           ? index_data->get().len() - 2
                                                                                         : 0;
    return detail::mesh_triangles_view(*vertices, index_data->get(), primitive_type, triangle_count);
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

std::optional<std::reference_wrapper<const VertexAttributeValues>> Mesh::attribute(
    const MeshVertexAttribute& descriptor) const {
    return attribute(descriptor.id);
}

std::optional<std::reference_wrapper<const VertexAttributeValues>> Mesh::attribute(MeshVertexAttributeId id) const {
    auto result = try_attribute_option(id);
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<std::reference_wrapper<const VertexAttributeValues>, MeshAccessError> Mesh::try_attribute(
    const MeshVertexAttribute& descriptor) const {
    return try_attribute(descriptor.id);
}

std::expected<std::reference_wrapper<const VertexAttributeValues>, MeshAccessError> Mesh::try_attribute(
    MeshVertexAttributeId id) const {
    auto result = try_attribute_option(id);
    if (!result) return std::unexpected(result.error());
    if (!*result) return std::unexpected(MeshAccessError::NotFound);
    return result->value();
}

std::expected<std::optional<std::reference_wrapper<const VertexAttributeValues>>, MeshAccessError>
Mesh::try_attribute_option(const MeshVertexAttribute& descriptor) const {
    return try_attribute_option(descriptor.id);
}

std::expected<std::optional<std::reference_wrapper<const VertexAttributeValues>>, MeshAccessError>
Mesh::try_attribute_option(MeshVertexAttributeId id) const {
    const auto stored_attributes = _attributes.as_ref();
    if (!stored_attributes) return std::unexpected(stored_attributes.error());
    const auto it = stored_attributes->get().find(id);
    if (it == stored_attributes->get().end()) return std::nullopt;
    return std::optional{std::cref(it->second.values)};
}

std::optional<std::reference_wrapper<VertexAttributeValues>> Mesh::attribute_mut(
    const MeshVertexAttribute& descriptor) {
    return attribute_mut(descriptor.id);
}

std::optional<std::reference_wrapper<VertexAttributeValues>> Mesh::attribute_mut(MeshVertexAttributeId id) {
    auto result = try_attribute_mut_option(id);
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<std::reference_wrapper<VertexAttributeValues>, MeshAccessError> Mesh::try_attribute_mut(
    const MeshVertexAttribute& descriptor) {
    return try_attribute_mut(descriptor.id);
}

std::expected<std::reference_wrapper<VertexAttributeValues>, MeshAccessError> Mesh::try_attribute_mut(
    MeshVertexAttributeId id) {
    auto result = try_attribute_mut_option(id);
    if (!result) return std::unexpected(result.error());
    if (!*result) return std::unexpected(MeshAccessError::NotFound);
    return result->value();
}

std::expected<std::optional<std::reference_wrapper<VertexAttributeValues>>, MeshAccessError>
Mesh::try_attribute_mut_option(const MeshVertexAttribute& descriptor) {
    return try_attribute_mut_option(descriptor.id);
}

std::expected<std::optional<std::reference_wrapper<VertexAttributeValues>>, MeshAccessError>
Mesh::try_attribute_mut_option(MeshVertexAttributeId id) {
    auto stored_attributes = _attributes.as_mut();
    if (!stored_attributes) return std::unexpected(stored_attributes.error());
    auto it = stored_attributes->get().find(id);
    if (it == stored_attributes->get().end()) return std::nullopt;
    return std::optional{std::ref(it->second.values)};
}

std::optional<VertexAttributeValues> Mesh::remove_attribute(const MeshVertexAttribute& descriptor) {
    return remove_attribute(descriptor.id);
}

std::optional<VertexAttributeValues> Mesh::remove_attribute(MeshVertexAttributeId id) {
    auto stored_attributes = _attributes.as_mut();
    if (!stored_attributes) throw_access_error(stored_attributes.error());
    auto it = stored_attributes->get().find(id);
    if (it == stored_attributes->get().end()) return std::nullopt;
    VertexAttributeValues values = std::move(it->second.values);
    stored_attributes->get().erase(it);
    return values;
}

std::expected<VertexAttributeValues, MeshAccessError> Mesh::try_remove_attribute(
    const MeshVertexAttribute& descriptor) {
    return try_remove_attribute(descriptor.id);
}

std::expected<VertexAttributeValues, MeshAccessError> Mesh::try_remove_attribute(MeshVertexAttributeId id) {
    auto stored_attributes = _attributes.as_mut();
    if (!stored_attributes) return std::unexpected(stored_attributes.error());
    auto it = stored_attributes->get().find(id);
    if (it == stored_attributes->get().end()) return std::unexpected(MeshAccessError::NotFound);
    VertexAttributeValues values = std::move(it->second.values);
    stored_attributes->get().erase(it);
    return values;
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

std::expected<std::optional<std::reference_wrapper<const Indices>>, MeshAccessError> Mesh::try_indices_option() const {
    return _indices.as_ref_option();
}

std::optional<std::reference_wrapper<Indices>> Mesh::indices_mut() {
    auto result = try_indices_mut_option();
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<std::reference_wrapper<Indices>, MeshAccessError> Mesh::try_indices_mut() { return _indices.as_mut(); }

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
        std::visit(
            [&](auto& alternative) {
                using Element = typename std::remove_cvref_t<decltype(alternative)>::value_type;
                std::vector<Element> duplicated;
                duplicated.reserve(removed_indices->value().len());
                for (const auto index : removed_indices->value().iter()) {
                    duplicated.push_back(alternative.values.at(index));
                }
                alternative.values = std::move(duplicated);
            },
            attribute.values.variant());
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
                return std::unexpected(MeshWindingInvertError{mesh_winding_invert_error::AbruptIndicesEnd{}});
            }
            for (std::size_t index = 0; index < indices.size(); index += 3) {
                std::swap(indices[index + 1], indices[index + 2]);
            }
            return {};
        case wgpu::PrimitiveTopology::eLineList:
            if (indices.size() % 2 != 0) {
                return std::unexpected(MeshWindingInvertError{mesh_winding_invert_error::AbruptIndicesEnd{}});
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

std::expected<void, MeshMergeError> Mesh::merge(const Mesh& other) {
    // Two meshes of different topologies would most likely produce an invalid
    // result, so this is rejected before anything is appended.
    if (primitive_type != other.primitive_type) {
        return std::unexpected(MeshMergeError{mesh_merge_error::IncompatiblePrimitiveTopology{
            .self_primitive_topology  = primitive_type,
            .other_primitive_topology = other.primitive_type,
        }});
    }

    // The indices of `other` start after this mesh's last vertex.
    const std::size_t index_offset = count_vertices();

    auto attributes = try_attributes_mut();
    if (!attributes) return std::unexpected(MeshMergeError{attributes.error()});
    for (auto&& [attribute, values] : *attributes) {
        auto other_values = other.try_attribute_option(attribute.id);
        if (!other_values) return std::unexpected(MeshMergeError{other_values.error()});
        if (!*other_values) continue;

        const auto& other_attribute_values = other_values->value().get();
        const bool merged                  = std::visit(
            [&other_attribute_values](auto& alternative) {
                using Alternative             = std::remove_cvref_t<decltype(alternative)>;
                const auto* other_alternative = std::get_if<Alternative>(&other_attribute_values.variant());
                if (other_alternative == nullptr) return false;
                alternative.values.insert(alternative.values.end(), other_alternative->values.begin(),
                                          other_alternative->values.end());
                return true;
            },
            values.variant());
        if (!merged) {
            // The other mesh's descriptor for this ID, as Bevy reports it. Only the type differs by
            // construction, so the layout lookup always finds it.
            const MeshAttributeLayout other_layout = other.attribute_layout();
            const auto other_attribute             = other_layout.get_attribute(attribute.id);
            return std::unexpected(MeshMergeError{mesh_merge_error::IncompatibleVertexAttributes{
                .self_attribute = attribute,
                .other_attribute =
                    other_attribute ? std::optional<MeshVertexAttribute>{other_attribute->get()} : std::nullopt,
            }});
        }
    }

    auto indices = try_indices_mut_option();
    if (!indices) return std::unexpected(MeshMergeError{indices.error()});
    auto other_indices = other.try_indices_option();
    if (!other_indices) return std::unexpected(MeshMergeError{other_indices.error()});
    if (*indices && *other_indices) {
        indices->value().get().extend(other_indices->value().get().iter() |
                                      std::views::transform([index_offset](std::size_t index) {
                                          return static_cast<std::uint32_t>(index + index_offset);
                                      }));
    }
    return {};
}

void Mesh::translate_by(glm::vec3 translation) {
    auto result = try_translate_by(translation);
    if (!result) throw_access_error(result.error());
}

std::expected<void, MeshAccessError> Mesh::try_translate_by(glm::vec3 translation) {
    if (translation == glm::vec3{0.0f}) return {};

    auto positions = try_attribute_mut_option(Mesh::ATTRIBUTE_POSITION);
    if (!positions) return std::unexpected(positions.error());
    if (!*positions) return {};

    auto* values = positions->value().get().get_if<VertexAttributeValues::Float32x3>();
    if (values == nullptr) return {};
    for (auto& position : *values) {
        const glm::vec3 translated = glm::vec3{position[0], position[1], position[2]} + translation;
        position                   = {translated.x, translated.y, translated.z};
    }
    return {};
}

Mesh Mesh::translated_by(glm::vec3 translation) && {
    translate_by(translation);
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::try_translated_by(glm::vec3 translation) && {
    auto result = try_translate_by(translation);
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

namespace {
/** @brief Scale and renormalize an already normalized normal by the reciprocal
 * scale (Bevy's `mesh::scale_normal`). */
glm::vec3 scale_normal(glm::vec3 normal, glm::vec3 scale_reciprocal) {
    // This is `normal * scale_reciprocal`, but `0. * anything == 0.` is applied
    // first because components of the reciprocal may be infinities, which do not
    // multiply to zero.
    glm::vec3 scaled{normal.x == 0.0f ? 0.0f : normal.x * scale_reciprocal.x,
                     normal.y == 0.0f ? 0.0f : normal.y * scale_reciprocal.y,
                     normal.z == 0.0f ? 0.0f : normal.z * scale_reciprocal.z};

    // If `scaled` is finite, no component of the reciprocal was infinite or the
    // normal was perpendicular to the scale. Otherwise the scale had at least one
    // zero component and the normal must point along that component.
    if (std::isfinite(scaled.x) && std::isfinite(scaled.y) && std::isfinite(scaled.z)) {
        return detail::normalize_or_zero(scaled);
    }

    glm::vec3 selected{std::isinf(std::abs(scaled.x)) ? std::copysign(1.0f, scaled.x) : 0.0f,
                       std::isinf(std::abs(scaled.y)) ? std::copysign(1.0f, scaled.y) : 0.0f,
                       std::isinf(std::abs(scaled.z)) ? std::copysign(1.0f, scaled.z) : 0.0f};
    return selected / glm::length(selected);
}

/** @brief `glam::f32::math::acos_approx` (glam 0.30 `f32/math.rs`): the DirectXMath
 * `XMScalarAcos` 7-degree minimax approximation of `clamp(value, -1, 1).acos()`.
 *
 * Ported because `Quat::is_near_identity` is defined in terms of it, and the threshold only
 * makes sense against the approximated angle. */
float acos_approx(float value) {
    const bool nonnegative = value >= 0.0f;
    const float x          = std::abs(value);
    float omx              = 1.0f - x;
    if (omx < 0.0f) omx = 0.0f;
    const float root = std::sqrt(omx);

    float result = -0.0012624911f;
    result       = result * x + 0.00667009f;
    result       = result * x - 0.017088126f;
    result       = result * x + 0.03089188f;
    result       = result * x - 0.050174303f;
    result       = result * x + 0.08897899f;
    result       = result * x - 0.2145988f;
    result       = result * x + 1.5707963f;
    result *= root;

    return nonnegative ? result : glm::pi<float>() - result;
}

/** @brief `Vec3::from([f32; 3])`. */
glm::vec3 to_vec3(const VertexAttributeValues::Float32x3Value& value) {
    return glm::vec3{value[0], value[1], value[2]};
}

/** @brief `glam::Vec3::angle_between`: the angle between two non-zero vectors, computed through
 * the same approximation `acos_approx` applies (and therefore clamped the same way). */
float angle_between(glm::vec3 lhs, glm::vec3 rhs) {
    return acos_approx(glm::dot(lhs, rhs) / std::sqrt(glm::dot(lhs, lhs) * glm::dot(rhs, rhs)));
}

/** @brief `glam::Quat::is_near_identity` (glam 0.30 `f32/quat.rs`): the shortest-path angle of
 * `rotation` against `THRESHOLD_ANGLE`, using the same approximation glam uses.
 *
 * Forcing the angle to the shortest path (through `abs(w)`) is what makes a quaternion close to
 * `-1` count as a near-identity rotation. */
bool is_near_identity(glm::quat rotation) {
    constexpr float kThresholdAngle = 0.0028471446f;
    return acos_approx(std::abs(rotation.w)) * 2.0f < kThresholdAngle;
}
}  // namespace

void Mesh::scale_by(glm::vec3 scale) {
    auto result = try_scale_by(scale);
    if (!result) throw_access_error(result.error());
}

std::expected<void, MeshAccessError> Mesh::try_scale_by(glm::vec3 scale) {
    // Needed when transforming normals and tangents.
    const glm::vec3 scale_reciprocal{1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z};
    // The scale cannot be zero on more than one axis, as in Bevy.
    assert(glm::vec3(scale.y, scale.z, scale.x) * glm::vec3(scale.z, scale.x, scale.y) != glm::vec3(0.0f));

    auto positions = try_attribute_mut_option(Mesh::ATTRIBUTE_POSITION);
    if (!positions) return std::unexpected(positions.error());
    if (*positions) {
        if (auto* values = positions->value().get().get_if<VertexAttributeValues::Float32x3>()) {
            for (auto& position : *values) {
                const glm::vec3 scaled = glm::vec3{position[0], position[1], position[2]} * scale;
                position               = {scaled.x, scaled.y, scaled.z};
            }
        }
    }

    // Normals and tangents do not need transforming when the scale is uniform.
    if (scale.x == scale.y && scale.y == scale.z) return {};

    auto normals = try_attribute_mut_option(Mesh::ATTRIBUTE_NORMAL);
    if (!normals) return std::unexpected(normals.error());
    if (*normals) {
        if (auto* values = normals->value().get().get_if<VertexAttributeValues::Float32x3>()) {
            for (auto& normal : *values) {
                const glm::vec3 transformed =
                    scale_normal(glm::vec3{normal[0], normal[1], normal[2]}, scale_reciprocal);
                normal = {transformed.x, transformed.y, transformed.z};
            }
        }
    }

    auto tangents = try_attribute_mut_option(Mesh::ATTRIBUTE_TANGENT);
    if (!tangents) return std::unexpected(tangents.error());
    if (*tangents) {
        if (auto* values = tangents->value().get().get_if<VertexAttributeValues::Float32x4>()) {
            for (auto& tangent : *values) {
                const float handedness = tangent[3];
                glm::vec3 scaled_tangent{tangent[0] * scale[0], tangent[1] * scale[1], tangent[2] * scale[2]};
                scaled_tangent = detail::normalize_or_zero(scaled_tangent);
                tangent        = {scaled_tangent.x, scaled_tangent.y, scaled_tangent.z, handedness};
            }
        }
    }
    return {};
}

Mesh Mesh::scaled_by(glm::vec3 scale) && {
    scale_by(scale);
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::try_scaled_by(glm::vec3 scale) && {
    auto result = try_scale_by(scale);
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

void Mesh::rotate_by(glm::quat rotation) {
    auto result = try_rotate_by(rotation);
    if (!result) throw_access_error(result.error());
}

std::expected<void, MeshAccessError> Mesh::try_rotate_by(glm::quat rotation) {
    auto positions = try_attribute_mut_option(Mesh::ATTRIBUTE_POSITION);
    if (!positions) return std::unexpected(positions.error());
    if (*positions) {
        if (auto* values = positions->value().get().get_if<VertexAttributeValues::Float32x3>()) {
            for (auto& position : *values) {
                const glm::vec3 rotated = rotation * glm::vec3{position[0], position[1], position[2]};
                position                = {rotated.x, rotated.y, rotated.z};
            }
        }
    }

    // Normals and tangents do not need transforming for a near-identity rotation.
    if (is_near_identity(rotation)) return {};

    auto normals = try_attribute_mut_option(Mesh::ATTRIBUTE_NORMAL);
    if (!normals) return std::unexpected(normals.error());
    if (*normals) {
        if (auto* values = normals->value().get().get_if<VertexAttributeValues::Float32x3>()) {
            for (auto& normal : *values) {
                const glm::vec3 source  = detail::normalize_or_zero(glm::vec3{normal[0], normal[1], normal[2]});
                const glm::vec3 rotated = rotation * source;
                normal                  = {rotated.x, rotated.y, rotated.z};
            }
        }
    }

    auto tangents = try_attribute_mut_option(Mesh::ATTRIBUTE_TANGENT);
    if (!tangents) return std::unexpected(tangents.error());
    if (*tangents) {
        if (auto* values = tangents->value().get().get_if<VertexAttributeValues::Float32x4>()) {
            for (auto& tangent : *values) {
                const float handedness  = tangent[3];
                const glm::vec3 source  = detail::normalize_or_zero(glm::vec3{tangent[0], tangent[1], tangent[2]});
                const glm::vec3 rotated = rotation * source;
                tangent                 = {rotated.x, rotated.y, rotated.z, handedness};
            }
        }
    }
    return {};
}

Mesh Mesh::rotated_by(glm::quat rotation) && {
    rotate_by(rotation);
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::try_rotated_by(glm::quat rotation) && {
    auto result = try_rotate_by(rotation);
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

void Mesh::transform_by(const epix::transform::Transform& transform) {
    auto result = try_transform_by(transform);
    if (!result) throw_access_error(result.error());
}

std::expected<void, MeshAccessError> Mesh::try_transform_by(const epix::transform::Transform& transform) {
    // Needed when transforming normals and tangents.
    const glm::vec3 scale_reciprocal{1.0f / transform.scaler.x, 1.0f / transform.scaler.y, 1.0f / transform.scaler.z};
    // The scale cannot be zero on more than one axis, as in Bevy.
    assert(glm::vec3(transform.scaler.y, transform.scaler.z, transform.scaler.x) *
               glm::vec3(transform.scaler.z, transform.scaler.x, transform.scaler.y) !=
           glm::vec3(0.0f));

    auto positions = try_attribute_mut_option(Mesh::ATTRIBUTE_POSITION);
    if (!positions) return std::unexpected(positions.error());
    if (*positions) {
        if (auto* values = positions->value().get().get_if<VertexAttributeValues::Float32x3>()) {
            for (auto& position : *values) {
                // `Transform::transform_point`: scale, then rotate, then translate.
                const glm::vec3 point{position[0], position[1], position[2]};
                const glm::vec3 scaled     = point * transform.scaler;
                const glm::vec3 rotated    = transform.rotation * scaled;
                const glm::vec3 translated = rotated + transform.translation;
                position                   = {translated.x, translated.y, translated.z};
            }
        }
    }

    // Normals and tangents do not need transforming when the rotation is near identity and the
    // scale is uniform.
    if (is_near_identity(transform.rotation) && transform.scaler.x == transform.scaler.y &&
        transform.scaler.y == transform.scaler.z) {
        return {};
    }

    auto normals = try_attribute_mut_option(Mesh::ATTRIBUTE_NORMAL);
    if (!normals) return std::unexpected(normals.error());
    if (*normals) {
        if (auto* values = normals->value().get().get_if<VertexAttributeValues::Float32x3>()) {
            for (auto& normal : *values) {
                const glm::vec3 transformed =
                    transform.rotation * scale_normal(glm::vec3{normal[0], normal[1], normal[2]}, scale_reciprocal);
                normal = {transformed.x, transformed.y, transformed.z};
            }
        }
    }

    auto tangents = try_attribute_mut_option(Mesh::ATTRIBUTE_TANGENT);
    if (!tangents) return std::unexpected(tangents.error());
    if (*tangents) {
        if (auto* values = tangents->value().get().get_if<VertexAttributeValues::Float32x4>()) {
            for (auto& tangent : *values) {
                const float handedness         = tangent[3];
                const glm::vec3 scaled_tangent = detail::normalize_or_zero(glm::vec3{tangent[0] * transform.scaler[0],
                                                                                     tangent[1] * transform.scaler[1],
                                                                                     tangent[2] * transform.scaler[2]});
                const glm::vec3 rotated        = transform.rotation * scaled_tangent;
                tangent                        = {rotated.x, rotated.y, rotated.z, handedness};
            }
        }
    }
    return {};
}

Mesh Mesh::transformed_by(const epix::transform::Transform& transform) && {
    transform_by(transform);
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::try_transformed_by(const epix::transform::Transform& transform) && {
    auto result = try_transform_by(transform);
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

void Mesh::compute_normals() {
    auto result = try_compute_normals();
    if (!result) throw_access_error(result.error());
}

std::expected<void, MeshAccessError> Mesh::try_compute_normals() {
    assert(primitive_type == wgpu::PrimitiveTopology::eTriangleList &&
           "`compute_normals` can only work on `TriangleList`s");
    auto stored_indices = try_indices_option();
    if (!stored_indices) return std::unexpected(stored_indices.error());
    if (!stored_indices->has_value()) return try_compute_flat_normals();
    return try_compute_smooth_normals();
}

void Mesh::compute_flat_normals() {
    auto result = try_compute_flat_normals();
    if (!result) throw_access_error(result.error());
}

std::expected<void, MeshAccessError> Mesh::try_compute_flat_normals() {
    auto stored_indices = try_indices_option();
    if (!stored_indices) return std::unexpected(stored_indices.error());
    assert(!stored_indices->has_value() &&
           "`compute_flat_normals` can't work on indexed geometry. Consider calling either "
           "`Mesh::compute_smooth_normals` or `Mesh::duplicate_vertices` followed by "
           "`Mesh::compute_flat_normals`.");
    assert(primitive_type == wgpu::PrimitiveTopology::eTriangleList &&
           "`compute_flat_normals` can only work on `TriangleList`s");

    auto position_data = try_attribute(Mesh::ATTRIBUTE_POSITION);
    if (!position_data) return std::unexpected(position_data.error());
    const auto* positions = position_data->get().as_float3();
    assert(positions != nullptr && "`Mesh::ATTRIBUTE_POSITION` vertex attributes should be of type `float3`");

    // One normal per face, repeated for that face's three vertices.
    std::vector<glm::vec3> normals;
    normals.reserve(positions->size());
    for (std::size_t offset = 0; offset + 3 <= positions->size(); offset += 3) {
        const glm::vec3 normal =
            triangle_normal((*positions)[offset], (*positions)[offset + 1], (*positions)[offset + 2]);
        normals.insert(normals.end(), 3, normal);
    }

    return try_insert_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals));
}

void Mesh::compute_smooth_normals() {
    auto result = try_compute_smooth_normals();
    if (!result) throw_access_error(result.error());
}

std::expected<void, MeshAccessError> Mesh::try_compute_smooth_normals() {
    return try_compute_custom_smooth_normals([](std::array<std::size_t, 3> face,
                                                std::span<const VertexAttributeValues::Float32x3Value> positions,
                                                std::span<glm::vec3> normals) {
        const glm::vec3 pa = to_vec3(positions[face[0]]);
        const glm::vec3 pb = to_vec3(positions[face[1]]);
        const glm::vec3 pc = to_vec3(positions[face[2]]);

        const glm::vec3 ab = pb - pa;
        const glm::vec3 ba = pa - pb;
        const glm::vec3 bc = pc - pb;
        const glm::vec3 cb = pb - pc;
        const glm::vec3 ca = pa - pc;
        const glm::vec3 ac = pc - pa;

        // Weight each corner by the angle it subtends, so triangle area and triangle count
        // do not influence the result. Degenerate corners weigh nothing.
        constexpr float kEpsilon = std::numeric_limits<float>::epsilon();
        const float weight_a     = glm::dot(ab, ab) * glm::dot(ac, ac) > kEpsilon ? angle_between(ab, ac) : 0.0f;
        const float weight_b     = glm::dot(ba, ba) * glm::dot(bc, bc) > kEpsilon ? angle_between(ba, bc) : 0.0f;
        const float weight_c     = glm::dot(ca, ca) * glm::dot(cb, cb) > kEpsilon ? angle_between(ca, cb) : 0.0f;

        const glm::vec3 normal = triangle_normal(positions[face[0]], positions[face[1]], positions[face[2]]);

        normals[face[0]] += normal * weight_a;
        normals[face[1]] += normal * weight_b;
        normals[face[2]] += normal * weight_c;
    });
}

void Mesh::compute_area_weighted_normals() {
    auto result = try_compute_area_weighted_normals();
    if (!result) throw_access_error(result.error());
}

std::expected<void, MeshAccessError> Mesh::try_compute_area_weighted_normals() {
    return try_compute_custom_smooth_normals([](std::array<std::size_t, 3> face,
                                                std::span<const VertexAttributeValues::Float32x3Value> positions,
                                                std::span<glm::vec3> normals) {
        // The double-area face normal is already weighted by the triangle's area.
        const glm::vec3 normal = triangle_area_normal(positions[face[0]], positions[face[1]], positions[face[2]]);
        for (const std::size_t index : face) normals[index] += normal;
    });
}

Mesh Mesh::with_computed_normals() && {
    compute_normals();
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::try_with_computed_normals() && {
    auto result = try_compute_normals();
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

Mesh Mesh::with_computed_flat_normals() && {
    compute_flat_normals();
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::try_with_computed_flat_normals() && {
    auto result = try_compute_flat_normals();
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

Mesh Mesh::with_computed_smooth_normals() && {
    compute_smooth_normals();
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::try_with_computed_smooth_normals() && {
    auto result = try_compute_smooth_normals();
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

Mesh Mesh::with_computed_area_weighted_normals() && {
    compute_area_weighted_normals();
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::try_with_computed_area_weighted_normals() && {
    auto result = try_compute_area_weighted_normals();
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

void Mesh::normalize_joint_weights() {
    auto result = try_normalize_joint_weights();
    if (!result) throw_access_error(result.error());
}

std::expected<void, MeshAccessError> Mesh::try_normalize_joint_weights() {
    auto joints = try_attribute_mut_option(Mesh::ATTRIBUTE_JOINT_WEIGHT);
    if (!joints) return std::unexpected(joints.error());
    if (*joints) {
        if (auto* weights = joints->value().get().get_if<VertexAttributeValues::Float32x4>()) {
            for (auto& vertex_weights : *weights) {
                // Negative weights are forced to zero. `std::fmax` is the equivalent of Rust's
                // `f32::max`, which prefers the other operand over a NaN.
                for (float& weight : vertex_weights) weight = std::fmax(weight, 0.0f);

                const float sum = vertex_weights[0] + vertex_weights[1] + vertex_weights[2] + vertex_weights[3];
                if (sum == 0.0f) {
                    // All-zero weights are invalid, so the first joint takes the vertex.
                    vertex_weights[0] = 1.0f;
                } else {
                    const float reciprocal = 1.0f / sum;
                    for (float& weight : vertex_weights) weight *= reciprocal;
                }
            }
        }
    }
    return {};
}

bool Mesh::has_morph_targets() const {
    auto result = try_has_morph_targets();
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<bool, MeshAccessError> Mesh::try_has_morph_targets() const {
    auto result = _morph_targets.as_ref_option();
    if (!result) return std::unexpected(result.error());
    return result->has_value();
}

void Mesh::set_morph_targets(assets::Handle<image::Image> morph_targets) {
    auto result = try_set_morph_targets(std::move(morph_targets));
    if (!result) throw_access_error(result.error());
}

std::expected<void, MeshAccessError> Mesh::try_set_morph_targets(assets::Handle<image::Image> morph_targets) {
    auto result = _morph_targets.replace(std::optional{std::move(morph_targets)});
    if (!result) return std::unexpected(result.error());
    return {};
}

std::optional<std::reference_wrapper<const epix::assets::Handle<epix::image::Image>>> Mesh::morph_targets() const {
    auto result = _morph_targets.as_ref_option();
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<std::reference_wrapper<const epix::assets::Handle<epix::image::Image>>, MeshAccessError>
Mesh::try_morph_targets() const {
    return _morph_targets.as_ref();
}

Mesh Mesh::with_morph_targets(assets::Handle<image::Image> morph_targets) && {
    set_morph_targets(std::move(morph_targets));
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::try_with_morph_targets(assets::Handle<image::Image> morph_targets) && {
    auto result = try_set_morph_targets(std::move(morph_targets));
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

void Mesh::set_morph_target_names(std::vector<std::string> names) {
    auto result = try_set_morph_target_names(std::move(names));
    if (!result) throw_access_error(result.error());
}

std::expected<void, MeshAccessError> Mesh::try_set_morph_target_names(std::vector<std::string> names) {
    auto result = _morph_target_names.replace(std::optional{std::move(names)});
    if (!result) return std::unexpected(result.error());
    return {};
}

Mesh Mesh::with_morph_target_names(std::vector<std::string> names) && {
    set_morph_target_names(std::move(names));
    return std::move(*this);
}

std::expected<Mesh, MeshAccessError> Mesh::try_with_morph_target_names(std::vector<std::string> names) && {
    auto result = try_set_morph_target_names(std::move(names));
    if (!result) return std::unexpected(result.error());
    return std::move(*this);
}

std::optional<std::span<const std::string>> Mesh::morph_target_names() const {
    auto result = try_morph_target_names();
    if (!result) throw_access_error(result.error());
    return *result;
}

std::expected<std::optional<std::span<const std::string>>, MeshAccessError> Mesh::try_morph_target_names() const {
    auto result = _morph_target_names.as_ref_option();
    if (!result) return std::unexpected(result.error());
    if (!*result) return std::nullopt;
    return std::span<const std::string>{result->value().get()};
}

std::expected<Mesh, MeshAccessError> Mesh::take_gpu_data() {
    auto attributes = _attributes.extract();
    if (!attributes) return std::unexpected(attributes.error());
    auto indices = _indices.extract();
    if (!indices) return std::unexpected(indices.error());
    auto morph_targets = _morph_targets.extract();
    if (!morph_targets) return std::unexpected(morph_targets.error());
    auto morph_target_names = _morph_target_names.extract();
    if (!morph_target_names) return std::unexpected(morph_target_names.error());

    Mesh extracted{primitive_type, asset_usage};
    extracted._attributes         = std::move(attributes).value();
    extracted._indices            = std::move(indices).value();
    extracted._morph_targets      = std::move(morph_targets).value();
    extracted._morph_target_names = std::move(morph_target_names).value();
    extracted.final_aabb          = final_aabb;

    // Store the AABB extents as they cannot be computed after extraction. Bevy assigns them to
    // `self` and returns `Self { ..self.clone() }`, so both this mesh and the extracted copy keep
    // the bounds once the attribute data has moved to the render world.
    const auto position_data = extracted.try_attribute(Mesh::ATTRIBUTE_POSITION);
    if (position_data) {
        const auto* position_values = position_data->get().as_float3();
        if (position_values != nullptr && !position_values->empty()) {
            glm::vec3 minimum = to_vec3(position_values->front());
            glm::vec3 maximum = minimum;
            for (const auto& components : *position_values) {
                const glm::vec3 point = to_vec3(components);
                minimum               = glm::min(minimum, point);
                maximum               = glm::max(maximum, point);
            }
            final_aabb           = Aabb3d::from_min_max(minimum, maximum);
            extracted.final_aabb = final_aabb;
        }
    }
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
    assets::app_register_asset<SkinnedMeshInverseBindposes>(app);
    app.add_systems(app::PostUpdate, ecs::into(mark_3d_meshes_as_changed_if_their_assets_changed)
                                         .after(assets::AssetSystems::WriteEvents)
                                         .set_name("mark 3d meshes changed after mesh asset changes"));
}

}  // namespace epix::mesh
