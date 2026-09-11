#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstdint>
#include <epix/mesh/mesh.hpp>
#include <expected>
#include <glm/glm.hpp>
#include <numbers>
#include <span>
#include <string_view>
#include <utility>
#include <vector>
#endif

namespace epix::mesh {

class Polyline2dMeshBuilder;
class Polyline3dMeshBuilder;
struct AnnulusMeshBuilder;
struct Capsule2dMeshBuilder;
struct CircleMeshBuilder;
struct CircularSectorMeshBuilder;
struct CircularSegmentMeshBuilder;
struct ConeMeshBuilder;
struct ConicalFrustumMeshBuilder;
struct ConvexPolygonMeshBuilder;
struct CuboidMeshBuilder;
struct CylinderMeshBuilder;
struct EllipseMeshBuilder;
struct PlaneMeshBuilder;
struct RectangleMeshBuilder;
struct RegularPolygonMeshBuilder;
struct RhombusMeshBuilder;
struct Segment2dMeshBuilder;
struct Segment3dMeshBuilder;
struct TetrahedronMeshBuilder;
struct TorusMeshBuilder;
struct Triangle2dMeshBuilder;
struct Triangle3dMeshBuilder;

/** @brief Circle primitive accepted by `CircleMeshBuilder`. */
EPIX_EXPORT struct Circle {
    float radius = 0.5f;

    constexpr Circle() = default;
    constexpr explicit Circle(float radius) : radius(radius) {}
    constexpr float diameter() const noexcept { return 2.0f * radius; }
    CircleMeshBuilder mesh() const noexcept;

    bool operator==(const Circle&) const = default;
};

/** @brief Circular arc used by the sector and segment mesh inputs. */
EPIX_EXPORT struct Arc2d {
    float radius     = 0.5f;
    float half_angle = 2.0f * std::numbers::pi_v<float> / 3.0f;

    constexpr Arc2d() = default;
    constexpr Arc2d(float radius, float half_angle) : radius(radius), half_angle(half_angle) {}
    static constexpr Arc2d from_radians(float radius, float angle) { return Arc2d{radius, angle * 0.5f}; }
    static constexpr Arc2d from_degrees(float radius, float angle) {
        return from_radians(radius, angle * std::numbers::pi_v<float> / 180.0f);
    }
    static constexpr Arc2d from_turns(float radius, float fraction) {
        return Arc2d{radius, fraction * std::numbers::pi_v<float>};
    }
    constexpr float angle() const noexcept { return half_angle * 2.0f; }
    constexpr float length() const noexcept { return angle() * radius; }
    float half_chord_length() const noexcept;
    float chord_length() const noexcept { return 2.0f * half_chord_length(); }
    glm::vec2 chord_midpoint() const noexcept;
    float apothem() const noexcept;
    constexpr bool is_minor() const noexcept { return half_angle <= std::numbers::pi_v<float> * 0.5f; }

    bool operator==(const Arc2d&) const = default;
};

/** @brief Pie-slice primitive centered on the origin and symmetric about +Y. */
EPIX_EXPORT struct CircularSector {
    Arc2d arc{};

    constexpr CircularSector() = default;
    constexpr CircularSector(float radius, float half_angle) : arc(radius, half_angle) {}
    static constexpr CircularSector from_radians(float radius, float angle) {
        return CircularSector{Arc2d::from_radians(radius, angle)};
    }
    static constexpr CircularSector from_degrees(float radius, float angle) {
        return CircularSector{Arc2d::from_degrees(radius, angle)};
    }
    static constexpr CircularSector from_turns(float radius, float fraction) {
        return CircularSector{Arc2d::from_turns(radius, fraction)};
    }
    constexpr float half_angle() const noexcept { return arc.half_angle; }
    constexpr float angle() const noexcept { return arc.angle(); }
    constexpr float radius() const noexcept { return arc.radius; }
    CircularSectorMeshBuilder mesh() const noexcept;

    bool operator==(const CircularSector&) const = default;

   private:
    constexpr explicit CircularSector(Arc2d arc) : arc(arc) {}
};

/** @brief Region enclosed by a circular arc and its chord. */
EPIX_EXPORT struct CircularSegment {
    Arc2d arc{};

    constexpr CircularSegment() = default;
    constexpr CircularSegment(float radius, float half_angle) : arc(radius, half_angle) {}
    static constexpr CircularSegment from_radians(float radius, float angle) {
        return CircularSegment{Arc2d::from_radians(radius, angle)};
    }
    static constexpr CircularSegment from_degrees(float radius, float angle) {
        return CircularSegment{Arc2d::from_degrees(radius, angle)};
    }
    static constexpr CircularSegment from_turns(float radius, float fraction) {
        return CircularSegment{Arc2d::from_turns(radius, fraction)};
    }
    constexpr float half_angle() const noexcept { return arc.half_angle; }
    constexpr float angle() const noexcept { return arc.angle(); }
    constexpr float radius() const noexcept { return arc.radius; }
    glm::vec2 chord_midpoint() const noexcept { return arc.chord_midpoint(); }
    float apothem() const noexcept { return arc.apothem(); }
    CircularSegmentMeshBuilder mesh() const noexcept;

    bool operator==(const CircularSegment&) const = default;

   private:
    constexpr explicit CircularSegment(Arc2d arc) : arc(arc) {}
};

/** @brief UV mapping mode shared by circular sector and segment builders. */
EPIX_EXPORT struct CircularMeshUvMode {
    struct Mask {
        float angle                        = 0.0f;
        bool operator==(const Mask&) const = default;
    };

    Mask value{};

    constexpr CircularMeshUvMode() = default;
    constexpr CircularMeshUvMode(Mask value) : value(value) {}

    bool operator==(const CircularMeshUvMode&) const = default;
};

/** @brief Winding classification used when validating polygon inputs. */
EPIX_EXPORT enum class WindingOrder : std::uint8_t { Clockwise, CounterClockwise, Invalid };

/** @brief Two-endpoint line segment. */
EPIX_EXPORT struct Segment2d {
    std::array<glm::vec2, 2> vertices{glm::vec2(-0.5f, 0.0f), glm::vec2(0.5f, 0.0f)};

    constexpr Segment2d() = default;
    constexpr Segment2d(glm::vec2 first, glm::vec2 second) : vertices{first, second} {}
    static Segment2d from_direction_and_length(glm::vec2 direction, float length);
    static constexpr Segment2d from_scaled_direction(glm::vec2 scaled_direction) {
        const auto endpoint = scaled_direction * 0.5f;
        return Segment2d{-endpoint, endpoint};
    }
    constexpr glm::vec2 point1() const noexcept { return vertices[0]; }
    constexpr glm::vec2 point2() const noexcept { return vertices[1]; }
    constexpr glm::vec2 center() const noexcept { return (vertices[0] + vertices[1]) * 0.5f; }
    float length() const noexcept;
    Segment2dMeshBuilder mesh() const noexcept;

    bool operator==(const Segment2d&) const = default;
};

/** @brief Series of connected line segments. */
EPIX_EXPORT struct Polyline2d {
    std::vector<glm::vec2> vertices{glm::vec2(-0.5f, 0.0f), glm::vec2(0.5f, 0.0f)};

    Polyline2d() = default;
    explicit Polyline2d(std::vector<glm::vec2> vertices) : vertices(std::move(vertices)) {}
    static Polyline2d with_subdivisions(glm::vec2 start, glm::vec2 end, std::size_t subdivisions);
    Polyline2dMeshBuilder mesh() const;

    bool operator==(const Polyline2d&) const = default;
};

/** @brief Error returned when checked polygon construction finds concavity. */
EPIX_EXPORT enum class ConvexPolygonError : std::uint8_t { Concave };
EPIX_EXPORT std::string_view to_string(ConvexPolygonError error) noexcept;

/** @brief Convex polygon input with checked and unchecked construction. */
EPIX_EXPORT class ConvexPolygon {
   public:
    static std::expected<ConvexPolygon, ConvexPolygonError> create(std::vector<glm::vec2> vertices);
    static ConvexPolygon new_unchecked(std::vector<glm::vec2> vertices);
    std::span<const glm::vec2> vertices() const noexcept { return vertices_; }
    ConvexPolygonMeshBuilder mesh() const;

    bool operator==(const ConvexPolygon&) const = default;

   private:
    explicit ConvexPolygon(std::vector<glm::vec2> vertices) : vertices_(std::move(vertices)) {}
    std::vector<glm::vec2> vertices_;
};

/** @brief Triangle in 2D space described by its three vertices. */
EPIX_EXPORT struct Triangle2d {
    std::array<glm::vec2, 3> vertices{glm::vec2(0.0f, 0.5f), glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, -0.5f)};

    constexpr Triangle2d() = default;
    constexpr Triangle2d(glm::vec2 a, glm::vec2 b, glm::vec2 c) : vertices{a, b, c} {}
    WindingOrder winding_order() const noexcept;
    float area() const noexcept;
    float perimeter() const noexcept;
    bool is_degenerate() const noexcept;
    bool is_acute() const noexcept;
    bool is_obtuse() const noexcept;
    void reverse() noexcept { std::swap(vertices[0], vertices[2]); }
    Triangle2d reversed() const noexcept {
        Triangle2d value = *this;
        value.reverse();
        return value;
    }
    Triangle2dMeshBuilder mesh() const noexcept;

    bool operator==(const Triangle2d&) const = default;
};

/** @brief Rhombus (diamond) primitive described by its half diagonals. */
EPIX_EXPORT struct Rhombus {
    glm::vec2 half_diagonals{0.5f};

    constexpr Rhombus() = default;
    constexpr Rhombus(float horizontal_diagonal, float vertical_diagonal)
        : half_diagonals(horizontal_diagonal * 0.5f, vertical_diagonal * 0.5f) {}
    static constexpr Rhombus from_side(float side) {
        const float half_diagonal = side * 0.5f * std::numbers::sqrt2_v<float>;
        return Rhombus{half_diagonal * 2.0f, half_diagonal * 2.0f};
    }
    static constexpr Rhombus from_inradius(float inradius) {
        const float half_diagonal = inradius * 2.0f / std::numbers::sqrt2_v<float>;
        return Rhombus{half_diagonal * 2.0f, half_diagonal * 2.0f};
    }
    float side() const noexcept;
    constexpr float circumradius() const noexcept { return std::max(half_diagonals.x, half_diagonals.y); }
    float inradius() const noexcept;
    glm::vec2 closest_point(glm::vec2 point) const noexcept;
    RhombusMeshBuilder mesh() const noexcept;

    bool operator==(const Rhombus&) const = default;
};

/** @brief Annulus primitive described by its inner and outer circle. */
EPIX_EXPORT struct Annulus {
    Circle inner_circle{0.5f};
    Circle outer_circle{1.0f};

    constexpr Annulus() = default;
    constexpr Annulus(float inner_radius, float outer_radius)
        : inner_circle(inner_radius), outer_circle(outer_radius) {}
    constexpr float diameter() const noexcept { return outer_circle.diameter(); }
    constexpr float thickness() const noexcept { return outer_circle.radius - inner_circle.radius; }
    glm::vec2 closest_point(glm::vec2 point) const noexcept;
    AnnulusMeshBuilder mesh() const noexcept;

    bool operator==(const Annulus&) const = default;
};

/** @brief Ellipse primitive described by its perpendicular half extents. */
EPIX_EXPORT struct Ellipse {
    glm::vec2 half_size{1.0f, 0.5f};

    constexpr Ellipse() = default;
    constexpr Ellipse(float half_width, float half_height) : half_size(half_width, half_height) {}
    constexpr explicit Ellipse(glm::vec2 half_size) : half_size(half_size) {}
    static constexpr Ellipse from_size(glm::vec2 size) { return Ellipse{size * 0.5f}; }
    EllipseMeshBuilder mesh() const noexcept;

    bool operator==(const Ellipse&) const = default;
};

/** @brief Regular polygon primitive whose vertices lie on a circumcircle. */
EPIX_EXPORT struct RegularPolygon {
    Circle circumcircle{};
    std::uint32_t sides = 6;

    RegularPolygon() = default;
    RegularPolygon(float circumradius, std::uint32_t sides);
    float circumradius() const noexcept { return circumcircle.radius; }
    RegularPolygonMeshBuilder mesh() const noexcept;

    /** @brief Internal angle of the regular polygon (Bevy `internal_angle_radians`). */
    constexpr float internal_angle_radians() const noexcept {
        return static_cast<float>(sides - 2) * std::numbers::pi_v<float> / static_cast<float>(sides);
    }
    /** @brief External angle of the regular polygon (Bevy `external_angle_radians`). */
    constexpr float external_angle_radians() const noexcept {
        return 2.0f * std::numbers::pi_v<float> / static_cast<float>(sides);
    }

    bool operator==(const RegularPolygon&) const = default;
};

/** @brief Axis-aligned rectangle primitive described by half extents. */
EPIX_EXPORT struct Rectangle {
    glm::vec2 half_size{0.5f};

    constexpr Rectangle() = default;
    constexpr Rectangle(float width, float height) : half_size(width * 0.5f, height * 0.5f) {}
    constexpr explicit Rectangle(glm::vec2 half_size) : half_size(half_size) {}
    static constexpr Rectangle from_size(glm::vec2 size) { return Rectangle{size * 0.5f}; }
    static Rectangle from_corners(glm::vec2 first, glm::vec2 second) noexcept;
    static constexpr Rectangle from_length(float length) { return Rectangle{glm::vec2(length * 0.5f)}; }
    constexpr glm::vec2 size() const noexcept { return half_size * 2.0f; }
    RectangleMeshBuilder mesh() const noexcept;

    bool operator==(const Rectangle&) const = default;
};

/** @brief Capsule (stadium) primitive described by its radius and half length. */
EPIX_EXPORT struct Capsule2d {
    float radius      = 0.5f;
    float half_length = 0.5f;

    constexpr Capsule2d() = default;
    constexpr Capsule2d(float radius, float length) : radius(radius), half_length(length * 0.5f) {}
    constexpr Rectangle to_inner_rectangle() const { return Rectangle{radius * 2.0f, half_length * 2.0f}; }
    Capsule2dMeshBuilder mesh() const noexcept;

    bool operator==(const Capsule2d&) const = default;
};

/**
 * @brief A primitive that can be resized uniformly (Bevy `Inset`).
 *
 * The customization point is `InsetImpl<T>`, mirroring the `AsAssetIdImpl` pattern used for
 * generic asset access elsewhere in this module.
 */
template <typename T>
struct InsetImpl;

/** @brief Satisfied when `InsetImpl<T>` provides the Bevy `inset` operation. */
template <typename T>
concept Inset = requires(const T& shape, float distance) {
    { InsetImpl<T>::inset(shape, distance) } -> std::same_as<T>;
};

/** @brief Create a uniformly inset copy of `shape` (Bevy `Inset::inset`). */
template <Inset T>
constexpr T inset(const T& shape, float distance) {
    return InsetImpl<T>::inset(shape, distance);
}

template <>
struct InsetImpl<Circle> {
    static constexpr Circle inset(const Circle& shape, float distance) { return Circle{shape.radius - distance}; }
};

template <>
struct InsetImpl<Capsule2d> {
    static constexpr Capsule2d inset(const Capsule2d& shape, float distance) {
        return Capsule2d{shape.radius - distance, shape.half_length * 2.0f};
    }
};

template <>
struct InsetImpl<Rectangle> {
    static constexpr Rectangle inset(const Rectangle& shape, float distance) {
        return Rectangle{shape.half_size - glm::vec2(distance)};
    }
};

template <>
struct InsetImpl<Rhombus> {
    static Rhombus inset(const Rhombus& shape, float distance);
};

template <>
struct InsetImpl<Triangle2d> {
    static Triangle2d inset(const Triangle2d& shape, float distance);
};

template <>
struct InsetImpl<CircularSegment> {
    static CircularSegment inset(const CircularSegment& shape, float distance);
};

template <>
struct InsetImpl<RegularPolygon> {
    static RegularPolygon inset(const RegularPolygon& shape, float distance);
};

/**
 * @brief Builder used to create a ring mesh from an outer and an inner shape, both meshed with the
 * same builder type (Bevy `RingMeshBuilder<P>`).
 *
 * The inner mesh is remapped so its UVs continue the outer mesh's radial mapping, and the last
 * triangle pair is rewired back to the first vertex.
 */
template <Inset P>
struct RingMeshBuilder {
    using ShapeMeshBuilder = decltype(std::declval<const P&>().mesh());

    ShapeMeshBuilder outer_shape_builder{};
    ShapeMeshBuilder inner_shape_builder{};

    constexpr RingMeshBuilder() = default;
    constexpr RingMeshBuilder(ShapeMeshBuilder outer, ShapeMeshBuilder inner)
        : outer_shape_builder(outer), inner_shape_builder(inner) {}

    /** @brief Apply `func` to both the inner and the outer builder (Bevy `with_inner`). */
    template <typename Func>
    RingMeshBuilder with_inner(Func func) const {
        return RingMeshBuilder{func(outer_shape_builder), func(inner_shape_builder)};
    }

    /**
     * @brief Build the ring mesh.
     *
     * Bevy asserts that both meshes are triangle lists with Position, Normal, and UV attributes and
     * that they have the same vertex count. Epix reports that through an `std::expected` instead of
     * a Rust panic.
     */
    std::expected<Mesh, MeshAccessError> try_build() const;
    Mesh build() const;
};

/** @brief A 2D shape hollowed into a ring, keeping the inner and outer shapes (Bevy `Ring`). */
template <Inset P>
struct Ring {
    P outer_shape{};
    P inner_shape{};

    constexpr Ring() = default;
    constexpr Ring(P outer, P inner) : outer_shape(outer), inner_shape(inner) {}

    /** @brief Build a ring by insetting `primitive` by `thickness` (Bevy `from_primitive_and_thickness`). */
    static constexpr Ring from_primitive_and_thickness(P primitive, float thickness) {
        return Ring{primitive, inset(primitive, thickness)};
    }

    bool operator==(const Ring&) const = default;

    RingMeshBuilder<P> mesh() const;
};

// ─── 3D shapes (Bevy `bevy_math::primitives::dim3`) ────────────────────────────────────────────

/** @brief Cuboid primitive described by its half extents. */
EPIX_EXPORT struct Cuboid {
    glm::vec3 half_size{0.5f};

    constexpr Cuboid() = default;
    constexpr Cuboid(float x_length, float y_length, float z_length)
        : half_size(x_length * 0.5f, y_length * 0.5f, z_length * 0.5f) {}
    constexpr explicit Cuboid(glm::vec3 half_size) : half_size(half_size) {}
    static constexpr Cuboid from_size(glm::vec3 size) { return Cuboid{size * 0.5f}; }
    static constexpr Cuboid from_length(float length) { return Cuboid{glm::vec3(length * 0.5f)}; }
    static Cuboid from_corners(glm::vec3 point1, glm::vec3 point2) noexcept;
    constexpr glm::vec3 size() const noexcept { return half_size * 2.0f; }
    CuboidMeshBuilder mesh() const noexcept;

    bool operator==(const Cuboid&) const = default;
};

/** @brief Plane primitive described by its normal and half extents in the XZ plane. */
EPIX_EXPORT struct Plane3d {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 half_size{0.5f};

    constexpr Plane3d() = default;
    constexpr Plane3d(glm::vec3 normal, glm::vec2 half_size) : normal(normal), half_size(half_size) {}
    static Plane3d from_points(glm::vec3 a, glm::vec3 b, glm::vec3 c);
    PlaneMeshBuilder mesh() const noexcept;

    bool operator==(const Plane3d&) const = default;
};

/** @brief Three-dimensional line segment. */
EPIX_EXPORT struct Segment3d {
    std::array<glm::vec3, 2> vertices{glm::vec3(-0.5f, 0.0f, 0.0f), glm::vec3(0.5f, 0.0f, 0.0f)};

    constexpr Segment3d() = default;
    constexpr Segment3d(glm::vec3 point1, glm::vec3 point2) : vertices{point1, point2} {}
    static Segment3d from_direction_and_length(glm::vec3 direction, float length);
    static constexpr Segment3d from_scaled_direction(glm::vec3 scaled_direction) {
        const auto endpoint = scaled_direction * 0.5f;
        return Segment3d{-endpoint, endpoint};
    }
    constexpr glm::vec3 point1() const noexcept { return vertices[0]; }
    constexpr glm::vec3 point2() const noexcept { return vertices[1]; }
    constexpr glm::vec3 center() const noexcept { return (vertices[0] + vertices[1]) * 0.5f; }
    float length() const noexcept;
    Segment3dMeshBuilder mesh() const noexcept;

    bool operator==(const Segment3d&) const = default;
};

/** @brief Series of connected line segments in 3D space. */
EPIX_EXPORT struct Polyline3d {
    std::vector<glm::vec3> vertices{glm::vec3(-0.5f, 0.0f, 0.0f), glm::vec3(0.5f, 0.0f, 0.0f)};

    Polyline3d() = default;
    explicit Polyline3d(std::vector<glm::vec3> vertices) : vertices(std::move(vertices)) {}
    static Polyline3d with_subdivisions(glm::vec3 start, glm::vec3 end, std::size_t subdivisions);
    Polyline3dMeshBuilder mesh() const;

    bool operator==(const Polyline3d&) const = default;
};

/** @brief Tetrahedron described by its four vertices. */
EPIX_EXPORT struct Tetrahedron {
    std::array<glm::vec3, 4> vertices{
        glm::vec3(0.5f, 0.5f, 0.5f),
        glm::vec3(-0.5f, 0.5f, -0.5f),
        glm::vec3(-0.5f, -0.5f, 0.5f),
        glm::vec3(0.5f, -0.5f, -0.5f),
    };

    constexpr Tetrahedron() = default;
    constexpr Tetrahedron(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) : vertices{a, b, c, d} {}
    /** @brief Signed volume; negative means the first face normal points toward the fourth vertex. */
    float signed_volume() const noexcept;
    constexpr glm::vec3 centroid() const noexcept {
        return (vertices[0] + vertices[1] + vertices[2] + vertices[3]) * 0.25f;
    }
    std::array<Triangle3d, 4> faces() const noexcept;
    TetrahedronMeshBuilder mesh() const noexcept;

    bool operator==(const Tetrahedron&) const = default;
};

/** @brief Builder used to create a cuboid mesh. */
EPIX_EXPORT struct CuboidMeshBuilder {
    glm::vec3 half_size{0.5f};

    constexpr CuboidMeshBuilder() = default;
    constexpr explicit CuboidMeshBuilder(glm::vec3 half_size) : half_size(half_size) {}
    Mesh build() const;
};

/** @brief Builder used to create a plane mesh. */
EPIX_EXPORT struct PlaneMeshBuilder {
    Plane3d plane{};
    /** @brief Number of interior splits along each axis; `0` keeps the four XZ-plane corners. */
    std::uint32_t subdivisions = 0;

    constexpr PlaneMeshBuilder() = default;
    /** @brief Non-explicit so `shape.mesh()` and `Mesh mesh = shape;` work like the other builders. */
    constexpr PlaneMeshBuilder(Plane3d plane, std::uint32_t subdivisions = 0)
        : plane(plane), subdivisions(subdivisions) {}
    PlaneMeshBuilder(glm::vec3 normal, glm::vec2 size);
    static PlaneMeshBuilder from_size(glm::vec2 size);
    static PlaneMeshBuilder from_length(float length);
    PlaneMeshBuilder with_normal(glm::vec3 normal) && noexcept {
        plane.normal = normal;
        return std::move(*this);
    }
    PlaneMeshBuilder with_size(float width, float height) && noexcept {
        plane.half_size = glm::vec2(width, height) * 0.5f;
        return std::move(*this);
    }
    PlaneMeshBuilder with_subdivisions(std::uint32_t value) && noexcept {
        subdivisions = value;
        return std::move(*this);
    }
    Mesh build() const;
};

/** @brief Builder used to create a 3D segment mesh. */
EPIX_EXPORT struct Segment3dMeshBuilder {
    Segment3d segment{};

    constexpr Segment3dMeshBuilder() = default;
    constexpr explicit Segment3dMeshBuilder(Segment3d segment) : segment(segment) {}
    Mesh build() const;
};

/** @brief Owning builder used to create a connected 3D line-list mesh. */
EPIX_EXPORT class Polyline3dMeshBuilder {
   public:
    Polyline3dMeshBuilder() = default;
    explicit Polyline3dMeshBuilder(Polyline3d polyline) : polyline_(std::move(polyline)) {}
    Mesh build() const;

   private:
    Polyline3d polyline_{};
};

/** @brief Builder used to create a 3D triangle mesh. */
EPIX_EXPORT struct Triangle3dMeshBuilder {
    Triangle3d triangle{};

    constexpr Triangle3dMeshBuilder() = default;
    constexpr explicit Triangle3dMeshBuilder(Triangle3d triangle) : triangle(triangle) {}
    Mesh build() const;
};

/** @brief Builder used to create a tetrahedron mesh. */
EPIX_EXPORT struct TetrahedronMeshBuilder {
    Tetrahedron tetrahedron{};

    constexpr TetrahedronMeshBuilder() = default;
    constexpr explicit TetrahedronMeshBuilder(Tetrahedron tetrahedron) : tetrahedron(tetrahedron) {}
    Mesh build() const;
};

/** @brief Cylinder primitive described by its radius and half height. */
EPIX_EXPORT struct Cylinder {
    float radius      = 0.5f;
    float half_height = 0.5f;

    constexpr Cylinder() = default;
    constexpr Cylinder(float radius, float height) : radius(radius), half_height(height * 0.5f) {}
    constexpr Circle base() const noexcept { return Circle{radius}; }
    /** @brief Surface area of the cylinder side (Bevy `lateral_area`). */
    constexpr float lateral_area() const noexcept { return 4.0f * std::numbers::pi_v<float> * radius * half_height; }
    /** @brief Surface area of one base (Bevy `base_area`). */
    float base_area() const noexcept;
    CylinderMeshBuilder mesh() const noexcept;

    bool operator==(const Cylinder&) const = default;
};

/** @brief Torus primitive described by its tube and ring radii. */
EPIX_EXPORT struct Torus {
    float minor_radius = 0.25f;
    float major_radius = 0.75f;

    constexpr Torus() = default;
    constexpr Torus(float minor_radius, float major_radius) : minor_radius(minor_radius), major_radius(major_radius) {}
    /** @brief Create from the hole radius and the overall outer radius (Bevy `Torus::new`). */
    static constexpr Torus from_inner_and_outer(float inner_radius, float outer_radius) {
        const float minor = (outer_radius - inner_radius) * 0.5f;
        return Torus{minor, outer_radius - minor};
    }
    TorusMeshBuilder mesh() const noexcept;

    bool operator==(const Torus&) const = default;
};

/** @brief Anchor point of a cylinder mesh (Bevy `CylinderAnchor`). */
EPIX_EXPORT enum class CylinderAnchor : std::uint8_t { MidPoint, Top, Bottom };

/** @brief Builder used to create a cylinder mesh. */
EPIX_EXPORT struct CylinderMeshBuilder {
    Cylinder cylinder{};
    /** @brief Vertices around the top and bottom circles. Default: 32. */
    std::uint32_t resolution = 32;
    /** @brief Segments along the height; must be greater than 0 for geometry to be generated. */
    std::uint32_t segments = 1;
    /** @brief When false the caps are skipped and the mesh is a shallow tube. */
    bool caps             = true;
    CylinderAnchor anchor = CylinderAnchor::MidPoint;

    constexpr CylinderMeshBuilder() = default;
    constexpr CylinderMeshBuilder(float radius, float height, std::uint32_t resolution)
        : cylinder(radius, height), resolution(resolution) {}
    constexpr explicit CylinderMeshBuilder(Cylinder cylinder) : cylinder(cylinder) {}
    CylinderMeshBuilder with_resolution(std::uint32_t value) && noexcept {
        resolution = value;
        return std::move(*this);
    }
    CylinderMeshBuilder with_segments(std::uint32_t value) && noexcept {
        segments = value;
        return std::move(*this);
    }
    /** @brief Ignore the caps, making the mesh a shallow tube instead. */
    CylinderMeshBuilder without_caps() && noexcept {
        caps = false;
        return std::move(*this);
    }
    CylinderMeshBuilder with_anchor(CylinderAnchor value) && noexcept {
        anchor = value;
        return std::move(*this);
    }
    Mesh build() const;
};

/** @brief Builder used to create a torus mesh. */
EPIX_EXPORT struct TorusMeshBuilder {
    Torus torus{};
    /** @brief Vertices per circular segment of the tube. Default: 24. */
    std::uint32_t minor_resolution = 24;
    /** @brief Segments around the main ring. Default: 32. */
    std::uint32_t major_resolution = 32;
    /** @brief Start of the swept angle range in radians. Default: 0. */
    float start_angle = 0.0f;
    /** @brief End of the swept angle range in radians. Default: `TAU`. */
    float end_angle = 2.0f * std::numbers::pi_v<float>;

    constexpr TorusMeshBuilder() = default;
    TorusMeshBuilder(float inner_radius, float outer_radius);
    constexpr explicit TorusMeshBuilder(Torus torus) : torus(torus) {}
    TorusMeshBuilder with_minor_resolution(std::uint32_t value) && noexcept {
        minor_resolution = value;
        return std::move(*this);
    }
    TorusMeshBuilder with_major_resolution(std::uint32_t value) && noexcept {
        major_resolution = value;
        return std::move(*this);
    }
    /** @brief Set the swept angle range in radians instead of a full circle. */
    TorusMeshBuilder with_angle_range(float start, float end) && noexcept {
        start_angle = start;
        end_angle   = end;
        return std::move(*this);
    }
    Mesh build() const;
};

/** @brief Anchor point of a cone mesh (Bevy `ConeAnchor`). */
EPIX_EXPORT enum class ConeAnchor : std::uint8_t { MidPoint, Tip, Base };

/** @brief Cone primitive centered on the midpoint between its tip and its base
 * (Bevy `Cone`). The tip points towards +Y. */
EPIX_EXPORT struct Cone {
    /** @brief Radius of the base. Default: 0.5. */
    float radius = 0.5f;
    /** @brief Total height. Default: 1.0. */
    float height = 1.0f;

    constexpr Cone() = default;
    constexpr Cone(float radius, float height) : radius(radius), height(height) {}
    /** @brief The base circle of the cone (Bevy `Cone::base`). */
    constexpr Circle base() const noexcept { return Circle{radius}; }
    /** @brief Length from a base point to the apex (Bevy `Cone::slant_height`). */
    float slant_height() const noexcept;
    /** @brief Surface area of the side (Bevy `Cone::lateral_area`). */
    float lateral_area() const noexcept;
    /** @brief Surface area of the base (Bevy `Cone::base_area`). */
    float base_area() const noexcept;
    ConeMeshBuilder mesh() const noexcept;

    bool operator==(const Cone&) const = default;
};

/** @brief Builder used to create a cone mesh. */
EPIX_EXPORT struct ConeMeshBuilder {
    Cone cone{};
    /** @brief Vertices used for the base circle. Default: 32. */
    std::uint32_t resolution = 32;
    ConeAnchor anchor        = ConeAnchor::MidPoint;

    constexpr ConeMeshBuilder() = default;
    constexpr ConeMeshBuilder(float radius, float height, std::uint32_t resolution)
        : cone(radius, height), resolution(resolution) {}
    constexpr explicit ConeMeshBuilder(Cone cone) : cone(cone) {}
    ConeMeshBuilder with_resolution(std::uint32_t value) && noexcept {
        resolution = value;
        return std::move(*this);
    }
    ConeMeshBuilder with_anchor(ConeAnchor value) && noexcept {
        anchor = value;
        return std::move(*this);
    }
    Mesh build() const;
};

/** @brief Truncated cone primitive described by its two radii and height
 * (Bevy `ConicalFrustum`). */
EPIX_EXPORT struct ConicalFrustum {
    /** @brief Radius of the top. Default: 0.25. */
    float radius_top = 0.25f;
    /** @brief Radius of the base. Default: 0.5. */
    float radius_bottom = 0.5f;
    /** @brief Height. Default: 0.5. */
    float height = 0.5f;

    constexpr ConicalFrustum() = default;
    constexpr ConicalFrustum(float radius_top, float radius_bottom, float height)
        : radius_top(radius_top), radius_bottom(radius_bottom), height(height) {}
    /** @brief The base circle (Bevy `ConicalFrustum::bottom_base`). */
    constexpr Circle bottom_base() const noexcept { return Circle{radius_bottom}; }
    /** @brief The top circle (Bevy `ConicalFrustum::top_base`). */
    constexpr Circle top_base() const noexcept { return Circle{radius_top}; }
    /** @brief Length from the base edge to the closest top edge point (Bevy
     * `ConicalFrustum::slant_height`). */
    float slant_height() const noexcept;
    /** @brief Surface area of the side (Bevy `ConicalFrustum::lateral_area`). */
    float lateral_area() const noexcept;
    /** @brief Surface area of the base (Bevy `ConicalFrustum::bottom_area`). */
    float bottom_area() const noexcept;
    /** @brief Surface area of the top (Bevy `ConicalFrustum::top_area`). */
    float top_area() const noexcept;
    ConicalFrustumMeshBuilder mesh() const noexcept;

    bool operator==(const ConicalFrustum&) const = default;
};

/** @brief Builder used to create a conical-frustum mesh. */
EPIX_EXPORT struct ConicalFrustumMeshBuilder {
    ConicalFrustum frustum{};
    /** @brief Vertices used for the top and bottom circles. Default: 32. */
    std::uint32_t resolution = 32;
    /** @brief Horizontal lines subdividing the lateral surface. Default: 1. */
    std::uint32_t segments = 1;

    constexpr ConicalFrustumMeshBuilder() = default;
    constexpr ConicalFrustumMeshBuilder(float radius_top, float radius_bottom, float height, std::uint32_t resolution)
        : frustum(radius_top, radius_bottom, height), resolution(resolution) {}
    constexpr explicit ConicalFrustumMeshBuilder(ConicalFrustum frustum) : frustum(frustum) {}
    ConicalFrustumMeshBuilder with_resolution(std::uint32_t value) && noexcept {
        resolution = value;
        return std::move(*this);
    }
    ConicalFrustumMeshBuilder with_segments(std::uint32_t value) && noexcept {
        segments = value;
        return std::move(*this);
    }
    Mesh build() const;
};

/** @brief Builder used to create a circle mesh. */
EPIX_EXPORT struct CircleMeshBuilder {
    Circle circle{};
    std::uint32_t resolution = 32;

    constexpr CircleMeshBuilder() = default;
    constexpr CircleMeshBuilder(float radius, std::uint32_t resolution) : circle(radius), resolution(resolution) {}
    CircleMeshBuilder with_resolution(std::uint32_t value) && noexcept {
        resolution = value;
        return std::move(*this);
    }
    Mesh build() const;
};

/** @brief Builder used to create a circular-sector mesh. */
EPIX_EXPORT struct CircularSectorMeshBuilder {
    CircularSector sector{};
    std::uint32_t resolution = 32;
    CircularMeshUvMode uv_mode{};

    constexpr CircularSectorMeshBuilder() = default;
    constexpr explicit CircularSectorMeshBuilder(CircularSector sector) : sector(sector) {}
    CircularSectorMeshBuilder with_resolution(std::uint32_t value) && noexcept {
        resolution = value;
        return std::move(*this);
    }
    CircularSectorMeshBuilder with_uv_mode(CircularMeshUvMode value) && noexcept {
        uv_mode = value;
        return std::move(*this);
    }
    Mesh build() const;
};

/** @brief Builder used to create a circular-segment mesh. */
EPIX_EXPORT struct CircularSegmentMeshBuilder {
    CircularSegment segment{};
    std::uint32_t resolution = 32;
    CircularMeshUvMode uv_mode{};

    constexpr CircularSegmentMeshBuilder() = default;
    constexpr explicit CircularSegmentMeshBuilder(CircularSegment segment) : segment(segment) {}
    CircularSegmentMeshBuilder with_resolution(std::uint32_t value) && noexcept {
        resolution = value;
        return std::move(*this);
    }
    CircularSegmentMeshBuilder with_uv_mode(CircularMeshUvMode value) && noexcept {
        uv_mode = value;
        return std::move(*this);
    }
    Mesh build() const;
};

/** @brief Builder used to create a filled convex-polygon mesh. */
EPIX_EXPORT struct ConvexPolygonMeshBuilder {
    std::vector<glm::vec2> vertices;

    Mesh build() const;
};

/** @brief Builder used to create an annulus mesh. */
EPIX_EXPORT struct AnnulusMeshBuilder {
    Annulus annulus{};
    std::uint32_t resolution = 32;

    constexpr AnnulusMeshBuilder() = default;
    constexpr AnnulusMeshBuilder(float inner_radius, float outer_radius, std::uint32_t resolution)
        : annulus(inner_radius, outer_radius), resolution(resolution) {}
    AnnulusMeshBuilder with_resolution(std::uint32_t value) && noexcept {
        resolution = value;
        return std::move(*this);
    }
    Mesh build() const;
};

/** @brief Builder used to create a rhombus (diamond) mesh. */
EPIX_EXPORT struct RhombusMeshBuilder {
    glm::vec2 half_diagonals{0.5f};

    constexpr RhombusMeshBuilder() = default;
    constexpr explicit RhombusMeshBuilder(glm::vec2 half_diagonals) : half_diagonals(half_diagonals) {}
    RhombusMeshBuilder(float horizontal_diagonal, float vertical_diagonal);
    Mesh build() const;
};

/** @brief Builder used to create a triangle mesh. */
EPIX_EXPORT struct Triangle2dMeshBuilder {
    Triangle2d triangle{};

    constexpr Triangle2dMeshBuilder() = default;
    constexpr explicit Triangle2dMeshBuilder(Triangle2d triangle) : triangle(triangle) {}
    constexpr Triangle2dMeshBuilder(glm::vec2 a, glm::vec2 b, glm::vec2 c) : triangle(a, b, c) {}
    Mesh build() const;
};

/** @brief Builder used to create a capsule (stadium) mesh. */
EPIX_EXPORT struct Capsule2dMeshBuilder {
    Capsule2d capsule{};
    std::uint32_t resolution = 16;

    constexpr Capsule2dMeshBuilder() = default;
    constexpr Capsule2dMeshBuilder(float radius, float length, std::uint32_t resolution)
        : capsule(radius, length), resolution(resolution) {}
    Capsule2dMeshBuilder with_resolution(std::uint32_t value) && noexcept {
        resolution = value;
        return std::move(*this);
    }
    Mesh build() const;
};

/** @brief Builder used to create an ellipse mesh. */
EPIX_EXPORT struct EllipseMeshBuilder {
    Ellipse ellipse{};
    std::uint32_t resolution = 32;

    constexpr EllipseMeshBuilder() = default;
    constexpr EllipseMeshBuilder(float half_width, float half_height, std::uint32_t resolution)
        : ellipse(half_width, half_height), resolution(resolution) {}
    EllipseMeshBuilder with_resolution(std::uint32_t value) && noexcept {
        resolution = value;
        return std::move(*this);
    }
    Mesh build() const;
};

/** @brief Builder used to create a two-endpoint line mesh. */
EPIX_EXPORT struct Segment2dMeshBuilder {
    Segment2d segment{};

    constexpr Segment2dMeshBuilder() = default;
    constexpr explicit Segment2dMeshBuilder(Segment2d segment) : segment(segment) {}
    Mesh build() const;
};

/** @brief Owning builder used to create a connected line-list mesh. */
EPIX_EXPORT class Polyline2dMeshBuilder {
   public:
    Polyline2dMeshBuilder() = default;
    explicit Polyline2dMeshBuilder(Polyline2d polyline) : polyline_(std::move(polyline)) {}
    Mesh build() const;

   private:
    Polyline2d polyline_{};
};

/** @brief Builder used to create a regular polygon mesh. */
EPIX_EXPORT struct RegularPolygonMeshBuilder {
    float circumradius  = 0.5f;
    std::uint32_t sides = 6;

    constexpr RegularPolygonMeshBuilder() = default;
    RegularPolygonMeshBuilder(float circumradius, std::uint32_t sides);
    Mesh build() const;
};

/** @brief Builder used to create a rectangle mesh. */
EPIX_EXPORT struct RectangleMeshBuilder {
    glm::vec2 half_size{0.5f};

    constexpr RectangleMeshBuilder() = default;
    RectangleMeshBuilder(float width, float height);
    constexpr explicit RectangleMeshBuilder(glm::vec2 half_size) : half_size(half_size) {}
    Mesh build() const;
};

static_assert(MeshBuilder<AnnulusMeshBuilder>);
static_assert(MeshBuilder<Capsule2dMeshBuilder>);
static_assert(MeshBuilder<CircleMeshBuilder>);
static_assert(MeshBuilder<CircularSectorMeshBuilder>);
static_assert(MeshBuilder<CircularSegmentMeshBuilder>);
static_assert(MeshBuilder<ConvexPolygonMeshBuilder>);
static_assert(MeshBuilder<EllipseMeshBuilder>);
static_assert(MeshBuilder<Polyline2dMeshBuilder>);
static_assert(MeshBuilder<RegularPolygonMeshBuilder>);
static_assert(MeshBuilder<RectangleMeshBuilder>);
static_assert(MeshBuilder<RhombusMeshBuilder>);
static_assert(MeshBuilder<Segment2dMeshBuilder>);
static_assert(MeshBuilder<Triangle2dMeshBuilder>);
static_assert(Meshable<Annulus>);
static_assert(Meshable<Capsule2d>);
static_assert(Meshable<Circle>);
static_assert(Meshable<CircularSector>);
static_assert(Meshable<CircularSegment>);
static_assert(Meshable<ConvexPolygon>);
static_assert(Meshable<Ellipse>);
static_assert(Meshable<Polyline2d>);
static_assert(Meshable<RegularPolygon>);
static_assert(Meshable<Rectangle>);
static_assert(Meshable<Rhombus>);
static_assert(Meshable<Segment2d>);
static_assert(Meshable<Triangle2d>);
static_assert(Inset<Circle>);
static_assert(Inset<Capsule2d>);
static_assert(Inset<CircularSegment>);
static_assert(Inset<Rectangle>);
static_assert(Inset<RegularPolygon>);
static_assert(Inset<Rhombus>);
static_assert(Inset<Triangle2d>);
static_assert(Meshable<Cuboid>);
static_assert(Meshable<Plane3d>);
static_assert(Meshable<Segment3d>);
static_assert(Meshable<Polyline3d>);
static_assert(Meshable<Triangle3d>);
static_assert(Meshable<Tetrahedron>);
static_assert(Meshable<Cylinder>);
static_assert(Meshable<Torus>);
static_assert(Meshable<Cone>);
static_assert(Meshable<ConicalFrustum>);
static_assert(MeshBuilder<CylinderMeshBuilder>);
static_assert(MeshBuilder<TorusMeshBuilder>);
static_assert(MeshBuilder<ConeMeshBuilder>);
static_assert(MeshBuilder<ConicalFrustumMeshBuilder>);
static_assert(MeshBuilder<CuboidMeshBuilder>);
static_assert(MeshBuilder<PlaneMeshBuilder>);
static_assert(MeshBuilder<Segment3dMeshBuilder>);
static_assert(MeshBuilder<Polyline3dMeshBuilder>);
static_assert(MeshBuilder<Triangle3dMeshBuilder>);
static_assert(MeshBuilder<TetrahedronMeshBuilder>);
static_assert(Meshable<Ring<Circle>>);
static_assert(MeshBuilder<RingMeshBuilder<Circle>>);

}  // namespace epix::mesh
