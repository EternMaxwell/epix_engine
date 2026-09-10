#include <array>
#include <epix/mesh/vertex_attribute_values.hpp>
#include <format>

namespace epix::mesh {
namespace {

template <typename Array, typename Vector>
std::vector<Array> vectors_to_vertex_arrays(std::vector<Vector>&& source) {
    std::vector<Array> result;
    result.reserve(source.size());
    for (const auto& value : source) {
        if constexpr (std::tuple_size_v<Array> == 2) {
            result.push_back({value.x, value.y});
        } else if constexpr (std::tuple_size_v<Array> == 3) {
            result.push_back({value.x, value.y, value.z});
        } else {
            result.push_back({value.x, value.y, value.z, value.w});
        }
    }
    return result;
}

}  // namespace

std::string FromVertexAttributeError::to_string() const {
    return std::format("cannot convert VertexAttributeValues::{} to {}", variant, into);
}

VertexAttributeValues::VertexAttributeValues(std::vector<glm::vec2> values)
    : VertexAttributeValues(vectors_to_vertex_arrays<Float32x2Value>(std::move(values))) {}
VertexAttributeValues::VertexAttributeValues(std::vector<glm::vec3> values)
    : VertexAttributeValues(vectors_to_vertex_arrays<Float32x3Value>(std::move(values))) {}
VertexAttributeValues::VertexAttributeValues(std::vector<glm::vec4> values)
    : VertexAttributeValues(vectors_to_vertex_arrays<Float32x4Value>(std::move(values))) {}
VertexAttributeValues::VertexAttributeValues(std::vector<glm::ivec2> values)
    : VertexAttributeValues(vectors_to_vertex_arrays<Sint32x2Value>(std::move(values))) {}
VertexAttributeValues::VertexAttributeValues(std::vector<glm::ivec3> values)
    : VertexAttributeValues(vectors_to_vertex_arrays<Sint32x3Value>(std::move(values))) {}
VertexAttributeValues::VertexAttributeValues(std::vector<glm::ivec4> values)
    : VertexAttributeValues(vectors_to_vertex_arrays<Sint32x4Value>(std::move(values))) {}
VertexAttributeValues::VertexAttributeValues(std::vector<glm::uvec2> values)
    : VertexAttributeValues(vectors_to_vertex_arrays<Uint32x2Value>(std::move(values))) {}
VertexAttributeValues::VertexAttributeValues(std::vector<glm::uvec3> values)
    : VertexAttributeValues(vectors_to_vertex_arrays<Uint32x3Value>(std::move(values))) {}
VertexAttributeValues::VertexAttributeValues(std::vector<glm::uvec4> values)
    : VertexAttributeValues(vectors_to_vertex_arrays<Uint32x4Value>(std::move(values))) {}

std::size_t VertexAttributeValues::len() const noexcept {
    return std::visit([](const auto& alternative) { return alternative.values.size(); }, values_);
}

std::string_view VertexAttributeValues::enum_variant_name() const noexcept {
    static constexpr std::array<std::string_view, 28> names{
        "Float32",  "Sint32",    "Uint32",    "Float32x2", "Sint32x2", "Uint32x2",  "Float32x3",
        "Sint32x3", "Uint32x3",  "Float32x4", "Sint32x4",  "Uint32x4", "Sint16x2",  "Snorm16x2",
        "Uint16x2", "Unorm16x2", "Sint16x4",  "Snorm16x4", "Uint16x4", "Unorm16x4", "Sint8x2",
        "Snorm8x2", "Uint8x2",   "Unorm8x2",  "Sint8x4",   "Snorm8x4", "Uint8x4",   "Unorm8x4",
    };
    return names[values_.index()];
}

wgpu::VertexFormat VertexAttributeValues::format() const noexcept {
    static constexpr std::array<wgpu::VertexFormat, 28> formats{
        wgpu::VertexFormat::eFloat32,   wgpu::VertexFormat::eSint32,    wgpu::VertexFormat::eUint32,
        wgpu::VertexFormat::eFloat32x2, wgpu::VertexFormat::eSint32x2,  wgpu::VertexFormat::eUint32x2,
        wgpu::VertexFormat::eFloat32x3, wgpu::VertexFormat::eSint32x3,  wgpu::VertexFormat::eUint32x3,
        wgpu::VertexFormat::eFloat32x4, wgpu::VertexFormat::eSint32x4,  wgpu::VertexFormat::eUint32x4,
        wgpu::VertexFormat::eSint16x2,  wgpu::VertexFormat::eSnorm16x2, wgpu::VertexFormat::eUint16x2,
        wgpu::VertexFormat::eUnorm16x2, wgpu::VertexFormat::eSint16x4,  wgpu::VertexFormat::eSnorm16x4,
        wgpu::VertexFormat::eUint16x4,  wgpu::VertexFormat::eUnorm16x4, wgpu::VertexFormat::eSint8x2,
        wgpu::VertexFormat::eSnorm8x2,  wgpu::VertexFormat::eUint8x2,   wgpu::VertexFormat::eUnorm8x2,
        wgpu::VertexFormat::eSint8x4,   wgpu::VertexFormat::eSnorm8x4,  wgpu::VertexFormat::eUint8x4,
        wgpu::VertexFormat::eUnorm8x4,
    };
    return formats[values_.index()];
}

std::span<const std::uint8_t> VertexAttributeValues::get_bytes() const noexcept {
    return std::visit(
        [](const auto& alternative) {
            using Element = typename std::remove_cvref_t<decltype(alternative)>::value_type;
            return std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(alternative.values.data()),
                                                 alternative.values.size() * sizeof(Element)};
        },
        values_);
}

}  // namespace epix::mesh
