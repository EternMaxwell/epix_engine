#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/mesh/mesh.hpp>
#include <glm/glm.hpp>
#include <numbers>
#include <utility>
#endif

namespace epix::mesh {

struct CircleMeshBuilder;
struct CircularSectorMeshBuilder;
struct CircularSegmentMeshBuilder;
struct EllipseMeshBuilder;
class RegularPolygonMeshBuilder;
class RectangleMeshBuilder;

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

/** @brief Builder used to create a regular polygon mesh. */
EPIX_EXPORT class RegularPolygonMeshBuilder {
   public:
    constexpr RegularPolygonMeshBuilder() = default;
    RegularPolygonMeshBuilder(float circumradius, std::uint32_t sides);
    Mesh build() const;

    constexpr float circumradius() const noexcept { return circumradius_; }
    constexpr std::uint32_t sides() const noexcept { return sides_; }

   private:
    float circumradius_  = 0.5f;
    std::uint32_t sides_ = 6;
};

/** @brief Builder used to create a rectangle mesh. */
EPIX_EXPORT class RectangleMeshBuilder {
   public:
    constexpr RectangleMeshBuilder() = default;
    RectangleMeshBuilder(float width, float height);
    constexpr explicit RectangleMeshBuilder(glm::vec2 half_size) : half_size_(half_size) {}
    Mesh build() const;

    constexpr glm::vec2 half_size() const noexcept { return half_size_; }

   private:
    glm::vec2 half_size_{0.5f};
};

static_assert(MeshBuilder<CircleMeshBuilder>);
static_assert(MeshBuilder<CircularSectorMeshBuilder>);
static_assert(MeshBuilder<CircularSegmentMeshBuilder>);
static_assert(MeshBuilder<EllipseMeshBuilder>);
static_assert(MeshBuilder<RegularPolygonMeshBuilder>);
static_assert(MeshBuilder<RectangleMeshBuilder>);
static_assert(Meshable<Circle>);
static_assert(Meshable<CircularSector>);
static_assert(Meshable<CircularSegment>);
static_assert(Meshable<Ellipse>);
static_assert(Meshable<RegularPolygon>);
static_assert(Meshable<Rectangle>);

}  // namespace epix::mesh
