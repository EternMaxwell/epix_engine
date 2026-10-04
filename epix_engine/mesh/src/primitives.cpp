#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <epix/mesh/primitives.hpp>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace epix::mesh {
namespace {
constexpr auto kDefaultMeshAssetUsage = static_cast<assets::RenderAssetUsages>(assets::RenderAssetUsages::MAIN_WORLD |
                                                                               assets::RenderAssetUsages::RENDER_WORLD);

glm::vec2 unit_from_angle(float angle) { return {std::cos(angle), std::sin(angle)}; }

template <typename Vector>
std::optional<Vector> try_normalize_direction(Vector value) noexcept {
    const float length = glm::length(value);
    if (!(length > 0.0f) || !std::isfinite(length)) return std::nullopt;
    return value / length;
}

glm::vec2 rotate_vector(glm::vec2 value, float rotation) noexcept {
    const auto [sine, cosine] = std::pair{std::sin(rotation), std::cos(rotation)};
    return {cosine * value.x - sine * value.y, sine * value.x + cosine * value.y};
}

/** @brief Bevy's `Triangle2d::winding_order` cross product for one vertex triple. */
WindingOrder winding_order(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) noexcept {
    const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (area > std::numeric_limits<float>::epsilon()) return WindingOrder::CounterClockwise;
    if (area < -std::numeric_limits<float>::epsilon()) return WindingOrder::Clockwise;
    return WindingOrder::Invalid;
}

int total_compare(float left, float right) noexcept {
    auto left_bits  = std::bit_cast<std::int32_t>(left);
    auto right_bits = std::bit_cast<std::int32_t>(right);
    left_bits ^= static_cast<std::int32_t>(static_cast<std::uint32_t>(left_bits >> 31) >> 1);
    right_bits ^= static_cast<std::int32_t>(static_cast<std::uint32_t>(right_bits >> 31) >> 1);
    return left_bits < right_bits ? -1 : left_bits > right_bits ? 1 : 0;
}

int xy_compare(glm::vec2 left, glm::vec2 right) noexcept {
    const int x_order = total_compare(left.x, right.x);
    return x_order != 0 ? x_order : total_compare(left.y, right.y);
}

struct PolygonSegment {
    std::size_t edge_index{};
    glm::vec2 left{};
    glm::vec2 right{};
};

struct PolygonSegmentLess {
    bool operator()(const PolygonSegment& left, const PolygonSegment& right) const noexcept {
        const int left_order = total_compare(left.left.y, right.left.y);
        return left_order != 0 ? left_order < 0 : total_compare(left.right.y, right.right.y) < 0;
    }
};

enum class PolygonEndpoint : std::uint8_t { Left, Right };

struct PolygonSweepEvent {
    PolygonSegment segment;
    PolygonEndpoint endpoint{};

    glm::vec2 position() const noexcept { return endpoint == PolygonEndpoint::Left ? segment.left : segment.right; }
};

struct PolygonSegmentOrder {
    std::optional<std::size_t> above;
    std::optional<std::size_t> below;
};

float point_side(glm::vec2 first, glm::vec2 second, glm::vec2 point) noexcept {
    return (second.x - first.x) * (point.y - first.y) - (point.x - first.x) * (second.y - first.y);
}

class PolygonSweepLine {
   public:
    explicit PolygonSweepLine(std::span<const glm::vec2> vertices) : vertices_(vertices) {}

    bool intersects(std::optional<std::size_t> first, std::optional<std::size_t> second) const noexcept {
        if (!first || !second) return false;
        if (*first == *second || (*first + 1) % vertices_.size() == *second ||
            (*second + 1) % vertices_.size() == *first) {
            return false;
        }

        const auto first_start  = vertices_[*first];
        const auto first_end    = vertices_[(*first + 1) % vertices_.size()];
        const auto second_start = vertices_[*second];
        const auto second_end   = vertices_[(*second + 1) % vertices_.size()];
        if (point_side(first_start, first_end, second_start) * point_side(first_start, first_end, second_end) > 0.0f) {
            return false;
        }
        if (point_side(second_start, second_end, first_start) * point_side(second_start, second_end, first_end) >
            0.0f) {
            return false;
        }
        return true;
    }

    PolygonSegmentOrder add(const PolygonSegment& segment) {
        std::optional<std::size_t> above;
        auto next = tree_.lower_bound(segment);
        if (next != tree_.end()) {
            next->second.below = segment.edge_index;
            above              = next->first.edge_index;
        }

        std::optional<std::size_t> below;
        auto previous = tree_.lower_bound(segment);
        if (previous != tree_.begin()) {
            --previous;
            previous->second.above = segment.edge_index;
            below                  = previous->first.edge_index;
        }

        const PolygonSegmentOrder order{above, below};
        tree_.insert_or_assign(segment, order);
        return order;
    }

    const PolygonSegmentOrder* find(const PolygonSegment& segment) const {
        const auto found = tree_.find(segment);
        return found == tree_.end() ? nullptr : &found->second;
    }

    void remove(const PolygonSegment& segment) {
        const auto found = tree_.find(segment);
        if (found == tree_.end()) return;
        const auto order = found->second;

        auto above = tree_.lower_bound(segment);
        if (above != tree_.end()) above->second.below = order.below;
        auto below = tree_.lower_bound(segment);
        if (below != tree_.begin()) {
            --below;
            below->second.above = order.above;
        }
        tree_.erase(segment);
    }

   private:
    std::span<const glm::vec2> vertices_;
    std::map<PolygonSegment, PolygonSegmentOrder, PolygonSegmentLess> tree_;
};

bool is_polygon_simple(std::span<const glm::vec2> vertices) {
    if (vertices.size() < 3) return true;
    if (vertices.size() == 3) return area(Triangle2d{vertices[0], vertices[1], vertices[2]}) > 0.0f;

    std::vector<PolygonSweepEvent> events;
    events.reserve(vertices.size() * 2);
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const auto first         = vertices[index];
        const auto second        = vertices[(index + 1) % vertices.size()];
        const auto [left, right] = xy_compare(first, second) < 0 ? std::pair{first, second} : std::pair{second, first};
        const PolygonSegment segment{index, left, right};
        events.push_back({segment, PolygonEndpoint::Left});
        events.push_back({segment, PolygonEndpoint::Right});
    }
    std::stable_sort(events.begin(), events.end(), [](const auto& left, const auto& right) {
        return xy_compare(left.position(), right.position()) < 0;
    });

    PolygonSweepLine sweep_line(vertices);
    for (const auto& event : events) {
        if (event.endpoint == PolygonEndpoint::Left) {
            const auto order = sweep_line.add(event.segment);
            if (sweep_line.intersects(event.segment.edge_index, order.above) ||
                sweep_line.intersects(event.segment.edge_index, order.below)) {
                return false;
            }
        } else if (const auto* order = sweep_line.find(event.segment)) {
            if (sweep_line.intersects(order->above, order->below)) return false;
            sweep_line.remove(event.segment);
        }
    }
    return true;
}
}  // namespace

std::uint32_t vertices_per_layer(const PerimeterSegment& segment) noexcept {
    // Bevy `PerimeterSegment::vertices_per_layer`, verbatim.
    return std::visit(
        [](const auto& value) -> std::uint32_t {
            using Alternative = std::remove_cvref_t<decltype(value)>;
            assert(value.indices.size() >= 1 && "a perimeter segment needs at least one index");
            if constexpr (std::same_as<Alternative, PerimeterSegment::Smooth>) {
                return static_cast<std::uint32_t>(value.indices.size());
            } else {
                return 2 * (static_cast<std::uint32_t>(value.indices.size()) - 1);
            }
        },
        segment.value);
}

std::size_t indices_per_segment(const PerimeterSegment& segment) noexcept {
    // Bevy `PerimeterSegment::indices_per_segment`, verbatim for both variants.
    return std::visit(
        [](const auto& value) -> std::size_t {
            assert(value.indices.size() >= 1 && "a perimeter segment needs at least one index");
            return 6 * (value.indices.size() - 1);
        },
        segment.value);
}

glm::vec2 Circle::closest_point(glm::vec2 point) const noexcept {
    const float distance_squared = glm::dot(point, point);
    if (distance_squared <= radius * radius) return point;
    return radius * point / std::sqrt(distance_squared);
}

CircleMeshBuilder Circle::mesh() const noexcept { return CircleMeshBuilder{radius, 32}; }

float Arc2d::half_chord_length() const noexcept { return radius * std::sin(half_angle); }

glm::vec2 Arc2d::right_endpoint() const noexcept {
    const float angle = std::numbers::pi_v<float> * 0.5f - half_angle;
    return radius * unit_from_angle(angle);
}

glm::vec2 Arc2d::left_endpoint() const noexcept {
    const float angle = std::numbers::pi_v<float> * 0.5f + half_angle;
    return radius * unit_from_angle(angle);
}

float Arc2d::apothem() const noexcept {
    const float half_chord = half_chord_length();
    const float sign       = is_minor() ? 1.0f : -1.0f;
    return sign * std::sqrt(radius * radius - half_chord * half_chord);
}

glm::vec2 Arc2d::chord_midpoint() const noexcept { return {0.0f, apothem()}; }

CircularSectorMeshBuilder CircularSector::mesh() const noexcept { return CircularSectorMeshBuilder{*this}; }

CircularSegmentMeshBuilder CircularSegment::mesh() const noexcept { return CircularSegmentMeshBuilder{*this}; }

Plane2d::Plane2d(glm::vec2 value) {
    const auto normalized = try_normalize_direction(value);
    if (!normalized) throw std::invalid_argument("normal must be nonzero and finite");
    normal = *normalized;
}

Segment2d Segment2d::from_direction_and_length(glm::vec2 direction, float length) {
    const auto normalized = try_normalize_direction(direction);
    if (!normalized) throw std::invalid_argument("segment direction must be finite and nonzero");
    const auto endpoint = *normalized * (length * 0.5f);
    return Segment2d{-endpoint, endpoint};
}

Segment2d Segment2d::from_ray_and_length(glm::vec2 origin, glm::vec2 direction, float length) {
    const auto normalized = try_normalize_direction(direction);
    if (!normalized) throw std::invalid_argument("ray direction must be finite and nonzero");
    return Segment2d{origin, origin + *normalized * length};
}

float Segment2d::length() const noexcept { return glm::length(vertices[1] - vertices[0]); }

float Segment2d::length_squared() const noexcept { return glm::dot(scaled_direction(), scaled_direction()); }

std::optional<glm::vec2> Segment2d::try_direction() const noexcept {
    return try_normalize_direction(scaled_direction());
}

glm::vec2 Segment2d::direction() const {
    const auto value = try_direction();
    if (!value) throw std::invalid_argument("failed to compute the direction of a line segment");
    return *value;
}

std::optional<glm::vec2> Segment2d::try_left_normal() const noexcept {
    return try_normalize_direction(scaled_left_normal());
}

glm::vec2 Segment2d::left_normal() const {
    const auto value = try_left_normal();
    if (!value) throw std::invalid_argument("failed to compute the left-hand side normal of a line segment");
    return *value;
}

std::optional<glm::vec2> Segment2d::try_right_normal() const noexcept {
    return try_normalize_direction(scaled_right_normal());
}

glm::vec2 Segment2d::right_normal() const {
    const auto value = try_right_normal();
    if (!value) throw std::invalid_argument("failed to compute the right-hand side normal of a line segment");
    return *value;
}

Segment2d Segment2d::transformed(glm::vec2 translation, float rotation) const noexcept {
    return rotated(rotation).translated(translation);
}

Segment2d Segment2d::translated(glm::vec2 translation) const noexcept {
    return Segment2d{point1() + translation, point2() + translation};
}

Segment2d Segment2d::rotated(float rotation) const noexcept {
    return Segment2d{rotate_vector(point1(), rotation), rotate_vector(point2(), rotation)};
}

Segment2d Segment2d::rotated_around(float rotation, glm::vec2 point) const noexcept {
    return translated(-point).rotated(rotation).translated(point);
}

Segment2d Segment2d::rotated_around_center(float rotation) const noexcept { return rotated_around(rotation, center()); }

Segment2d Segment2d::centered() const noexcept { return translated(-center()); }

Segment2d Segment2d::resized(float new_length) const noexcept {
    const auto offset_from_origin = center();
    const auto centered_segment   = translated(-offset_from_origin);
    const float ratio             = new_length / length();
    return Segment2d{centered_segment.point1() * ratio, centered_segment.point2() * ratio}.translated(
        offset_from_origin);
}

glm::vec2 Segment2d::closest_point(glm::vec2 point) const noexcept {
    const auto segment_vector     = scaled_direction();
    const auto offset             = point - point1();
    const float projection_scaled = glm::dot(segment_vector, offset);
    if (projection_scaled <= 0.0f) return point1();
    const float segment_length_squared = glm::dot(segment_vector, segment_vector);
    if (projection_scaled >= segment_length_squared) return point2();
    return point1() + (projection_scaled / segment_length_squared) * segment_vector;
}

Segment2dMeshBuilder Segment2d::mesh() const noexcept { return Segment2dMeshBuilder{*this}; }

Polyline2d Polyline2d::with_subdivisions(glm::vec2 start, glm::vec2 end, std::size_t subdivisions) {
    const auto vertex_count = subdivisions + 2;
    std::vector<glm::vec2> vertices;
    vertices.reserve(vertex_count);
    const auto step = (end - start) / static_cast<float>(subdivisions + 1);
    for (std::size_t index = 0; index < vertex_count; ++index) {
        vertices.push_back(start + step * static_cast<float>(index));
    }
    return Polyline2d{std::move(vertices)};
}

Polyline2dMeshBuilder Polyline2d::mesh() const { return Polyline2dMeshBuilder{*this}; }

Polygon::Polygon(ConvexPolygon polygon) : vertices(std::move(polygon.vertices_)) {}

bool Polygon::is_simple() const { return is_polygon_simple(vertices); }

std::string_view to_string(ConvexPolygonError) noexcept { return "The created polygon is not convex"; }

std::expected<ConvexPolygon, ConvexPolygonError> ConvexPolygon::create(std::vector<glm::vec2> vertices) {
    if (vertices.size() < 3) return std::unexpected(ConvexPolygonError::Concave);
    const auto triple_winding = [&vertices](std::size_t first, std::size_t second, std::size_t third) {
        return winding_order(vertices[first], vertices[second], vertices[third]);
    };

    const auto reference = triple_winding(vertices.size() - 1, 0, 1);
    for (std::size_t index = 1; index < vertices.size(); ++index) {
        if (triple_winding(index - 1, index, (index + 1) % vertices.size()) != reference) {
            return std::unexpected(ConvexPolygonError::Concave);
        }
    }
    return ConvexPolygon{std::move(vertices)};
}

std::expected<ConvexPolygon, ConvexPolygonError> ConvexPolygon::from_polygon(Polygon polygon) {
    return create(std::move(polygon.vertices));
}

ConvexPolygon ConvexPolygon::new_unchecked(std::vector<glm::vec2> vertices) {
    return ConvexPolygon{std::move(vertices)};
}

ConvexPolygonMeshBuilder ConvexPolygon::mesh() const {
    return ConvexPolygonMeshBuilder{std::vector<glm::vec2>(vertices_.begin(), vertices_.end())};
}

WindingOrder Triangle2d::winding_order() const noexcept {
    return ::epix::mesh::winding_order(vertices[0], vertices[1], vertices[2]);
}

float Measured2dImpl<Triangle2d>::area(const Triangle2d& triangle) noexcept {
    const auto& a = triangle.vertices[0];
    const auto& b = triangle.vertices[1];
    const auto& c = triangle.vertices[2];
    return std::abs(a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y)) / 2.0f;
}

float Measured2dImpl<Triangle2d>::perimeter(const Triangle2d& triangle) noexcept {
    const auto& a = triangle.vertices[0];
    const auto& b = triangle.vertices[1];
    const auto& c = triangle.vertices[2];
    return glm::distance(a, b) + glm::distance(b, c) + glm::distance(c, a);
}

bool Triangle2d::is_degenerate() const noexcept {
    const auto ab = glm::vec3(vertices[1] - vertices[0], 0.0f);
    const auto ac = glm::vec3(vertices[2] - vertices[0], 0.0f);
    return glm::length(glm::cross(ab, ac)) < 10e-7f;
}

bool Triangle2d::is_acute() const noexcept {
    const std::array<float, 3> side_lengths{
        glm::dot(vertices[1] - vertices[0], vertices[1] - vertices[0]),
        glm::dot(vertices[2] - vertices[1], vertices[2] - vertices[1]),
        glm::dot(vertices[0] - vertices[2], vertices[0] - vertices[2]),
    };
    const float sum = side_lengths[0] + side_lengths[1] + side_lengths[2];
    const float max = std::max({side_lengths[0], side_lengths[1], side_lengths[2]});
    return sum - max > max;
}

bool Triangle2d::is_obtuse() const noexcept {
    const std::array<float, 3> side_lengths{
        glm::dot(vertices[1] - vertices[0], vertices[1] - vertices[0]),
        glm::dot(vertices[2] - vertices[1], vertices[2] - vertices[1]),
        glm::dot(vertices[0] - vertices[2], vertices[0] - vertices[2]),
    };
    const float sum = side_lengths[0] + side_lengths[1] + side_lengths[2];
    const float max = std::max({side_lengths[0], side_lengths[1], side_lengths[2]});
    return sum - max < max;
}

std::pair<Circle, glm::vec2> Triangle2d::circumcircle() const noexcept {
    const glm::vec2 a            = vertices[0];
    const glm::vec2 b            = vertices[1] - a;
    const glm::vec2 c            = vertices[2] - a;
    const float b_length_squared = glm::dot(b, b);
    const float c_length_squared = glm::dot(c, c);
    const float inverse_d        = 1.0f / (2.0f * (b.x * c.y - b.y * c.x));
    const glm::vec2 u{inverse_d * (c.y * b_length_squared - b.y * c_length_squared),
                      inverse_d * (b.x * c_length_squared - c.x * b_length_squared)};
    return {Circle{glm::length(u)}, u + a};
}

Triangle2dMeshBuilder Triangle2d::mesh() const noexcept { return Triangle2dMeshBuilder{*this}; }

float Rhombus::side() const noexcept { return glm::length(half_diagonals); }

float Rhombus::inradius() const noexcept {
    const float side_length = side();
    if (side_length == 0.0f) return 0.0f;
    return (half_diagonals.x * half_diagonals.y) / side_length;
}

glm::vec2 Rhombus::closest_point(glm::vec2 point) const noexcept {
    // Fold the problem into the positive quadrant, exactly as Bevy does.
    const glm::vec2 point_abs            = glm::abs(point);
    const glm::vec2 folded_diagons       = glm::abs(half_diagonals);
    const glm::vec2 normal               = {folded_diagons.y, folded_diagons.x};
    const float normal_magnitude_squared = glm::dot(normal, normal);
    if (normal_magnitude_squared == 0.0f) return glm::vec2(0.0f);

    const float distance_unnormalised = glm::dot(normal, point_abs) - folded_diagons.x * folded_diagons.y;
    if (distance_unnormalised <= 0.0f) return point;

    glm::vec2 result = point_abs - normal * (distance_unnormalised / normal_magnitude_squared);
    if (result.x <= 0.0f) {
        result = {0.0f, folded_diagons.y};
    } else if (result.y <= 0.0f) {
        result = {folded_diagons.x, 0.0f};
    }
    return {std::copysign(result.x, point.x), std::copysign(result.y, point.y)};
}

RhombusMeshBuilder Rhombus::mesh() const noexcept { return RhombusMeshBuilder{half_diagonals}; }

glm::vec2 Annulus::closest_point(glm::vec2 point) const noexcept {
    const float distance_squared = glm::dot(point, point);
    if (inner_circle.radius * inner_circle.radius <= distance_squared) {
        if (distance_squared <= outer_circle.radius * outer_circle.radius) {
            // The point is inside the annulus.
            return point;
        }
        // Outside the annulus and closer to the outer perimeter.
        return outer_circle.radius * (point / std::sqrt(distance_squared));
    }
    // Inside the hole and closer to the inner perimeter.
    return inner_circle.radius * (point / std::sqrt(distance_squared));
}

AnnulusMeshBuilder Annulus::mesh() const noexcept {
    return AnnulusMeshBuilder{inner_circle.radius, outer_circle.radius, 32};
}

Capsule2dMeshBuilder Capsule2d::mesh() const noexcept { return Capsule2dMeshBuilder{radius, half_length * 2.0f, 16}; }

// ─── Inset implementations (Bevy `bevy_math/src/primitives/inset.rs`) ───────────────────────────

Rhombus InsetImpl<Rhombus>::inset(const Rhombus& shape, float distance) {
    const float half_width  = shape.half_diagonals.x;
    const float half_height = shape.half_diagonals.y;
    const float angle       = std::atan(half_height / half_width);
    const glm::vec2 offset{distance / std::sin(angle), distance / std::cos(angle)};
    Rhombus result = shape;
    result.half_diagonals -= offset;
    return result;
}

CircularSegment InsetImpl<CircularSegment>::inset(const CircularSegment& shape, float distance) {
    const auto old_arc  = shape.arc;
    const float radius  = old_arc.radius - distance;
    const float apothem = old_arc.apothem() + distance;
    // https://en.wikipedia.org/wiki/Circular_segment
    const float half_angle = std::acos(apothem / radius);
    return CircularSegment{radius, half_angle};
}

RegularPolygon InsetImpl<RegularPolygon>::inset(const RegularPolygon& shape, float distance) {
    const float half_angle = shape.internal_angle_radians() * 0.5f;
    const float offset     = distance / std::sin(half_angle);
    RegularPolygon result  = shape;
    result.circumcircle.radius -= offset;
    return result;
}

namespace {
/** @brief Unskewed UV coordinates for a triangle (Bevy `triangle3d::uv_coords`). */
std::array<glm::vec2, 3> triangle_uv_coords(const std::array<glm::vec3, 3>& vertices) {
    const auto& a = vertices[0];
    const auto& b = vertices[1];
    const auto& c = vertices[2];

    const float main_length = glm::distance(a, b);
    const auto direction    = b - a;
    if (glm::dot(direction, direction) == 0.0f) {
        return {glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 1.0f)};
    }
    const auto x = glm::normalize(direction);
    const auto y = c - a;

    // `x` is one UV axis; the second direction is `y` orthogonalized against it, so the
    // triangle is mapped without skew into the rectangle that contains it.
    const auto y_proj  = x * glm::dot(y, x);
    const float offset = glm::dot(y_proj, x) / main_length;

    if (offset < 0.0f) {
        // Obtuse triangle leaning left: the x direction extends left, shifting `a` from 0.
        const float total_length = 1.0f - offset;
        return {glm::vec2(std::abs(offset) / total_length, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 1.0f)};
    }
    if (offset > 1.0f) {
        // Obtuse triangle leaning right: the x direction extends right, shifting `b` from 1.
        return {glm::vec2(0.0f, 0.0f), glm::vec2(1.0f / offset, 0.0f), glm::vec2(1.0f, 1.0f)};
    }
    // Acute triangle: no extension needed, `a` stays at 0 and `b` stays at 1.
    return {glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(offset, 1.0f)};
}

/** @brief Move one triangle vertex inward by `distance` along its angle bisector. */
glm::vec2 find_inset_point(glm::vec2 a, glm::vec2 b, glm::vec2 c, float distance) {
    const auto unit_vector_ab  = glm::normalize(b - a);
    const auto unit_vector_ac  = glm::normalize(c - a);
    const float half_angle_bac = std::acos(glm::clamp(glm::dot(unit_vector_ab, unit_vector_ac), -1.0f, 1.0f)) * 0.5f;
    const auto mean            = (unit_vector_ab + unit_vector_ac) * 0.5f;
    const auto direction       = glm::normalize(mean);
    const float magnitude      = distance / std::sin(half_angle_bac);
    return a + direction * magnitude;
}
}  // namespace

Triangle2d InsetImpl<Triangle2d>::inset(const Triangle2d& shape, float distance) {
    const auto& a = shape.vertices[0];
    const auto& b = shape.vertices[1];
    const auto& c = shape.vertices[2];
    return Triangle2d{find_inset_point(a, b, c, distance), find_inset_point(b, c, a, distance),
                      find_inset_point(c, a, b, distance)};
}
template <Primitive2d P>
RingMeshBuilder<P> Ring<P>::mesh() const
    requires Meshable<P>
{
    return RingMeshBuilder<P>{outer_shape.mesh(), inner_shape.mesh()};
}

template <Primitive2d P>
    requires Meshable<P>
std::expected<Mesh, MeshAccessError> RingMeshBuilder<P>::try_build() const {
    Mesh outer = outer_shape_builder.build();
    Mesh inner = inner_shape_builder.build();

    if (outer.primitive_topology() != wgpu::PrimitiveTopology::eTriangleList ||
        inner.primitive_topology() != wgpu::PrimitiveTopology::eTriangleList) {
        return std::unexpected{MeshAccessError::NotFound};
    }

    const auto outer_positions = outer.try_attribute(Mesh::ATTRIBUTE_POSITION);
    const auto inner_positions = inner.try_attribute(Mesh::ATTRIBUTE_POSITION);
    const auto outer_normals   = outer.try_attribute(Mesh::ATTRIBUTE_NORMAL);
    const auto inner_normals   = inner.try_attribute(Mesh::ATTRIBUTE_NORMAL);
    const auto outer_uvs       = outer.try_attribute(Mesh::ATTRIBUTE_UV_0);
    const auto inner_uvs       = inner.try_attribute(Mesh::ATTRIBUTE_UV_0);
    if (!outer_positions || !inner_positions || !outer_normals || !inner_normals || !outer_uvs || !inner_uvs) {
        return std::unexpected{MeshAccessError::NotFound};
    }

    const auto* outer_position_values = outer_positions->get().get_if<VertexAttributeValues::Float32x3>();
    const auto* inner_position_values = inner_positions->get().get_if<VertexAttributeValues::Float32x3>();
    const auto* outer_normal_values   = outer_normals->get().get_if<VertexAttributeValues::Float32x3>();
    const auto* inner_normal_values   = inner_normals->get().get_if<VertexAttributeValues::Float32x3>();
    const auto* outer_uv_values       = outer_uvs->get().get_if<VertexAttributeValues::Float32x2>();
    const auto* inner_uv_values       = inner_uvs->get().get_if<VertexAttributeValues::Float32x2>();
    if (!outer_position_values || !inner_position_values || !outer_normal_values || !inner_normal_values ||
        !outer_uv_values || !inner_uv_values) {
        return std::unexpected{MeshAccessError::NotFound};
    }

    const auto point_count = outer_position_values->size();
    if (outer_uv_values->size() != inner_uv_values->size() ||
        outer_position_values->size() != inner_position_values->size() ||
        outer_normal_values->size() != inner_normal_values->size()) {
        return std::unexpected{MeshAccessError::NotFound};
    }

    // The inner UVs are radially rescaled so they continue the outer mesh's UV mapping.
    std::vector<VertexAttributeValues::Float32x2Value> uvs = *outer_uv_values;
    for (std::size_t index = 0; index < point_count; ++index) {
        const auto& inner_position = (*inner_position_values)[index];
        const auto& outer_position = (*outer_position_values)[index];
        const auto& inner_uv       = (*inner_uv_values)[index];
        const float inner_length   = glm::length(glm::vec3(inner_position[0], inner_position[1], inner_position[2]));
        const float outer_length   = glm::length(glm::vec3(outer_position[0], outer_position[1], outer_position[2]));
        const float scale          = outer_length == 0.0f ? 0.0f : inner_length / outer_length;
        const glm::vec2 uv         = glm::vec2(inner_uv[0], inner_uv[1]) - glm::vec2(0.5f);
        const auto result_uv       = uv * scale + glm::vec2(0.5f);
        uvs.push_back({result_uv.x, result_uv.y});
    }

    std::vector<VertexAttributeValues::Float32x3Value> normals = *outer_normal_values;
    normals.insert(normals.end(), inner_normal_values->begin(), inner_normal_values->end());

    std::vector<VertexAttributeValues::Float32x3Value> positions = *outer_position_values;
    positions.insert(positions.end(), inner_position_values->begin(), inner_position_values->end());

    const auto points = static_cast<std::uint32_t>(point_count);
    std::vector<std::uint32_t> indices;
    indices.reserve(point_count * 6);
    for (std::uint32_t index = 0; index < points; ++index) {
        indices.push_back(index);
        indices.push_back(index + 1);
        indices.push_back(points + index);
        indices.push_back(points + index);
        indices.push_back(index + 1);
        indices.push_back(points + index + 1);
    }
    // Rewire the final triangle pair back to the first vertex so the ring closes.
    if (indices.size() >= 6) {
        const auto base   = indices.size() - 6;
        indices[base + 1] = 0;
        indices[base + 4] = 0;
        indices[base + 5] = points;
    }

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs))
        .with_inserted_indices(Indices{std::move(indices)});
}

template <Primitive2d P>
    requires Meshable<P>
Mesh RingMeshBuilder<P>::build() const {
    auto result = try_build();
    if (!result) throw std::runtime_error("ring inner and outer meshes must match in vertex count and attributes");
    return std::move(*result);
}
// ─── 3D shapes and their mesh builders ─────────────────────────────────────────────────────────

Cuboid Cuboid::from_corners(glm::vec3 point1, glm::vec3 point2) noexcept {
    return Cuboid{glm::abs(point2 - point1) * 0.5f};
}

glm::vec3 Cuboid::closest_point(glm::vec3 point) const noexcept { return glm::clamp(point, -half_size, half_size); }

CuboidMeshBuilder Cuboid::mesh() const noexcept { return CuboidMeshBuilder{half_size}; }

glm::vec3 Sphere::closest_point(glm::vec3 point) const noexcept {
    const float distance_squared = glm::dot(point, point);
    if (distance_squared <= radius * radius) {
        // The point is inside the sphere.
        return point;
    }
    // The point is outside the sphere: find the closest point on its surface.
    const glm::vec3 direction_to_point = point / std::sqrt(distance_squared);
    return radius * direction_to_point;
}

SphereMeshBuilder Sphere::mesh() const noexcept {
    // Bevy's `..Default::default()` leaves the kind at `Ico { subdivisions: 5 }`.
    SphereMeshBuilder builder;
    builder.sphere = *this;
    return builder;
}

namespace {
// ─── A port of the parts of `hexasphere` 16.0.0 that Bevy's icosphere uses (B48b) ────────────────
//
// Bevy's `SphereMeshBuilder::ico` calls `hexasphere::shapes::IcoSphere::new(subdivisions, ..)`, which
// is `Subdivided<Vec2, IcoSphereBase>`. The pieces below are that crate's 16.0.0 source, transcribed:
// `src/slice.rs` (the direction-aware `Slice`), the `TriangleContents`/`Triangle` subdivision
// recursion with its index emission (`src/lib.rs`), the geometric slerp interpolators
// (`src/interpolation.rs`), and the icosahedron base plus its 20 main triangles (`src/shapes.rs`).
// Reproducing that source's vertex, UV and index order is what makes Epix's icosphere identical to
// Bevy's; the recorded dumps of the real crate are the test's oracle.

using IcoPoint = glm::vec3;

/** @brief `hexasphere::interpolation::geometric_slerp_half`. */
IcoPoint slerp_half(const IcoPoint& a, const IcoPoint& b) {
    return (a + b) / std::sqrt(2.0f * (1.0f + glm::dot(a, b)));
}

/** @brief `hexasphere::interpolation::geometric_slerp_multiple`. */
void slerp_multiple(const IcoPoint& a,
                    const IcoPoint& b,
                    const std::vector<std::uint32_t>& indices,
                    std::vector<IcoPoint>& points) {
    const float angle     = std::acos(glm::dot(a, b));
    const float sin_recip = 1.0f / std::sin(angle);
    for (std::size_t position = 0; position < indices.size(); ++position) {
        const float percent = static_cast<float>(position + 1) / static_cast<float>(indices.size() + 1);
        points[indices[position]] =
            a * (std::sin((1.0f - percent) * angle) * sin_recip) + b * (std::sin(percent * angle) * sin_recip);
    }
}

/** @brief `hexasphere::Slice`: a forwards or backwards view over an edge's interior points. */
struct IcoSlice {
    const std::vector<std::uint32_t>* values = nullptr;
    bool forward                             = true;

    std::size_t len() const { return values->size(); }
    std::uint32_t operator[](std::size_t index) const {
        return forward ? (*values)[index] : (*values)[values->size() - 1 - index];
    }
};

struct IcoTriangle;
struct IcoTriangleContents;

/** @brief `hexasphere::add_indices_triangular`. */
void add_indices_triangular(std::uint32_t a,
                            std::uint32_t b,
                            std::uint32_t c,
                            const IcoSlice& ab,
                            const IcoSlice& bc,
                            const IcoSlice& ca,
                            const IcoTriangleContents& contents,
                            std::vector<std::uint32_t>& buffer);

/** @brief `hexasphere::TriangleContents`: the points interior to one main triangle. */
struct IcoTriangleContents {
    enum class Kind {
        /** @brief Nothing inside the triangle: subdivisions 0 and 1. */
        None,
        /** @brief One point inside the triangle: subdivision 2. */
        One,
        /** @brief Three points inside the triangle: subdivision 3. */
        Three,
        /** @brief Six points inside the triangle: subdivision 4. */
        Six,
        /** @brief More than six points inside the triangle: subdivision > 4. */
        More,
    };

    Kind kind = Kind::None;
    /** @brief `One`'s point. */
    std::uint32_t one = 0;
    /** @brief `Three`'s and `Six`'s points. */
    std::uint32_t a = 0, b = 0, c = 0;
    /** @brief `Six`'s edge midpoints. */
    std::uint32_t ab = 0, bc = 0, ca = 0;
    /** @brief `More`'s three `my_side_length`-long edge segments. */
    std::vector<std::uint32_t> sides;
    /** @brief `More`'s segment length. */
    std::uint32_t my_side_length = 0;
    /** @brief `More`'s inner triangle. */
    std::unique_ptr<IcoTriangleContents> contents;

    /** @brief `TriangleContents::subdivide`. */
    void subdivide(std::size_t& points);
    /** @brief `TriangleContents::calculate`. */
    void calculate(const IcoSlice& ab, const IcoSlice& bc, const IcoSlice& ca, std::vector<IcoPoint>& points);
    /** @brief `TriangleContents::add_indices`. */
    void add_indices(std::vector<std::uint32_t>& buffer) const;

    /** @brief `TriangleContents::idx_ab`. */
    std::uint32_t idx_ab(std::size_t index) const;
    /** @brief `TriangleContents::idx_bc`. */
    std::uint32_t idx_bc(std::size_t index) const;
    /** @brief `TriangleContents::idx_ca`. */
    std::uint32_t idx_ca(std::size_t index) const;

    /** @brief `TriangleContents::calculate_one`. */
    void calculate_one(const IcoSlice& ab, const IcoSlice& bc, std::vector<IcoPoint>& points);
    /** @brief `TriangleContents::calculate_three`. */
    void calculate_three(const IcoSlice& ab, const IcoSlice& bc, const IcoSlice& ca, std::vector<IcoPoint>& points);
    /** @brief `TriangleContents::calculate_six`. */
    void calculate_six(const IcoSlice& ab, const IcoSlice& bc, const IcoSlice& ca, std::vector<IcoPoint>& points);
};

/** @brief `hexasphere::Triangle`: one main triangle of the base shape. */
struct IcoTriangle {
    std::uint32_t a = 0, b = 0, c = 0;
    std::size_t ab_edge = 0, bc_edge = 0, ca_edge = 0;
    bool ab_forward = false, bc_forward = false, ca_forward = false;
    IcoTriangleContents contents;

    /** @brief `hexasphere::Triangle::subdivide`. */
    void subdivide(std::size_t& points, std::size_t subdivision_level) {
        if (subdivision_level >= 1) contents.subdivide(points);
    }

    /** @brief `hexasphere::Triangle::calculate_edges`. */
    std::size_t calculate_edges(std::vector<std::vector<std::uint32_t>>& edges,
                                std::vector<bool>& edges_done,
                                std::vector<IcoPoint>& points) {
        const auto divide = [&](std::uint32_t first, std::uint32_t second, std::size_t edge, bool& forward) {
            if (!edges_done[edge]) {
                slerp_multiple(points[first], points[second], edges[edge], points);
                edges_done[edge] = true;
                forward          = true;
            } else {
                forward = false;
            }
        };
        divide(a, b, ab_edge, ab_forward);
        divide(b, c, bc_edge, bc_forward);
        divide(c, a, ca_edge, ca_forward);
        return edges[ab_edge].size();
    }

    /** @brief `hexasphere::Triangle::calculate`. */
    void calculate(std::vector<std::vector<std::uint32_t>>& edges,
                   std::vector<bool>& edges_done,
                   std::vector<IcoPoint>& points) {
        const std::size_t side_length = calculate_edges(edges, edges_done, points) + 1;
        if (side_length > 2) {
            const IcoSlice ab{&edges[ab_edge], ab_forward};
            const IcoSlice bc{&edges[bc_edge], bc_forward};
            const IcoSlice ca{&edges[ca_edge], ca_forward};
            contents.calculate(ab, bc, ca, points);
        }
    }

    /** @brief `hexasphere::Triangle::add_indices`. */
    void add_indices(std::vector<std::uint32_t>& buffer, const std::vector<std::vector<std::uint32_t>>& edges) const {
        const IcoSlice ab{&edges[ab_edge], ab_forward};
        const IcoSlice bc{&edges[bc_edge], bc_forward};
        const IcoSlice ca{&edges[ca_edge], ca_forward};
        add_indices_triangular(a, b, c, ab, bc, ca, contents, buffer);
        contents.add_indices(buffer);
    }
};

/** @brief `hexasphere::add_indices_triangular`. */
void add_indices_triangular(std::uint32_t a,
                            std::uint32_t b,
                            std::uint32_t c,
                            const IcoSlice& ab,
                            const IcoSlice& bc,
                            const IcoSlice& ca,
                            const IcoTriangleContents& contents,
                            std::vector<std::uint32_t>& buffer) {
    const std::size_t subdivisions = ab.len();
    if (subdivisions == 0) {
        buffer.insert(buffer.end(), {a, b, c});
        return;
    }
    if (subdivisions == 1) {
        buffer.insert(buffer.end(), {a, ab[0], ca[0]});
        buffer.insert(buffer.end(), {b, bc[0], ab[0]});
        buffer.insert(buffer.end(), {c, ca[0], bc[0]});
        buffer.insert(buffer.end(), {ab[0], bc[0], ca[0]});
        return;
    }
    if (subdivisions == 2) {
        buffer.insert(buffer.end(), {a, ab[0], ca[1]});
        buffer.insert(buffer.end(), {b, bc[0], ab[1]});
        buffer.insert(buffer.end(), {c, ca[0], bc[1]});

        buffer.insert(buffer.end(), {ab[1], contents.idx_ab(0), ab[0]});
        buffer.insert(buffer.end(), {bc[1], contents.idx_ab(0), bc[0]});
        buffer.insert(buffer.end(), {ca[1], contents.idx_ab(0), ca[0]});

        buffer.insert(buffer.end(), {ab[0], contents.idx_ab(0), ca[1]});
        buffer.insert(buffer.end(), {bc[0], contents.idx_ab(0), ab[1]});
        buffer.insert(buffer.end(), {ca[0], contents.idx_ab(0), bc[1]});
        return;
    }

    const std::size_t last_idx = ab.len() - 1;

    buffer.insert(buffer.end(), {a, ab[0], ca[last_idx]});
    buffer.insert(buffer.end(), {b, bc[0], ab[last_idx]});
    buffer.insert(buffer.end(), {c, ca[0], bc[last_idx]});

    buffer.insert(buffer.end(), {ab[0], contents.idx_ab(0), ca[last_idx]});
    buffer.insert(buffer.end(), {bc[0], contents.idx_bc(0), ab[last_idx]});
    buffer.insert(buffer.end(), {ca[0], contents.idx_ca(0), bc[last_idx]});

    for (std::size_t index = 0; index < last_idx - 1; ++index) {
        // Exclude the special case `last_idx - 1`.
        buffer.insert(buffer.end(), {ab[index], ab[index + 1], contents.idx_ab(index)});
        buffer.insert(buffer.end(), {ab[index + 1], contents.idx_ab(index + 1), contents.idx_ab(index)});

        buffer.insert(buffer.end(), {bc[index], bc[index + 1], contents.idx_bc(index)});
        buffer.insert(buffer.end(), {bc[index + 1], contents.idx_bc(index + 1), contents.idx_bc(index)});

        buffer.insert(buffer.end(), {ca[index], ca[index + 1], contents.idx_ca(index)});
        buffer.insert(buffer.end(), {ca[index + 1], contents.idx_ca(index + 1), contents.idx_ca(index)});
    }

    // The special case `last_idx - 1`.
    buffer.insert(buffer.end(), {ab[last_idx], contents.idx_ab(last_idx - 1), ab[last_idx - 1]});
    buffer.insert(buffer.end(), {bc[last_idx], contents.idx_bc(last_idx - 1), bc[last_idx - 1]});
    buffer.insert(buffer.end(), {ca[last_idx], contents.idx_ca(last_idx - 1), ca[last_idx - 1]});
}

void IcoTriangleContents::subdivide(std::size_t& points) {
    switch (kind) {
        case Kind::None:
            kind = Kind::One;
            one  = static_cast<std::uint32_t>(points);
            ++points;
            return;
        case Kind::One:
            // `TriangleContents::three`.
            points += 2;
            kind = Kind::Three;
            a    = one;
            b    = static_cast<std::uint32_t>(points) - 2;
            c    = static_cast<std::uint32_t>(points) - 1;
            return;
        case Kind::Three:
            // `TriangleContents::six`.
            points += 3;
            kind = Kind::Six;
            ab   = static_cast<std::uint32_t>(points) - 3;
            bc   = static_cast<std::uint32_t>(points) - 2;
            ca   = static_cast<std::uint32_t>(points) - 1;
            return;
        case Kind::Six:
            kind = Kind::More;
            sides.assign({ab, bc, ca});
            my_side_length = 1;
            contents       = std::make_unique<IcoTriangleContents>();
            subdivide(points);
            return;
        case Kind::More: {
            points += 3;
            const auto last = static_cast<std::uint32_t>(points);
            sides.insert(sides.end(), {last - 3, last - 2, last - 1});
            ++my_side_length;
            contents->subdivide(points);
            return;
        }
    }
}

std::uint32_t IcoTriangleContents::idx_ab(std::size_t index) const {
    switch (kind) {
        case Kind::None:
            throw std::logic_error("invalid index: triangle contents are empty");
        case Kind::One:
            if (index != 0) throw std::logic_error("invalid index: triangle contents hold one point");
            return one;
        case Kind::Three:
            return index == 0 ? a : b;
        case Kind::Six:
            return index == 0 ? a : (index == 1 ? ab : b);
        case Kind::More:
            if (index == 0) return a;
            if (index <= my_side_length) return sides[index - 1];
            return b;
    }
    std::unreachable();
}

std::uint32_t IcoTriangleContents::idx_bc(std::size_t index) const {
    switch (kind) {
        case Kind::None:
            throw std::logic_error("invalid index: triangle contents are empty");
        case Kind::One:
            if (index != 0) throw std::logic_error("invalid index: triangle contents hold one point");
            return one;
        case Kind::Three:
            return index == 0 ? b : c;
        case Kind::Six:
            return index == 0 ? b : (index == 1 ? bc : c);
        case Kind::More:
            if (index == 0) return b;
            if (index <= my_side_length) return sides[my_side_length + index - 1];
            return c;
    }
    std::unreachable();
}

std::uint32_t IcoTriangleContents::idx_ca(std::size_t index) const {
    switch (kind) {
        case Kind::None:
            throw std::logic_error("invalid index: triangle contents are empty");
        case Kind::One:
            if (index != 0) throw std::logic_error("invalid index: triangle contents hold one point");
            return one;
        case Kind::Three:
            return index == 0 ? c : a;
        case Kind::Six:
            return index == 0 ? c : (index == 1 ? ca : a);
        case Kind::More:
            if (index == 0) return c;
            if (index <= my_side_length) return sides[my_side_length * 2 + index - 1];
            return a;
    }
    std::unreachable();
}

void IcoTriangleContents::calculate_one(const IcoSlice& ab, const IcoSlice& bc, std::vector<IcoPoint>& points) {
    points[one] = slerp_half(points[ab[0]], points[bc[1]]);
}

void IcoTriangleContents::calculate_three(const IcoSlice& ab,
                                          const IcoSlice& bc,
                                          const IcoSlice& ca,
                                          std::vector<IcoPoint>& points) {
    const IcoPoint ab_point = points[ab[1]];
    const IcoPoint bc_point = points[bc[1]];
    const IcoPoint ca_point = points[ca[1]];

    points[a] = slerp_half(ab_point, ca_point);
    points[b] = slerp_half(bc_point, ab_point);
    points[c] = slerp_half(ca_point, bc_point);
}

void IcoTriangleContents::calculate_six(const IcoSlice& ab,
                                        const IcoSlice& bc,
                                        const IcoSlice& ca,
                                        std::vector<IcoPoint>& points) {
    const IcoPoint aba = points[ab[1]];
    const IcoPoint abb = points[ab[2]];
    const IcoPoint bcb = points[bc[1]];
    const IcoPoint bcc = points[bc[2]];
    const IcoPoint cac = points[ca[1]];
    const IcoPoint caa = points[ca[2]];

    const IcoPoint a_point = slerp_half(aba, caa);
    const IcoPoint b_point = slerp_half(abb, bcb);
    const IcoPoint c_point = slerp_half(bcc, cac);

    const IcoPoint ab_point = slerp_half(a_point, b_point);
    const IcoPoint bc_point = slerp_half(b_point, c_point);
    const IcoPoint ca_point = slerp_half(c_point, a_point);

    // Rust destructures the fields into *_index bindings here, because the parameters below
    // share the edge names; the explicit 	his-> is the C++ equivalent.
    points[this->a]  = a_point;
    points[this->b]  = b_point;
    points[this->c]  = c_point;
    points[this->ab] = ab_point;
    points[this->bc] = bc_point;
    points[this->ca] = ca_point;
}

void IcoTriangleContents::calculate(const IcoSlice& ab,
                                    const IcoSlice& bc,
                                    const IcoSlice& ca,
                                    std::vector<IcoPoint>& points) {
    switch (kind) {
        case Kind::None:
            throw std::logic_error("invalid triangle contents");
        case Kind::One:
            calculate_one(ab, bc, points);
            return;
        case Kind::Three:
            calculate_three(ab, bc, ca, points);
            return;
        case Kind::Six:
            calculate_six(ab, bc, ca, points);
            return;
        case Kind::More: {
            const std::size_t side_length = my_side_length;
            const std::size_t outer_len   = ab.len();

            const IcoPoint aba = points[ab[1]];
            const IcoPoint abb = points[ab[outer_len - 2]];
            const IcoPoint bcb = points[bc[1]];
            const IcoPoint bcc = points[bc[outer_len - 2]];
            const IcoPoint cac = points[ca[1]];
            const IcoPoint caa = points[ca[outer_len - 2]];

            points[a] = slerp_half(aba, caa);
            points[b] = slerp_half(abb, bcb);
            points[c] = slerp_half(bcc, cac);

            const std::vector<std::uint32_t> ab_sides{sides.begin(),
                                                      sides.begin() + static_cast<std::ptrdiff_t>(side_length)};
            const std::vector<std::uint32_t> bc_sides{sides.begin() + static_cast<std::ptrdiff_t>(side_length),
                                                      sides.begin() + static_cast<std::ptrdiff_t>(side_length * 2)};
            const std::vector<std::uint32_t> ca_sides{sides.begin() + static_cast<std::ptrdiff_t>(side_length * 2),
                                                      sides.end()};

            slerp_multiple(points[a], points[b], ab_sides, points);
            slerp_multiple(points[b], points[c], bc_sides, points);
            slerp_multiple(points[c], points[a], ca_sides, points);

            const IcoSlice inner_ab{&ab_sides, true};
            const IcoSlice inner_bc{&bc_sides, true};
            const IcoSlice inner_ca{&ca_sides, true};
            contents->calculate(inner_ab, inner_bc, inner_ca, points);
            return;
        }
    }
}

void IcoTriangleContents::add_indices(std::vector<std::uint32_t>& buffer) const {
    switch (kind) {
        case Kind::None:
        case Kind::One:
            return;
        case Kind::Three:
            buffer.insert(buffer.end(), {a, b, c});
            return;
        case Kind::Six:
            buffer.insert(buffer.end(), {a, ab, ca});
            buffer.insert(buffer.end(), {ab, b, bc});
            buffer.insert(buffer.end(), {bc, c, ca});
            buffer.insert(buffer.end(), {ab, bc, ca});
            return;
        case Kind::More: {
            const std::size_t side_length = my_side_length;
            const std::vector<std::uint32_t> ab_sides{sides.begin(),
                                                      sides.begin() + static_cast<std::ptrdiff_t>(side_length)};
            const std::vector<std::uint32_t> bc_sides{sides.begin() + static_cast<std::ptrdiff_t>(side_length),
                                                      sides.begin() + static_cast<std::ptrdiff_t>(side_length * 2)};
            const std::vector<std::uint32_t> ca_sides{sides.begin() + static_cast<std::ptrdiff_t>(side_length * 2),
                                                      sides.end()};

            // Contents are always stored forward.
            const IcoSlice forward_ab{&ab_sides, true};
            const IcoSlice forward_bc{&bc_sides, true};
            const IcoSlice forward_ca{&ca_sides, true};
            add_indices_triangular(a, b, c, forward_ab, forward_bc, forward_ca, *contents, buffer);
            contents->add_indices(buffer);
            return;
        }
    }
}

/** @brief `hexasphere::shapes::IcoSphereBase`: the icosahedron's 12 points and 20 main triangles. */
struct IcoSphereBase {
    static constexpr std::size_t EDGES = 30;

    static const std::vector<IcoPoint>& initial_points() {
        static const std::vector<IcoPoint> points{
            {0.0f, 1.0f, 0.0f},
            {0.89442718f, 0.44721359f, 0.0f},
            {0.27639320f, 0.44721359f, 0.85065079f},
            {-0.72360682f, 0.44721359f, 0.52573109f},
            {-0.72360682f, 0.44721359f, -0.52573109f},
            {0.27639320f, 0.44721359f, -0.85065079f},
            {0.72360682f, -0.44721359f, -0.52573109f},
            {0.72360682f, -0.44721359f, 0.52573109f},
            {-0.27639320f, -0.44721359f, 0.85065079f},
            {-0.89442718f, -0.44721359f, 0.0f},
            {-0.27639320f, -0.44721359f, -0.85065079f},
            {0.0f, -1.0f, 0.0f},
        };
        return points;
    }

    /** @brief The 20 main triangles, as `(a, b, c, ab_edge, bc_edge, ca_edge)`. */
    static std::vector<IcoTriangle> triangles() {
        static constexpr std::array<std::array<std::uint32_t, 6>, 20> table{{
            {0, 2, 1, 0, 5, 4},     {0, 3, 2, 1, 6, 0},     {0, 4, 3, 2, 7, 1},     {0, 5, 4, 3, 8, 2},
            {0, 1, 5, 4, 9, 3},     {5, 1, 6, 9, 10, 15},   {1, 2, 7, 5, 11, 16},   {2, 3, 8, 6, 12, 17},
            {3, 4, 9, 7, 13, 18},   {4, 5, 10, 8, 14, 19},  {5, 6, 10, 15, 20, 14}, {1, 7, 6, 16, 21, 10},
            {2, 8, 7, 17, 22, 11},  {3, 9, 8, 18, 23, 12},  {4, 10, 9, 19, 24, 13}, {10, 6, 11, 20, 26, 25},
            {6, 7, 11, 21, 27, 26}, {7, 8, 11, 22, 28, 27}, {8, 9, 11, 23, 29, 28}, {9, 10, 11, 24, 25, 29},
        }};

        std::vector<IcoTriangle> triangles;
        triangles.reserve(table.size());
        for (const auto& row : table) {
            IcoTriangle triangle;
            triangle.a       = row[0];
            triangle.b       = row[1];
            triangle.c       = row[2];
            triangle.ab_edge = row[3];
            triangle.bc_edge = row[4];
            triangle.ca_edge = row[5];
            triangles.push_back(std::move(triangle));
        }
        return triangles;
    }
};

/** @brief `hexasphere::Subdivided<T, IcoSphereBase>`: the subdivided icosphere Bevy consumes. */
struct IcoSphere {
    std::vector<IcoPoint> points;
    std::vector<VertexAttributeValues::Float32x2Value> data;
    std::vector<IcoTriangle> triangles;
    std::vector<std::vector<std::uint32_t>> edges;
    std::vector<bool> edges_done;
    std::size_t subdivisions = 1;

    /** @brief `Subdivided::new_custom_shape`. */
    static IcoSphere generate(std::size_t subdivisions);

    /** @brief `Subdivided::raw_points`. */
    const std::vector<IcoPoint>& raw_points() const { return points; }
    /** @brief `Subdivided::raw_data`. */
    const std::vector<VertexAttributeValues::Float32x2Value>& raw_data() const { return data; }
    /** @brief `Subdivided::indices_per_main_triangle`. */
    std::size_t indices_per_main_triangle() const { return (subdivisions + 1) * (subdivisions + 1); }
    /** @brief `Subdivided::get_indices`. */
    void get_indices(std::size_t triangle, std::vector<std::uint32_t>& buffer) const {
        triangles[triangle].add_indices(buffer, edges);
    }
};

IcoSphere IcoSphere::generate(std::size_t subdivisions) {
    IcoSphere sphere;
    sphere.points    = IcoSphereBase::initial_points();
    sphere.triangles = IcoSphereBase::triangles();
    sphere.edges.resize(IcoSphereBase::EDGES);
    sphere.edges_done.assign(IcoSphereBase::EDGES, true);
    sphere.subdivisions = 1;

    std::size_t new_points = sphere.points.size();

    for (std::size_t edge_index = 0; edge_index < sphere.edges.size(); ++edge_index) {
        for (std::size_t index = 0; index < subdivisions; ++index) {
            sphere.edges[edge_index].push_back(static_cast<std::uint32_t>(new_points));
            ++new_points;
        }
        sphere.edges_done[edge_index] = false;
    }

    for (auto& triangle : sphere.triangles) {
        for (std::size_t index = 0; index < subdivisions; ++index) {
            triangle.subdivide(new_points, index);
        }
    }

    sphere.points.resize(new_points, IcoPoint{0.0f});

    for (auto& triangle : sphere.triangles) {
        triangle.calculate(sphere.edges, sphere.edges_done, sphere.points);
    }

    // Bevy's interpolator: the UV of each point, from its spherical coordinates.
    constexpr float pi  = std::numbers::pi_v<float>;
    constexpr float tau = 2.0f * pi;
    sphere.data.reserve(sphere.points.size());
    for (const auto& point : sphere.points) {
        const float inclination      = std::acos(point.y);
        const float azimuth          = std::atan2(point.z, point.x);
        const float norm_inclination = inclination / pi;
        const float norm_azimuth     = 0.5f - (azimuth / tau);
        sphere.data.push_back({norm_azimuth, norm_inclination});
    }

    return sphere;
}
}  // namespace

std::expected<Mesh, IcosphereError> SphereMeshBuilder::ico(std::uint32_t subdivisions) const {
    if (subdivisions >= 80) {
        // Bevy's vertex-count derivation, verbatim: `10(s + 1)^2 + 2`.
        const std::uint32_t steps                      = subdivisions + 1;
        const std::uint32_t number_of_resulting_points = steps * steps * 10 + 2;
        return std::unexpected(IcosphereError{
            .subdivisions               = subdivisions,
            .number_of_resulting_points = number_of_resulting_points,
        });
    }

    const IcoSphere generated = IcoSphere::generate(subdivisions);
    const auto& raw_points    = generated.raw_points();

    std::vector<VertexAttributeValues::Float32x3Value> points;
    std::vector<VertexAttributeValues::Float32x3Value> normals;
    points.reserve(raw_points.size());
    normals.reserve(raw_points.size());
    for (const auto& point : raw_points) {
        points.push_back({point.x * sphere.radius, point.y * sphere.radius, point.z * sphere.radius});
        normals.push_back({point.x, point.y, point.z});
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(generated.indices_per_main_triangle() * 20);
    for (std::size_t triangle = 0; triangle < 20; ++triangle) {
        generated.get_indices(triangle, indices);
    }

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(points))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, generated.raw_data());
}

Mesh SphereMeshBuilder::uv(std::uint32_t sectors, std::uint32_t stacks) const {
    // Largely inspired from http://www.songho.ca/opengl/gl_sphere.html
    const float sectors_f32 = static_cast<float>(sectors);
    const float stacks_f32  = static_cast<float>(stacks);
    const float length_inv  = 1.0f / sphere.radius;
    const float sector_step = 2.0f * std::numbers::pi_v<float> / sectors_f32;
    const float stack_step  = std::numbers::pi_v<float> / stacks_f32;

    const auto n_vertices = static_cast<std::size_t>(stacks) * sectors;
    std::vector<VertexAttributeValues::Float32x3Value> vertices;
    std::vector<VertexAttributeValues::Float32x3Value> normals;
    std::vector<VertexAttributeValues::Float32x2Value> uvs;
    std::vector<std::uint32_t> indices;
    vertices.reserve(n_vertices);
    normals.reserve(n_vertices);
    uvs.reserve(n_vertices);
    indices.reserve(n_vertices * 2 * 3);

    for (std::uint32_t i = 0; i <= stacks; ++i) {
        const float stack_angle = std::numbers::pi_v<float> / 2.0f - static_cast<float>(i) * stack_step;
        const float xy          = sphere.radius * std::cos(stack_angle);
        const float z           = sphere.radius * std::sin(stack_angle);

        for (std::uint32_t j = 0; j <= sectors; ++j) {
            const float sector_angle = static_cast<float>(j) * sector_step;
            const float x            = xy * std::cos(sector_angle);
            const float y            = xy * std::sin(sector_angle);

            vertices.push_back({x, y, z});
            normals.push_back({x * length_inv, y * length_inv, z * length_inv});
            uvs.push_back({static_cast<float>(j) / sectors_f32, static_cast<float>(i) / stacks_f32});
        }
    }

    //  k1--k1+1
    //  |  / |
    //  | /  |
    //  k2--k2+1
    for (std::uint32_t i = 0; i < stacks; ++i) {
        std::uint32_t k1 = i * (sectors + 1);
        std::uint32_t k2 = k1 + sectors + 1;
        for (std::uint32_t j = 0; j < sectors; ++j) {
            if (i != 0) {
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k1 + 1);
            }
            if (i != stacks - 1) {
                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
            ++k1;
            ++k2;
        }
    }

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(vertices))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
}

Mesh SphereMeshBuilder::build() const {
    return std::visit(
        [this](const auto& kind) -> Mesh {
            using Kind = std::remove_cvref_t<decltype(kind)>;
            if constexpr (std::same_as<Kind, SphereKind::Ico>) {
                auto result = ico(kind.subdivisions);
                if (!result) throw std::runtime_error(result.error().to_string());
                return std::move(*result);
            } else {
                return uv(kind.sectors, kind.stacks);
            }
        },
        kind.value);
}

Plane3d Plane3d::from_points(glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    const auto normal = glm::cross(b - a, c - a);
    if (glm::dot(normal, normal) == 0.0f) {
        throw std::invalid_argument(
            "finite plane must be defined by three finite points that don't lie on the same line");
    }
    return Plane3d{glm::normalize(normal), glm::vec2(0.5f)};
}

PlaneMeshBuilder Plane3d::mesh() const noexcept { return PlaneMeshBuilder(Plane3d{*this}); }

Segment3d Segment3d::from_direction_and_length(glm::vec3 direction, float length) {
    const auto normalized = try_normalize_direction(direction);
    if (!normalized) throw std::invalid_argument("segment direction must be finite and nonzero");
    const auto endpoint = *normalized * (length * 0.5f);
    return Segment3d{-endpoint, endpoint};
}

Segment3d Segment3d::from_ray_and_length(glm::vec3 origin, glm::vec3 direction, float length) {
    const auto normalized = try_normalize_direction(direction);
    if (!normalized) throw std::invalid_argument("ray direction must be finite and nonzero");
    return Segment3d{origin, origin + *normalized * length};
}

float Segment3d::length() const noexcept { return glm::length(vertices[1] - vertices[0]); }

float Segment3d::length_squared() const noexcept { return glm::dot(scaled_direction(), scaled_direction()); }

std::optional<glm::vec3> Segment3d::try_direction() const noexcept {
    return try_normalize_direction(scaled_direction());
}

glm::vec3 Segment3d::direction() const {
    const auto value = try_direction();
    if (!value) throw std::invalid_argument("failed to compute the direction of a line segment");
    return *value;
}

Segment3d Segment3d::transformed(glm::vec3 translation, glm::quat rotation) const noexcept {
    return rotated(rotation).translated(translation);
}

Segment3d Segment3d::translated(glm::vec3 translation) const noexcept {
    return Segment3d{point1() + translation, point2() + translation};
}

Segment3d Segment3d::rotated(glm::quat rotation) const noexcept {
    return Segment3d{rotation * point1(), rotation * point2()};
}

Segment3d Segment3d::rotated_around(glm::quat rotation, glm::vec3 point) const noexcept {
    return translated(-point).rotated(rotation).translated(point);
}

Segment3d Segment3d::rotated_around_center(glm::quat rotation) const noexcept {
    return rotated_around(rotation, center());
}

Segment3d Segment3d::centered() const noexcept { return translated(-center()); }

Segment3d Segment3d::resized(float new_length) const noexcept {
    const auto offset_from_origin = center();
    const auto centered_segment   = translated(-offset_from_origin);
    const float ratio             = new_length / length();
    return Segment3d{centered_segment.point1() * ratio, centered_segment.point2() * ratio}.translated(
        offset_from_origin);
}

glm::vec3 Segment3d::closest_point(glm::vec3 point) const noexcept {
    const auto segment_vector     = scaled_direction();
    const auto offset             = point - point1();
    const float projection_scaled = glm::dot(segment_vector, offset);
    if (projection_scaled <= 0.0f) return point1();
    const float segment_length_squared = glm::dot(segment_vector, segment_vector);
    if (projection_scaled >= segment_length_squared) return point2();
    return point1() + (projection_scaled / segment_length_squared) * segment_vector;
}

Segment3dMeshBuilder Segment3d::mesh() const noexcept { return Segment3dMeshBuilder{*this}; }

Polyline3d Polyline3d::with_subdivisions(glm::vec3 start, glm::vec3 end, std::size_t subdivisions) {
    const auto vertex_count = subdivisions + 2;
    std::vector<glm::vec3> vertices;
    vertices.reserve(vertex_count);
    const auto step = (end - start) / static_cast<float>(subdivisions + 1);
    for (std::size_t index = 0; index < vertex_count; ++index) {
        vertices.push_back(start + step * static_cast<float>(index));
    }
    return Polyline3d{std::move(vertices)};
}

Polyline3dMeshBuilder Polyline3d::mesh() const { return Polyline3dMeshBuilder{*this}; }

std::expected<Dir3, InvalidDirectionError> Triangle3d::normal() const noexcept {
    const auto ab = vertices[1] - vertices[0];
    const auto ac = vertices[2] - vertices[0];
    return Dir3::make(glm::cross(ab, ac));
}

bool Triangle3d::is_degenerate() const noexcept {
    const auto ab = vertices[1] - vertices[0];
    const auto ac = vertices[2] - vertices[0];
    return glm::length(glm::cross(ab, ac)) < 10e-7f;
}

bool Triangle3d::is_acute() const noexcept {
    const std::array<float, 3> side_lengths{
        glm::dot(vertices[1] - vertices[0], vertices[1] - vertices[0]),
        glm::dot(vertices[2] - vertices[1], vertices[2] - vertices[1]),
        glm::dot(vertices[0] - vertices[2], vertices[0] - vertices[2]),
    };
    const float sum = side_lengths[0] + side_lengths[1] + side_lengths[2];
    const float max = std::max({side_lengths[0], side_lengths[1], side_lengths[2]});
    return sum - max > max;
}

bool Triangle3d::is_obtuse() const noexcept {
    const std::array<float, 3> side_lengths{
        glm::dot(vertices[1] - vertices[0], vertices[1] - vertices[0]),
        glm::dot(vertices[2] - vertices[1], vertices[2] - vertices[1]),
        glm::dot(vertices[0] - vertices[2], vertices[0] - vertices[2]),
    };
    const float sum = side_lengths[0] + side_lengths[1] + side_lengths[2];
    const float max = std::max({side_lengths[0], side_lengths[1], side_lengths[2]});
    return sum - max < max;
}

glm::vec3 Triangle3d::centroid() const noexcept { return (vertices[0] + vertices[1] + vertices[2]) / 3.0f; }

std::pair<glm::vec3, glm::vec3> Triangle3d::largest_side() const noexcept {
    const auto& [a, b, c] = vertices;
    std::pair result{a, b};
    const glm::vec3 ab    = b - a;
    const glm::vec3 bc    = c - b;
    const glm::vec3 ca    = a - c;
    float largest_length  = glm::dot(ab, ab);
    const float bc_length = glm::dot(bc, bc);
    if (bc_length > largest_length) {
        result         = {b, c};
        largest_length = bc_length;
    }
    if (const float ca_length = glm::dot(ca, ca); ca_length > largest_length) result = {a, c};
    return result;
}

glm::vec3 Triangle3d::circumcenter() const noexcept {
    if (is_degenerate()) {
        const auto [first, second] = largest_side();
        return (first + second) * 0.5f;
    }
    const auto& [a, b, c]  = vertices;
    const glm::vec3 ab     = b - a;
    const glm::vec3 ac     = c - a;
    const glm::vec3 normal = glm::cross(ab, ac);
    return a + (glm::dot(ac, ac) * glm::cross(normal, ab) + glm::dot(ab, ab) * glm::cross(glm::cross(ac, ab), ac)) /
                   (2.0f * glm::dot(normal, normal));
}

Triangle3dMeshBuilder Triangle3d::mesh() const noexcept { return Triangle3dMeshBuilder{*this}; }

float Tetrahedron::signed_volume() const noexcept {
    const auto ab = vertices[1] - vertices[0];
    const auto ac = vertices[2] - vertices[0];
    const auto ad = vertices[3] - vertices[0];
    return glm::dot(ab, glm::cross(ac, ad)) / 6.0f;
}

std::array<Triangle3d, 4> Tetrahedron::faces() const noexcept {
    const auto& a = vertices[0];
    const auto& b = vertices[1];
    const auto& c = vertices[2];
    const auto& d = vertices[3];
    return {
        Triangle3d{b, c, d},
        Triangle3d{a, c, d}.reversed(),
        Triangle3d{a, b, d},
        Triangle3d{a, b, c}.reversed(),
    };
}

TetrahedronMeshBuilder Tetrahedron::mesh() const noexcept { return TetrahedronMeshBuilder{*this}; }

float Cylinder::base_area() const noexcept { return std::numbers::pi_v<float> * radius * radius; }

CylinderMeshBuilder Cylinder::mesh() const noexcept { return CylinderMeshBuilder{Cylinder{*this}}; }

TorusMeshBuilder::TorusMeshBuilder(float inner_radius, float outer_radius)
    : torus(Torus::from_inner_and_outer(inner_radius, outer_radius)) {}

TorusMeshBuilder Torus::mesh() const noexcept { return TorusMeshBuilder{Torus{*this}}; }

TorusKind Torus::kind() const noexcept {
    if (minor_radius <= 0.0f || !std::isfinite(minor_radius) || major_radius <= 0.0f || !std::isfinite(major_radius)) {
        return TorusKind::Invalid;
    }
    if (major_radius > minor_radius) return TorusKind::Ring;
    if (major_radius == minor_radius) return TorusKind::Horn;
    return TorusKind::Spindle;
}

Mesh CylinderMeshBuilder::build() const {
    // Bevy asserts these in debug builds; Epix keeps the same contract.
    assert(resolution > 2 && "cylinder resolution must be greater than 2");
    assert(segments > 0 && "cylinder segments must be greater than 0");

    const std::uint32_t num_rings = segments + 1;

    std::vector<VertexAttributeValues::Float32x3Value> positions;
    std::vector<VertexAttributeValues::Float32x3Value> normals;
    std::vector<VertexAttributeValues::Float32x2Value> uvs;
    std::vector<std::uint32_t> indices;
    const std::size_t vertex_capacity =
        static_cast<std::size_t>(resolution) * 2 + static_cast<std::size_t>(num_rings) * (resolution + 1);
    positions.reserve(vertex_capacity);
    normals.reserve(vertex_capacity);
    uvs.reserve(vertex_capacity);

    const float step_theta = std::numbers::pi_v<float> * 2.0f / static_cast<float>(resolution);
    const float step_y     = 2.0f * cylinder.half_height / static_cast<float>(segments);

    // Rings, each with a duplicated seam vertex so the UVs can wrap.
    for (std::uint32_t ring = 0; ring < num_rings; ++ring) {
        const float y = -cylinder.half_height + static_cast<float>(ring) * step_y;
        for (std::uint32_t step = 0; step <= resolution; ++step) {
            const float theta = static_cast<float>(step) * step_theta;
            const float sine  = std::sin(theta);
            const float cosin = std::cos(theta);
            positions.push_back({cylinder.radius * cosin, y, cylinder.radius * sine});
            normals.push_back({cosin, 0.0f, sine});
            uvs.push_back({static_cast<float>(step) / static_cast<float>(resolution),
                           static_cast<float>(ring) / static_cast<float>(segments)});
        }
    }

    // Barrel skin.
    for (std::uint32_t i = 0; i < segments; ++i) {
        const std::uint32_t ring      = i * (resolution + 1);
        const std::uint32_t next_ring = (i + 1) * (resolution + 1);
        for (std::uint32_t j = 0; j < resolution; ++j) {
            indices.insert(indices.end(),
                           {ring + j, next_ring + j, ring + j + 1, next_ring + j, next_ring + j + 1, ring + j + 1});
        }
    }

    if (caps) {
        const auto build_cap = [&](bool top) {
            const auto offset    = static_cast<std::uint32_t>(positions.size());
            const float y        = top ? cylinder.half_height : -cylinder.half_height;
            const float normal_y = top ? 1.0f : -1.0f;
            // The winding offsets keep both caps facing outward.
            const std::uint32_t first  = top ? 1u : 0u;
            const std::uint32_t second = top ? 0u : 1u;

            for (std::uint32_t i = 0; i < resolution; ++i) {
                const float theta = static_cast<float>(i) * step_theta;
                const float sine  = std::sin(theta);
                const float cosin = std::cos(theta);
                positions.push_back({cosin * cylinder.radius, y, sine * cylinder.radius});
                normals.push_back({0.0f, normal_y, 0.0f});
                uvs.push_back({0.5f * (cosin + 1.0f), 1.0f - 0.5f * (sine + 1.0f)});
            }

            for (std::uint32_t i = 1; i + 1 < resolution; ++i) {
                indices.insert(indices.end(), {offset, offset + i + first, offset + i + second});
            }
        };

        build_cap(true);
        build_cap(false);
    }

    // Offset the vertex Y positions to match the anchor.
    if (anchor != CylinderAnchor::MidPoint) {
        const float offset = anchor == CylinderAnchor::Top ? -cylinder.half_height : cylinder.half_height;
        for (auto& position : positions) position[1] += offset;
    }

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
}

Mesh TorusMeshBuilder::build() const {
    // Adapted from Bevy, which adapted it from
    // http://apparat-engine.blogspot.com/2013/04/procedural-meshes-torus.html
    const std::size_t n_vertices = static_cast<std::size_t>(major_resolution + 1) * (minor_resolution + 1);
    std::vector<VertexAttributeValues::Float32x3Value> positions;
    std::vector<VertexAttributeValues::Float32x3Value> normals;
    std::vector<VertexAttributeValues::Float32x2Value> uvs;
    positions.reserve(n_vertices);
    normals.reserve(n_vertices);
    uvs.reserve(n_vertices);

    const float segment_stride = (end_angle - start_angle) / static_cast<float>(major_resolution);
    const float side_stride    = std::numbers::pi_v<float> * 2.0f / static_cast<float>(minor_resolution);

    for (std::uint32_t segment = 0; segment <= major_resolution; ++segment) {
        const float theta = start_angle + segment_stride * static_cast<float>(segment);
        for (std::uint32_t side = 0; side <= minor_resolution; ++side) {
            const float phi         = side_stride * static_cast<float>(side);
            const float sin_theta   = std::sin(theta);
            const float cos_theta   = std::cos(theta);
            const float sin_phi     = std::sin(phi);
            const float cos_phi     = std::cos(phi);
            const float tube_radius = torus.major_radius + torus.minor_radius * cos_phi;

            const glm::vec3 position{cos_theta * tube_radius, torus.minor_radius * sin_phi, sin_theta * tube_radius};
            const glm::vec3 center{torus.major_radius * cos_theta, 0.0f, torus.major_radius * sin_theta};
            const glm::vec3 normal = glm::normalize(position - center);

            positions.push_back({position.x, position.y, position.z});
            normals.push_back({normal.x, normal.y, normal.z});
            uvs.push_back({static_cast<float>(segment) / static_cast<float>(major_resolution),
                           static_cast<float>(side) / static_cast<float>(minor_resolution)});
        }
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(major_resolution) * minor_resolution * 6);

    const std::uint32_t vertices_per_row = minor_resolution + 1;
    for (std::uint32_t segment = 0; segment < major_resolution; ++segment) {
        for (std::uint32_t side = 0; side < minor_resolution; ++side) {
            const std::uint32_t left_top     = side + segment * vertices_per_row;
            const std::uint32_t right_top    = (side + 1) + segment * vertices_per_row;
            const std::uint32_t left_bottom  = side + (segment + 1) * vertices_per_row;
            const std::uint32_t right_bottom = (side + 1) + (segment + 1) * vertices_per_row;

            indices.insert(indices.end(), {left_top, right_top, left_bottom});
            indices.insert(indices.end(), {right_top, right_bottom, left_bottom});
        }
    }

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
}

float Cone::slant_height() const noexcept { return std::sqrt(radius * radius + height * height); }
float Cone::lateral_area() const noexcept { return std::numbers::pi_v<float> * radius * slant_height(); }
float Cone::base_area() const noexcept { return std::numbers::pi_v<float> * radius * radius; }

ConeMeshBuilder Cone::mesh() const noexcept { return ConeMeshBuilder{Cone{*this}}; }

Mesh ConeMeshBuilder::build() const {
    // Bevy's `ConeMeshBuilder::build` (bevy_mesh/src/primitives/dim3/cone.rs:72):
    // `resolution` base vertices, `resolution` lateral-surface vertices, and one
    // tip vertex, with the lateral fan then the closed base fan.
    const float half_height = cone.height * 0.5f;

    std::vector<VertexAttributeValues::Float32x3Value> positions;
    std::vector<VertexAttributeValues::Float32x3Value> normals;
    std::vector<VertexAttributeValues::Float32x2Value> uvs;
    positions.reserve(static_cast<std::size_t>(resolution) * 2 + 1);
    normals.reserve(static_cast<std::size_t>(resolution) * 2 + 1);
    uvs.reserve(static_cast<std::size_t>(resolution) * 2 + 1);

    // Tip. The tip has no single correct normal, so Bevy stores the zero vector
    // and lets the fragment shader derive it, which keeps the lateral surface
    // crease-free.
    positions.push_back({0.0f, half_height, 0.0f});
    normals.push_back({0.0f, 0.0f, 0.0f});
    uvs.push_back({0.5f, 0.5f});

    // The lateral normal's slope, used as a fast normalization factor.
    const float normal_slope         = cone.radius / cone.height;
    const float normalization_factor = 1.0f / std::sqrt(1.0f + normal_slope * normal_slope);
    const float step_theta           = 2.0f * std::numbers::pi_v<float> / static_cast<float>(resolution);

    for (std::uint32_t segment = 0; segment < resolution; ++segment) {
        const float theta = static_cast<float>(segment) * step_theta;
        const float sin_t = std::sin(theta);
        const float cos_t = std::cos(theta);

        positions.push_back({cone.radius * cos_t, -half_height, cone.radius * sin_t});
        normals.push_back(
            {cos_t * normalization_factor, normal_slope * normalization_factor, sin_t * normalization_factor});
        uvs.push_back({0.5f + cos_t * 0.5f, 0.5f + sin_t * 0.5f});
    }

    std::vector<std::uint32_t> indices;
    indices.reserve((static_cast<std::size_t>(resolution) * 6) - 6);

    // Lateral fan: the tip plus two consecutive base vertices.
    for (std::uint32_t j = 1; j < resolution; ++j) {
        indices.insert(indices.end(), {0u, j + 1u, j});
    }
    // Close the surface between the tip, the first and the last lateral vertex.
    indices.insert(indices.end(), {0u, 1u, resolution});

    const auto index_offset = static_cast<std::uint32_t>(positions.size());

    for (std::uint32_t i = 0; i < resolution; ++i) {
        const float theta = static_cast<float>(i) * step_theta;
        const float sin_t = std::sin(theta);
        const float cos_t = std::cos(theta);

        positions.push_back({cos_t * cone.radius, -half_height, sin_t * cone.radius});
        normals.push_back({0.0f, -1.0f, 0.0f});
        uvs.push_back({0.5f * (cos_t + 1.0f), 1.0f - 0.5f * (sin_t + 1.0f)});
    }

    for (std::uint32_t i = 1; i + 1 < resolution; ++i) {
        indices.insert(indices.end(), {index_offset, index_offset + i, index_offset + i + 1u});
    }

    switch (anchor) {
        case ConeAnchor::Tip:
            for (auto& position : positions) position[1] -= half_height;
            break;
        case ConeAnchor::Base:
            for (auto& position : positions) position[1] += half_height;
            break;
        case ConeAnchor::MidPoint:
        default:
            break;
    }

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
}

float ConicalFrustum::slant_height() const noexcept {
    const float radius_delta = radius_bottom - radius_top;
    return std::sqrt(radius_delta * radius_delta + height * height);
}

float ConicalFrustum::lateral_area() const noexcept {
    return std::numbers::pi_v<float> * (radius_bottom + radius_top) * slant_height();
}

float ConicalFrustum::bottom_base_area() const noexcept {
    return std::numbers::pi_v<float> * radius_bottom * radius_bottom;
}
float ConicalFrustum::top_base_area() const noexcept { return std::numbers::pi_v<float> * radius_top * radius_top; }

ConicalFrustumMeshBuilder ConicalFrustum::mesh() const noexcept {
    return ConicalFrustumMeshBuilder{ConicalFrustum{*this}};
}

// ─── Measurement (Bevy `Measured2d` / `Measured3d`) ─────────────────────────────────────────────
//
// Only the measurements that lean on a non-`constexpr` helper live here; the rest are inline in
// `primitives.hpp`. Every body mirrors the corresponding Bevy trait method, including the order of
// the floating point operations.

/** @brief Bevy's `FRAC_PI_3` (`std::f32::consts::FRAC_PI_3`), the correctly rounded f32 `pi / 3`. */
constexpr float kFracPi3 = 1.0471975511965977461542144610931676f;

float Measured2dImpl<CircularSegment>::area(const CircularSegment& segment) noexcept {
    return 0.5f * (segment.arc.radius * segment.arc.radius) * (segment.arc.angle() - std::sin(segment.arc.angle()));
}

float Measured2dImpl<CircularSegment>::perimeter(const CircularSegment& segment) noexcept {
    return segment.chord_length() + segment.arc_length();
}

float Measured2dImpl<Ellipse>::area(const Ellipse& ellipse) noexcept {
    return std::numbers::pi_v<float> * ellipse.half_size.x * ellipse.half_size.y;
}

float Measured2dImpl<Ellipse>::perimeter(const Ellipse& ellipse) noexcept {
    const float a = ellipse.semi_major();
    const float b = ellipse.semi_minor();

    // In the case that `a == b`, the ellipse is a circle
    if (a / b - 1.0f < 1e-5f) return std::numbers::pi_v<float> * (a + b);

    // In the case that `a` is much larger than `b`, the ellipse is a line
    if (a / b > 1e4f) return 4.0f * a;

    // These values are the result of (0.5 choose n)^2 where n is the index in the array.
    constexpr std::array<float, 21> kBinomialCoefficients{
        1.0f,
        0.25f,
        0.015625f,
        0.00390625f,
        0.0015258789f,
        0.00074768066f,
        0.00042057037f,
        0.00025963783f,
        0.00017140154f,
        0.000119028846f,
        0.00008599834f,
        0.00006414339f,
        0.000049109784f,
        0.000038430585f,
        0.000030636627f,
        0.000024815668f,
        0.000020380836f,
        0.000016942893f,
        0.000014236736f,
        0.000012077564f,
        0.000010333865f,
    };

    // The Gauss-Kummer infinite series expansion of the elliptic integral expression for the
    // perimeter of ellipses, with the terms up to `i == 20`.
    const float eccentricity = (a - b) / (a + b);
    const float h            = eccentricity * eccentricity;

    float series = 0.0f;
    for (std::size_t index = 0; index < kBinomialCoefficients.size(); ++index) {
        series += kBinomialCoefficients[index] * std::pow(h, static_cast<float>(index));
    }
    return std::numbers::pi_v<float> * (a + b) * series;
}

float Measured2dImpl<Rhombus>::area(const Rhombus& rhombus) noexcept {
    return 2.0f * rhombus.half_diagonals.x * rhombus.half_diagonals.y;
}

float Measured2dImpl<Rhombus>::perimeter(const Rhombus& rhombus) noexcept { return 4.0f * rhombus.side(); }

float RegularPolygon::side_length() const noexcept {
    return 2.0f * circumradius() * std::sin(std::numbers::pi_v<float> / static_cast<float>(sides));
}

float RegularPolygon::inradius() const noexcept {
    return circumradius() * std::cos(std::numbers::pi_v<float> / static_cast<float>(sides));
}

std::vector<glm::vec2> RegularPolygon::vertices(float rotation) const {
    std::vector<glm::vec2> result;
    result.reserve(sides);
    const float start_angle = rotation + std::numbers::pi_v<float> * 0.5f;
    const float step        = 2.0f * std::numbers::pi_v<float> / static_cast<float>(sides);
    for (std::uint32_t index = 0; index < sides; ++index) {
        const float angle = start_angle + static_cast<float>(index) * step;
        result.emplace_back(std::cos(angle) * circumradius(), std::sin(angle) * circumradius());
    }
    return result;
}

float Ellipse::eccentricity() const noexcept {
    const float major = semi_major();
    const float minor = semi_minor();
    return std::sqrt(major * major - minor * minor) / major;
}

float Ellipse::focal_length() const noexcept {
    const float major = semi_major();
    const float minor = semi_minor();
    return std::sqrt(major * major - minor * minor);
}

float Measured2dImpl<RegularPolygon>::area(const RegularPolygon& polygon) noexcept {
    const float angle = 2.0f * std::numbers::pi_v<float> / static_cast<float>(polygon.sides);
    return static_cast<float>(polygon.sides) * (polygon.circumradius() * polygon.circumradius()) * std::sin(angle) /
           2.0f;
}

float Measured2dImpl<RegularPolygon>::perimeter(const RegularPolygon& polygon) noexcept {
    return static_cast<float>(polygon.sides) * polygon.side_length();
}

float Measured2dImpl<Triangle3d>::area(const Triangle3d& triangle) noexcept {
    const auto& a = triangle.vertices[0];
    const auto& b = triangle.vertices[1];
    const auto& c = triangle.vertices[2];
    return glm::length(glm::cross(b - a, c - a)) / 2.0f;
}

float Measured2dImpl<Triangle3d>::perimeter(const Triangle3d& triangle) noexcept {
    const auto& a = triangle.vertices[0];
    const auto& b = triangle.vertices[1];
    const auto& c = triangle.vertices[2];
    return glm::distance(a, b) + glm::distance(b, c) + glm::distance(c, a);
}

float Measured3dImpl<Cylinder>::volume(const Cylinder& cylinder) noexcept {
    return cylinder.base_area() * 2.0f * cylinder.half_height;
}

float Measured3dImpl<Cone>::area(const Cone& cone) noexcept { return cone.base_area() + cone.lateral_area(); }

float Measured3dImpl<Cone>::volume(const Cone& cone) noexcept { return (cone.base_area() * cone.height) / 3.0f; }

float Measured3dImpl<ConicalFrustum>::area(const ConicalFrustum& frustum) noexcept {
    return frustum.bottom_base_area() + frustum.top_base_area() + frustum.lateral_area();
}

float Measured3dImpl<ConicalFrustum>::volume(const ConicalFrustum& frustum) noexcept {
    return kFracPi3 * frustum.height *
           (frustum.radius_bottom * frustum.radius_bottom + frustum.radius_top * frustum.radius_top +
            frustum.radius_top * frustum.radius_bottom);
}

float Measured3dImpl<Tetrahedron>::area(const Tetrahedron& tetrahedron) noexcept {
    const auto [a, b, c, d] = tetrahedron.vertices;
    const glm::vec3 ab      = b - a;
    const glm::vec3 ac      = c - a;
    const glm::vec3 ad      = d - a;
    const glm::vec3 bc      = c - b;
    const glm::vec3 bd      = d - b;
    return (glm::length(glm::cross(ab, ac)) + glm::length(glm::cross(ab, ad)) + glm::length(glm::cross(ac, ad)) +
            glm::length(glm::cross(bc, bd))) /
           2.0f;
}

float Measured3dImpl<Tetrahedron>::volume(const Tetrahedron& tetrahedron) noexcept {
    return std::abs(tetrahedron.signed_volume());
}

Mesh ConicalFrustumMeshBuilder::build() const {
    // Bevy's `ConicalFrustumMeshBuilder::build`
    // (bevy_mesh/src/primitives/dim3/conical_frustum.rs:63): `segments + 1`
    // seam-duplicated rings of `resolution + 1` vertices, the lateral surface
    // between consecutive rings, then the top and bottom cap fans with opposite
    // winding.
    const float half_height = frustum.height * 0.5f;
    const auto num_rings    = static_cast<std::uint32_t>(segments) + 1u;

    std::vector<VertexAttributeValues::Float32x3Value> positions;
    std::vector<VertexAttributeValues::Float32x3Value> normals;
    std::vector<VertexAttributeValues::Float32x2Value> uvs;
    const std::size_t ring_vertices = static_cast<std::size_t>(num_rings) * (resolution + 1u);
    positions.reserve(ring_vertices + static_cast<std::size_t>(resolution) * 2u);
    normals.reserve(ring_vertices + static_cast<std::size_t>(resolution) * 2u);
    uvs.reserve(ring_vertices + static_cast<std::size_t>(resolution) * 2u);

    const float step_theta  = 2.0f * std::numbers::pi_v<float> / static_cast<float>(resolution);
    const float step_y      = frustum.height / static_cast<float>(segments);
    const float step_radius = (frustum.radius_top - frustum.radius_bottom) / static_cast<float>(segments);
    const float normal_y    = (frustum.radius_bottom - frustum.radius_top) / frustum.height;

    for (std::uint32_t ring = 0; ring < num_rings; ++ring) {
        const float y      = -half_height + static_cast<float>(ring) * step_y;
        const float radius = frustum.radius_bottom + static_cast<float>(ring) * step_radius;

        for (std::uint32_t segment = 0; segment <= resolution; ++segment) {
            const float theta = static_cast<float>(segment) * step_theta;
            const float sin_t = std::sin(theta);
            const float cos_t = std::cos(theta);

            positions.push_back({radius * cos_t, y, radius * sin_t});
            const glm::vec3 normal = glm::normalize(glm::vec3{cos_t, normal_y, sin_t});
            normals.push_back({normal.x, normal.y, normal.z});
            uvs.push_back({static_cast<float>(segment) / static_cast<float>(resolution),
                           static_cast<float>(ring) / static_cast<float>(segments)});
        }
    }

    std::vector<std::uint32_t> indices;
    for (std::uint32_t i = 0; i < segments; ++i) {
        const std::uint32_t ring      = i * (resolution + 1u);
        const std::uint32_t next_ring = (i + 1u) * (resolution + 1u);

        for (std::uint32_t j = 0; j < resolution; ++j) {
            indices.insert(indices.end(),
                           {ring + j, next_ring + j, ring + j + 1u, next_ring + j, next_ring + j + 1u, ring + j + 1u});
        }
    }

    // Caps: Bevy builds the top with winding (1, 0) and the bottom with (0, 1).
    const auto build_cap = [&](bool top, float radius) {
        const auto offset             = static_cast<std::uint32_t>(positions.size());
        const float y                 = top ? half_height : -half_height;
        const float normal_y_         = top ? 1.0f : -1.0f;
        const std::uint32_t winding_a = top ? 1u : 0u;
        const std::uint32_t winding_b = top ? 0u : 1u;

        for (std::uint32_t i = 0; i < resolution; ++i) {
            const float theta = static_cast<float>(i) * step_theta;
            const float sin_t = std::sin(theta);
            const float cos_t = std::cos(theta);

            positions.push_back({cos_t * radius, y, sin_t * radius});
            normals.push_back({0.0f, normal_y_, 0.0f});
            uvs.push_back({0.5f * (cos_t + 1.0f), 1.0f - 0.5f * (sin_t + 1.0f)});
        }

        for (std::uint32_t i = 1; i + 1 < resolution; ++i) {
            indices.insert(indices.end(), {offset, offset + i + winding_a, offset + i + winding_b});
        }
    };

    build_cap(true, frustum.radius_top);
    build_cap(false, frustum.radius_bottom);

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
}

Capsule3dMeshBuilder Capsule3d::mesh() const noexcept { return Capsule3dMeshBuilder{Capsule3d{*this}}; }

Mesh Capsule3dMeshBuilder::build() const {
    // Bevy's `Capsule3dMeshBuilder::build`
    // (bevy_mesh/src/primitives/dim3/capsule.rs:96-419), adapted from
    // <https://behreajj.medium.com/making-a-capsule-mesh-via-script-in-five-3d-environments-c2214abf02db>.
    // The capsule is built as a single index-ordered vertex pool with fixed
    // offsets per region: a north polar ring, the north hemisphere, the north
    // equator, an optional cylinder, the south equator, the south hemisphere, a
    // south polar ring, and a south cap ring. Each region is written into its
    // slice of the pool, and the triangle list is filled in the same region
    // order, which is why the offsets are computed up front.
    const Capsule3d capsule_copy = capsule;
    const auto rings_            = rings;
    const auto longitudes_       = longitudes;
    const auto latitudes_        = latitudes;
    const auto uv_profile_       = uv_profile;
    const float radius           = capsule_copy.radius;
    const float half_length      = capsule_copy.half_length;

    const bool calc_middle          = rings_ > 0;
    const std::uint32_t half_lats   = latitudes_ / 2u;
    const std::uint32_t half_latsn1 = half_lats - 1u;
    const std::uint32_t half_latsn2 = half_lats - 2u;
    const std::uint32_t ringsp1     = rings_ + 1u;
    const std::uint32_t lonsp1      = longitudes_ + 1u;
    const float summit              = half_length + radius;

    // Vertex index offsets.
    const std::uint32_t vert_offset_north_hemi    = longitudes_;
    const std::uint32_t vert_offset_north_equator = vert_offset_north_hemi + lonsp1 * half_latsn1;
    const std::uint32_t vert_offset_cylinder      = vert_offset_north_equator + lonsp1;
    const std::uint32_t vert_offset_south_equator =
        calc_middle ? vert_offset_cylinder + lonsp1 * rings_ : vert_offset_cylinder;
    const std::uint32_t vert_offset_south_hemi  = vert_offset_south_equator + lonsp1;
    const std::uint32_t vert_offset_south_polar = vert_offset_south_hemi + lonsp1 * half_latsn2;
    const std::uint32_t vert_offset_south_cap   = vert_offset_south_polar + lonsp1;

    const std::size_t vert_len = static_cast<std::size_t>(vert_offset_south_cap) + longitudes_;

    std::vector<VertexAttributeValues::Float32x3Value> positions(vert_len, VertexAttributeValues::Float32x3Value{});
    std::vector<VertexAttributeValues::Float32x3Value> normals(vert_len, VertexAttributeValues::Float32x3Value{});
    std::vector<VertexAttributeValues::Float32x2Value> uvs(vert_len, VertexAttributeValues::Float32x2Value{});

    const float to_theta          = 2.0f * std::numbers::pi_v<float> / static_cast<float>(longitudes_);
    const float to_phi            = std::numbers::pi_v<float> / static_cast<float>(latitudes_);
    const float to_tex_horizontal = 1.0f / static_cast<float>(longitudes_);
    const float to_tex_vertical   = 1.0f / static_cast<float>(half_lats);

    const float vt_aspect_ratio = uv_profile_ == CapsuleUvProfile::Aspect
                                      ? radius / (2.0f * half_length + radius + radius)
                                      : (uv_profile_ == CapsuleUvProfile::Uniform
                                             ? static_cast<float>(half_lats) / static_cast<float>(ringsp1 + latitudes_)
                                             : 1.0f / 3.0f);
    const float vt_aspect_north = 1.0f - vt_aspect_ratio;
    const float vt_aspect_south = vt_aspect_ratio;

    std::vector<glm::vec2> theta_cartesian(static_cast<std::size_t>(longitudes_));
    std::vector<glm::vec2> rho_theta_cartesian(static_cast<std::size_t>(longitudes_));
    std::vector<float> s_texture_cache(static_cast<std::size_t>(lonsp1));

    for (std::size_t j = 0; j < static_cast<std::size_t>(longitudes_); ++j) {
        const float jf              = static_cast<float>(j);
        const float s_texture_polar = 1.0f - ((jf + 0.5f) * to_tex_horizontal);
        const float theta           = jf * to_theta;

        theta_cartesian[j]     = glm::vec2{std::cos(theta), std::sin(theta)};
        rho_theta_cartesian[j] = radius * theta_cartesian[j];

        // North pole.
        positions[j] = VertexAttributeValues::Float32x3Value{0.0f, summit, 0.0f};
        uvs[j]       = VertexAttributeValues::Float32x2Value{s_texture_polar, 1.0f};
        normals[j]   = VertexAttributeValues::Float32x3Value{0.0f, 1.0f, 0.0f};

        // South pole.
        const std::size_t idx = static_cast<std::size_t>(vert_offset_south_cap) + j;
        positions[idx]        = VertexAttributeValues::Float32x3Value{0.0f, -summit, 0.0f};
        uvs[idx]              = VertexAttributeValues::Float32x2Value{s_texture_polar, 0.0f};
        normals[idx]          = VertexAttributeValues::Float32x3Value{0.0f, -1.0f, 0.0f};
    }

    // Equatorial vertices. The last column repeats the first so the seam has its
    // own vertices; `s_texture` runs from 1 at the seam back down to 0.
    for (std::size_t j = 0; j < static_cast<std::size_t>(lonsp1); ++j) {
        const float s_texture = 1.0f - static_cast<float>(j) * to_tex_horizontal;
        s_texture_cache[j]    = s_texture;

        // Wrap to the first element on reaching the last.
        const std::size_t j_mod = j % static_cast<std::size_t>(longitudes_);
        const glm::vec2 tc      = theta_cartesian[j_mod];
        const glm::vec2 rtc     = rho_theta_cartesian[j_mod];

        // North equator.
        const std::size_t idxn = static_cast<std::size_t>(vert_offset_north_equator) + j;
        positions[idxn]        = VertexAttributeValues::Float32x3Value{rtc.x, half_length, -rtc.y};
        uvs[idxn]              = VertexAttributeValues::Float32x2Value{s_texture, vt_aspect_north};
        normals[idxn]          = VertexAttributeValues::Float32x3Value{tc.x, 0.0f, -tc.y};

        // South equator.
        const std::size_t idxs = static_cast<std::size_t>(vert_offset_south_equator) + j;
        positions[idxs]        = VertexAttributeValues::Float32x3Value{rtc.x, -half_length, -rtc.y};
        uvs[idxs]              = VertexAttributeValues::Float32x2Value{s_texture, vt_aspect_south};
        normals[idxs]          = VertexAttributeValues::Float32x3Value{tc.x, 0.0f, -tc.y};
    }

    // Hemisphere vertices.
    for (std::uint32_t i = 0; i < half_latsn1; ++i) {
        const float ip1f = static_cast<float>(i) + 1.0f;
        const float phi  = ip1f * to_phi;

        const float sin_phi_south = std::sin(phi);
        const float cos_phi_south = std::cos(phi);

        // The hemispheres are symmetrical, so the sine and cosine only need to be
        // computed once and swapped between north and south.
        const float cos_phi_north = sin_phi_south;
        const float sin_phi_north = -cos_phi_south;

        const float rho_cos_phi_north = radius * cos_phi_north;
        const float rho_sin_phi_north = radius * sin_phi_north;
        const float z_offset_north    = half_length - rho_sin_phi_north;

        const float rho_cos_phi_south = radius * cos_phi_south;
        const float rho_sin_phi_south = radius * sin_phi_south;
        const float z_offset_south    = -half_length - rho_sin_phi_south;

        // Texture coordinates.
        const float t_tex_fac    = ip1f * to_tex_vertical;
        const float cmpl_tex_fac = 1.0f - t_tex_fac;
        const float t_tex_north  = cmpl_tex_fac + vt_aspect_north * t_tex_fac;
        const float t_tex_south  = cmpl_tex_fac * vt_aspect_south;

        const std::uint32_t i_lonsp1            = i * lonsp1;
        const std::uint32_t vert_curr_lat_north = vert_offset_north_hemi + i_lonsp1;
        const std::uint32_t vert_curr_lat_south = vert_offset_south_hemi + i_lonsp1;

        for (std::size_t j = 0; j < static_cast<std::size_t>(lonsp1); ++j) {
            const std::size_t j_mod = j % static_cast<std::size_t>(longitudes_);
            const glm::vec2 tc      = theta_cartesian[j_mod];
            const float s_texture   = s_texture_cache[j];

            // North hemisphere.
            const std::size_t idxn = static_cast<std::size_t>(vert_curr_lat_north) + j;
            positions[idxn]        = VertexAttributeValues::Float32x3Value{rho_cos_phi_north * tc.x, z_offset_north,
                                                                           -rho_cos_phi_north * tc.y};
            uvs[idxn]              = VertexAttributeValues::Float32x2Value{s_texture, t_tex_north};
            normals[idxn] =
                VertexAttributeValues::Float32x3Value{cos_phi_north * tc.x, -sin_phi_north, -cos_phi_north * tc.y};

            // South hemisphere.
            const std::size_t idxs = static_cast<std::size_t>(vert_curr_lat_south) + j;
            positions[idxs]        = VertexAttributeValues::Float32x3Value{rho_cos_phi_south * tc.x, z_offset_south,
                                                                           -rho_cos_phi_south * tc.y};
            uvs[idxs]              = VertexAttributeValues::Float32x2Value{s_texture, t_tex_south};
            normals[idxs] =
                VertexAttributeValues::Float32x3Value{cos_phi_south * tc.x, -sin_phi_south, -cos_phi_south * tc.y};
        }
    }

    // Cylinder vertices. Both equators are excluded from the interpolation, so
    // only the `rings - 1` interior rings are written here.
    if (calc_middle) {
        const float to_fac      = 1.0f / static_cast<float>(ringsp1);
        std::size_t idx_cyl_lat = static_cast<std::size_t>(vert_offset_cylinder);

        for (std::uint32_t h = 1; h < ringsp1; ++h) {
            const float fac       = static_cast<float>(h) * to_fac;
            const float cmpl_fac  = 1.0f - fac;
            const float t_texture = cmpl_fac * vt_aspect_north + fac * vt_aspect_south;
            const float z         = half_length - 2.0f * half_length * fac;

            for (std::size_t j = 0; j < static_cast<std::size_t>(lonsp1); ++j) {
                const std::size_t j_mod = j % static_cast<std::size_t>(longitudes_);
                const glm::vec2 tc      = theta_cartesian[j_mod];
                const glm::vec2 rtc     = rho_theta_cartesian[j_mod];

                positions[idx_cyl_lat] = VertexAttributeValues::Float32x3Value{rtc.x, z, -rtc.y};
                uvs[idx_cyl_lat]       = VertexAttributeValues::Float32x2Value{s_texture_cache[j], t_texture};
                normals[idx_cyl_lat]   = VertexAttributeValues::Float32x3Value{tc.x, 0.0f, -tc.y};

                ++idx_cyl_lat;
            }
        }
    }

    // Triangle indices. The stride is 3 for the polar fans and 6 for the two
    // triangles of each quad, so the region offsets are computed the same way.
    const std::uint32_t lons3     = longitudes_ * 3u;
    const std::uint32_t lons6     = longitudes_ * 6u;
    const std::uint32_t hemi_lons = half_latsn1 * lons6;

    const std::uint32_t tri_offset_north_hemi = lons3;
    const std::uint32_t tri_offset_cylinder   = tri_offset_north_hemi + hemi_lons;
    const std::uint32_t tri_offset_south_hemi = tri_offset_cylinder + ringsp1 * lons6;
    const std::uint32_t tri_offset_south_cap  = tri_offset_south_hemi + hemi_lons;

    const std::size_t fs_len = static_cast<std::size_t>(tri_offset_south_cap) + lons3;
    std::vector<std::uint32_t> tris(fs_len, 0u);

    // Polar caps.
    {
        std::uint32_t i = 0;
        std::size_t k   = 0;
        std::size_t m   = static_cast<std::size_t>(tri_offset_south_cap);
        while (i < longitudes_) {
            // North.
            tris[k]     = i;
            tris[k + 1] = vert_offset_north_hemi + i;
            tris[k + 2] = vert_offset_north_hemi + i + 1u;

            // South.
            tris[m]     = vert_offset_south_cap + i;
            tris[m + 1] = vert_offset_south_polar + i + 1u;
            tris[m + 2] = vert_offset_south_polar + i;

            ++i;
            k += 3;
            m += 3;
        }
    }

    // Hemispheres.
    {
        std::uint32_t i = 0;
        std::size_t k   = static_cast<std::size_t>(tri_offset_north_hemi);
        std::size_t m   = static_cast<std::size_t>(tri_offset_south_hemi);

        while (i < half_latsn1) {
            const std::uint32_t i_lonsp1 = i * lonsp1;

            const std::uint32_t vert_curr_lat_north = vert_offset_north_hemi + i_lonsp1;
            const std::uint32_t vert_next_lat_north = vert_curr_lat_north + lonsp1;

            const std::uint32_t vert_curr_lat_south = vert_offset_south_equator + i_lonsp1;
            const std::uint32_t vert_next_lat_south = vert_curr_lat_south + lonsp1;

            std::uint32_t j = 0;
            while (j < longitudes_) {
                // North.
                const std::uint32_t north00 = vert_curr_lat_north + j;
                const std::uint32_t north01 = vert_next_lat_north + j;
                const std::uint32_t north11 = vert_next_lat_north + j + 1u;
                const std::uint32_t north10 = vert_curr_lat_north + j + 1u;

                tris[k]     = north00;
                tris[k + 1] = north11;
                tris[k + 2] = north10;

                tris[k + 3] = north00;
                tris[k + 4] = north01;
                tris[k + 5] = north11;

                // South.
                const std::uint32_t south00 = vert_curr_lat_south + j;
                const std::uint32_t south01 = vert_next_lat_south + j;
                const std::uint32_t south11 = vert_next_lat_south + j + 1u;
                const std::uint32_t south10 = vert_curr_lat_south + j + 1u;

                tris[m]     = south00;
                tris[m + 1] = south11;
                tris[m + 2] = south10;

                tris[m + 3] = south00;
                tris[m + 4] = south01;
                tris[m + 5] = south11;

                ++j;
                k += 6;
                m += 6;
            }

            ++i;
        }
    }

    // Cylinder.
    {
        std::uint32_t i = 0;
        std::size_t k   = static_cast<std::size_t>(tri_offset_cylinder);

        while (i < ringsp1) {
            const std::uint32_t vert_curr_lat = vert_offset_north_equator + i * lonsp1;
            const std::uint32_t vert_next_lat = vert_curr_lat + lonsp1;

            std::uint32_t j = 0;
            while (j < longitudes_) {
                const std::uint32_t cy00 = vert_curr_lat + j;
                const std::uint32_t cy01 = vert_next_lat + j;
                const std::uint32_t cy11 = vert_next_lat + j + 1u;
                const std::uint32_t cy10 = vert_curr_lat + j + 1u;

                tris[k]     = cy00;
                tris[k + 1] = cy11;
                tris[k + 2] = cy10;

                tris[k + 3] = cy00;
                tris[k + 4] = cy01;
                tris[k + 5] = cy11;

                ++j;
                k += 6;
            }

            ++i;
        }
    }

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs))
        .with_inserted_indices(Indices{std::move(tris)});
}

Mesh CuboidMeshBuilder::build() const {
    const glm::vec3 minimum = -half_size;
    const glm::vec3 maximum = half_size;

    // Bevy's vertex table: position, face normal, and face UV, six faces of four vertices each.
    struct FaceVertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
    };
    const std::array<FaceVertex, 24> vertices{{
        // Front
        {{minimum.x, minimum.y, maximum.z}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{maximum.x, minimum.y, maximum.z}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{maximum.x, maximum.y, maximum.z}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{minimum.x, maximum.y, maximum.z}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        // Back
        {{minimum.x, maximum.y, minimum.z}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{maximum.x, maximum.y, minimum.z}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{maximum.x, minimum.y, minimum.z}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
        {{minimum.x, minimum.y, minimum.z}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        // Right
        {{maximum.x, minimum.y, minimum.z}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{maximum.x, maximum.y, minimum.z}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{maximum.x, maximum.y, maximum.z}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{maximum.x, minimum.y, maximum.z}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        // Left
        {{minimum.x, minimum.y, maximum.z}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{minimum.x, maximum.y, maximum.z}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{minimum.x, maximum.y, minimum.z}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{minimum.x, minimum.y, minimum.z}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        // Top
        {{maximum.x, maximum.y, minimum.z}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{minimum.x, maximum.y, minimum.z}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{minimum.x, maximum.y, maximum.z}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{maximum.x, maximum.y, maximum.z}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        // Bottom
        {{maximum.x, minimum.y, maximum.z}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{minimum.x, minimum.y, maximum.z}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        {{minimum.x, minimum.y, minimum.z}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{maximum.x, minimum.y, minimum.z}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
    }};

    std::vector<VertexAttributeValues::Float32x3Value> positions;
    std::vector<VertexAttributeValues::Float32x3Value> normals;
    std::vector<VertexAttributeValues::Float32x2Value> uvs;
    positions.reserve(vertices.size());
    normals.reserve(vertices.size());
    uvs.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        positions.push_back({vertex.position.x, vertex.position.y, vertex.position.z});
        normals.push_back({vertex.normal.x, vertex.normal.y, vertex.normal.z});
        uvs.push_back({vertex.uv.x, vertex.uv.y});
    }

    std::vector<std::uint32_t> indices{
        0,  1,  2,  2,  3,  0,   // front
        4,  5,  6,  6,  7,  4,   // back
        8,  9,  10, 10, 11, 8,   // right
        12, 13, 14, 14, 15, 12,  // left
        16, 17, 18, 18, 19, 16,  // top
        20, 21, 22, 22, 23, 20,  // bottom
    };

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs))
        .with_inserted_indices(Indices{std::move(indices)});
}

PlaneMeshBuilder::PlaneMeshBuilder(glm::vec3 normal, glm::vec2 size)
    : plane(Plane3d{normal, size * 0.5f}), subdivisions(0) {}

PlaneMeshBuilder PlaneMeshBuilder::from_size(glm::vec2 size) {
    PlaneMeshBuilder builder{};
    builder.plane.half_size = size * 0.5f;
    return builder;
}

PlaneMeshBuilder PlaneMeshBuilder::from_length(float length) {
    PlaneMeshBuilder builder{};
    builder.plane.half_size = glm::vec2(length) * 0.5f;
    return builder;
}

Mesh PlaneMeshBuilder::build() const {
    const std::uint32_t z_vertex_count = subdivisions + 2;
    const std::uint32_t x_vertex_count = subdivisions + 2;
    const std::size_t num_vertices     = static_cast<std::size_t>(z_vertex_count) * x_vertex_count;
    const std::size_t num_indices      = static_cast<std::size_t>(z_vertex_count - 1) * (x_vertex_count - 1) * 6;

    std::vector<VertexAttributeValues::Float32x3Value> positions;
    std::vector<VertexAttributeValues::Float32x3Value> normals;
    std::vector<VertexAttributeValues::Float32x2Value> uvs;
    std::vector<std::uint32_t> indices;
    positions.reserve(num_vertices);
    normals.reserve(num_vertices);
    uvs.reserve(num_vertices);
    indices.reserve(num_indices);

    // The default normal is +Y; every other normal rotates the XZ plane onto it with Bevy's
    // `Quat::from_rotation_arc`. Epix builds the rotation directly so the mesh module keeps its
    // plain `glm/glm.hpp` math dependency; glam's three branches are reproduced exactly, because
    // the opposite case picks one specific axis rather than any 180 degree rotation.
    const glm::vec3 from             = {0.0f, 1.0f, 0.0f};
    const glm::vec3 to               = glm::normalize(plane.normal);
    const float dot                  = glm::dot(from, to);
    constexpr float kOneMinusEpsilon = 1.0f - 2.0f * std::numeric_limits<float>::epsilon();
    const auto rotate                = [&](glm::vec3 value) {
        // `dot > 1 - 2 * f32::EPSILON`: the vectors already agree, so the rotation is the identity.
        if (dot > kOneMinusEpsilon) return value;
        if (dot < -kOneMinusEpsilon) {
            // `Quat::from_axis_angle(from.any_orthonormal_vector(), PI)`: the axis is the Pixar
            // orthonormal-basis vector, and a half turn about it reflects through that axis.
            const float sign = std::signbit(from.z) ? -1.0f : 1.0f;
            const float a    = -1.0f / (sign + from.z);
            const float b    = from.x * from.y * a;
            const glm::vec3 axis{b, sign + from.y * from.y * a, -from.y};
            return 2.0f * glm::dot(axis, value) * axis - value;
        }
        // Otherwise the shortest arc: Rodrigues about the normalized cross product, by the angle
        // whose sine is that product's length and whose cosine is the dot product.
        const glm::vec3 cross_in = glm::cross(from, to);
        const float cross_length = glm::length(cross_in);
        const glm::vec3 axis     = cross_in / cross_length;
        const float angle        = std::atan2(cross_length, dot);
        const float sin_angle    = std::sin(angle);
        const float cos_rot      = std::cos(angle);
        return value * cos_rot + glm::cross(axis, value) * sin_angle +
               axis * (glm::dot(axis, value) * (1.0f - cos_rot));
    };
    const glm::vec2 size = plane.half_size * 2.0f;

    for (std::uint32_t z = 0; z < z_vertex_count; ++z) {
        for (std::uint32_t x = 0; x < x_vertex_count; ++x) {
            const float tx      = static_cast<float>(x) / static_cast<float>(x_vertex_count - 1);
            const float tz      = static_cast<float>(z) / static_cast<float>(z_vertex_count - 1);
            const auto position = rotate(glm::vec3((-0.5f + tx) * size.x, 0.0f, (-0.5f + tz) * size.y));
            positions.push_back({position.x, position.y, position.z});
            normals.push_back({plane.normal.x, plane.normal.y, plane.normal.z});
            uvs.push_back({tx, tz});
        }
    }

    for (std::uint32_t z = 0; z + 1 < z_vertex_count; ++z) {
        for (std::uint32_t x = 0; x + 1 < x_vertex_count; ++x) {
            const std::uint32_t quad = z * x_vertex_count + x;
            indices.push_back(quad + x_vertex_count + 1);
            indices.push_back(quad + 1);
            indices.push_back(quad + x_vertex_count);
            indices.push_back(quad);
            indices.push_back(quad + x_vertex_count);
            indices.push_back(quad + 1);
        }
    }

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
}

Mesh Segment3dMeshBuilder::build() const {
    std::vector<VertexAttributeValues::Float32x3Value> positions;
    positions.reserve(2);
    for (const auto& vertex : segment.vertices) positions.push_back({vertex.x, vertex.y, vertex.z});
    std::vector<std::uint32_t> indices{0, 1};

    return Mesh(wgpu::PrimitiveTopology::eLineList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions));
}

Mesh Polyline3dMeshBuilder::build() const {
    std::vector<VertexAttributeValues::Float32x3Value> positions;
    positions.reserve(polyline_.vertices.size());
    for (const auto& vertex : polyline_.vertices) positions.push_back({vertex.x, vertex.y, vertex.z});

    std::vector<std::uint32_t> indices;
    if (polyline_.vertices.size() >= 2) indices.reserve((polyline_.vertices.size() - 1) * 2);
    for (std::uint32_t index = 0; index + 1 < static_cast<std::uint32_t>(polyline_.vertices.size()); ++index) {
        indices.insert(indices.end(), {index, index + 1});
    }

    return Mesh(wgpu::PrimitiveTopology::eLineList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions));
}

namespace {
/** @brief The normal of a triangle with zeroing, so a `glm::vec3` is always produced
 *  for meshing (Bevy `normal_vec`, `bevy_mesh/src/primitives/dim3/triangle3d.rs:43-47`). */
glm::vec3 normal_vec(const Triangle3d& triangle) {
    const auto normal = triangle.normal();
    return normal.has_value() ? normal->value() : glm::vec3(0.0f);
}
}  // namespace

Mesh Triangle3dMeshBuilder::build() const {
    std::vector<VertexAttributeValues::Float32x3Value> positions;
    positions.reserve(3);
    for (const auto& vertex : triangle.vertices) positions.push_back({vertex.x, vertex.y, vertex.z});

    // A degenerate triangle takes Bevy's zero normal instead of an undefined direction.
    const auto normal       = normal_vec(triangle);
    const auto triangle_uvs = triangle_uv_coords(triangle.vertices);
    std::vector<VertexAttributeValues::Float32x3Value> normals(3, {normal.x, normal.y, normal.z});
    std::vector<VertexAttributeValues::Float32x2Value> uvs{
        {triangle_uvs[0].x, triangle_uvs[0].y},
        {triangle_uvs[1].x, triangle_uvs[1].y},
        {triangle_uvs[2].x, triangle_uvs[2].y},
    };
    std::vector<std::uint32_t> indices{0, 1, 2};

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
}

Mesh TetrahedronMeshBuilder::build() const {
    auto faces = tetrahedron.faces();
    // A negative signed volume means the face normals point inward, so reverse every face.
    if (tetrahedron.signed_volume() < 0.0f) {
        for (auto& face : faces) face.reverse();
    }

    std::vector<VertexAttributeValues::Float32x3Value> positions;
    std::vector<VertexAttributeValues::Float32x3Value> normals;
    std::vector<VertexAttributeValues::Float32x2Value> uvs;
    positions.reserve(12);
    normals.reserve(12);
    uvs.reserve(12);

    // Each face is meshed as its own triangle and the data is appended in order.
    for (const auto& face : faces) {
        const auto face_normal = normal_vec(face);
        const auto face_uvs    = triangle_uv_coords(face.vertices);
        for (std::size_t index = 0; index < 3; ++index) {
            positions.push_back({face.vertices[index].x, face.vertices[index].y, face.vertices[index].z});
            normals.push_back({face_normal.x, face_normal.y, face_normal.z});
            uvs.push_back({face_uvs[index].x, face_uvs[index].y});
        }
    }

    // Four faces sharing no vertices.
    std::vector<std::uint32_t> indices{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
}
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

glm::vec2 Rectangle::closest_point(glm::vec2 point) const noexcept { return glm::clamp(point, -half_size, half_size); }

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

Mesh ConvexPolygonMeshBuilder::build() const {
    const auto vertex_count = vertices.size();
    std::vector<VertexAttributeValues::Float32x3Value> positions;
    std::vector<std::uint32_t> indices;
    positions.reserve(vertex_count);
    if (vertex_count >= 2) indices.reserve((vertex_count - 2) * 3);

    for (const auto vertex : vertices) {
        positions.push_back({vertex.x, vertex.y, 0.0f});
    }
    for (std::uint32_t index = 2; index < static_cast<std::uint32_t>(vertex_count); ++index) {
        indices.insert(indices.end(), {0, index - 1, index});
    }

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_indices(Indices{std::move(indices)});
}

Mesh AnnulusMeshBuilder::build() const {
    const float inner_radius = annulus.inner_circle.radius;
    const float outer_radius = annulus.outer_circle.radius;

    const std::size_t num_vertices = (static_cast<std::size_t>(resolution) + 1) * 2;
    std::vector<VertexAttributeValues::Float32x3Value> positions;
    std::vector<VertexAttributeValues::Float32x3Value> normals(num_vertices, {0.0f, 0.0f, 1.0f});
    std::vector<VertexAttributeValues::Float32x2Value> uvs;
    std::vector<std::uint32_t> indices;
    positions.reserve(num_vertices);
    uvs.reserve(num_vertices);
    indices.reserve(static_cast<std::size_t>(resolution) * 6);

    // One more set of vertices than might be expected: the vertices at `start_angle` are
    // duplicated so the UV rectangle's top and bottom can meet as the circle closes.
    constexpr float start_angle = std::numbers::pi_v<float> * 0.5f;
    const float step            = std::numbers::pi_v<float> * 2.0f / static_cast<float>(resolution);
    for (std::uint32_t index = 0; index <= resolution; ++index) {
        const float theta  = start_angle + static_cast<float>(index % resolution) * step;
        const float sine   = std::sin(theta);
        const float cosine = std::cos(theta);
        positions.push_back({cosine * inner_radius, sine * inner_radius, 0.0f});
        positions.push_back({cosine * outer_radius, sine * outer_radius, 0.0f});

        // The first UV direction is radial and the second angular: lines of constant U map to
        // circles and lines of constant V map to radial segments.
        const float v = static_cast<float>(index) / static_cast<float>(resolution);
        uvs.push_back({0.0f, v});
        uvs.push_back({1.0f, v});
    }

    for (std::uint32_t index = 0; index < resolution; ++index) {
        const std::uint32_t inner_vertex = 2 * index;
        const std::uint32_t outer_vertex = 2 * index + 1;
        const std::uint32_t next_inner   = inner_vertex + 2;
        const std::uint32_t next_outer   = outer_vertex + 2;
        indices.insert(indices.end(), {inner_vertex, outer_vertex, next_outer});
        indices.insert(indices.end(), {next_outer, next_inner, inner_vertex});
    }

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs))
        .with_inserted_indices(Indices{std::move(indices)});
}

RhombusMeshBuilder::RhombusMeshBuilder(float horizontal_diagonal, float vertical_diagonal)
    : half_diagonals(horizontal_diagonal * 0.5f, vertical_diagonal * 0.5f) {
    assert(horizontal_diagonal >= 0.0f && "rhombus has a negative horizontal size");
    assert(vertical_diagonal >= 0.0f && "rhombus has a negative vertical size");
}

Mesh RhombusMeshBuilder::build() const {
    const float half_horizontal = half_diagonals.x;
    const float half_vertical   = half_diagonals.y;
    std::vector<VertexAttributeValues::Float32x3Value> positions{
        {half_horizontal, 0.0f, 0.0f},
        {0.0f, half_vertical, 0.0f},
        {-half_horizontal, 0.0f, 0.0f},
        {0.0f, -half_vertical, 0.0f},
    };
    std::vector<VertexAttributeValues::Float32x3Value> normals(4, {0.0f, 0.0f, 1.0f});
    std::vector<VertexAttributeValues::Float32x2Value> uvs{{1.0f, 0.5f}, {0.5f, 0.0f}, {0.0f, 0.5f}, {0.5f, 1.0f}};
    std::vector<std::uint32_t> indices{2, 0, 1, 2, 3, 0};

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
}

Mesh Triangle2dMeshBuilder::build() const {
    const std::array<glm::vec3, 3> vertices_3d{
        glm::vec3(triangle.vertices[0], 0.0f),
        glm::vec3(triangle.vertices[1], 0.0f),
        glm::vec3(triangle.vertices[2], 0.0f),
    };
    std::vector<VertexAttributeValues::Float32x3Value> positions{
        {vertices_3d[0].x, vertices_3d[0].y, vertices_3d[0].z},
        {vertices_3d[1].x, vertices_3d[1].y, vertices_3d[1].z},
        {vertices_3d[2].x, vertices_3d[2].y, vertices_3d[2].z},
    };
    std::vector<VertexAttributeValues::Float32x3Value> normals(3, {0.0f, 0.0f, 1.0f});
    const auto triangle_uvs = triangle_uv_coords(vertices_3d);
    std::vector<VertexAttributeValues::Float32x2Value> uvs{
        {triangle_uvs[0].x, triangle_uvs[0].y},
        {triangle_uvs[1].x, triangle_uvs[1].y},
        {triangle_uvs[2].x, triangle_uvs[2].y},
    };
    const bool is_counter_clockwise = triangle.winding_order() == WindingOrder::CounterClockwise;
    std::vector<std::uint32_t> indices =
        is_counter_clockwise ? std::vector<std::uint32_t>{0, 1, 2} : std::vector<std::uint32_t>{2, 1, 0};

    return Mesh(wgpu::PrimitiveTopology::eTriangleList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, std::move(normals))
        .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, std::move(uvs));
}

Mesh Capsule2dMeshBuilder::build() const {
    // The resolution is the number of vertices for one semicircle.
    const std::uint32_t vertex_count = 2 * resolution;

    std::vector<VertexAttributeValues::Float32x3Value> positions;
    std::vector<VertexAttributeValues::Float32x3Value> normals(vertex_count, {0.0f, 0.0f, 1.0f});
    std::vector<VertexAttributeValues::Float32x2Value> uvs;
    std::vector<std::uint32_t> indices;
    positions.reserve(vertex_count);
    uvs.reserve(vertex_count);
    // Six extra indices for the two triangles between the semicircles.
    if (resolution > 2) indices.reserve((static_cast<std::size_t>(resolution) - 2) * 6 + 6);

    const float radius = capsule.radius;
    const float step   = std::numbers::pi_v<float> * 2.0f / static_cast<float>(vertex_count);

    // With an even vertex count the top semicircle starts half a step in so the vertices are
    // positioned evenly around the capsule.
    const float start_angle = vertex_count % 2 == 0 ? step * 0.5f : 0.0f;

    // How much of the capsule's total half-height the hemicircle radius covers; used to stop
    // the UVs from stretching between the semicircles.
    const float radius_fraction = capsule.radius / (capsule.half_length + capsule.radius);

    for (std::uint32_t index = 0; index < resolution; ++index) {
        const float theta  = start_angle + static_cast<float>(index) * step;
        const float sine   = std::sin(theta);
        const float cosine = std::cos(theta);
        positions.push_back({cosine * radius, sine * radius + capsule.half_length, 0.0f});
        uvs.push_back({0.5f * (cosine + 1.0f), radius_fraction * (1.0f - 0.5f * (sine + 1.0f))});
    }
    for (std::uint32_t index = 1; index + 1 < resolution; ++index) {
        indices.insert(indices.end(), {0, index, index + 1});
    }
    // Top left triangle of the band between the semicircles.
    if (resolution >= 1) indices.insert(indices.end(), {0, resolution - 1, resolution});

    for (std::uint32_t index = resolution; index < vertex_count; ++index) {
        const float theta  = start_angle + static_cast<float>(index) * step;
        const float sine   = std::sin(theta);
        const float cosine = std::cos(theta);
        positions.push_back({cosine * radius, sine * radius - capsule.half_length, 0.0f});
        uvs.push_back({0.5f * (cosine + 1.0f), 1.0f - radius_fraction * 0.5f * (sine + 1.0f)});
    }
    for (std::uint32_t index = 1; index + 1 < resolution; ++index) {
        indices.insert(indices.end(), {resolution, resolution + index, resolution + index + 1});
    }
    // Bottom right triangle of the band between the semicircles.
    if (vertex_count >= 1) indices.insert(indices.end(), {resolution, vertex_count - 1, 0});

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

Mesh Segment2dMeshBuilder::build() const {
    std::vector<VertexAttributeValues::Float32x3Value> positions;
    positions.reserve(2);
    for (const auto vertex : segment.vertices) positions.push_back({vertex.x, vertex.y, 0.0f});
    std::vector<std::uint32_t> indices{0, 1};

    return Mesh(wgpu::PrimitiveTopology::eLineList, kDefaultMeshAssetUsage)
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions))
        .with_inserted_indices(Indices{std::move(indices)});
}

Mesh Polyline2dMeshBuilder::build() const {
    std::vector<VertexAttributeValues::Float32x3Value> positions;
    positions.reserve(polyline_.vertices.size());
    for (const auto vertex : polyline_.vertices) positions.push_back({vertex.x, vertex.y, 0.0f});

    std::vector<std::uint32_t> indices;
    if (polyline_.vertices.size() >= 2) indices.reserve((polyline_.vertices.size() - 1) * 2);
    for (std::uint32_t index = 0; index + 1 < static_cast<std::uint32_t>(polyline_.vertices.size()); ++index) {
        indices.insert(indices.end(), {index, index + 1});
    }

    return Mesh(wgpu::PrimitiveTopology::eLineList, kDefaultMeshAssetUsage)
        .with_inserted_indices(Indices{std::move(indices)})
        .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, std::move(positions));
}

RegularPolygonMeshBuilder::RegularPolygonMeshBuilder(float circumradius, std::uint32_t sides)
    : circumradius(circumradius), sides(sides) {
    assert(!std::signbit(circumradius) && "polygon has a negative radius");
    assert(sides > 2 && "polygon has less than 3 sides");
}

Mesh RegularPolygonMeshBuilder::build() const { return EllipseMeshBuilder{circumradius, circumradius, sides}.build(); }

RectangleMeshBuilder::RectangleMeshBuilder(float width, float height) : half_size(width * 0.5f, height * 0.5f) {
    assert(width >= 0.0f && "rectangle has a negative width");
    assert(height >= 0.0f && "rectangle has a negative height");
}

Mesh RectangleMeshBuilder::build() const {
    const auto half_width  = half_size.x;
    const auto half_height = half_size.y;
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

// The ring builder is defined in this translation unit, so the shape combinations that Epix
// supports are instantiated here to keep template bodies out of the header.
template struct EPIX_API RingMeshBuilder<Circle>;
template struct EPIX_API RingMeshBuilder<Capsule2d>;
template struct EPIX_API RingMeshBuilder<CircularSegment>;
template struct EPIX_API RingMeshBuilder<Rectangle>;
template struct EPIX_API RingMeshBuilder<RegularPolygon>;
template struct EPIX_API RingMeshBuilder<Rhombus>;
template struct EPIX_API RingMeshBuilder<Triangle2d>;

template struct EPIX_API Ring<Circle>;
template struct EPIX_API Ring<Capsule2d>;
template struct EPIX_API Ring<CircularSegment>;
template struct EPIX_API Ring<Rectangle>;
template struct EPIX_API Ring<RegularPolygon>;
template struct EPIX_API Ring<Rhombus>;
template struct EPIX_API Ring<Triangle2d>;
}  // namespace epix::mesh
