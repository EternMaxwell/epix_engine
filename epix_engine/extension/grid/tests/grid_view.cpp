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
// filter_view tests
// ────────────────────────────────────────────────────────────

TEST(FilterView, ContainsOnlyWhenPredTrue) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 1);
    g.set({1, 1}, -5);
    g.set({2, 2}, 3);

    auto fv = filter(g, [](const int& v) { return v > 0; });
    EXPECT_TRUE(fv.contains({0, 0}));
    EXPECT_FALSE(fv.contains({1, 1}));
    EXPECT_TRUE(fv.contains({2, 2}));
    EXPECT_FALSE(fv.contains({3, 3}));
}

TEST(FilterView, GetReturnsEmptyCellWhenPredFalse) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, -1);
    auto fv = filter(g, [](const int& v) { return v > 0; });
    EXPECT_FALSE(fv.contains({0, 0}));
    EXPECT_EQ(fv.get({0, 0}).error(), grid_error::EmptyCell);
}

TEST(FilterView, GetReturnsValueWhenPredTrue) {
    dense_grid<2, int> g({4, 4});
    g.set({1, 1}, 42);
    auto fv = filter(g, [](const int& v) { return v > 0; });
    auto r  = fv.get({1, 1});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->get(), 42);
}

TEST(FilterView, GetMutGatedByPred) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 10);
    g.set({1, 1}, -10);
    auto fv = filter(g, [](const int& v) { return v > 0; });
    EXPECT_TRUE(fv.get_mut({0, 0}).has_value());
    EXPECT_EQ(fv.get_mut({1, 1}).error(), grid_error::EmptyCell);
}

TEST(FilterView, SetAlwaysDelegates) {
    dense_grid<2, int> g({4, 4});
    auto fv = filter(g, [](const int& v) { return true; });
    EXPECT_TRUE(fv.set({0, 0}, 99).has_value());
    EXPECT_EQ(g.get({0, 0})->get(), 99);
}

TEST(FilterView, RemoveAlwaysDelegates) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 1);
    auto fv = filter(g, [](const int& v) { return true; });
    EXPECT_TRUE(fv.remove({0, 0}).has_value());
    EXPECT_FALSE(g.contains({0, 0}));
}

TEST(FilterView, TakeGatedByPred) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 10);
    g.set({1, 1}, -5);
    auto fv = filter(g, [](const int& v) { return v > 0; });
    auto r  = fv.take({0, 0});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 10);
    EXPECT_FALSE(g.contains({0, 0}));
    EXPECT_EQ(fv.take({1, 1}).error(), grid_error::EmptyCell);
}

TEST(FilterView, DimensionsDelegates) {
    dense_grid<2, int> g({5, 7});
    auto fv   = filter(g, [](const int& v) { return true; });
    auto dims = fv.dimensions();
    EXPECT_EQ(dims[0], 5u);
    EXPECT_EQ(dims[1], 7u);
}

TEST(FilterView, UnsafeAccessorsDelegate) {
    dense_grid<2, int> g({4, 4});
    auto fv = filter(g, [](const int& v) { return true; });
    EXPECT_EQ(fv.set_unsafe({0, 0}, 42), 42);
    EXPECT_EQ(fv.get_unsafe({0, 0}), 42);
    fv.get_mut_unsafe({0, 0}) = 99;
    EXPECT_EQ(fv.get_unsafe({0, 0}), 99);
}

// ────────────────────────────────────────────────────────────
// shadow_view tests
// ────────────────────────────────────────────────────────────

TEST(ShadowView, ContainsOnlyWhenPredTrue) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 1);
    g.set({1, 1}, 2);
    g.set({2, 2}, 3);

    auto sv = shadow(g, [](const std::array<std::uint32_t, 2>& p) {
        return p[0] == 0 && p[1] == 0;  // only {0,0}
    });
    EXPECT_TRUE(sv.contains({0, 0}));
    EXPECT_FALSE(sv.contains({2, 2}));
    EXPECT_FALSE(sv.contains({1, 1}));
    EXPECT_FALSE(sv.contains({0, 1}));
}

TEST(ShadowView, GetReturnsEmptyCellWhenPosPredFalse) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 42);
    auto sv = shadow(g, [](const std::array<std::uint32_t, 2>&) { return false; });
    EXPECT_FALSE(sv.contains({0, 0}));
    EXPECT_EQ(sv.get({0, 0}).error(), grid_error::EmptyCell);
}

TEST(ShadowView, GetMutGatedByPosPred) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 10);
    g.set({1, 1}, 20);
    auto sv = shadow(g, [](const std::array<std::uint32_t, 2>& p) { return p[0] == 0; });
    EXPECT_TRUE(sv.get_mut({0, 0}).has_value());
    EXPECT_EQ(sv.get_mut({1, 1}).error(), grid_error::EmptyCell);
}

TEST(ShadowView, SetAlwaysDelegates) {
    dense_grid<2, int> g({4, 4});
    auto sv = shadow(g, [](const std::array<std::uint32_t, 2>&) { return false; });
    EXPECT_TRUE(sv.set({0, 0}, 99).has_value());
    EXPECT_EQ(g.get({0, 0})->get(), 99);
}

TEST(ShadowView, RemoveAlwaysDelegates) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 1);
    auto sv = shadow(g, [](const std::array<std::uint32_t, 2>&) { return true; });
    EXPECT_TRUE(sv.remove({0, 0}).has_value());
    EXPECT_FALSE(g.contains({0, 0}));
}

TEST(ShadowView, TakeGatedByPosPred) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 10);
    g.set({1, 1}, 20);
    auto sv = shadow(g, [](const std::array<std::uint32_t, 2>& p) { return p[0] == 0; });
    auto r  = sv.take({0, 0});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 10);
    EXPECT_FALSE(g.contains({0, 0}));
    EXPECT_EQ(sv.take({1, 1}).error(), grid_error::EmptyCell);
}

TEST(ShadowView, DimensionsDelegates) {
    dense_grid<2, int> g({3, 8});
    auto sv   = shadow(g, [](const std::array<std::uint32_t, 2>&) { return true; });
    auto dims = sv.dimensions();
    EXPECT_EQ(dims[0], 3u);
    EXPECT_EQ(dims[1], 8u);
}

TEST(ShadowView, UnsafeAccessorsDelegate) {
    dense_grid<2, int> g({4, 4});
    auto sv = shadow(g, [](const std::array<std::uint32_t, 2>&) { return true; });
    EXPECT_EQ(sv.set_unsafe({0, 0}, 42), 42);
    EXPECT_EQ(sv.get_unsafe({0, 0}), 42);
    sv.get_mut_unsafe({0, 0}) = 88;
    EXPECT_EQ(sv.get_unsafe({0, 0}), 88);
}
