#include <gtest/gtest.h>
#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstdint>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.extension.grid;
using namespace epix::ext::grid;

// ────────────────────────────────────────────────────────────
// rasterise tests
// ────────────────────────────────────────────────────────────

TEST(PolygonRasterise, FromBitGrid2D) {
    bit_grid<2> src({4, 4});
    src.set({0, 0});
    src.set({1, 1});
    src.set({2, 2});

    auto bg = rasterise(src);
    EXPECT_EQ(bg.dimensions(), (std::array<std::uint32_t, 2>{4, 4}));
    EXPECT_TRUE(bg.contains({0, 0}));
    EXPECT_TRUE(bg.contains({1, 1}));
    EXPECT_TRUE(bg.contains({2, 2}));
    EXPECT_FALSE(bg.contains({3, 3}));
}

TEST(PolygonRasterise, FromDenseGrid2D) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 1);
    g.set({1, 1}, 2);
    g.set({2, 2}, 3);

    auto bg = rasterise(g);
    EXPECT_TRUE(bg.contains({0, 0}));
    EXPECT_TRUE(bg.contains({1, 1}));
    EXPECT_TRUE(bg.contains({2, 2}));
    EXPECT_FALSE(bg.contains({3, 3}));
}

TEST(PolygonRasterise, EmptyGridProducesEmptyBinaryGrid) {
    bit_grid<2> src({2, 2});
    auto bg = rasterise(src);
    EXPECT_TRUE(bg.is_clear());
    EXPECT_EQ(bg.count(), 0u);
}

// ────────────────────────────────────────────────────────────
// find_outline tests
// ────────────────────────────────────────────────────────────

TEST(PolygonOutline, SingleCell) {
    bit_grid<2> g({4, 4});
    g.set({1, 1});
    auto ring = find_outline(g);
    EXPECT_FALSE(ring.empty());
    EXPECT_GE(ring.size(), 4u);
}

TEST(PolygonOutline, EmptyGridReturnsEmptyRing) {
    bit_grid<2> g({4, 4});
    auto ring = find_outline(g);
    EXPECT_TRUE(ring.empty());
}

TEST(PolygonOutline, SolidRectangle) {
    bit_grid<2> g({4, 4});
    for (std::uint32_t y = 1; y < 3; ++y)
        for (std::uint32_t x = 1; x < 3; ++x) g.set({x, y});

    auto ring = find_outline(g);
    EXPECT_FALSE(ring.empty());
    // A 2x2 solid block should have an outline
    EXPECT_GE(ring.size(), 4u);
}

// ────────────────────────────────────────────────────────────
// find_holes tests
// ────────────────────────────────────────────────────────────

TEST(PolygonHoles, SolidRectangleNoHoles) {
    bit_grid<2> g({6, 6});
    for (std::uint32_t y = 1; y < 5; ++y)
        for (std::uint32_t x = 1; x < 5; ++x) g.set({x, y});

    auto holes = find_holes(g);
    EXPECT_TRUE(holes.empty());
}

TEST(PolygonHoles, RingHasHole) {
    bit_grid<2> g({6, 6});
    // outer ring
    for (std::uint32_t x = 1; x < 5; ++x) g.set({x, 1});
    for (std::uint32_t x = 1; x < 5; ++x) g.set({x, 4});
    for (std::uint32_t y = 1; y < 5; ++y) g.set({1, y});
    for (std::uint32_t y = 1; y < 5; ++y) g.set({4, y});

    auto holes = find_holes(g);
    EXPECT_FALSE(holes.empty());
}

// ────────────────────────────────────────────────────────────
// get_polygon tests
// ────────────────────────────────────────────────────────────

TEST(PolygonExtraction, SingleComponent) {
    bit_grid<2> g({4, 4});
    for (std::uint32_t y = 1; y < 3; ++y)
        for (std::uint32_t x = 1; x < 3; ++x) g.set({x, y});

    auto poly = get_polygon(g);
    EXPECT_FALSE(poly.empty());
    EXPECT_FALSE(poly.outer.empty());
}

TEST(PolygonExtraction, EmptyGrid) {
    bit_grid<2> g({4, 4});
    auto poly = get_polygon(g);
    EXPECT_TRUE(poly.empty());
}

TEST(PolygonExtraction, Simplified) {
    bit_grid<2> g({4, 4});
    g.set({1, 1});
    g.set({2, 1});
    g.set({1, 2});
    g.set({2, 2});

    auto poly = get_polygon_simplified(g);
    EXPECT_FALSE(poly.empty());
}

// ────────────────────────────────────────────────────────────
// get_polygons_multi tests
// ────────────────────────────────────────────────────────────

TEST(PolygonMulti, TwoSeparateComponents) {
    bit_grid<2> g({8, 4});
    // component 1: left 2x2
    g.set({0, 0});
    g.set({1, 0});
    g.set({0, 1});
    g.set({1, 1});
    // component 2: right 2x2
    g.set({5, 1});
    g.set({6, 1});
    g.set({5, 2});
    g.set({6, 2});

    auto polys = get_polygons_multi(g);
    EXPECT_EQ(polys.size(), 2u);
    for (auto& p : polys) EXPECT_FALSE(p.empty());
}

TEST(PolygonMulti, EmptyGridReturnsEmpty) {
    bit_grid<2> g({4, 4});
    auto polys = get_polygons_multi(g);
    EXPECT_TRUE(polys.empty());
}

// ────────────────────────────────────────────────────────────
// Polygon extraction via filter_view
// ────────────────────────────────────────────────────────────

TEST(PolygonViaView, FilterViewWithPredicate) {
    dense_grid<2, int> g({4, 4});
    g.set({1, 1}, 5);
    g.set({2, 1}, 5);
    g.set({1, 2}, 0);  // filtered out
    g.set({2, 2}, 5);

    auto fv   = filter(g, [](const int& v) { return v > 0; });
    auto ring = find_outline(fv);
    EXPECT_FALSE(ring.empty());
}

TEST(PolygonViaView, ShadowViewWithPredicate) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 1);
    g.set({1, 1}, 2);
    g.set({2, 2}, 3);

    auto sv   = shadow(g, [](const std::array<std::uint32_t, 2>& p) { return p[0] < 2 && p[1] < 2; });
    auto ring = find_outline(sv);
    EXPECT_FALSE(ring.empty());
}
