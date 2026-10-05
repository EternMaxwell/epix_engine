#include <gtest/gtest.h>

#include <epix/math/rect.hpp>
#include <glm/gtc/epsilon.hpp>

namespace {
using ::epix::math::IRect;
using ::epix::math::Rect;
using ::epix::math::URect;

/** @brief Bevy's `abs_diff_eq(x, 1e-5)` on a `Vec2`. */
bool near(glm::vec2 actual, glm::vec2 expected) {
    return glm::all(glm::epsilonEqual(actual, expected, 1e-5f));
}
}  // namespace

// Bevy's `bevy_math::rects::rect` tests, ported.
TEST(Rect, WellFormed) {
    const Rect r = Rect::from_center_size(glm::vec2(3.0f, -5.0f), glm::vec2(8.0f, 11.0f));

    EXPECT_TRUE(near(r.min, glm::vec2(-1.0f, -10.5f)));
    EXPECT_TRUE(near(r.max, glm::vec2(7.0f, 0.5f)));
    EXPECT_TRUE(near(r.center(), glm::vec2(3.0f, -5.0f)));
    EXPECT_NEAR(r.width(), 8.0f, 1e-5f);
    EXPECT_NEAR(r.height(), 11.0f, 1e-5f);
    EXPECT_TRUE(near(r.size(), glm::vec2(8.0f, 11.0f)));
    EXPECT_TRUE(near(r.half_size(), glm::vec2(4.0f, 5.5f)));

    EXPECT_TRUE(r.contains(glm::vec2(3.0f, -5.0f)));
    EXPECT_TRUE(r.contains(glm::vec2(-1.0f, -10.5f)));
    EXPECT_TRUE(r.contains(glm::vec2(-1.0f, 0.5f)));
    EXPECT_TRUE(r.contains(glm::vec2(7.0f, -10.5f)));
    EXPECT_TRUE(r.contains(glm::vec2(7.0f, 0.5f)));
    EXPECT_FALSE(r.contains(glm::vec2(50.0f, -5.0f)));
}

TEST(Rect, Union) {
    const Rect r = Rect::from_center_size(glm::vec2(0.0f), glm::vec2(1.0f));

    const Rect overlapping{.min = glm::vec2(-0.8f, 0.3f), .max = glm::vec2(0.1f, 0.7f)};
    EXPECT_TRUE(near(r.union_with(overlapping).min, glm::vec2(-0.8f, -0.5f)));
    EXPECT_TRUE(near(r.union_with(overlapping).max, glm::vec2(0.5f, 0.7f)));

    const Rect disjoint{.min = glm::vec2(-1.8f, -0.5f), .max = glm::vec2(-1.5f, 0.3f)};
    EXPECT_TRUE(near(r.union_with(disjoint).min, glm::vec2(-1.8f, -0.5f)));
    EXPECT_TRUE(near(r.union_with(disjoint).max, glm::vec2(0.5f, 0.5f)));

    const Rect included = Rect::from_center_size(glm::vec2(0.0f), glm::vec2(0.5f));
    EXPECT_TRUE(near(r.union_with(included).min, r.min));
    EXPECT_TRUE(near(r.union_with(included).max, r.max));

    const Rect including = Rect::from_center_size(glm::vec2(0.0f), glm::vec2(1.5f));
    EXPECT_TRUE(near(r.union_with(including).min, including.min));
    EXPECT_TRUE(near(r.union_with(including).max, including.max));
}

TEST(Rect, UnionPoint) {
    const Rect r = Rect::from_center_size(glm::vec2(0.0f), glm::vec2(1.0f));

    EXPECT_TRUE(near(r.union_point(glm::vec2(0.3f, -0.2f)).min, r.min));
    EXPECT_TRUE(near(r.union_point(glm::vec2(0.3f, -0.2f)).max, r.max));

    const Rect outside = r.union_point(glm::vec2(10.0f, -3.0f));
    EXPECT_TRUE(near(outside.min, glm::vec2(-0.5f, -3.0f)));
    EXPECT_TRUE(near(outside.max, glm::vec2(10.0f, 0.5f)));
}

TEST(Rect, Intersect) {
    const Rect r = Rect::from_center_size(glm::vec2(0.0f), glm::vec2(1.0f));

    const Rect overlapping{.min = glm::vec2(-0.8f, 0.3f), .max = glm::vec2(0.1f, 0.7f)};
    EXPECT_TRUE(near(r.intersect(overlapping).min, glm::vec2(-0.5f, 0.3f)));
    EXPECT_TRUE(near(r.intersect(overlapping).max, glm::vec2(0.1f, 0.5f)));

    // Disjoint: the collapsed min keeps width from going negative.
    const Rect disjoint{.min = glm::vec2(-1.8f, -0.5f), .max = glm::vec2(-1.5f, 0.3f)};
    EXPECT_TRUE(r.intersect(disjoint).is_empty());
    EXPECT_LE(r.intersect(disjoint).width(), 1e-5f);

    const Rect included = Rect::from_center_size(glm::vec2(0.0f), glm::vec2(0.5f));
    EXPECT_TRUE(near(r.intersect(included).min, included.min));
    EXPECT_TRUE(near(r.intersect(included).max, included.max));

    const Rect including = Rect::from_center_size(glm::vec2(0.0f), glm::vec2(1.5f));
    EXPECT_TRUE(near(r.intersect(including).min, r.min));
    EXPECT_TRUE(near(r.intersect(including).max, r.max));
}

TEST(Rect, Inflate) {
    const Rect r = Rect::from_center_size(glm::vec2(0.0f), glm::vec2(1.0f));
    const Rect inflated = r.inflate(0.3f);
    EXPECT_TRUE(near(inflated.min, glm::vec2(-0.8f, -0.8f)));
    EXPECT_TRUE(near(inflated.max, glm::vec2(0.8f, 0.8f)));
}

TEST(Rect, Normalize) {
    const Rect r = Rect::create(2.0f, 3.0f, 4.0f, 6.0f);
    const Rect outer = Rect::create(0.0f, 0.0f, 10.0f, 10.0f);
    const Rect n = r.normalize(outer);

    EXPECT_EQ(n.min.x, 0.2f);
    EXPECT_EQ(n.min.y, 0.3f);
    EXPECT_EQ(n.max.x, 0.4f);
    EXPECT_EQ(n.max.y, 0.6f);
}

TEST(Rect, AreaAndEmptiness) {
    const Rect r = Rect::create(0.0f, 0.0f, 10.0f, 10.0f);
    EXPECT_EQ(r.area(), 100.0f);
    EXPECT_FALSE(r.is_empty());
    EXPECT_TRUE(Rect::EMPTY.is_empty());

    // A union with the empty rect returns the other rect, which is what EMPTY
    // is for (Bevy's comment on `Rect::EMPTY`).
    EXPECT_TRUE(near(r.union_with(Rect::EMPTY).min, r.min));
    EXPECT_TRUE(near(r.union_with(Rect::EMPTY).max, r.max));
}

TEST(Rect, ConvertsToIntegerRectsByTruncation) {
    const Rect r = Rect::create(1.5f, 2.5f, 3.5f, 4.5f);
    EXPECT_EQ(r.as_irect().min, glm::ivec2(1, 2));
    EXPECT_EQ(r.as_irect().max, glm::ivec2(3, 4));
    EXPECT_EQ(r.as_urect().min, glm::uvec2(1u, 2u));
    EXPECT_EQ(r.as_urect().max, glm::uvec2(3u, 4u));
}

TEST(IRect, WellFormed) {
    const IRect r = IRect::from_center_size(glm::ivec2(0), glm::ivec2(3, 2));

    EXPECT_EQ(r.min, glm::ivec2(-1));
    EXPECT_EQ(r.max, glm::ivec2(1));
    EXPECT_EQ(r.center(), glm::ivec2(0));
    EXPECT_EQ(r.width(), 2);
    EXPECT_EQ(r.height(), 2);
    EXPECT_EQ(r.size(), glm::ivec2(2));
    EXPECT_EQ(r.half_size(), glm::ivec2(1));
    EXPECT_TRUE(r.contains(glm::ivec2(0)));
    EXPECT_TRUE(r.contains(r.min));
    EXPECT_TRUE(r.contains(r.max));
    EXPECT_FALSE(r.contains(glm::ivec2(50, 5)));
}

TEST(IRect, UnionIntersectAndInflate) {
    const IRect r = IRect::create(0, 0, 5, 1);
    const IRect other = IRect::create(1, -1, 3, 3);

    EXPECT_EQ(r.union_with(other).min, glm::ivec2(0, -1));
    EXPECT_EQ(r.union_with(other).max, glm::ivec2(5, 3));
    EXPECT_EQ(r.union_point(glm::ivec2(3, 6)).min, glm::ivec2(0));
    EXPECT_EQ(r.union_point(glm::ivec2(3, 6)).max, glm::ivec2(5, 6));
    EXPECT_EQ(r.intersect(other).min, glm::ivec2(1, 0));
    EXPECT_EQ(r.intersect(other).max, glm::ivec2(3, 1));
    EXPECT_EQ(r.inflate(3).min, glm::ivec2(-3));
    EXPECT_EQ(r.inflate(3).max, glm::ivec2(8, 4));

    const IRect disjoint = r.intersect(IRect::create(12, 12, 14, 18));
    EXPECT_TRUE(disjoint.is_empty());
    EXPECT_EQ(disjoint.width(), 0);
    EXPECT_TRUE(IRect::EMPTY.is_empty());
}

TEST(IRect, ConvertsBetweenRectTypes) {
    const IRect r = IRect::create(-2, -3, 4, 5);
    EXPECT_EQ(r.as_rect().min, glm::vec2(-2.0f, -3.0f));
    EXPECT_EQ(r.as_rect().max, glm::vec2(4.0f, 5.0f));

    // Bevy casts each corner with `as u32`, wrapping a negative corner to a very
    // large value, and `URect::from_corners` then orders the two corners - so a
    // rect with a negative corner converts with its corners swapped. That is
    // Bevy's own arithmetic, not a port artifact.
    const URect converted = r.as_urect();
    EXPECT_EQ(converted.min, glm::uvec2(4u, 5u));
    EXPECT_EQ(converted.max, glm::uvec2(static_cast<std::uint32_t>(-2), static_cast<std::uint32_t>(-3)));
}

TEST(URect, WellFormed) {
    const URect r = URect::from_center_size(glm::uvec2(10u, 16u), glm::uvec2(8u, 12u));

    EXPECT_EQ(r.min, glm::uvec2(6u, 10u));
    EXPECT_EQ(r.max, glm::uvec2(14u, 22u));
    EXPECT_EQ(r.center(), glm::uvec2(10u, 16u));
    EXPECT_EQ(r.width(), 8u);
    EXPECT_EQ(r.height(), 12u);
    EXPECT_EQ(r.size(), glm::uvec2(8u, 12u));
    EXPECT_EQ(r.half_size(), glm::uvec2(4u, 6u));

    EXPECT_TRUE(r.contains(glm::uvec2(7u, 10u)));
    EXPECT_TRUE(r.contains(glm::uvec2(14u, 10u)));
    EXPECT_TRUE(r.contains(glm::uvec2(10u, 22u)));
    EXPECT_TRUE(r.contains(r.min));
    EXPECT_TRUE(r.contains(r.max));
    EXPECT_FALSE(r.contains(glm::uvec2(50u, 5u)));
}

TEST(URect, UnionIntersectAndInflate) {
    const URect r = URect::from_center_size(glm::uvec2(4u), glm::uvec2(4u));

    const URect overlapping{.min = glm::uvec2(0u), .max = glm::uvec2(3u)};
    EXPECT_EQ(r.union_with(overlapping).min, glm::uvec2(0u));
    EXPECT_EQ(r.union_with(overlapping).max, glm::uvec2(6u));

    const URect disjoint{.min = glm::uvec2(4u, 7u), .max = glm::uvec2(8u)};
    EXPECT_EQ(r.union_with(disjoint).min, glm::uvec2(2u));
    EXPECT_EQ(r.union_with(disjoint).max, glm::uvec2(8u));

    const URect included = URect::from_center_size(glm::uvec2(4u), glm::uvec2(2u));
    EXPECT_EQ(r.union_with(included).min, r.min);
    EXPECT_EQ(r.union_with(included).max, r.max);

    const URect including = URect::from_center_size(glm::uvec2(4u), glm::uvec2(6u));
    EXPECT_EQ(r.union_with(including).min, including.min);

    const URect point_inside = r.union_point(glm::uvec2(2u, 5u));
    EXPECT_EQ(point_inside.min, r.min);
    EXPECT_EQ(point_inside.max, r.max);
    const URect point_outside = r.union_point(glm::uvec2(10u, 5u));
    EXPECT_EQ(point_outside.min, glm::uvec2(2u));
    EXPECT_EQ(point_outside.max, glm::uvec2(10u, 6u));

    const URect wide = URect::from_center_size(glm::uvec2(6u), glm::uvec2(8u));
    const URect overlap_rect{.min = glm::uvec2(8u), .max = glm::uvec2(12u)};
    EXPECT_EQ(wide.intersect(overlap_rect).min, glm::uvec2(8u));
    EXPECT_EQ(wide.intersect(overlap_rect).max, glm::uvec2(10u));
    const URect far_rect{.min = glm::uvec2(12u), .max = glm::uvec2(14u, 18u)};
    EXPECT_TRUE(wide.intersect(far_rect).is_empty());
    EXPECT_EQ(wide.intersect(far_rect).width(), 0u);

    const URect inflate_source = URect::from_center_size(glm::uvec2(6u), glm::uvec2(6u));
    EXPECT_EQ(inflate_source.inflate(2).min, glm::uvec2(1u));
    EXPECT_EQ(inflate_source.inflate(2).max, glm::uvec2(11u));
    EXPECT_TRUE(URect::EMPTY.is_empty());
}

TEST(URect, InflateSaturatesAtZero) {
    // Bevy's `saturating_add_signed` clamps the min at zero before the collapse.
    const URect r = URect::create(0u, 0u, 1u, 1u);
    const URect shrunk = r.inflate(-5);
    EXPECT_EQ(shrunk.min, glm::uvec2(0u));
    EXPECT_EQ(shrunk.max, glm::uvec2(0u));
    EXPECT_TRUE(shrunk.is_empty());
}

TEST(URect, ConvertsBetweenRectTypes) {
    const URect r = URect::create(1u, 2u, 3u, 4u);
    EXPECT_EQ(r.as_rect().min, glm::vec2(1.0f, 2.0f));
    EXPECT_EQ(r.as_irect().max, glm::ivec2(3, 4));
}
