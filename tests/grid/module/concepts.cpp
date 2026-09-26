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
// viewable_grid — all basic grid types must satisfy this
// ────────────────────────────────────────────────────────────

static_assert(viewable_grid<packed_grid<2, int>>);
static_assert(viewable_grid<dense_grid<2, int>>);
static_assert(viewable_grid<sparse_grid<2, int>>);
static_assert(viewable_grid<dense_extendible_grid<2, int>>);
static_assert(viewable_grid<tree_extendible_grid<2, int>>);
static_assert(viewable_grid<tree_grid<2, int>>);
static_assert(viewable_grid<bit_grid<2>>);

static_assert(viewable_grid<packed_grid<3, float>>);
static_assert(viewable_grid<dense_grid<3, double>>);
static_assert(viewable_grid<tree_extendible_grid<3, int>>);
static_assert(viewable_grid<tree_grid<3, int>>);

// ────────────────────────────────────────────────────────────
// unsafe_grid_container — all basic grid types must satisfy this
// ────────────────────────────────────────────────────────────

static_assert(unsafe_grid_container<packed_grid<2, int>>);
static_assert(unsafe_grid_container<dense_grid<2, int>>);
static_assert(unsafe_grid_container<sparse_grid<2, int>>);
static_assert(unsafe_grid_container<dense_extendible_grid<2, int>>);
static_assert(unsafe_grid_container<tree_extendible_grid<2, int>>);
static_assert(unsafe_grid_container<tree_grid<2, int>>);

// ────────────────────────────────────────────────────────────
// grid_container — all basic grid types must satisfy this
// (packed_grid::set_new always returns AlreadyOccupied but the method exists)
// ────────────────────────────────────────────────────────────

static_assert(grid_container<packed_grid<2, int>>);
static_assert(grid_container<dense_grid<2, int>>);
static_assert(grid_container<sparse_grid<2, int>>);
static_assert(grid_container<dense_extendible_grid<2, int>>);
static_assert(grid_container<tree_extendible_grid<2, int>>);
static_assert(grid_container<tree_grid<2, int>>);

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

// ────────────────────────────────────────────────────────────
// filter_view & shadow_view — satisfy viewable_grid and unsafe_viewable_grid
// ────────────────────────────────────────────────────────────

using fv_t = epix::ext::grid::views::filter_view<dense_grid<2, int>, decltype([](const int& v) { return v > 0; })>;
static_assert(viewable_grid<fv_t>);
static_assert(unsafe_viewable_grid<fv_t>);
static_assert(iterable_grid<fv_t>);

using sv_t = epix::ext::grid::views::shadow_view<dense_grid<2, int>,
                                                 decltype([](const std::array<std::uint32_t, 2>&) { return true; })>;
static_assert(viewable_grid<sv_t>);
static_assert(unsafe_viewable_grid<sv_t>);

// ────────────────────────────────────────────────────────────
// any_grid / any_grid_view — must satisfy viewable + iterable
// ────────────────────────────────────────────────────────────

static_assert(viewable_grid<any_grid<2, int>>);
static_assert(iterable_grid<any_grid<2, int>>);
static_assert(viewable_grid<any_grid_view<2, int&>>);
static_assert(viewable_grid<any_grid_view<2, int&, grid_category::iterable>>);
static_assert(iterable_grid<any_grid_view<2, int&, grid_category::iterable>>);

// transform_view with value-returning get() must satisfy viewable_grid
static_assert(viewable_grid<
              decltype(views::transform(std::declval<dense_grid<2, int>&>(),
                                        std::declval<const decltype([](const int& v) -> int { return v * 2; })&>()))>);

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
}

TEST(GridTrait, ExtendibleGridHasCorrectTraits) {
    grid_trait<dense_extendible_grid<2, int>> t;
    static_assert(decltype(t)::dim == 2);
}

// ────────────────────────────────────────────────────────────
// recursive_grid tests
//
// Nested grid types for testing.  Only dense_extendible_grid
// and tree_extendible_grid are default-constructible, so they
// are the only grid types that can appear as the cell_type of
// packed_grid or dense_grid (which require std::constructible_from<T>).
// tree_grid, sparse_grid, dense_extendible_grid, and tree_extendible_grid
// only require std::movable<T>, so they accept any grid as cell_type.
// ────────────────────────────────────────────────────────────

// Depth = 0: only checks that G is viewable_grid (base case)
static_assert(recursive_grid<packed_grid<2, int>, 0>);
static_assert(recursive_grid<dense_grid<2, int>, 0>);
static_assert(recursive_grid<sparse_grid<2, int>, 0>);
static_assert(recursive_grid<dense_extendible_grid<2, int>, 0>);
static_assert(recursive_grid<tree_extendible_grid<2, int>, 0>);
static_assert(recursive_grid<tree_grid<2, int>, 0>);

// Depth = 0 still requires viewable_grid, so non-grid types fail
static_assert(!recursive_grid<int, 0>);
static_assert(!recursive_grid<float, 0>);
static_assert(!recursive_grid<std::array<int, 3>, 0>);

// Depth = 1 (default): G must be viewable_grid AND G::cell_type must be viewable_grid
// Single-level grids (cell_type is a non-grid like int/float) should FAIL
static_assert(!recursive_grid<packed_grid<2, int>>);
static_assert(!recursive_grid<dense_grid<2, int>>);
static_assert(!recursive_grid<sparse_grid<2, int>>);
static_assert(!recursive_grid<dense_extendible_grid<2, int>>);
static_assert(!recursive_grid<tree_extendible_grid<2, int>>);
static_assert(!recursive_grid<tree_grid<2, int>>);

// Depth = 1: nested grids using default-constructible inner types
static_assert(recursive_grid<dense_extendible_grid<2, dense_extendible_grid<2, int>>>);
static_assert(recursive_grid<tree_extendible_grid<2, tree_extendible_grid<2, int>>>);

// Depth = 1: tree_grid and sparse_grid accept any movable grid as cell_type
static_assert(recursive_grid<tree_grid<2, tree_grid<2, int>>>);
static_assert(recursive_grid<tree_grid<2, dense_extendible_grid<2, int>>>);
static_assert(recursive_grid<tree_grid<2, tree_extendible_grid<2, int>>>);

// Depth = 1: packed_grid / dense_grid can hold default-constructible grids
static_assert(recursive_grid<packed_grid<2, dense_extendible_grid<2, int>>>);
static_assert(recursive_grid<dense_grid<2, dense_extendible_grid<2, int>>>);
static_assert(recursive_grid<packed_grid<2, tree_extendible_grid<2, int>>>);

// Depth = 1 explicit: same as default
static_assert(recursive_grid<tree_extendible_grid<2, tree_extendible_grid<2, int>>, 1>);
static_assert(!recursive_grid<tree_extendible_grid<2, int>, 1>);

// Depth = 2: triple nesting with default-constructible grids
static_assert(recursive_grid<dense_extendible_grid<2, dense_extendible_grid<2, dense_extendible_grid<2, int>>>, 2>);
static_assert(recursive_grid<tree_extendible_grid<2, tree_extendible_grid<2, tree_extendible_grid<2, int>>>, 2>);

// Depth = 2: tree_grid triple nesting (only needs movable)
static_assert(recursive_grid<tree_grid<2, tree_grid<2, tree_grid<2, int>>>, 2>);

// Depth = 2: should FAIL when only one level of nesting exists
static_assert(!recursive_grid<tree_extendible_grid<2, tree_extendible_grid<2, int>>, 2>);
static_assert(!recursive_grid<tree_grid<2, tree_grid<2, int>>, 2>);

// Depth = 2: should PASS (depth is sufficient but not exceeded)
static_assert(recursive_grid<tree_extendible_grid<2, tree_extendible_grid<2, tree_extendible_grid<2, int>>>, 1>);

// Heterogeneous mixing — packed_grid holding d_e_g, holding tree_grid
static_assert(recursive_grid<packed_grid<2, dense_extendible_grid<2, tree_grid<2, int>>>, 2>);

// ────────────────────────────────────────────────────────────
// Runtime recursive_grid verification
// ────────────────────────────────────────────────────────────

TEST(RecursiveGrid, DepthZeroEqualsViewableGrid) {
    // Depth 0: any viewable_grid satisfies it
    EXPECT_TRUE((recursive_grid<packed_grid<2, int>, 0>));
    EXPECT_TRUE((recursive_grid<tree_grid<2, int>, 0>));
    EXPECT_FALSE((recursive_grid<int, 0>));
}

TEST(RecursiveGrid, SingleLevelGridFailsDepthOne) {
    // A plain grid whose cell_type is int is NOT recursive at depth >= 1
    EXPECT_FALSE((recursive_grid<packed_grid<2, int>>));
    EXPECT_FALSE((recursive_grid<dense_grid<2, float>>));
    EXPECT_FALSE((recursive_grid<tree_grid<2, double>>));
}

TEST(RecursiveGrid, NestedGridPassesDepthOne) {
    // A grid whose cell_type is itself a viewable_grid passes depth 1
    EXPECT_TRUE((recursive_grid<tree_extendible_grid<2, tree_extendible_grid<2, int>>>));
    EXPECT_TRUE((recursive_grid<tree_grid<2, dense_extendible_grid<2, int>>>));
}

TEST(RecursiveGrid, DoubleNestingPassesDepthTwo) {
    EXPECT_TRUE((recursive_grid<tree_extendible_grid<2, tree_extendible_grid<2, tree_extendible_grid<2, int>>>, 2>));
    EXPECT_TRUE((recursive_grid<tree_grid<2, tree_grid<2, tree_grid<2, int>>>, 2>));
}

TEST(RecursiveGrid, InsufficientNestingFailsDeeperDepth) {
    // Only 1 nesting level but depth=2 requested → fails
    EXPECT_FALSE((recursive_grid<tree_extendible_grid<2, tree_extendible_grid<2, int>>, 2>));
    // Only 2 nesting levels but depth=3 requested → fails
    EXPECT_FALSE((recursive_grid<tree_extendible_grid<2, tree_extendible_grid<2, tree_extendible_grid<2, int>>>, 3>));
}
