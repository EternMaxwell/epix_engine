#include <algorithm>
#include <cassert>
#include <cmath>
#include <epix/mesh/primitives.hpp>
#include <numbers>
#include <stdexcept>

namespace epix::mesh {
namespace {
constexpr auto kDefaultMeshAssetUsage = static_cast<assets::RenderAssetUsages>(assets::RenderAssetUsages::MAIN_WORLD |
                                                                               assets::RenderAssetUsages::RENDER_WORLD);

glm::vec2 unit_from_angle(float angle) { return {std::cos(angle), std::sin(angle)}; }
}  // namespace

CircleMeshBuilder Circle::mesh() const noexcept { return CircleMeshBuilder{radius, 32}; }

float Arc2d::half_chord_length() const noexcept { return radius * std::sin(half_angle); }

float Arc2d::apothem() const noexcept {
    const float half_chord = half_chord_length();
    const float sign       = is_minor() ? 1.0f : -1.0f;
    return sign * std::sqrt(radius * radius - half_chord * half_chord);
}

glm::vec2 Arc2d::chord_midpoint() const noexcept { return {0.0f, apothem()}; }

CircularSectorMeshBuilder CircularSector::mesh() const noexcept { return CircularSectorMeshBuilder{*this}; }

CircularSegmentMeshBuilder CircularSegment::mesh() const noexcept { return CircularSegmentMeshBuilder{*this}; }

EllipseMeshBuilder Ellipse::mesh() const noexcept { return EllipseMeshBuilder{half_size.x, half_size.y, 32}; }

RegularPolygon::RegularPolygon(float circumradius, std::uint32_t sides) : circumcircle(circumradius), sides(sides) {
    if (!std::signbit(circumradius) && sides > 2) return;
    if (std::signbit(circumradius)) throw std::invalid_argument("polygon has a negative radius");
    throw std::invalid_argument("polygon has less than 3 sides");
}

RegularPolygonMeshBuilder RegularPolygon::mesh() const noexcept {
    return RegularPolygonMeshBuilder{circumcircle.radius, sides};
}

Rectangle Rectangle::from_corners(glm::vec2 first, glm::vec2 second) noexcept {
    return Rectangle{glm::abs(second - first) * 0.5f};
}

RectangleMeshBuilder Rectangle::mesh() const noexcept { return RectangleMeshBuilder{half_size}; }

Mesh CircleMeshBuilder::build() const { return EllipseMeshBuilder{circle.radius, circle.radius, resolution}.build(); }

Mesh CircularSectorMeshBuilder::build() const {
    const auto arc_vertex_count = static_cast<std::size_t>(resolution);
    std::vector<VertexAttributeValues::Float32x3Value> positions;
    std::vector<VertexAttributeValues::Float32x3Value> normals(arc_vertex_count + 1, {0.0f, 0.0f, 1.0f});
    std::vector<VertexAttributeValues::Float32x2Value> uvs;
    std::vector<std::uint32_t> indices;
    positions.reserve(arc_vertex_count + 1);
    uvs.reserve(arc_vertex_count + 1);
    if (resolution >= 1) indices.reserve(static_cast<std::size_t>(resolution - 1) * 3);

    positions.push_back({0.0f, 0.0f, 0.0f});
    uvs.push_back({0.5f, 0.5f});

    constexpr float half_pi = std::numbers::pi_v<float> * 0.5f;
    const float first_angle = half_pi - sector.half_angle();
    const float last_angle  = half_pi + sector.half_angle();
    const float last_index  = static_cast<float>(resolution - 1);
    const float uv_angle    = uv_mode.value.angle;
    for (std::uint32_t index = 0; index < resolution; ++index) {
        const float angle = std::lerp(first_angle, last_angle, static_cast<float>(index) / last_index);
        const auto vertex = sector.radius() * unit_from_angle(angle);
        const auto uv     = unit_from_angle(-(angle + uv_angle)) * 0.5f + glm::vec2(0.5f);
        positions.push_back({vertex.x, vertex.y, 0.0f});
        uvs.push_back({uv.x, uv.y});
    }
    for (std::uint32_t index = 1; index < resolution; ++index) {
        indices.insert(indices.end(), {0, index, index + 1});
    }

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs))
        .with_inserted_indices(Indices{std::move(indices)});
}

Mesh CircularSegmentMeshBuilder::build() const {
    const auto arc_vertex_count = static_cast<std::size_t>(resolution);
    std::vector<VertexAttributeValues::Float32x3Value> positions;
    std::vector<VertexAttributeValues::Float32x3Value> normals(arc_vertex_count + 1, {0.0f, 0.0f, 1.0f});
    std::vector<VertexAttributeValues::Float32x2Value> uvs;
    std::vector<std::uint32_t> indices;
    positions.reserve(arc_vertex_count + 1);
    uvs.reserve(arc_vertex_count + 1);
    if (resolution >= 1) indices.reserve(static_cast<std::size_t>(resolution - 1) * 3);

    const auto midpoint = segment.chord_midpoint();
    positions.push_back({midpoint.x, midpoint.y, 0.0f});

    constexpr float half_pi = std::numbers::pi_v<float> * 0.5f;
    const float uv_angle    = uv_mode.value.angle;
    const auto midpoint_uv =
        unit_from_angle(-uv_angle - half_pi) * (0.5f * segment.apothem() / segment.radius()) + glm::vec2(0.5f);
    uvs.push_back({midpoint_uv.x, midpoint_uv.y});

    const float first_angle = half_pi - segment.half_angle();
    const float last_angle  = half_pi + segment.half_angle();
    const float last_index  = static_cast<float>(resolution - 1);
    for (std::uint32_t index = 0; index < resolution; ++index) {
        const float angle = std::lerp(first_angle, last_angle, static_cast<float>(index) / last_index);
        const auto vertex = segment.radius() * unit_from_angle(angle);
        const auto uv     = unit_from_angle(-(angle + uv_angle)) * 0.5f + glm::vec2(0.5f);
        positions.push_back({vertex.x, vertex.y, 0.0f});
        uvs.push_back({uv.x, uv.y});
    }
    for (std::uint32_t index = 1; index < resolution; ++index) {
        indices.insert(indices.end(), {0, index, index + 1});
    }

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs))
        .with_inserted_indices(Indices{std::move(indices)});
}

Mesh EllipseMeshBuilder::build() const {
    const auto vertex_count = static_cast<std::size_t>(resolution);
    std::vector<VertexAttributeValues::Float32x3Value> positions;
    std::vector<VertexAttributeValues::Float32x3Value> normals(vertex_count, {0.0f, 0.0f, 1.0f});
    std::vector<VertexAttributeValues::Float32x2Value> uvs;
    std::vector<std::uint32_t> indices;
    positions.reserve(vertex_count);
    uvs.reserve(vertex_count);
    if (resolution >= 2) indices.reserve(static_cast<std::size_t>(resolution - 2) * 3);

    constexpr float start_angle = std::numbers::pi_v<float> * 0.5f;
    const float step            = std::numbers::pi_v<float> * 2.0f / static_cast<float>(resolution);
    for (std::uint32_t index = 0; index < resolution; ++index) {
        const float angle  = start_angle + static_cast<float>(index) * step;
        const float sine   = std::sin(angle);
        const float cosine = std::cos(angle);
        positions.push_back({cosine * ellipse.half_size.x, sine * ellipse.half_size.y, 0.0f});
        uvs.push_back({0.5f * (cosine + 1.0f), 1.0f - 0.5f * (sine + 1.0f)});
    }
    for (std::uint32_t index = 1; index + 1 < resolution; ++index) {
        indices.insert(indices.end(), {0, index, index + 1});
    }

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs))
        .with_inserted_indices(Indices{std::move(indices)});
}

RegularPolygonMeshBuilder::RegularPolygonMeshBuilder(float circumradius, std::uint32_t sides)
    : circumradius_(circumradius), sides_(sides) {
    assert(!std::signbit(circumradius) && "polygon has a negative radius");
    assert(sides > 2 && "polygon has less than 3 sides");
}

Mesh RegularPolygonMeshBuilder::build() const {
    return EllipseMeshBuilder{circumradius_, circumradius_, sides_}.build();
}

RectangleMeshBuilder::RectangleMeshBuilder(float width, float height) : half_size_(width * 0.5f, height * 0.5f) {
    assert(width >= 0.0f && "rectangle has a negative width");
    assert(height >= 0.0f && "rectangle has a negative height");
}

Mesh RectangleMeshBuilder::build() const {
    const auto half_width  = half_size_.x;
    const auto half_height = half_size_.y;
    std::vector<VertexAttributeValues::Float32x3Value> positions{
        {half_width, half_height, 0.0f},
        {-half_width, half_height, 0.0f},
        {-half_width, -half_height, 0.0f},
        {half_width, -half_height, 0.0f},
    };
    std::vector<VertexAttributeValues::Float32x3Value> normals(4, {0.0f, 0.0f, 1.0f});
    std::vector<VertexAttributeValues::Float32x2Value> uvs{{1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};
    std::vector<std::uint32_t> indices{0, 1, 2, 0, 2, 3};

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
}

}  // namespace epix::mesh
