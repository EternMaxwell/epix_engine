#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <epix/mesh/mesh.hpp>
#include <expected>
#include <glm/glm.hpp>
#include <initializer_list>
#include <iterator>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif

namespace epix::mesh {

/** @brief C++ marker counterpart of Bevy's `Primitive2d` trait. */
template <typename T>
inline constexpr bool is_primitive_2d_v = false;

/** @brief Satisfied exactly by the 2D math primitives represented by this module. */
template <typename T>
concept Primitive2d = is_primitive_2d_v<std::remove_cvref_t<T>>;

class Polyline2dMeshBuilder;
class Polyline3dMeshBuilder;
struct AnnulusMeshBuilder;
struct Capsule2dMeshBuilder;
struct Capsule3dMeshBuilder;
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
struct SphereMeshBuilder;
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
    glm::vec2 closest_point(glm::vec2 point) const noexcept;
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
    glm::vec2 right_endpoint() const noexcept;
    glm::vec2 left_endpoint() const noexcept;
    std::array<glm::vec2, 2> endpoints() const noexcept { return {left_endpoint(), right_endpoint()}; }
    constexpr glm::vec2 midpoint() const noexcept { return radius * glm::vec2{0.0f, 1.0f}; }
    float half_chord_length() const noexcept;
    float chord_length() const noexcept { return 2.0f * half_chord_length(); }
    glm::vec2 chord_midpoint() const noexcept;
    float apothem() const noexcept;
    float sagitta() const noexcept { return radius - apothem(); }
    constexpr bool is_minor() const noexcept { return half_angle <= std::numbers::pi_v<float> * 0.5f; }
    constexpr bool is_major() const noexcept { return half_angle >= std::numbers::pi_v<float> * 0.5f; }

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
    /** @brief Length of the arc defining the sector (Bevy `CircularSector::arc_length`). */
    constexpr float arc_length() const noexcept { return arc.length(); }
    float half_chord_length() const noexcept { return arc.half_chord_length(); }
    float chord_length() const noexcept { return arc.chord_length(); }
    glm::vec2 chord_midpoint() const noexcept { return arc.chord_midpoint(); }
    float apothem() const noexcept { return arc.apothem(); }
    float sagitta() const noexcept { return arc.sagitta(); }
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
    /** @brief Length of the arc defining the segment (Bevy `CircularSegment::arc_length`). */
    constexpr float arc_length() const noexcept { return arc.length(); }
    /** @brief Length of the segment's base, also known as its chord (Bevy
     * `CircularSegment::chord_length`). */
    float half_chord_length() const noexcept { return arc.half_chord_length(); }
    float chord_length() const noexcept { return arc.chord_length(); }
    glm::vec2 chord_midpoint() const noexcept { return arc.chord_midpoint(); }
    float apothem() const noexcept { return arc.apothem(); }
    float sagitta() const noexcept { return arc.sagitta(); }
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

/** @brief An unbounded plane through the origin in 2D space. */
EPIX_EXPORT struct Plane2d {
    /** @brief Checked unit normal; Epix uses a normalized GLM vector in place of Bevy's `Dir2`. */
    glm::vec2 normal{0.0f, 1.0f};

    constexpr Plane2d() = default;
    /** @throws std::invalid_argument if `normal` is zero, too small to normalize, or non-finite. */
    explicit Plane2d(glm::vec2 normal);

    bool operator==(const Plane2d&) const = default;
};

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
    /** @brief Construct a segment starting at `origin` along a checked unit direction. */
    static Segment2d from_ray_and_length(glm::vec2 origin, glm::vec2 direction, float length);
    constexpr glm::vec2 point1() const noexcept { return vertices[0]; }
    constexpr glm::vec2 point2() const noexcept { return vertices[1]; }
    constexpr glm::vec2 center() const noexcept { return (vertices[0] + vertices[1]) * 0.5f; }
    float length() const noexcept;
    float length_squared() const noexcept;
    glm::vec2 direction() const;
    std::optional<glm::vec2> try_direction() const noexcept;
    constexpr glm::vec2 scaled_direction() const noexcept { return point2() - point1(); }
    glm::vec2 left_normal() const;
    std::optional<glm::vec2> try_left_normal() const noexcept;
    constexpr glm::vec2 scaled_left_normal() const noexcept {
        const auto value = scaled_direction();
        return {-value.y, value.x};
    }
    glm::vec2 right_normal() const;
    std::optional<glm::vec2> try_right_normal() const noexcept;
    constexpr glm::vec2 scaled_right_normal() const noexcept {
        const auto value = scaled_direction();
        return {value.y, -value.x};
    }
    /** @brief Apply a Bevy-style rigid transform: rotate, then translate. */
    Segment2d transformed(glm::vec2 translation, float rotation) const noexcept;
    Segment2d translated(glm::vec2 translation) const noexcept;
    Segment2d rotated(float rotation) const noexcept;
    Segment2d rotated_around(float rotation, glm::vec2 point) const noexcept;
    Segment2d rotated_around_center(float rotation) const noexcept;
    Segment2d centered() const noexcept;
    Segment2d resized(float length) const noexcept;
    void reverse() noexcept { std::swap(vertices[0], vertices[1]); }
    Segment2d reversed() const noexcept {
        Segment2d value = *this;
        value.reverse();
        return value;
    }
    glm::vec2 closest_point(glm::vec2 point) const noexcept;
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

class ConvexPolygon;

/** @brief Polygon with an arbitrary number of vertices (Bevy `Polygon`). */
EPIX_EXPORT struct Polygon {
    std::vector<glm::vec2> vertices;

    Polygon() = default;
    explicit Polygon(std::vector<glm::vec2> vertices) : vertices(std::move(vertices)) {}
    Polygon(std::initializer_list<glm::vec2> vertices) : vertices(vertices) {}
    template <std::input_iterator Iterator>
    Polygon(Iterator first, Iterator last) : vertices(first, last) {}
    /** @brief Consuming counterpart of Bevy's `From<ConvexPolygon> for Polygon`. */
    explicit Polygon(ConvexPolygon polygon);

    /** @brief Whether no non-adjacent edges cross or touch (Bevy `Polygon::is_simple`). */
    bool is_simple() const;

    bool operator==(const Polygon&) const = default;
};

/** @brief Error returned when checked polygon construction finds concavity. */
EPIX_EXPORT enum class ConvexPolygonError : std::uint8_t { Concave };
EPIX_EXPORT std::string_view to_string(ConvexPolygonError error) noexcept;

/** @brief Convex polygon input with checked and unchecked construction. */
EPIX_EXPORT class ConvexPolygon {
   public:
    static std::expected<ConvexPolygon, ConvexPolygonError> create(std::vector<glm::vec2> vertices);
    /** @brief Consuming counterpart of Bevy's `TryFrom<Polygon> for ConvexPolygon`. */
    static std::expected<ConvexPolygon, ConvexPolygonError> from_polygon(Polygon polygon);
    static ConvexPolygon new_unchecked(std::vector<glm::vec2> vertices);
    std::span<const glm::vec2> vertices() const noexcept { return vertices_; }
    ConvexPolygonMeshBuilder mesh() const;

    bool operator==(const ConvexPolygon&) const = default;

   private:
    friend struct Polygon;
    explicit ConvexPolygon(std::vector<glm::vec2> vertices) : vertices_(std::move(vertices)) {}
    std::vector<glm::vec2> vertices_;
};

/** @brief Triangle in 2D space described by its three vertices. */
EPIX_EXPORT struct Triangle2d {
    std::array<glm::vec2, 3> vertices{glm::vec2(0.0f, 0.5f), glm::vec2(-0.5f, -0.5f), glm::vec2(0.5f, -0.5f)};

    constexpr Triangle2d() = default;
    constexpr Triangle2d(glm::vec2 a, glm::vec2 b, glm::vec2 c) : vertices{a, b, c} {}
    WindingOrder winding_order() const noexcept;
    bool is_degenerate() const noexcept;
    bool is_acute() const noexcept;
    bool is_obtuse() const noexcept;
    std::pair<Circle, glm::vec2> circumcircle() const noexcept;
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
    /** @brief Length of the longest radius (Bevy `Ellipse::semi_major`). */
    constexpr float semi_major() const noexcept { return std::max(half_size.x, half_size.y); }
    /** @brief Length of the shortest radius (Bevy `Ellipse::semi_minor`). */
    constexpr float semi_minor() const noexcept { return std::min(half_size.x, half_size.y); }
    float eccentricity() const noexcept;
    float focal_length() const noexcept;
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
    /** @brief Length of one side of the regular polygon (Bevy `RegularPolygon::side_length`). */
    float side_length() const noexcept;
    float inradius() const noexcept;
    std::vector<glm::vec2> vertices(float rotation) const;
    RegularPolygonMeshBuilder mesh() const noexcept;

    /** @brief Internal angle of the regular polygon (Bevy `internal_angle_radians`). */
    constexpr float internal_angle_radians() const noexcept {
        return static_cast<float>(sides - 2) * std::numbers::pi_v<float> / static_cast<float>(sides);
    }
    /** @brief External angle of the regular polygon (Bevy `external_angle_radians`). */
    constexpr float external_angle_radians() const noexcept {
        return 2.0f * std::numbers::pi_v<float> / static_cast<float>(sides);
    }
    constexpr float internal_angle_degrees() const noexcept {
        return static_cast<float>(sides - 2) / static_cast<float>(sides) * 180.0f;
    }
    constexpr float external_angle_degrees() const noexcept { return 360.0f / static_cast<float>(sides); }

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
    glm::vec2 closest_point(glm::vec2 point) const noexcept;
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

template <>
inline constexpr bool is_primitive_2d_v<Circle> = true;
template <>
inline constexpr bool is_primitive_2d_v<Arc2d> = true;
template <>
inline constexpr bool is_primitive_2d_v<CircularSector> = true;
template <>
inline constexpr bool is_primitive_2d_v<CircularSegment> = true;
template <>
inline constexpr bool is_primitive_2d_v<Ellipse> = true;
template <>
inline constexpr bool is_primitive_2d_v<Annulus> = true;
template <>
inline constexpr bool is_primitive_2d_v<Rhombus> = true;
template <>
inline constexpr bool is_primitive_2d_v<Plane2d> = true;
template <>
inline constexpr bool is_primitive_2d_v<Segment2d> = true;
template <>
inline constexpr bool is_primitive_2d_v<Polyline2d> = true;
template <>
inline constexpr bool is_primitive_2d_v<Triangle2d> = true;
template <>
inline constexpr bool is_primitive_2d_v<Rectangle> = true;
template <>
inline constexpr bool is_primitive_2d_v<Polygon> = true;
template <>
inline constexpr bool is_primitive_2d_v<ConvexPolygon> = true;
template <>
inline constexpr bool is_primitive_2d_v<RegularPolygon> = true;
template <>
inline constexpr bool is_primitive_2d_v<Capsule2d> = true;

static_assert(Primitive2d<Circle>);
static_assert(Primitive2d<Arc2d>);
static_assert(Primitive2d<CircularSector>);
static_assert(Primitive2d<CircularSegment>);
static_assert(Primitive2d<Ellipse>);
static_assert(Primitive2d<Annulus>);
static_assert(Primitive2d<Rhombus>);
static_assert(Primitive2d<Plane2d>);
static_assert(Primitive2d<Segment2d>);
static_assert(Primitive2d<Polyline2d>);
static_assert(Primitive2d<Triangle2d>);
static_assert(Primitive2d<Rectangle>);
static_assert(Primitive2d<Polygon>);
static_assert(Primitive2d<ConvexPolygon>);
static_assert(Primitive2d<RegularPolygon>);
static_assert(Primitive2d<Capsule2d>);
static_assert(!Primitive2d<SphereMeshBuilder>);

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
template <Primitive2d P>
    requires Meshable<P>
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
template <Primitive2d P>
struct Ring {
    P outer_shape{};
    P inner_shape{};

    constexpr Ring() = default;
    constexpr Ring(P outer, P inner) : outer_shape(outer), inner_shape(inner) {}

    /** @brief Build a ring by insetting `primitive` by `thickness` (Bevy `from_primitive_and_thickness`). */
    static constexpr Ring from_primitive_and_thickness(P primitive, float thickness)
        requires Inset<P>
    {
        return Ring{primitive, inset(primitive, thickness)};
    }

    bool operator==(const Ring&) const = default;

    RingMeshBuilder<P> mesh() const
        requires Meshable<P>;
};

template <Primitive2d P>
inline constexpr bool is_primitive_2d_v<Ring<P>> = true;

// ─── 3D shapes (Bevy `bevy_math::primitives::dim3`) ────────────────────────────────────────────

/** @brief Sphere primitive described by its radius (Bevy `Sphere`, `dim3.rs:30-75`). */
EPIX_EXPORT struct Sphere {
    /** @brief The radius of the sphere. */
    float radius = 0.5f;

    constexpr Sphere() = default;
    constexpr explicit Sphere(float radius) : radius(radius) {}
    /** @brief Diameter of the sphere (Bevy `Sphere::diameter`). */
    constexpr float diameter() const noexcept { return 2.0f * radius; }
    /** @brief The point on the sphere's surface closest to `point`, or `point` itself when it is
     * already inside (Bevy `Sphere::closest_point`). */
    glm::vec3 closest_point(glm::vec3 point) const noexcept;
    SphereMeshBuilder mesh() const noexcept;

    bool operator==(const Sphere&) const = default;
};

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
    glm::vec3 closest_point(glm::vec3 point) const noexcept;
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
    /** @brief Construct a segment starting at `origin` along a checked unit direction. */
    static Segment3d from_ray_and_length(glm::vec3 origin, glm::vec3 direction, float length);
    constexpr glm::vec3 point1() const noexcept { return vertices[0]; }
    constexpr glm::vec3 point2() const noexcept { return vertices[1]; }
    constexpr glm::vec3 center() const noexcept { return (vertices[0] + vertices[1]) * 0.5f; }
    float length() const noexcept;
    float length_squared() const noexcept;
    glm::vec3 direction() const;
    std::optional<glm::vec3> try_direction() const noexcept;
    constexpr glm::vec3 scaled_direction() const noexcept { return point2() - point1(); }
    /** @brief Apply a Bevy-style rigid transform: rotate, then translate. */
    Segment3d transformed(glm::vec3 translation, glm::quat rotation) const noexcept;
    Segment3d translated(glm::vec3 translation) const noexcept;
    Segment3d rotated(glm::quat rotation) const noexcept;
    Segment3d rotated_around(glm::quat rotation, glm::vec3 point) const noexcept;
    Segment3d rotated_around_center(glm::quat rotation) const noexcept;
    Segment3d centered() const noexcept;
    Segment3d resized(float length) const noexcept;
    void reverse() noexcept { std::swap(vertices[0], vertices[1]); }
    Segment3d reversed() const noexcept {
        Segment3d value = *this;
        value.reverse();
        return value;
    }
    glm::vec3 closest_point(glm::vec3 point) const noexcept;
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

/** @brief Bevy's geometric classification of a torus. */
EPIX_EXPORT enum class TorusKind : std::uint8_t { Ring, Horn, Spindle, Invalid };

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
    constexpr float inner_radius() const noexcept { return major_radius - minor_radius; }
    constexpr float outer_radius() const noexcept { return major_radius + minor_radius; }
    TorusKind kind() const noexcept;
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
    /** @brief Surface area of the base (Bevy `ConicalFrustum::bottom_base_area`). */
    float bottom_base_area() const noexcept;
    /** @brief Surface area of the top (Bevy `ConicalFrustum::top_base_area`). */
    float top_base_area() const noexcept;
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

/** @brief Manner in which a capsule mesh distributes UV coordinates vertically
 * (Bevy `CapsuleUvProfile`). */
EPIX_EXPORT enum class CapsuleUvProfile : std::uint8_t {
    /** @brief UV space is distributed by how much of the capsule consists of the
     * hemispheres. */
    Aspect,
    /** @brief Hemispheres get UV space according to the ratio of latitudes to
     * rings. */
    Uniform,
    /** @brief The upper third of the texture goes to the northern hemisphere, the
     * middle third to the cylinder and the lower third to the southern one. */
    Fixed,
};

/** @brief Capsule primitive described by its radius and its length excluding the
 * hemispheres (Bevy `bevy_math::primitives::Capsule3d`). The cylindrical segment
 * is twice `half_length` long and both ends are hemispheres of `radius`. */
EPIX_EXPORT struct Capsule3d {
    /** @brief Radius of the capsule. Default: 0.5. */
    float radius = 0.5f;
    /** @brief Half the height of the capsule, excluding the hemispheres.
     * Default: 0.5, so the total height is 2.0. */
    float half_length = 0.5f;

    constexpr Capsule3d() = default;
    /** @brief Create from a radius and the length of the cylindrical segment
     * (Bevy `Capsule3d::new`). */
    constexpr Capsule3d(float radius, float length) : radius(radius), half_length(length * 0.5f) {}
    /** @brief The part connecting the hemispherical ends, as a cylinder (Bevy
     * `Capsule3d::to_cylinder`). */
    constexpr Cylinder to_cylinder() const noexcept { return Cylinder{radius, half_length * 2.0f}; }
    Capsule3dMeshBuilder mesh() const noexcept;

    bool operator==(const Capsule3d&) const = default;
};

/** @brief Builder used to create a capsule mesh (Bevy `Capsule3dMeshBuilder`).
 *
 * `height` passed to the constructor is the distance between the centres of the
 * hemispheres; `radius` is added at both ends to get the mesh's real height. */
EPIX_EXPORT struct Capsule3dMeshBuilder {
    Capsule3d capsule{};
    /** @brief Horizontal lines subdividing the cylindrical part. Default: 0. */
    std::uint32_t rings = 0;
    /** @brief Vertical lines subdividing the hemispheres. Default: 32. */
    std::uint32_t longitudes = 32;
    /** @brief Horizontal lines subdividing the hemispheres. Default: 16. */
    std::uint32_t latitudes = 16;
    /** @brief How UV coordinates are distributed vertically. Default:
     * `CapsuleUvProfile::Aspect`. */
    CapsuleUvProfile uv_profile = CapsuleUvProfile::Aspect;

    constexpr Capsule3dMeshBuilder() = default;
    constexpr Capsule3dMeshBuilder(float radius, float height, std::uint32_t longitudes, std::uint32_t latitudes)
        : capsule(radius, height), longitudes(longitudes), latitudes(latitudes) {}
    constexpr explicit Capsule3dMeshBuilder(Capsule3d capsule) : capsule(capsule) {}
    Capsule3dMeshBuilder with_rings(std::uint32_t value) && noexcept {
        rings = value;
        return std::move(*this);
    }
    Capsule3dMeshBuilder with_longitudes(std::uint32_t value) && noexcept {
        longitudes = value;
        return std::move(*this);
    }
    Capsule3dMeshBuilder with_latitudes(std::uint32_t value) && noexcept {
        latitudes = value;
        return std::move(*this);
    }
    Capsule3dMeshBuilder with_uv_profile(CapsuleUvProfile value) && noexcept {
        uv_profile = value;
        return std::move(*this);
    }
    Mesh build() const;
};

/** @brief The kind of sphere mesh to build (Bevy `SphereKind`, `sphere.rs:25-48`).
 *
 * Bevy's variants carry no defaults and its `Default` impl is `Ico { subdivisions: 5 }`; the C++
 * alternatives need member initializers to stay aggregate-initializable, so they carry Bevy's
 * recommended values instead. */
EPIX_EXPORT struct SphereKind {
    /** @brief An icosphere: similar sized triangles. The face count quadruples per subdivision. */
    struct Ico {
        /** @brief The number of subdivisions applied. */
        std::uint32_t subdivisions = 5;

        bool operator==(const Ico&) const = default;
    };
    /** @brief A UV sphere: quadrilaterals apart from triangles at the top and bottom. */
    struct Uv {
        /** @brief The number of longitudinal sectors, the horizontal resolution. */
        std::uint32_t sectors = 32;
        /** @brief The number of latitudinal stacks, the vertical resolution. */
        std::uint32_t stacks = 18;

        bool operator==(const Uv&) const = default;
    };

    std::variant<Ico, Uv> value{Ico{5}};

    bool operator==(const SphereKind&) const = default;
};

/** @brief Error returned while creating an icosphere mesh (Bevy `IcosphereError`,
 * `sphere.rs:11-20`).
 *
 * Bevy's error enum has a single variant, so the C++ counterpart is a plain struct. */
EPIX_EXPORT struct IcosphereError {
    /** @brief The number of subdivisions that was requested. */
    std::uint32_t subdivisions = 0;
    /** @brief The number of vertices the request would have generated. */
    std::uint32_t number_of_resulting_points = 0;

    /** @brief Return Bevy's `Error` display text for this error. */
    std::string to_string() const {
        return std::format(
            "Cannot create an icosphere of {} subdivisions due to there being too many vertices being generated: {}. "
            "(Limited to 65535 vertices or 79 subdivisions)",
            subdivisions, number_of_resulting_points);
    }

    bool operator==(const IcosphereError&) const = default;
};

/** @brief Builder used to create a sphere mesh (Bevy `SphereMeshBuilder`, `sphere.rs:53-232`). */
EPIX_EXPORT struct SphereMeshBuilder {
    /** @brief The sphere shape. */
    Sphere sphere{};
    /** @brief The kind of sphere mesh that will be built. */
    SphereKind kind{};

    constexpr SphereMeshBuilder() = default;
    constexpr SphereMeshBuilder(float radius, SphereKind kind) : sphere(radius), kind(kind) {}

    /** @brief Set the kind of sphere mesh to build (Bevy `SphereMeshBuilder::kind`). */
    SphereMeshBuilder with_kind(SphereKind value) && noexcept {
        kind = value;
        return std::move(*this);
    }

    /** @brief Create an icosphere mesh with the given number of subdivisions (Bevy
     * `SphereMeshBuilder::ico`).
     *
     * Returns Bevy's `TooManyVertices` error from `80` subdivisions upwards, before any vertex is
     * generated. **B48b:** the generator itself is not ported yet, so a valid subdivision count
     * throws; see the module-wide tracker. */
    std::expected<Mesh, IcosphereError> ico(std::uint32_t subdivisions) const;

    /** @brief Create a UV sphere mesh with the given longitudinal sectors and latitudinal stacks
     * (Bevy `SphereMeshBuilder::uv`). */
    Mesh uv(std::uint32_t sectors, std::uint32_t stacks) const;

    /** @brief Build the mesh for the configured kind (Bevy `MeshBuilder for SphereMeshBuilder`). */
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

// ─── Measurement (Bevy `Measured2d` and `Measured3d`) ───────────────────────────────────────────
//
// Bevy measures primitives through the `Measured2d { area, perimeter }` and
// `Measured3d { area, volume }` traits. As with `Inset`, the traits are modelled as a
// per-shape customization point (`Measured2dImpl` / `Measured3dImpl`), a concept naming it, and
// free functions standing in for the trait methods. The formulas below are Bevy's, term for term.

/** @brief Customization point for Bevy's `Measured2d` methods. */
template <typename T>
struct Measured2dImpl;

/** @brief Satisfied when `Measured2dImpl<T>` provides the Bevy `area` and `perimeter` methods. */
template <typename T>
concept Measured2d = requires(const T& shape) {
    { Measured2dImpl<T>::area(shape) } -> std::same_as<float>;
    { Measured2dImpl<T>::perimeter(shape) } -> std::same_as<float>;
};

/** @brief Get the area of `shape` (Bevy `Measured2d::area`). */
template <Measured2d T>
constexpr float area(const T& shape) {
    return Measured2dImpl<T>::area(shape);
}

/** @brief Get the perimeter of `shape` (Bevy `Measured2d::perimeter`). */
template <Measured2d T>
constexpr float perimeter(const T& shape) {
    return Measured2dImpl<T>::perimeter(shape);
}

/** @brief Customization point for Bevy's `Measured3d` methods. */
template <typename T>
struct Measured3dImpl;

/** @brief Satisfied when `Measured3dImpl<T>` provides the Bevy `area` and `volume` methods. */
template <typename T>
concept Measured3d = requires(const T& shape) {
    { Measured3dImpl<T>::area(shape) } -> std::same_as<float>;
    { Measured3dImpl<T>::volume(shape) } -> std::same_as<float>;
};

/** @brief Get the surface area of `shape` (Bevy `Measured3d::area`). */
template <Measured3d T>
constexpr float area(const T& shape) {
    return Measured3dImpl<T>::area(shape);
}

/** @brief Get the volume of `shape` (Bevy `Measured3d::volume`). */
template <Measured3d T>
constexpr float volume(const T& shape) {
    return Measured3dImpl<T>::volume(shape);
}

/** @brief Bevy `Measured2d for Circle` (`dim2.rs:83`). */
template <>
struct Measured2dImpl<Circle> {
    static constexpr float area(const Circle& circle) noexcept {
        return std::numbers::pi_v<float> * (circle.radius * circle.radius);
    }
    static constexpr float perimeter(const Circle& circle) noexcept {
        return 2.0f * std::numbers::pi_v<float> * circle.radius;
    }
};

/** @brief Bevy `Measured2d for CircularSector` (`dim2.rs:307`). */
template <>
struct Measured2dImpl<CircularSector> {
    static constexpr float area(const CircularSector& sector) noexcept {
        return (sector.arc.radius * sector.arc.radius) * sector.arc.half_angle;
    }
    static constexpr float perimeter(const CircularSector& sector) noexcept {
        // A sector covering at least half a circle has no chord shorter than the diameter.
        if (sector.half_angle() >= std::numbers::pi_v<float>) {
            return sector.arc.radius * 2.0f * std::numbers::pi_v<float>;
        }
        return 2.0f * sector.radius() + sector.arc_length();
    }
};

/** @brief Bevy `Measured2d for CircularSegment` (`dim2.rs:459`). */
template <>
struct Measured2dImpl<CircularSegment> {
    static float area(const CircularSegment& segment) noexcept;
    static float perimeter(const CircularSegment& segment) noexcept;
};

/** @brief Bevy `Measured2d for Ellipse` (`dim2.rs:886`). */
template <>
struct Measured2dImpl<Ellipse> {
    static float area(const Ellipse& ellipse) noexcept;
    static float perimeter(const Ellipse& ellipse) noexcept;
};

/** @brief Bevy `Measured2d for Annulus` (`dim2.rs:1032`). */
template <>
struct Measured2dImpl<Annulus> {
    static constexpr float area(const Annulus& annulus) noexcept {
        return std::numbers::pi_v<float> * ((annulus.outer_circle.radius * annulus.outer_circle.radius) -
                                            (annulus.inner_circle.radius * annulus.inner_circle.radius));
    }
    static constexpr float perimeter(const Annulus& annulus) noexcept {
        return 2.0f * std::numbers::pi_v<float> * (annulus.outer_circle.radius + annulus.inner_circle.radius);
    }
};

/** @brief Bevy `Measured2d for Rhombus` (`dim2.rs:1172`). */
template <>
struct Measured2dImpl<Rhombus> {
    static float area(const Rhombus& rhombus) noexcept;
    static float perimeter(const Rhombus& rhombus) noexcept;
};

/** @brief Bevy `Measured2d for Triangle2d` (`dim2.rs:1775`). */
template <>
struct Measured2dImpl<Triangle2d> {
    static float area(const Triangle2d& triangle) noexcept;
    static float perimeter(const Triangle2d& triangle) noexcept;
};

/** @brief Bevy `Measured2d for Rectangle` (`dim2.rs:1874`). */
template <>
struct Measured2dImpl<Rectangle> {
    static constexpr float area(const Rectangle& rectangle) noexcept {
        return 4.0f * rectangle.half_size.x * rectangle.half_size.y;
    }
    static constexpr float perimeter(const Rectangle& rectangle) noexcept {
        return 4.0f * (rectangle.half_size.x + rectangle.half_size.y);
    }
};

/** @brief Bevy `Measured2d for RegularPolygon` (`dim2.rs:2160`). */
template <>
struct Measured2dImpl<RegularPolygon> {
    static float area(const RegularPolygon& polygon) noexcept;
    static float perimeter(const RegularPolygon& polygon) noexcept;
};

/** @brief Bevy `Measured2d for Capsule2d` (`dim2.rs:2227`). */
template <>
struct Measured2dImpl<Capsule2d> {
    static constexpr float area(const Capsule2d& capsule) noexcept {
        // pi*r^2 + (2r)*l
        return std::numbers::pi_v<float> * (capsule.radius * capsule.radius) +
               ::epix::mesh::area(capsule.to_inner_rectangle());
    }
    static constexpr float perimeter(const Capsule2d& capsule) noexcept {
        // 2pi*r + 2l
        return 2.0f * std::numbers::pi_v<float> * capsule.radius + 4.0f * capsule.half_length;
    }
};

/** @brief Bevy `Measured2d for Ring<P>` (`dim2.rs:2288`). */
template <Primitive2d P>
    requires Measured2d<P>
struct Measured2dImpl<Ring<P>> {
    static constexpr float area(const Ring<P>& ring) noexcept {
        return ::epix::mesh::area(ring.outer_shape) - ::epix::mesh::area(ring.inner_shape);
    }
    static constexpr float perimeter(const Ring<P>& ring) noexcept {
        return ::epix::mesh::perimeter(ring.outer_shape) + ::epix::mesh::perimeter(ring.inner_shape);
    }
};

/** @brief Bevy `Measured2d for Plane3d` (`dim3.rs:162`). */
template <>
struct Measured2dImpl<Plane3d> {
    static constexpr float area(const Plane3d& plane) noexcept {
        return (plane.half_size.x * plane.half_size.y) * 4.0f;
    }
    static constexpr float perimeter(const Plane3d& plane) noexcept {
        return (plane.half_size.x + plane.half_size.y) * 4.0f;
    }
};

/** @brief Bevy `Measured2d for Triangle3d` (`dim3.rs:1410`). */
template <>
struct Measured2dImpl<Triangle3d> {
    static float area(const Triangle3d& triangle) noexcept;
    static float perimeter(const Triangle3d& triangle) noexcept;
};

/** @brief Bevy `Measured3d for Sphere` (`dim3.rs:77`). */
template <>
struct Measured3dImpl<Sphere> {
    static constexpr float area(const Sphere& sphere) noexcept {
        return 4.0f * std::numbers::pi_v<float> * (sphere.radius * sphere.radius);
    }
    static constexpr float volume(const Sphere& sphere) noexcept {
        return 4.0f * (std::numbers::pi_v<float> / 3.0f) * (sphere.radius * sphere.radius * sphere.radius);
    }
};

/** @brief Bevy `Measured3d for Cuboid` (`dim3.rs:756`). */
template <>
struct Measured3dImpl<Cuboid> {
    static constexpr float area(const Cuboid& cuboid) noexcept {
        return 8.0f * (cuboid.half_size.x * cuboid.half_size.y + cuboid.half_size.y * cuboid.half_size.z +
                       cuboid.half_size.x * cuboid.half_size.z);
    }
    static constexpr float volume(const Cuboid& cuboid) noexcept {
        return 8.0f * cuboid.half_size.x * cuboid.half_size.y * cuboid.half_size.z;
    }
};

/** @brief Bevy `Measured3d for Cylinder` (`dim3.rs:836`). */
template <>
struct Measured3dImpl<Cylinder> {
    static constexpr float area(const Cylinder& cylinder) noexcept {
        return 2.0f * std::numbers::pi_v<float> * cylinder.radius * (cylinder.radius + 2.0f * cylinder.half_height);
    }
    static float volume(const Cylinder& cylinder) noexcept;
};

/** @brief Bevy `Measured3d for Capsule3d` (`dim3.rs:903`). */
template <>
struct Measured3dImpl<Capsule3d> {
    static constexpr float area(const Capsule3d& capsule) noexcept {
        // Modified version of 2pi * r * (2r + h)
        return 4.0f * std::numbers::pi_v<float> * capsule.radius * (capsule.radius + capsule.half_length);
    }
    static constexpr float volume(const Capsule3d& capsule) noexcept {
        // Modified version of pi * r^2 * (4/3 * r + a)
        const float diameter = capsule.radius * 2.0f;
        return std::numbers::pi_v<float> * capsule.radius * diameter * (diameter / 3.0f + capsule.half_length);
    }
};

/** @brief Bevy `Measured3d for Cone` (`dim3.rs:989`). */
template <>
struct Measured3dImpl<Cone> {
    static float area(const Cone& cone) noexcept;
    static float volume(const Cone& cone) noexcept;
};

/** @brief Bevy `Measured3d for ConicalFrustum` (`dim3.rs:1085`). */
template <>
struct Measured3dImpl<ConicalFrustum> {
    static float area(const ConicalFrustum& frustum) noexcept;
    static float volume(const ConicalFrustum& frustum) noexcept;
};

/** @brief Bevy `Measured3d for Torus` (`dim3.rs:1215`). */
template <>
struct Measured3dImpl<Torus> {
    static constexpr float area(const Torus& torus) noexcept {
        constexpr float pi_squared = std::numbers::pi_v<float> * std::numbers::pi_v<float>;
        return 4.0f * pi_squared * torus.major_radius * torus.minor_radius;
    }
    static constexpr float volume(const Torus& torus) noexcept {
        constexpr float pi_squared = std::numbers::pi_v<float> * std::numbers::pi_v<float>;
        return 2.0f * pi_squared * torus.major_radius * (torus.minor_radius * torus.minor_radius);
    }
};

/** @brief Bevy `Measured3d for Tetrahedron` (`dim3.rs:1512`). */
template <>
struct Measured3dImpl<Tetrahedron> {
    static float area(const Tetrahedron& tetrahedron) noexcept;
    static float volume(const Tetrahedron& tetrahedron) noexcept;
};

static_assert(Measured2d<Circle>);
static_assert(Measured2d<CircularSector>);
static_assert(Measured2d<CircularSegment>);
static_assert(Measured2d<Ellipse>);
static_assert(Measured2d<Annulus>);
static_assert(Measured2d<Rhombus>);
static_assert(Measured2d<Triangle2d>);
static_assert(Measured2d<Rectangle>);
static_assert(Measured2d<RegularPolygon>);
static_assert(Measured2d<Capsule2d>);
static_assert(Measured2d<Plane3d>);
static_assert(Measured2d<Triangle3d>);
static_assert(Measured2d<Ring<Circle>>);
static_assert(Measured2d<Ring<Capsule2d>>);
static_assert(Measured3d<Sphere>);
static_assert(Measured3d<Cuboid>);
static_assert(Measured3d<Cylinder>);
static_assert(Measured3d<Capsule3d>);
static_assert(Measured3d<Cone>);
static_assert(Measured3d<ConicalFrustum>);
static_assert(Measured3d<Torus>);
static_assert(Measured3d<Tetrahedron>);

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
static_assert(Meshable<Sphere>);
static_assert(MeshBuilder<SphereMeshBuilder>);
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
static_assert(MeshBuilder<Capsule3dMeshBuilder>);
static_assert(MeshBuilder<CuboidMeshBuilder>);
static_assert(MeshBuilder<PlaneMeshBuilder>);
static_assert(MeshBuilder<Segment3dMeshBuilder>);
static_assert(MeshBuilder<Polyline3dMeshBuilder>);
static_assert(MeshBuilder<Triangle3dMeshBuilder>);
static_assert(MeshBuilder<TetrahedronMeshBuilder>);
/** @brief One segment of the perimeter of an extrudable mesh (Bevy
 * `PerimeterSegment`, `extrusion.rs:9-51`).
 *
 * Bevy derives nothing on this type and defines no `Default`, so the C++
 * counterpart has neither. The `indices` of both alternatives refer to the
 * vertices generated by the underlying 2D primitive's builder and are ordered
 * so that the outside of the mesh is to the right when walking them; at least
 * two indices are required for geometry to be rendered. */
EPIX_EXPORT struct PerimeterSegment {
    /** @brief A segment shaded smooth, appropriate for curved shapes.
     *
     * Each interior normal is interpolated from its neighbours; the first and
     * last vertices have only one neighbour, so their normals are supplied. */
    struct Smooth {
        /** @brief Normal of the first vertex. */
        glm::vec2 first_normal{0.0f, 1.0f};
        /** @brief Normal of the last vertex. */
        glm::vec2 last_normal{0.0f, 1.0f};
        /** @brief Vertex indices of the underlying 2D mesh. */
        std::vector<std::uint32_t> indices;
        bool operator==(const Smooth&) const = default;
    };
    /** @brief A segment shaded flat, so its faces keep hard edges. */
    struct Flat {
        /** @brief Vertex indices of the underlying 2D mesh. */
        std::vector<std::uint32_t> indices;
        bool operator==(const Flat&) const = default;
    };

    std::variant<Smooth, Flat> value{Flat{}};

    bool operator==(const PerimeterSegment&) const = default;
};

/** @brief Vertices each layer of an extrusion contributes for one segment
 * (Bevy `PerimeterSegment::vertices_per_layer`, private there: `indices.len()`
 * for `Smooth`, `2 * (indices.len() - 1)` for `Flat`). */
EPIX_EXPORT std::uint32_t vertices_per_layer(const PerimeterSegment& segment) noexcept;

/** @brief Indices each mantel segment contributes for one segment (Bevy
 * `PerimeterSegment::indices_per_segment`: `6 * (indices.len() - 1)`). */
EPIX_EXPORT std::size_t indices_per_segment(const PerimeterSegment& segment) noexcept;

/** @brief Per-builder extrusion perimeter (Bevy `Extrudable`,
 * `extrusion.rs:76-86`). Bevy's supertrait bound is `MeshBuilder` alone, it has
 * one required method, and it has no blanket impls: all twelve impls are
 * concrete and live in `dim2.rs`. */
template <typename T>
struct ExtrudableImpl;

/** @brief Satisfied when `ExtrudableImpl<T>` provides the Bevy `perimeter`
 * method for a mesh builder. */
template <typename T>
concept Extrudable = MeshBuilder<T> && requires(const T& builder) {
    { ExtrudableImpl<T>::perimeter(builder) } -> std::same_as<std::vector<PerimeterSegment>>;
};

/** @brief The perimeter segments of a 2D shape's builder (Bevy
 * `Extrudable::perimeter`). */
template <Extrudable T>
std::vector<PerimeterSegment> perimeter(const T& builder) {
    return ExtrudableImpl<T>::perimeter(builder);
}

/** @brief `0, 1, ..., count - 1` (Bevy's `0..count`). */
inline std::vector<std::uint32_t> perimeter_range(std::uint32_t count) {
    std::vector<std::uint32_t> indices;
    indices.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) indices.push_back(index);
    return indices;
}

/** @brief `start, start + step, ...` below `end` (Bevy's `(start..end).step_by`). */
inline std::vector<std::uint32_t> perimeter_step_range(std::uint32_t start, std::uint32_t end, std::uint32_t step) {
    std::vector<std::uint32_t> indices;
    for (std::uint32_t index = start; index < end; index += step) indices.push_back(index);
    return indices;
}

/** @brief `CircleMeshBuilder`'s perimeter (`dim2.rs:70-78`). */
template <>
struct ExtrudableImpl<CircleMeshBuilder> {
    static std::vector<PerimeterSegment> perimeter(const CircleMeshBuilder& builder) {
        auto indices = perimeter_range(builder.resolution);
        indices.push_back(0);
        return {PerimeterSegment{PerimeterSegment::Smooth{.first_normal = glm::vec2{0.0f, 1.0f},
                                                          .last_normal  = glm::vec2{0.0f, 1.0f},
                                                          .indices      = std::move(indices)}}};
    }
};

/** @brief `EllipseMeshBuilder`'s perimeter (`dim2.rs:616-624`). */
template <>
struct ExtrudableImpl<EllipseMeshBuilder> {
    static std::vector<PerimeterSegment> perimeter(const EllipseMeshBuilder& builder) {
        auto indices = perimeter_range(builder.resolution);
        indices.push_back(0);
        return {PerimeterSegment{PerimeterSegment::Smooth{.first_normal = glm::vec2{0.0f, 1.0f},
                                                          .last_normal  = glm::vec2{0.0f, 1.0f},
                                                          .indices      = std::move(indices)}}};
    }
};

/** @brief `ConvexPolygonMeshBuilder`'s perimeter (`dim2.rs:450-456`). */
template <>
struct ExtrudableImpl<ConvexPolygonMeshBuilder> {
    static std::vector<PerimeterSegment> perimeter(const ConvexPolygonMeshBuilder& builder) {
        auto indices = perimeter_range(static_cast<std::uint32_t>(builder.vertices.size()));
        indices.push_back(0);
        return {PerimeterSegment{PerimeterSegment::Flat{.indices = std::move(indices)}}};
    }
};

/** @brief `RegularPolygonMeshBuilder`'s perimeter (`dim2.rs:524-530`). */
template <>
struct ExtrudableImpl<RegularPolygonMeshBuilder> {
    static std::vector<PerimeterSegment> perimeter(const RegularPolygonMeshBuilder& builder) {
        auto indices = perimeter_range(builder.sides);
        indices.push_back(0);
        return {PerimeterSegment{PerimeterSegment::Flat{.indices = std::move(indices)}}};
    }
};

/** @brief `RhombusMeshBuilder`'s perimeter (`dim2.rs:922-928`). */
template <>
struct ExtrudableImpl<RhombusMeshBuilder> {
    static std::vector<PerimeterSegment> perimeter(const RhombusMeshBuilder&) {
        return {PerimeterSegment{PerimeterSegment::Flat{.indices = {0, 1, 2, 3, 0}}}};
    }
};

/** @brief `RectangleMeshBuilder`'s perimeter (`dim2.rs:1079-1085`). */
template <>
struct ExtrudableImpl<RectangleMeshBuilder> {
    static std::vector<PerimeterSegment> perimeter(const RectangleMeshBuilder&) {
        return {PerimeterSegment{PerimeterSegment::Flat{.indices = {0, 1, 2, 3, 0}}}};
    }
};

/** @brief `Triangle2dMeshBuilder`'s perimeter (`dim2.rs:1002-1015`). The index
 * order follows the triangle's winding so the outside stays to the right. */
template <>
struct ExtrudableImpl<Triangle2dMeshBuilder> {
    static std::vector<PerimeterSegment> perimeter(const Triangle2dMeshBuilder& builder) {
        const bool counter_clockwise = builder.triangle.winding_order() == WindingOrder::CounterClockwise;
        return {PerimeterSegment{PerimeterSegment::Flat{.indices = counter_clockwise
                                                                       ? std::vector<std::uint32_t>{0, 1, 2, 0}
                                                                       : std::vector<std::uint32_t>{2, 1, 0, 2}}}};
    }
};

/** @brief `CircularSectorMeshBuilder`'s perimeter (`dim2.rs:224-240`). */
template <>
struct ExtrudableImpl<CircularSectorMeshBuilder> {
    static std::vector<PerimeterSegment> perimeter(const CircularSectorMeshBuilder& builder) {
        const float half_angle = builder.sector.arc.half_angle;
        const glm::vec2 first_normal{std::sin(half_angle), std::cos(half_angle)};
        const glm::vec2 last_normal{-std::sin(half_angle), std::cos(half_angle)};
        std::vector<PerimeterSegment> segments;
        segments.push_back(PerimeterSegment{PerimeterSegment::Flat{.indices = {builder.resolution, 0, 1}}});
        segments.push_back(
            PerimeterSegment{PerimeterSegment::Smooth{.first_normal = first_normal,
                                                      .last_normal  = last_normal,
                                                      .indices = perimeter_step_range(1, builder.resolution + 1, 1)}});
        return segments;
    }
};

/** @brief `CircularSegmentMeshBuilder`'s perimeter (`dim2.rs:371-387`). */
template <>
struct ExtrudableImpl<CircularSegmentMeshBuilder> {
    static std::vector<PerimeterSegment> perimeter(const CircularSegmentMeshBuilder& builder) {
        const float half_angle = builder.segment.arc.half_angle;
        const glm::vec2 first_normal{std::sin(half_angle), std::cos(half_angle)};
        const glm::vec2 last_normal{-std::sin(half_angle), std::cos(half_angle)};
        std::vector<PerimeterSegment> segments;
        segments.push_back(PerimeterSegment{PerimeterSegment::Flat{.indices = {builder.resolution, 0, 1}}});
        segments.push_back(
            PerimeterSegment{PerimeterSegment::Smooth{.first_normal = first_normal,
                                                      .last_normal  = last_normal,
                                                      .indices = perimeter_step_range(1, builder.resolution + 1, 1)}});
        return segments;
    }
};

/** @brief `AnnulusMeshBuilder`'s perimeter (`dim2.rs:825-841`). The inner hole
 * is emitted first and wound opposite to the outer perimeter. */
template <>
struct ExtrudableImpl<AnnulusMeshBuilder> {
    static std::vector<PerimeterSegment> perimeter(const AnnulusMeshBuilder& builder) {
        const std::uint32_t vertex_count = 2 * builder.resolution;
        auto inner                       = perimeter_step_range(0, vertex_count, 2);
        inner.push_back(0);
        std::ranges::reverse(inner);
        auto outer = perimeter_step_range(1, vertex_count, 2);
        outer.push_back(1);
        std::vector<PerimeterSegment> segments;
        segments.push_back(PerimeterSegment{PerimeterSegment::Smooth{.first_normal = glm::vec2{0.0f, -1.0f},
                                                                     .last_normal  = glm::vec2{0.0f, -1.0f},
                                                                     .indices      = std::move(inner)}});
        segments.push_back(PerimeterSegment{PerimeterSegment::Smooth{
            .first_normal = glm::vec2{0.0f, 1.0f}, .last_normal = glm::vec2{0.0f, 1.0f}, .indices = std::move(outer)}});
        return segments;
    }
};

/** @brief `Capsule2dMeshBuilder`'s perimeter (`dim2.rs:1221-1245`): top
 * semi-circle, left edge, bottom semi-circle, right edge. */
template <>
struct ExtrudableImpl<Capsule2dMeshBuilder> {
    static std::vector<PerimeterSegment> perimeter(const Capsule2dMeshBuilder& builder) {
        const std::uint32_t resolution = builder.resolution;
        std::vector<PerimeterSegment> segments;
        segments.push_back(PerimeterSegment{PerimeterSegment::Smooth{.first_normal = glm::vec2{1.0f, 0.0f},
                                                                     .last_normal  = glm::vec2{-1.0f, 0.0f},
                                                                     .indices      = perimeter_range(resolution)}});
        segments.push_back(PerimeterSegment{PerimeterSegment::Flat{.indices = {resolution - 1, resolution}}});
        segments.push_back(
            PerimeterSegment{PerimeterSegment::Smooth{.first_normal = glm::vec2{-1.0f, 0.0f},
                                                      .last_normal  = glm::vec2{1.0f, 0.0f},
                                                      .indices = perimeter_step_range(resolution, 2 * resolution, 1)}});
        segments.push_back(PerimeterSegment{PerimeterSegment::Flat{.indices = {2 * resolution - 1, 0}}});
        return segments;
    }
};

/** @brief `RingMeshBuilder<P>`'s perimeter (`dim2.rs:1441-1505`): the outer
 * perimeter followed by the inner one, reversed, offset past the outer
 * vertices, and with its smooth normals negated and swapped.
 *
 * Bevy panics when the inner and outer meshes have different vertex counts; Epix
 * reports the same condition as `std::invalid_argument`, the module's
 * typed-exception convention for a method with no error channel. */
template <typename P>
    requires(Meshable<P> && Extrudable<typename RingMeshBuilder<P>::ShapeMeshBuilder>)
struct ExtrudableImpl<RingMeshBuilder<P>> {
    static std::vector<PerimeterSegment> perimeter(const RingMeshBuilder<P>& builder) {
        using ShapeMeshBuilder = typename RingMeshBuilder<P>::ShapeMeshBuilder;
        const auto outer_vertex_count =
            static_cast<std::uint32_t>(builder.outer_shape_builder.build().count_vertices());
        const auto inner_vertex_count =
            static_cast<std::uint32_t>(builder.inner_shape_builder.build().count_vertices());
        if (outer_vertex_count != inner_vertex_count) {
            throw std::invalid_argument("The inner and outer meshes of a ring should have the same number of vertices");
        }

        auto segments       = ExtrudableImpl<ShapeMeshBuilder>::perimeter(builder.outer_shape_builder);
        auto inner_segments = ExtrudableImpl<ShapeMeshBuilder>::perimeter(builder.inner_shape_builder);
        std::ranges::reverse(inner_segments);
        for (auto& segment : inner_segments) {
            std::visit(
                [outer_vertex_count](auto& value) {
                    using Alternative = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::same_as<Alternative, PerimeterSegment::Smooth>) {
                        // Normals are negated *and* swapped, then the indices are
                        // reversed and moved past the outer perimeter.
                        const auto first   = value.first_normal;
                        value.first_normal = -value.last_normal;
                        value.last_normal  = -first;
                    }
                    std::ranges::reverse(value.indices);
                    for (auto& index : value.indices) index += outer_vertex_count;
                },
                segment.value);
        }
        segments.insert(segments.end(), std::make_move_iterator(inner_segments.begin()),
                        std::make_move_iterator(inner_segments.end()));
        return segments;
    }
};

static_assert(Extrudable<CircleMeshBuilder>);
static_assert(Extrudable<EllipseMeshBuilder>);
static_assert(Extrudable<ConvexPolygonMeshBuilder>);
static_assert(Extrudable<RegularPolygonMeshBuilder>);
static_assert(Extrudable<RhombusMeshBuilder>);
static_assert(Extrudable<RectangleMeshBuilder>);
static_assert(Extrudable<Triangle2dMeshBuilder>);
static_assert(Extrudable<CircularSectorMeshBuilder>);
static_assert(Extrudable<CircularSegmentMeshBuilder>);
static_assert(Extrudable<AnnulusMeshBuilder>);
static_assert(Extrudable<Capsule2dMeshBuilder>);
static_assert(Extrudable<RingMeshBuilder<Circle>>);
// Bevy implements `Extrudable` for neither of these, so `Extrusion<Segment2d>`
// and `Extrusion<Polyline2d>` can mesh in neither engine.
static_assert(!Extrudable<Segment2dMeshBuilder>);
static_assert(!Extrudable<Polyline2dMeshBuilder>);

static_assert(Meshable<Ring<Circle>>);
static_assert(MeshBuilder<RingMeshBuilder<Circle>>);

// ─── Extrusion (Bevy `bevy_mesh/src/primitives/extrusion.rs`) ───────────────────────────────────
//
// `Extrusion<T>` thickens a 2D shape into a prism: the 2D mesh is the front cap, a copy mirrored
// through z is the back cap, and a mantel of `segments + 1` layers connects them around the
// perimeter supplied by the 2D builder's `Extrudable` impl.

/** @brief Bevy's `Vec2`/`Vec3::normalize_or_zero`: normalize the vector through the reciprocal of
 * its length, or return the zero vector when that reciprocal is zero or non-finite. */
template <typename Vec>
Vec normalize_or_zero(const Vec& value) {
    return detail::normalize_or_zero(value);
}

/** @brief The four shapes Bevy declares `ExtrusionBuilder::<Shape>::resolution` for
 * (`extrusion.rs:143-173`): `Circle`, `Ellipse`, `Annulus` and `Capsule2d`. */
template <typename P>
concept ExtrusionResolutionShape =
    std::same_as<P, Circle> || std::same_as<P, Ellipse> || std::same_as<P, Annulus> || std::same_as<P, Capsule2d>;

/** @brief Builder used to create an extrusion mesh (Bevy `ExtrusionBuilder<P>`,
 * `extrusion.rs:104-173`).
 *
 * Bevy derives nothing on this struct, so the C++ counterpart has public fields, no default
 * constructor and no comparison. `base_builder` is the 2D shape's builder, whose `perimeter`
 * supplies the mantel's cross-section. */
template <Primitive2d P>
    requires Meshable<P>
struct ExtrusionBuilder {
    using ShapeMeshBuilder = decltype(std::declval<const P&>().mesh());

    /** @brief The 2D shape's builder, which produces both caps. */
    ShapeMeshBuilder base_builder{};
    /** @brief Half of the extrusion's depth. */
    float half_depth = 0.0f;
    /** @brief Number of segments along the depth. Must be greater than `0` for the geometry of the
     * mantel to be generated. */
    std::size_t segments = 1;

    /** @brief Create a builder from the 2D `base_shape` and the full `depth` (Bevy `new`,
     * `extrusion.rs:253-259`). */
    ExtrusionBuilder(const P& base_shape, float depth)
        : base_builder(base_shape.mesh()), half_depth(depth * 0.5f), segments(1) {}

    /** @brief Create a builder from an already-configured 2D builder, which is how Bevy's
     * `Meshable for Extrusion<P>::mesh` and its consuming setters construct one. */
    ExtrusionBuilder(ShapeMeshBuilder base_builder, float half_depth, std::size_t segments)
        : base_builder(std::move(base_builder)), half_depth(half_depth), segments(segments) {}

    /** @brief Set the number of segments along the depth (Bevy `ExtrusionBuilder::segments`).
     *
     * Named `with_segments` because the field already carries Bevy's name. */
    ExtrusionBuilder with_segments(std::size_t value) const {
        return ExtrusionBuilder{base_builder, half_depth, value};
    }

    /** @brief Apply `func` to the inner 2D builder (Bevy `ExtrusionBuilder::with_inner`). Unlike
     * `RingMeshBuilder::with_inner`, this touches `base_builder` only, as in Bevy. */
    template <typename Func>
    ExtrusionBuilder with_inner(Func func) const {
        return ExtrusionBuilder{func(base_builder), half_depth, segments};
    }

    /** @brief Set the number of vertices used for each end of the extrusion (Bevy's
     * `ExtrusionBuilder::<Shape>::resolution`).
     *
     * Bevy declares this inherent method for exactly four shapes — `Circle`, `Ellipse`, `Annulus`
     * and `Capsule2d` (`extrusion.rs:143-173`) — even though six 2D builders carry a `resolution`
     * field, so the constraint reproduces that set rather than exposing it for every shape whose
     * builder happens to have the field. */
    ExtrusionBuilder resolution(std::uint32_t value) const
        requires ExtrusionResolutionShape<P>
    {
        ShapeMeshBuilder inner = base_builder;
        inner.resolution       = value;
        return ExtrusionBuilder{std::move(inner), half_depth, segments};
    }

    /** @brief Build the extrusion mesh (Bevy `MeshBuilder for ExtrusionBuilder<P>::build`,
     * `extrusion.rs:175-427`).
     *
     * The 2D base mesh becomes the front cap translated to `+half_depth` with its UVs halved; a copy
     * scaled by `(1, 1, -1)` becomes the back cap with `u += 0.5` and every triangle's winding
     * reversed; and the mantel walks `segments + 1` layers from `+half_depth` down to `-half_depth`
     * around the perimeter. A zero `half_depth` merges the two caps and emits no mantel.
     *
     * Bevy panics when the base mesh has no vertex positions or is not a `TriangleList`; Epix throws
     * instead, which is how this module reports conditions Bevy has no error channel for. */
    Mesh build() const
        requires Extrudable<ShapeMeshBuilder>
    {
        // Create and move the base mesh to the front.
        Mesh front_face = base_builder.build().translated_by(glm::vec3{0.0f, 0.0f, half_depth});

        // Move the uvs of the front face to be between (0, 0) and (0.5, 0.5).
        if (auto uvs = front_face.attribute_mut(Mesh::ATTRIBUTE_UV_0)) {
            if (auto* values = uvs->get().get_if<VertexAttributeValues::Float32x2>()) {
                for (auto& uv : *values) uv = {uv[0] * 0.5f, uv[1] * 0.5f};
            }
        }

        const wgpu::PrimitiveTopology topology = front_face.get_primitive_type();

        // Flip the normals, etc. and move the mesh to the back.
        Mesh back_face = Mesh{front_face}.scaled_by(glm::vec3{1.0f, 1.0f, -1.0f});

        // Move the uvs of the back face to be between (0.5, 0) and (1, 0.5).
        if (auto uvs = back_face.attribute_mut(Mesh::ATTRIBUTE_UV_0)) {
            if (auto* values = uvs->get().get_if<VertexAttributeValues::Float32x2>()) {
                for (auto& uv : *values) uv = {uv[0] + 0.5f, uv[1]};
            }
        }

        // By swapping the first and second indices of each triangle the winding order is inverted,
        // making the mesh visible from the other side.
        if (auto indices = back_face.indices_mut()) {
            if (topology != wgpu::PrimitiveTopology::eTriangleList) {
                throw std::runtime_error(
                    "Meshes used with Extrusions must have a primitive topology of `PrimitiveTopology::TriangleList`");
            }
            const auto swap_triangle_pairs = [](std::vector<std::uint16_t>& values) {
                for (std::size_t index = 0; index + 2 < values.size(); index += 3) {
                    std::swap(values[index], values[index + 1]);
                }
            };
            const auto swap_triangle_pairs_u32 = [](std::vector<std::uint32_t>& values) {
                for (std::size_t index = 0; index + 2 < values.size(); index += 3) {
                    std::swap(values[index], values[index + 1]);
                }
            };
            if (auto* values = indices->get().as_u16()) {
                swap_triangle_pairs(*values);
            } else if (auto* values = indices->get().as_u32()) {
                swap_triangle_pairs_u32(*values);
            }
        }

        // An extrusion of depth 0 does not need a mantel.
        if (half_depth == 0.0f) {
            auto merged = front_face.merge(back_face);
            if (!merged) throw std::runtime_error(merged.error().to_string());
            return front_face;
        }

        const auto* cap_vertices =
            front_face.attribute(Mesh::ATTRIBUTE_POSITION)->get().get_if<VertexAttributeValues::Float32x3>();
        if (cap_vertices == nullptr) throw std::runtime_error("The base mesh did not have vertex positions");

        assert(segments > 0);

        const std::size_t layers      = segments + 1;
        const float layer_depth_delta = half_depth * 2.0f / static_cast<float>(segments);

        const std::vector<PerimeterSegment> perimeter = ::epix::mesh::perimeter(base_builder);

        std::size_t vertex_count = 0;
        std::size_t index_count  = 0;
        for (const auto& segment : perimeter) {
            vertex_count += layers * vertices_per_layer(segment);
            index_count += segments * indices_per_segment(segment);
        }

        std::vector<VertexAttributeValues::Float32x3Value> positions;
        std::vector<VertexAttributeValues::Float32x3Value> normals;
        std::vector<VertexAttributeValues::Float32x2Value> uvs;
        std::vector<std::uint32_t> indices;
        positions.reserve(vertex_count);
        normals.reserve(vertex_count);
        uvs.reserve(vertex_count);
        indices.reserve(index_count);

        const auto vertex_uv_y = [this](std::size_t layer) {
            return 0.5f + 0.5f * static_cast<float>(layer) / static_cast<float>(segments);
        };

        // The amount of horizontal space allocated to each segment of the perimeter.
        const float uv_segment_delta = 1.0f / static_cast<float>(perimeter.size());
        for (std::size_t segment_index = 0; segment_index < perimeter.size(); ++segment_index) {
            // The start of the x range of the area of the current perimeter segment.
            const float uv_start = static_cast<float>(segment_index) * uv_segment_delta;

            std::visit(
                [&](const auto& segment) {
                    using Segment                                     = std::remove_cvref_t<decltype(segment)>;
                    const std::vector<std::uint32_t>& segment_indices = segment.indices;
                    const float uv_delta = uv_segment_delta / static_cast<float>(segment_indices.size() - 1);

                    if constexpr (std::same_as<Segment, PerimeterSegment::Flat>) {
                        for (std::size_t edge = 0; edge + 1 < segment_indices.size(); ++edge) {
                            const float uv_x = uv_start + uv_delta * static_cast<float>(edge);
                            const auto& a    = (*cap_vertices)[segment_indices[edge]];
                            const auto& b    = (*cap_vertices)[segment_indices[edge + 1]];

                            // The index of the next vertex added to the mantel.
                            const std::uint32_t index = static_cast<std::uint32_t>(positions.size());

                            for (std::size_t layer = 0; layer < layers; ++layer) {
                                const float z = a[2] - layer_depth_delta * static_cast<float>(layer);
                                positions.push_back({a[0], a[1], z});
                                positions.push_back({b[0], b[1], z});

                                // Mantel UVs are between (0, 0.5) and (1, 1).
                                const float uv_y = vertex_uv_y(layer);
                                uvs.push_back({uv_x, uv_y});
                                uvs.push_back({uv_x + uv_delta, uv_y});
                            }

                            // The normal is the normal of the line segment connecting `a` and `b`.
                            const glm::vec3 normal = normalize_or_zero(glm::vec3{b[1] - a[1], a[0] - b[0], 0.0f});
                            normals.insert(normals.end(), 2 * layers,
                                           VertexAttributeValues::Float32x3Value{normal.x, normal.y, normal.z});

                            for (std::size_t s = 0; s < segments; ++s) {
                                const std::uint32_t base_index = index + static_cast<std::uint32_t>(2 * s);
                                indices.insert(indices.end(), {base_index, base_index + 2, base_index + 1,
                                                               base_index + 1, base_index + 2, base_index + 3});
                            }
                        }
                    } else {
                        // The indices of a smooth segment are added after its vertices, so the index
                        // of its first vertex has to be remembered.
                        const std::uint32_t base_index = static_cast<std::uint32_t>(positions.size());

                        // The first vertex's normal is supplied by the segment.
                        if (!segment_indices.empty()) {
                            const auto& first = (*cap_vertices)[segment_indices.front()];
                            for (std::size_t layer = 0; layer < layers; ++layer) {
                                const float z = first[2] - layer_depth_delta * static_cast<float>(layer);
                                positions.push_back({first[0], first[1], z});
                                uvs.push_back({uv_start, vertex_uv_y(layer)});
                            }
                            normals.insert(normals.end(), layers,
                                           VertexAttributeValues::Float32x3Value{segment.first_normal.x,
                                                                                 segment.first_normal.y, 0.0f});
                        }

                        // Every vertex between the first and the last computes its normal from its
                        // two neighbours: closer vertices have a stronger effect on the normal.
                        for (std::size_t column = 1; column + 1 < segment_indices.size(); ++column) {
                            const float uv_x = uv_start + uv_delta * static_cast<float>(column);
                            const auto& a    = (*cap_vertices)[segment_indices[column - 1]];
                            const auto& b    = (*cap_vertices)[segment_indices[column]];
                            const auto& c    = (*cap_vertices)[segment_indices[column + 1]];

                            for (std::size_t layer = 0; layer < layers; ++layer) {
                                const float z = b[2] - layer_depth_delta * static_cast<float>(layer);
                                positions.push_back({b[0], b[1], z});
                                uvs.push_back({uv_x, vertex_uv_y(layer)});
                            }

                            const glm::vec2 ab     = normalize_or_zero(glm::vec2{b[0], b[1]} - glm::vec2{a[0], a[1]});
                            const glm::vec2 bc     = normalize_or_zero(glm::vec2{c[0], c[1]} - glm::vec2{b[0], b[1]});
                            const glm::vec2 sum    = ab + bc;
                            const glm::vec3 normal = normalize_or_zero(glm::vec3{sum.y, -sum.x, 0.0f});
                            normals.insert(normals.end(), layers,
                                           VertexAttributeValues::Float32x3Value{normal.x, normal.y, normal.z});
                        }

                        // The last vertex's normal is supplied by the segment.
                        if (!segment_indices.empty()) {
                            const auto& last = (*cap_vertices)[segment_indices.back()];
                            for (std::size_t layer = 0; layer < layers; ++layer) {
                                const float z = last[2] - layer_depth_delta * static_cast<float>(layer);
                                positions.push_back({last[0], last[1], z});
                                uvs.push_back({uv_start + uv_segment_delta, vertex_uv_y(layer)});
                            }
                            normals.insert(normals.end(), layers,
                                           VertexAttributeValues::Float32x3Value{segment.last_normal.x,
                                                                                 segment.last_normal.y, 0.0f});
                        }

                        const std::uint32_t columns = static_cast<std::uint32_t>(segment_indices.size());
                        const auto segment_layers   = static_cast<std::uint32_t>(layers);
                        for (std::uint32_t s = 0; s < static_cast<std::uint32_t>(segments); ++s) {
                            for (std::uint32_t column = 0; column + 1 < columns; ++column) {
                                const std::uint32_t index = base_index + s + column * segment_layers;
                                indices.insert(indices.end(),
                                               {index, index + 1, index + segment_layers, index + segment_layers,
                                                index + 1, index + segment_layers + 1});
                            }
                        }
                    }
                },
                perimeter[segment_index].value);
        }

        Mesh mantel{wgpu::PrimitiveTopology::eTriangleList, front_face.asset_usage};
        mantel.insert_indices(Indices{std::move(indices)});
        mantel.insert_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions));
        mantel.insert_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals));
        mantel.insert_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));

        auto back_merged = front_face.merge(back_face);
        if (!back_merged) throw std::runtime_error(back_merged.error().to_string());
        auto mantel_merged = front_face.merge(mantel);
        if (!mantel_merged) throw std::runtime_error(mantel_merged.error().to_string());
        return front_face;
    }
};

/** @brief A 3D shape representing an extruded 2D `base_shape` (Bevy `bevy_math`'s `Extrusion<T>`,
 * `dim3.rs:1536-1563`).
 *
 * Extruding a shape thickens it, creating a shape with the same cross-section over the entire depth.
 * Bevy derives `Clone, Copy, Debug, PartialEq` and declares no `Default`, so the C++ counterpart has
 * an implicit copy and `operator==` and no default constructor. */
template <Primitive2d P>
struct Extrusion {
    /** @brief The shape being extruded. */
    P base_shape;
    /** @brief Half of the depth of the extrusion. */
    float half_depth = 0.0f;

    /** @brief Create an `Extrusion` from a `base_shape` and the full `depth` (Bevy `new`). */
    Extrusion(P base_shape, float depth) : base_shape(std::move(base_shape)), half_depth(depth * 0.5f) {}

    /** @brief Create the builder for this extrusion (Bevy `Meshable for Extrusion<P>`,
     * `extrusion.rs:88-102`). Unlike `ExtrusionBuilder::new`, this takes `half_depth` as it stands. */
    auto mesh() const
        requires(Meshable<P> && Extrudable<decltype(std::declval<const P&>().mesh())>)
    {
        return ExtrusionBuilder<P>{base_shape.mesh(), half_depth, 1};
    }

    bool operator==(const Extrusion&) const = default;
};

static_assert(Meshable<Extrusion<Circle>>);
static_assert(MeshBuilder<ExtrusionBuilder<Circle>>);
static_assert(Meshable<Extrusion<CircularSector>>);
static_assert(Meshable<Extrusion<CircularSegment>>);
static_assert(Meshable<Extrusion<ConvexPolygon>>);
static_assert(Meshable<Extrusion<RegularPolygon>>);
static_assert(Meshable<Extrusion<Ellipse>>);
static_assert(Meshable<Extrusion<Annulus>>);
static_assert(Meshable<Extrusion<Rhombus>>);
static_assert(Meshable<Extrusion<Triangle2d>>);
static_assert(Meshable<Extrusion<Rectangle>>);
static_assert(Meshable<Extrusion<Capsule2d>>);
static_assert(Meshable<Extrusion<Ring<Circle>>>);
// Bevy's `Meshable for Extrusion<P>` requires `P::Output: Extrudable`, so neither of these can mesh.
static_assert(!Meshable<Extrusion<Segment2d>>);
static_assert(!Meshable<Extrusion<Polyline2d>>);

/** @brief `Extrusion<P>` is measured from its base shape (Bevy `Measured3d for Extrusion<T>`,
 * `dim3.rs:1565-1575`). */
template <Measured2d P>
struct Measured3dImpl<Extrusion<P>> {
    static float area(const Extrusion<P>& extrusion) {
        return 2.0f * (::epix::mesh::area(extrusion.base_shape) +
                       extrusion.half_depth * ::epix::mesh::perimeter(extrusion.base_shape));
    }
    static float volume(const Extrusion<P>& extrusion) {
        return 2.0f * ::epix::mesh::area(extrusion.base_shape) * extrusion.half_depth;
    }
};

static_assert(Measured3d<Extrusion<Circle>>);

}  // namespace epix::mesh
