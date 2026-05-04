#include <gtest/gtest.h>
#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstdint>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.extension.grid;
using namespace epix::ext::grid;

// ────────────────────────────────────────────────────────────
// Compile-time concept verification — each grid type is
// checked against the concepts it should satisfy.
// bit_grid is intentionally NOT an any_grid / basic_grid
// and is excluded from these tests.
// ────────────────────────────────────────────────────────────

// ────────────────────────────────────────────────────────────
// any_grid — all basic grid types must satisfy this
// ────────────────────────────────────────────────────────────

static_assert(any_grid<packed_grid<2, int>>);
static_assert(any_grid<dense_grid<2, int>>);
static_assert(any_grid<sparse_grid<2, int>>);
static_assert(any_grid<dense_extendible_grid<2, int>>);
static_assert(any_grid<tree_extendible_grid<2, int>>);
static_assert(any_grid<tree_grid<2, int>>);

static_assert(any_grid<packed_grid<3, float>>);
static_assert(any_grid<dense_grid<3, double>>);
static_assert(any_grid<tree_extendible_grid<3, int>>);
static_assert(any_grid<tree_grid<3, int>>);

// ────────────────────────────────────────────────────────────
// unsafe_grid
// ────────────────────────────────────────────────────────────

static_assert(unsafe_grid<packed_grid<2, int>>);
static_assert(unsafe_grid<dense_grid<2, int>>);
static_assert(unsafe_grid<sparse_grid<2, int>>);
static_assert(unsafe_grid<dense_extendible_grid<2, int>>);
static_assert(unsafe_grid<tree_extendible_grid<2, int>>);
static_assert(unsafe_grid<tree_grid<2, int>>);

// ────────────────────────────────────────────────────────────
// new_settable_grid
// ────────────────────────────────────────────────────────────

static_assert(!new_settable_grid<packed_grid<2, int>>);
static_assert(new_settable_grid<dense_grid<2, int>>);
static_assert(new_settable_grid<sparse_grid<2, int>>);
static_assert(new_settable_grid<dense_extendible_grid<2, int>>);
static_assert(new_settable_grid<tree_extendible_grid<2, int>>);
static_assert(new_settable_grid<tree_grid<2, int>>);

// ────────────────────────────────────────────────────────────
// iterable_grid
// ────────────────────────────────────────────────────────────

static_assert(iterable_grid<packed_grid<2, int>>);
static_assert(iterable_grid<dense_grid<2, int>>);
static_assert(iterable_grid<sparse_grid<2, int>>);
static_assert(iterable_grid<dense_extendible_grid<2, int>>);
static_assert(iterable_grid<tree_extendible_grid<2, int>>);
static_assert(iterable_grid<tree_grid<2, int>>);

// ────────────────────────────────────────────────────────────
// basic_grid = any_grid + iterable_grid
// ────────────────────────────────────────────────────────────

static_assert(basic_grid<packed_grid<2, int>>);
static_assert(basic_grid<dense_grid<2, int>>);
static_assert(basic_grid<sparse_grid<2, int>>);
static_assert(basic_grid<dense_extendible_grid<2, int>>);
static_assert(basic_grid<tree_extendible_grid<2, int>>);
static_assert(basic_grid<tree_grid<2, int>>);

// basic_extendible_grid
static_assert(!basic_extendible_grid<packed_grid<2, int>>);
static_assert(!basic_extendible_grid<dense_grid<2, int>>);
static_assert(basic_extendible_grid<dense_extendible_grid<2, int>>);
static_assert(basic_extendible_grid<tree_extendible_grid<2, int>>);
static_assert(!basic_extendible_grid<tree_grid<2, int>>);

// ────────────────────────────────────────────────────────────
// maybe_fixed_grid / maybe_extendible_grid
// ────────────────────────────────────────────────────────────

static_assert(maybe_fixed_grid<packed_grid<2, int>>);
static_assert(maybe_fixed_grid<dense_grid<2, int>>);
static_assert(maybe_fixed_grid<sparse_grid<2, int>>);
static_assert(maybe_fixed_grid<tree_grid<2, int>>);

static_assert(!maybe_fixed_grid<dense_extendible_grid<2, int>>);
static_assert(!maybe_fixed_grid<tree_extendible_grid<2, int>>);

static_assert(!maybe_extendible_grid<packed_grid<2, int>>);
static_assert(!maybe_extendible_grid<dense_grid<2, int>>);
static_assert(maybe_extendible_grid<dense_extendible_grid<2, int>>);
static_assert(maybe_extendible_grid<tree_extendible_grid<2, int>>);

// ────────────────────────────────────────────────────────────
// extendible_grid (= maybe_extendible_grid, implicit extension)
// ────────────────────────────────────────────────────────────

static_assert(!extendible_grid<packed_grid<2, int>>);
static_assert(!extendible_grid<dense_grid<2, int>>);
static_assert(!extendible_grid<sparse_grid<2, int>>);
static_assert(extendible_grid<dense_extendible_grid<2, int>>);
static_assert(extendible_grid<tree_extendible_grid<2, int>>);
static_assert(!extendible_grid<tree_grid<2, int>>);

// ────────────────────────────────────────────────────────────
// tree_based_grid
// ────────────────────────────────────────────────────────────

static_assert(!tree_based_grid<packed_grid<2, int>>);
static_assert(!tree_based_grid<dense_grid<2, int>>);
static_assert(!tree_based_grid<sparse_grid<2, int>>);
static_assert(tree_based_grid<tree_extendible_grid<2, int>>);
static_assert(tree_based_grid<tree_grid<2, int>>);

// ────────────────────────────────────────────────────────────
// filter_view & shadow_view — satisfy any_grid unconditionally
// ────────────────────────────────────────────────────────────

using fv_t = filter_view<dense_grid<2, int>, decltype([](const int& v) { return v > 0; })>;
static_assert(any_grid<fv_t>);
static_assert(unsafe_grid<fv_t>);
static_assert(iterable_grid<fv_t>);

using sv_t = shadow_view<dense_grid<2, int>, decltype([](const std::array<std::uint32_t, 2>&) { return true; })>;
static_assert(any_grid<sv_t>);
static_assert(unsafe_grid<sv_t>);

// ────────────────────────────────────────────────────────────
// Runtime: dimensions() returns unsigned array for all grids
// ────────────────────────────────────────────────────────────

TEST(ConceptDimensions, ExtendibleGridDimensionsIsUnsigned) {
    dense_extendible_grid<2, int> g;
    auto dims = g.dimensions();
    static_assert(std::unsigned_integral<decltype(dims)::value_type>);
    EXPECT_EQ(dims[0], 1u);
    EXPECT_EQ(dims[1], 1u);
}

TEST(ConceptDimensions, FixedGridDimensionsMatchesPosType) {
    packed_grid<2, int> g({4, 5}, 0);
    auto dims = g.dimensions();
    static_assert(std::same_as<decltype(dims), std::array<std::uint32_t, 2>>);
    EXPECT_EQ(dims[0], 4u);
    EXPECT_EQ(dims[1], 5u);
}

// ────────────────────────────────────────────────────────────
// grid_trait tests
// ────────────────────────────────────────────────────────────

TEST(GridTrait, FixedGridHasCorrectDim) {
    grid_trait<packed_grid<3, float>> t;
    static_assert(decltype(t)::dim == 3);
    EXPECT_FALSE(decltype(t)::is_extendible);
    EXPECT_FALSE(decltype(t)::has_coverage);
}

TEST(GridTrait, ExtendibleGridHasCorrectTraits) {
    grid_trait<dense_extendible_grid<2, int>> t;
    static_assert(decltype(t)::dim == 2);
    EXPECT_TRUE(decltype(t)::is_extendible);
    EXPECT_FALSE(decltype(t)::has_coverage);  // dense_extendible is not tree-based
}

TEST(GridTrait, TreeGridHasCoverage) {
    grid_trait<tree_grid<2, int>> t;
    static_assert(decltype(t)::dim == 2);
    EXPECT_FALSE(decltype(t)::is_extendible);
    EXPECT_TRUE(decltype(t)::has_coverage);
}
