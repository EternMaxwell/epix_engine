#include <gtest/gtest.h>
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.extension.grid;

using namespace epix::ext::grid;

// ============================================================
// Helpers
// ============================================================
static constexpr std::array<std::uint32_t, 1> pos1(std::uint32_t x) { return {x}; }
static constexpr std::array<std::uint32_t, 2> pos2(std::uint32_t x, std::uint32_t y) { return {x, y}; }
static constexpr std::array<std::uint32_t, 3> pos3(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return {x, y, z};
}

// ============================================================
// 1D tests
// ============================================================
TEST(BitGrid1D, ConstructorZeroInit) {
    bit_grid<1> g({8});
    EXPECT_EQ(g.dimensions(), (std::array<std::uint32_t, 1>{8}));
    EXPECT_EQ(g.count(), 0u);
    EXPECT_TRUE(g.is_clear());
}

TEST(BitGrid1D, ConstructorNonMultipleOf8) {
    bit_grid<1> g({5});
    EXPECT_EQ(g.dimensions(), (std::array<std::uint32_t, 1>{5}));
    EXPECT_EQ(g.count(), 0u);
}

TEST(BitGrid1D, SetAndContains) {
    bit_grid<1> g({16});
    EXPECT_FALSE(g.contains(pos1(3)));
    auto r = g.set(pos1(3));
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(g.contains(pos1(3)));
    EXPECT_FALSE(g.contains(pos1(4)));
}

TEST(BitGrid1D, SetClearBit) {
    bit_grid<1> g({16});
    g.set(pos1(7));
    EXPECT_TRUE(g.contains(pos1(7)));
    g.set(pos1(7), false);
    EXPECT_FALSE(g.contains(pos1(7)));
}

TEST(BitGrid1D, GetReturnsCorrectValue) {
    bit_grid<1> g({8});
    g.set(pos1(2));
    auto r0 = g.get(pos1(0));
    ASSERT_TRUE(r0.has_value());
    EXPECT_FALSE(*r0);
    auto r2 = g.get(pos1(2));
    ASSERT_TRUE(r2.has_value());
    EXPECT_TRUE(*r2);
}

TEST(BitGrid1D, OutOfBoundsReturnsError) {
    bit_grid<1> g({4});
    EXPECT_FALSE(g.contains(pos1(4)));
    auto r = g.set(pos1(4));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), grid_error::OutOfBounds);
    auto rg = g.get(pos1(10));
    EXPECT_FALSE(rg.has_value());
    EXPECT_EQ(rg.error(), grid_error::OutOfBounds);
}

TEST(BitGrid1D, ResetClearsAll) {
    bit_grid<1> g({8});
    g.set(pos1(0));
    g.set(pos1(3));
    g.set(pos1(7));
    EXPECT_EQ(g.count(), 3u);
    g.reset();
    EXPECT_EQ(g.count(), 0u);
    EXPECT_TRUE(g.is_clear());
}

TEST(BitGrid1D, CountOneByte) {
    bit_grid<1> g({8});
    for (std::uint32_t i = 0; i < 8; ++i) g.set(pos1(i));
    EXPECT_EQ(g.count(), 8u);
}

TEST(BitGrid1D, CountNonMultipleOf8NoPaddingLeak) {
    // dim0=5: bits 5,6,7 are padding — set them manually to confirm count ignores padding
    // (bit_grid uses offset() for set, which bounds-checks, so padding cannot be set via API)
    bit_grid<1> g({5});
    for (std::uint32_t i = 0; i < 5; ++i) g.set(pos1(i));
    EXPECT_EQ(g.count(), 5u);
}

TEST(BitGrid1D, Equality) {
    bit_grid<1> a({8}), b({8});
    EXPECT_EQ(a, b);
    a.set(pos1(3));
    EXPECT_NE(a, b);
    b.set(pos1(3));
    EXPECT_EQ(a, b);
}

TEST(BitGrid1D, EqualityDifferentDimensions) {
    bit_grid<1> a({8}), b({16});
    EXPECT_NE(a, b);
}

// ============================================================
// 1D bitwise operations
// ============================================================
TEST(BitGrid1D, BitAnd) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    a.set(pos1(5));
    b.set(pos1(3));
    b.set(pos1(5));
    b.set(pos1(7));
    auto c = a & b;
    EXPECT_TRUE(c.contains(pos1(3)));
    EXPECT_TRUE(c.contains(pos1(5)));
    EXPECT_FALSE(c.contains(pos1(1)));
    EXPECT_FALSE(c.contains(pos1(7)));
    EXPECT_EQ(c.count(), 2u);
}

TEST(BitGrid1D, BitOr) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    b.set(pos1(5));
    b.set(pos1(7));
    auto c = a | b;
    EXPECT_EQ(c.count(), 4u);
    EXPECT_TRUE(c.contains(pos1(1)));
    EXPECT_TRUE(c.contains(pos1(3)));
    EXPECT_TRUE(c.contains(pos1(5)));
    EXPECT_TRUE(c.contains(pos1(7)));
}

TEST(BitGrid1D, BitXor) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    b.set(pos1(3));
    b.set(pos1(5));
    auto c = a ^ b;
    EXPECT_TRUE(c.contains(pos1(1)));
    EXPECT_FALSE(c.contains(pos1(3)));
    EXPECT_TRUE(c.contains(pos1(5)));
    EXPECT_EQ(c.count(), 2u);
}

TEST(BitGrid1D, BitNot) {
    bit_grid<1> g({8});
    g.set(pos1(0));
    g.set(pos1(7));
    auto n = ~g;
    EXPECT_FALSE(n.contains(pos1(0)));
    EXPECT_FALSE(n.contains(pos1(7)));
    for (std::uint32_t i = 1; i <= 6; ++i) EXPECT_TRUE(n.contains(pos1(i)));
    EXPECT_EQ(n.count(), 6u);
}

TEST(BitGrid1D, BitNotNonMultipleOf8) {
    // dim0=5: padding bits must stay 0 after bit_not
    bit_grid<1> g({5});
    g.set(pos1(0));
    g.set(pos1(2));
    auto n = ~g;
    EXPECT_EQ(n.count(), 3u);  // bits 1,3,4 set; bits 5,6,7 (padding) must stay 0
    EXPECT_FALSE(n.contains(pos1(0)));
    EXPECT_TRUE(n.contains(pos1(1)));
    EXPECT_FALSE(n.contains(pos1(2)));
    EXPECT_TRUE(n.contains(pos1(3)));
    EXPECT_TRUE(n.contains(pos1(4)));
}

TEST(BitGrid1D, InPlaceAndOrXor) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    b.set(pos1(3));
    b.set(pos1(5));
    // a &= b -> {3}
    bit_grid<1> ac = a;
    ac &= b;
    EXPECT_EQ(ac.count(), 1u);
    EXPECT_TRUE(ac.contains(pos1(3)));
    // a |= b -> {1,3,5}
    bit_grid<1> ao = a;
    ao |= b;
    EXPECT_EQ(ao.count(), 3u);
    // a ^= b -> {1,5}
    bit_grid<1> ax = a;
    ax ^= b;
    EXPECT_EQ(ax.count(), 2u);
    EXPECT_TRUE(ax.contains(pos1(1)));
    EXPECT_TRUE(ax.contains(pos1(5)));
}

TEST(BitGrid1D, DifferenceWith) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    a.set(pos1(5));
    b.set(pos1(3));
    a.difference_with(b);
    EXPECT_TRUE(a.contains(pos1(1)));
    EXPECT_FALSE(a.contains(pos1(3)));
    EXPECT_TRUE(a.contains(pos1(5)));
    EXPECT_EQ(a.count(), 2u);
}

// ============================================================
// 1D set-algebra query functions
// ============================================================
TEST(BitGrid1D, IntersectCount) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    a.set(pos1(5));
    b.set(pos1(3));
    b.set(pos1(5));
    b.set(pos1(7));
    EXPECT_EQ(a.intersect_count(b), 2u);
}

TEST(BitGrid1D, Intersect) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(2));
    a.set(pos1(4));
    b.set(pos1(4));
    b.set(pos1(6));
    EXPECT_TRUE(a.intersect(b));
}

TEST(BitGrid1D, IntersectFalse) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(0));
    a.set(pos1(2));
    b.set(pos1(4));
    b.set(pos1(6));
    EXPECT_FALSE(a.intersect(b));
}

TEST(BitGrid1D, UnionCount) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    b.set(pos1(3));
    b.set(pos1(5));
    EXPECT_EQ(a.union_count(b), 3u);
}

TEST(BitGrid1D, DifferenceCount) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    a.set(pos1(5));
    b.set(pos1(3));
    EXPECT_EQ(a.difference_count(b), 2u);
}

TEST(BitGrid1D, SymmetricDifferenceCount) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    b.set(pos1(3));
    b.set(pos1(5));
    EXPECT_EQ(a.symmetric_difference_count(b), 2u);
}

TEST(BitGrid1D, IsDisjoint) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(0));
    a.set(pos1(2));
    b.set(pos1(4));
    b.set(pos1(6));
    EXPECT_TRUE(a.is_disjoint(b));
    a.set(pos1(4));
    EXPECT_FALSE(a.is_disjoint(b));
}

TEST(BitGrid1D, IsSubsetSuperset) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(2));
    a.set(pos1(4));
    b.set(pos1(2));
    b.set(pos1(4));
    b.set(pos1(6));
    EXPECT_TRUE(a.is_subset(b));
    EXPECT_FALSE(b.is_subset(a));
    EXPECT_TRUE(b.is_superset(a));
    EXPECT_FALSE(a.is_superset(b));
}

// ============================================================
// 1D iter_set / iter_unset views
// ============================================================
TEST(BitGrid1D, IterSet) {
    bit_grid<1> g({8});
    g.set(pos1(1));
    g.set(pos1(4));
    g.set(pos1(7));
    std::vector<std::array<std::uint32_t, 1>> result;
    for (auto p : g.iter_set()) result.push_back(p);
    std::sort(result.begin(), result.end());
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], pos1(1));
    EXPECT_EQ(result[1], pos1(4));
    EXPECT_EQ(result[2], pos1(7));
}

TEST(BitGrid1D, IterUnset) {
    bit_grid<1> g({4});
    g.set(pos1(1));
    g.set(pos1(3));
    std::vector<std::array<std::uint32_t, 1>> result;
    for (auto p : g.iter_unset()) result.push_back(p);
    std::sort(result.begin(), result.end());
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], pos1(0));
    EXPECT_EQ(result[1], pos1(2));
}

TEST(BitGrid1D, IterSetEmpty) {
    bit_grid<1> g({8});
    std::vector<std::array<std::uint32_t, 1>> result;
    for (auto p : g.iter_set()) result.push_back(p);
    EXPECT_TRUE(result.empty());
}

// ============================================================
// 1D set-algebra view methods returning new grid
// ============================================================
TEST(BitGrid1D, IntersectionView) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    a.set(pos1(5));
    b.set(pos1(3));
    b.set(pos1(5));
    b.set(pos1(7));
    auto c = a.intersection(b);
    std::vector<std::array<std::uint32_t, 1>> result;
    for (auto p : c) result.push_back(p);
    std::sort(result.begin(), result.end());
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], pos1(3));
    EXPECT_EQ(result[1], pos1(5));
}

TEST(BitGrid1D, UnionView) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    b.set(pos1(3));
    b.set(pos1(5));
    auto c = a.set_union(b);
    std::vector<std::array<std::uint32_t, 1>> result;
    for (auto p : c) result.push_back(p);
    std::sort(result.begin(), result.end());
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], pos1(1));
    EXPECT_EQ(result[1], pos1(3));
    EXPECT_EQ(result[2], pos1(5));
}

TEST(BitGrid1D, DifferenceView) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    a.set(pos1(5));
    b.set(pos1(3));
    auto c = a.difference(b);
    std::vector<std::array<std::uint32_t, 1>> result;
    for (auto p : c) result.push_back(p);
    std::sort(result.begin(), result.end());
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], pos1(1));
    EXPECT_EQ(result[1], pos1(5));
}

TEST(BitGrid1D, SymmetricDifferenceView) {
    bit_grid<1> a({8}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    b.set(pos1(3));
    b.set(pos1(5));
    auto c = a.symmetric_difference(b);
    std::vector<std::array<std::uint32_t, 1>> result;
    for (auto p : c) result.push_back(p);
    std::sort(result.begin(), result.end());
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], pos1(1));
    EXPECT_EQ(result[1], pos1(5));
}

// ============================================================
// 2D tests
// ============================================================
TEST(BitGrid2D, Constructor) {
    bit_grid<2> g({4, 3});
    EXPECT_EQ(g.dimensions(), (std::array<std::uint32_t, 2>{4, 3}));
    EXPECT_EQ(g.count(), 0u);
    EXPECT_TRUE(g.is_clear());
}

TEST(BitGrid2D, SetAndContains) {
    bit_grid<2> g({8, 4});
    g.set(pos2(0, 0));
    g.set(pos2(7, 3));
    EXPECT_TRUE(g.contains(pos2(0, 0)));
    EXPECT_TRUE(g.contains(pos2(7, 3)));
    EXPECT_FALSE(g.contains(pos2(1, 1)));
    EXPECT_EQ(g.count(), 2u);
}

TEST(BitGrid2D, OutOfBoundsX) {
    bit_grid<2> g({4, 4});
    auto r = g.set(pos2(4, 0));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), grid_error::OutOfBounds);
}

TEST(BitGrid2D, OutOfBoundsY) {
    bit_grid<2> g({4, 4});
    auto r = g.set(pos2(0, 4));
    EXPECT_FALSE(r.has_value());
}

TEST(BitGrid2D, NonMultipleOf8Width) {
    // width=5: row_stride=1; each row occupies 1 byte with 3 padding bits
    bit_grid<2> g({5, 3});
    EXPECT_EQ(g.count(), 0u);
    for (std::uint32_t row = 0; row < 3; ++row)
        for (std::uint32_t col = 0; col < 5; ++col) g.set(pos2(col, row));
    EXPECT_EQ(g.count(), 15u);
}

TEST(BitGrid2D, BitAnd2D) {
    bit_grid<2> a({4, 4}), b({4, 4});
    a.set(pos2(0, 0));
    a.set(pos2(2, 2));
    a.set(pos2(3, 3));
    b.set(pos2(2, 2));
    b.set(pos2(3, 3));
    b.set(pos2(1, 1));
    auto c = a & b;
    EXPECT_EQ(c.count(), 2u);
    EXPECT_TRUE(c.contains(pos2(2, 2)));
    EXPECT_TRUE(c.contains(pos2(3, 3)));
    EXPECT_FALSE(c.contains(pos2(0, 0)));
    EXPECT_FALSE(c.contains(pos2(1, 1)));
}

TEST(BitGrid2D, BitNot2D) {
    // 3x3 grid, set one bit, flip: 8 set
    bit_grid<2> g({3, 3});
    g.set(pos2(1, 1));
    auto n = ~g;
    EXPECT_EQ(n.count(), 8u);
    EXPECT_FALSE(n.contains(pos2(1, 1)));
}

TEST(BitGrid2D, IterSet2D) {
    bit_grid<2> g({4, 4});
    g.set(pos2(0, 0));
    g.set(pos2(2, 1));
    g.set(pos2(3, 3));
    std::vector<std::array<std::uint32_t, 2>> result;
    for (auto p : g.iter_set()) result.push_back(p);
    EXPECT_EQ(result.size(), 3u);
    auto has = [&](auto p) { return std::find(result.begin(), result.end(), p) != result.end(); };
    EXPECT_TRUE(has(pos2(0, 0)));
    EXPECT_TRUE(has(pos2(2, 1)));
    EXPECT_TRUE(has(pos2(3, 3)));
}

// ============================================================
// 3D tests
// ============================================================
TEST(BitGrid3D, Constructor) {
    bit_grid<3> g({4, 3, 2});
    EXPECT_EQ(g.dimensions(), (std::array<std::uint32_t, 3>{4, 3, 2}));
    EXPECT_EQ(g.count(), 0u);
}

TEST(BitGrid3D, SetAllAndCount) {
    bit_grid<3> g({4, 3, 2});
    for (std::uint32_t z = 0; z < 2; ++z)
        for (std::uint32_t y = 0; y < 3; ++y)
            for (std::uint32_t x = 0; x < 4; ++x) g.set(pos3(x, y, z));
    EXPECT_EQ(g.count(), 24u);
}

TEST(BitGrid3D, OutOfBoundsZ) {
    bit_grid<3> g({4, 3, 2});
    auto r = g.set(pos3(0, 0, 2));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), grid_error::OutOfBounds);
}

TEST(BitGrid3D, IndexToPosRoundTrip) {
    // Verify that offset and index_to_pos are inverses for all cells
    // by setting a specific pattern and retrieving it via iter_set
    bit_grid<3> g({5, 4, 3});
    // Set positions where (x+y+z) is even
    std::size_t expected = 0;
    for (std::uint32_t z = 0; z < 3; ++z)
        for (std::uint32_t y = 0; y < 4; ++y)
            for (std::uint32_t x = 0; x < 5; ++x)
                if ((x + y + z) % 2 == 0) {
                    g.set(pos3(x, y, z));
                    ++expected;
                }
    EXPECT_EQ(g.count(), expected);
    // Verify iter_set yields only even-sum positions
    for (auto p : g.iter_set()) {
        EXPECT_EQ((p[0] + p[1] + p[2]) % 2u, 0u);
    }
}

TEST(BitGrid3D, BitNot3D) {
    bit_grid<3> g({2, 2, 2});  // 8 cells total
    g.set(pos3(0, 0, 0));
    g.set(pos3(1, 1, 1));
    auto n = ~g;
    EXPECT_EQ(n.count(), 6u);
    EXPECT_FALSE(n.contains(pos3(0, 0, 0)));
    EXPECT_FALSE(n.contains(pos3(1, 1, 1)));
}

TEST(BitGrid3D, IterSet3D) {
    bit_grid<3> g({3, 3, 3});
    g.set(pos3(0, 0, 0));
    g.set(pos3(1, 1, 1));
    g.set(pos3(2, 2, 2));
    std::size_t count = 0;
    for ([[maybe_unused]] auto p : g.iter_set()) ++count;
    EXPECT_EQ(count, 3u);
}

// ============================================================
// Boundary cases
// ============================================================
TEST(BitGrid1D, Dim0Equals1) {
    bit_grid<1> g({1});
    EXPECT_EQ(g.count(), 0u);
    g.set(pos1(0));
    EXPECT_EQ(g.count(), 1u);
    EXPECT_TRUE(g.contains(pos1(0)));
    auto r = g.set(pos1(1));
    EXPECT_FALSE(r.has_value());
}

TEST(BitGrid1D, Dim0Equals8ExactByte) {
    bit_grid<1> g({8});
    for (std::uint32_t i = 0; i < 8; ++i) g.set(pos1(i));
    auto n = ~g;
    EXPECT_EQ(n.count(), 0u);
}

TEST(BitGrid1D, Dim0Equals9StradlesTwoBytes) {
    bit_grid<1> g({9});
    for (std::uint32_t i = 0; i < 9; ++i) g.set(pos1(i));
    EXPECT_EQ(g.count(), 9u);
    auto n = ~g;
    EXPECT_EQ(n.count(), 0u);  // padding bit 9 must stay 0
}

TEST(BitGrid2D, EmptyGrid) {
    bit_grid<2> g({8, 8});
    EXPECT_TRUE(g.is_clear());
    EXPECT_EQ(g.count(), 0u);
    EXPECT_FALSE(g.intersect(g));
}

TEST(BitGrid2D, FullGrid) {
    bit_grid<2> g({8, 4});
    for (std::uint32_t y = 0; y < 4; ++y)
        for (std::uint32_t x = 0; x < 8; ++x) g.set(pos2(x, y));
    EXPECT_EQ(g.count(), 32u);
    EXPECT_TRUE(g.is_superset(g));
    EXPECT_TRUE(g.is_subset(g));
    EXPECT_FALSE(g.is_disjoint(g));
}

// ============================================================
// Regression: bit_not clears padding on 2D non-multiple-of-8
// ============================================================
TEST(BitGrid2D, BitNotClearsPadding) {
    // 3x3: row_stride=1 byte; bits [3..7] in each row are padding
    bit_grid<2> g({3, 3});
    auto n = ~g;               // complement of empty = full
    EXPECT_EQ(n.count(), 9u);  // 3 set bits per row * 3 rows
}

// ============================================================
// Self-operations
// ============================================================
TEST(BitGrid1D, SelfAnd) {
    bit_grid<1> g({8});
    g.set(pos1(2));
    g.set(pos1(5));
    auto c = g & g;
    EXPECT_EQ(c, g);
}

TEST(BitGrid1D, SelfOr) {
    bit_grid<1> g({8});
    g.set(pos1(2));
    g.set(pos1(5));
    auto c = g | g;
    EXPECT_EQ(c, g);
}

TEST(BitGrid1D, SelfXor) {
    bit_grid<1> g({8});
    g.set(pos1(2));
    g.set(pos1(5));
    auto c = g ^ g;
    EXPECT_TRUE(c.is_clear());
}

// Main is provided by gtest_main linkage

// ============================================================
// Cross-dimension operations (the main focus of the fix)
// ============================================================

// 1D: a={8}, b={4} — different sizes, same row_stride (1 byte each)
TEST(BitGridCrossDim, And1D_ResultIsMinDims) {
    bit_grid<1> a({8}), b({4});
    a.set(pos1(0));
    a.set(pos1(3));
    a.set(pos1(5));
    a.set(pos1(7));
    b.set(pos1(0));
    b.set(pos1(2));
    b.set(pos1(3));
    auto c = a & b;
    // result dims = min(8,4) = 4
    EXPECT_EQ(c.dimensions(), (std::array<std::uint32_t, 1>{4}));
    // intersection in [0,3]: pos 0 and 3 are in both
    EXPECT_TRUE(c.contains(pos1(0)));
    EXPECT_FALSE(c.contains(pos1(2)));  // in b, not in a at that bit... wait a has pos1(0),3,5,7; b has 0,2,3
    EXPECT_TRUE(c.contains(pos1(3)));
    EXPECT_EQ(c.count(), 2u);
}

// 1D: a={4}, b={8} — a is smaller
TEST(BitGridCrossDim, And1D_SmallAndBig) {
    bit_grid<1> a({4}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    b.set(pos1(1));
    b.set(pos1(5));
    auto c = a & b;
    EXPECT_EQ(c.dimensions(), (std::array<std::uint32_t, 1>{4}));
    EXPECT_TRUE(c.contains(pos1(1)));
    EXPECT_FALSE(c.contains(pos1(3)));  // in a, not in b
    EXPECT_EQ(c.count(), 1u);
}

TEST(BitGridCrossDim, Or1D_ResultIsMaxDims) {
    bit_grid<1> a({4}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    b.set(pos1(5));
    b.set(pos1(7));
    auto c = a | b;
    EXPECT_EQ(c.dimensions(), (std::array<std::uint32_t, 1>{8}));
    EXPECT_TRUE(c.contains(pos1(1)));
    EXPECT_TRUE(c.contains(pos1(3)));
    EXPECT_TRUE(c.contains(pos1(5)));
    EXPECT_TRUE(c.contains(pos1(7)));
    EXPECT_EQ(c.count(), 4u);
}

TEST(BitGridCrossDim, Xor1D_ResultIsMaxDims) {
    bit_grid<1> a({4}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    b.set(pos1(1));
    b.set(pos1(5));
    auto c = a ^ b;
    // {1} is in both -> xored out; {3} only in a (pos 3 < 4, not in b); {5} only in b
    EXPECT_EQ(c.dimensions(), (std::array<std::uint32_t, 1>{8}));
    EXPECT_FALSE(c.contains(pos1(1)));
    EXPECT_TRUE(c.contains(pos1(3)));
    EXPECT_TRUE(c.contains(pos1(5)));
    EXPECT_EQ(c.count(), 2u);
}

TEST(BitGridCrossDim, IntersectCount_CrossDim) {
    bit_grid<1> a({8}), b({4});
    a.set(pos1(0));
    a.set(pos1(2));
    a.set(pos1(6));
    b.set(pos1(0));
    b.set(pos1(2));
    b.set(pos1(3));
    // intersection: pos 0 and 2 are in both (pos 6 is in a but outside b's domain)
    EXPECT_EQ(a.intersect_count(b), 2u);
}

TEST(BitGridCrossDim, UnionCount_CrossDim) {
    bit_grid<1> a({4}), b({8});
    a.set(pos1(0));
    a.set(pos1(2));
    b.set(pos1(4));
    b.set(pos1(6));
    // union: {0, 2, 4, 6}
    EXPECT_EQ(a.union_count(b), 4u);
}

TEST(BitGridCrossDim, DifferenceCount_CrossDim) {
    bit_grid<1> a({8}), b({4});
    a.set(pos1(0));
    a.set(pos1(2));
    a.set(pos1(6));
    b.set(pos1(0));
    b.set(pos1(1));
    // difference = bits in a not in b: pos 2 (b doesn't have it), pos 6 (outside b's domain)
    EXPECT_EQ(a.difference_count(b), 2u);
}

TEST(BitGridCrossDim, SymDiffCount_CrossDim) {
    bit_grid<1> a({4}), b({8});
    a.set(pos1(0));
    a.set(pos1(2));
    b.set(pos1(0));
    b.set(pos1(5));
    // sym diff: pos 2 (only in a) + pos 5 (only in b) = 2
    EXPECT_EQ(a.symmetric_difference_count(b), 2u);
}

TEST(BitGridCrossDim, IsSubset_CrossDim) {
    bit_grid<1> a({4}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    b.set(pos1(1));
    b.set(pos1(3));
    b.set(pos1(5));
    EXPECT_TRUE(a.is_subset(b));
    EXPECT_FALSE(b.is_subset(a));  // b has bit 5 outside a's domain
}

TEST(BitGridCrossDim, IsSubset_WithBitOutsideOther) {
    bit_grid<1> a({8}), b({4});
    a.set(pos1(1));
    a.set(pos1(6));  // bit 6 is outside b's domain
    b.set(pos1(1));
    // a is NOT a subset of b because pos1(6) is not in b
    EXPECT_FALSE(a.is_subset(b));
}

TEST(BitGridCrossDim, IsDisjoint_CrossDim) {
    bit_grid<1> a({4}), b({8});
    a.set(pos1(1));
    b.set(pos1(5));
    b.set(pos1(6));
    EXPECT_TRUE(a.is_disjoint(b));
    b.set(pos1(1));
    EXPECT_FALSE(a.is_disjoint(b));
}

TEST(BitGridCrossDim, InPlaceAnd_ClearsBitsOutsideOther) {
    bit_grid<1> a({8}), b({4});
    a.set(pos1(1));
    a.set(pos1(3));
    a.set(pos1(6));
    b.set(pos1(1));
    a &= b;
    // bits 3 and 6 should be cleared; bit 1 remains
    EXPECT_TRUE(a.contains(pos1(1)));
    EXPECT_FALSE(a.contains(pos1(3)));
    EXPECT_FALSE(a.contains(pos1(6)));
    EXPECT_EQ(a.count(), 1u);
}

TEST(BitGridCrossDim, InPlaceOr_SetsOnlyWithinDomain) {
    bit_grid<1> a({4}), b({8});
    a.set(pos1(0));
    b.set(pos1(2));
    b.set(pos1(6));  // pos1(6) is outside a's domain
    a |= b;
    EXPECT_TRUE(a.contains(pos1(0)));
    EXPECT_TRUE(a.contains(pos1(2)));
    EXPECT_FALSE(a.contains(pos1(6)));  // outside a, not set
    EXPECT_EQ(a.count(), 2u);
    EXPECT_EQ(a.dimensions(), (std::array<std::uint32_t, 1>{4}));  // dims unchanged
}

TEST(BitGridCrossDim, SetUnionView_IncludesPositionsBeyondThisDomain) {
    bit_grid<1> a({4}), b({8});
    a.set(pos1(1));
    b.set(pos1(5));
    std::vector<std::array<std::uint32_t, 1>> result;
    for (auto p : a.set_union(b)) result.push_back(p);
    // should include pos1(1) from a AND pos1(5) from b (outside a's original domain)
    auto has = [&](auto p) { return std::find(result.begin(), result.end(), p) != result.end(); };
    EXPECT_TRUE(has(pos1(1)));
    EXPECT_TRUE(has(pos1(5)));
    EXPECT_EQ(result.size(), 2u);
}

TEST(BitGridCrossDim, SymDiffView_IncludesPositionsBeyondThisDomain) {
    bit_grid<1> a({4}), b({8});
    a.set(pos1(1));
    a.set(pos1(3));
    b.set(pos1(1));
    b.set(pos1(6));  // pos 1 shared, pos 3 only in a, pos 6 only in b
    std::vector<std::array<std::uint32_t, 1>> result;
    for (auto p : a.symmetric_difference(b)) result.push_back(p);
    std::sort(result.begin(), result.end());
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], pos1(3));
    EXPECT_EQ(result[1], pos1(6));
}

// 2D cross-dim: different dim[0] means different row_stride — verify word-level ops cannot be used
TEST(BitGridCrossDim, And2D_DifferentRowStride) {
    // a: dim={9, 2} row_stride=2, b: dim={4, 3} row_stride=1
    // min_dims = {4, 2}
    bit_grid<2> a({9, 2}), b({4, 3});
    a.set(pos2(0, 0));
    a.set(pos2(3, 1));
    a.set(pos2(8, 0));
    b.set(pos2(0, 0));
    b.set(pos2(3, 1));
    b.set(pos2(1, 2));
    auto c = a & b;
    EXPECT_EQ(c.dimensions(), (std::array<std::uint32_t, 2>{4, 2}));
    // pos (0,0) and (3,1) are in both, within min_dims
    EXPECT_TRUE(c.contains(pos2(0, 0)));
    EXPECT_TRUE(c.contains(pos2(3, 1)));
    // pos (8,0) is outside min_dims[0]=4
    EXPECT_FALSE(c.contains(pos2(1, 2)));  // outside min_dims[1]=2
    EXPECT_EQ(c.count(), 2u);
}

TEST(BitGridCrossDim, Or2D_DifferentRowStride) {
    // a: dim={4, 2}, b: dim={9, 3}; max_dims = {9, 3}
    bit_grid<2> a({4, 2}), b({9, 3});
    a.set(pos2(1, 0));
    b.set(pos2(8, 2));
    auto c = a | b;
    EXPECT_EQ(c.dimensions(), (std::array<std::uint32_t, 2>{9, 3}));
    EXPECT_TRUE(c.contains(pos2(1, 0)));
    EXPECT_TRUE(c.contains(pos2(8, 2)));
    EXPECT_EQ(c.count(), 2u);
}
