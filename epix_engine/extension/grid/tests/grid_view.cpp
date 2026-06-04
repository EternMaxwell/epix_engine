#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <epix/extension/grid.hpp>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(disable : 4834)
#elif defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-value"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-value"
#endif

using namespace epix::ext::grid;
using namespace epix::ext::grid::views;

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
    EXPECT_TRUE(fv.get({0, 0}).has_value());
    EXPECT_EQ(fv.get({1, 1}).error(), grid_error::EmptyCell);
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
    g.set({0, 0}, 42);
    auto fv = filter(g, [](const int& v) { return true; });
    EXPECT_EQ(fv.get_unsafe({0, 0}), 42);
    fv.get_unsafe({0, 0}) = 99;
    EXPECT_EQ(fv.get_unsafe({0, 0}), 99);
}

TEST(FilterView, IterPosYieldsFilteredPositions) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 1);
    g.set({1, 1}, -5);
    g.set({2, 2}, 3);
    auto fv = filter(g, [](const int& v) { return v > 0; });
    std::vector<std::array<std::uint32_t, 2>> positions;
    for (auto pos : fv.iter_pos()) positions.push_back(pos);
    ASSERT_EQ(positions.size(), 2u);
    EXPECT_EQ(positions[0], (std::array<std::uint32_t, 2>{0, 0}));
    EXPECT_EQ(positions[1], (std::array<std::uint32_t, 2>{2, 2}));
}

TEST(FilterView, IterCellsYieldsFilteredValues) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 10);
    g.set({1, 1}, -20);
    g.set({2, 2}, 30);
    auto fv = filter(g, [](const int& v) { return v > 0; });
    int sum = 0;
    for (const int& cell : fv.iter_cells()) sum += cell;
    EXPECT_EQ(sum, 40);
}

TEST(FilterView, IterYieldsFilteredPairs) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 5);
    g.set({1, 1}, -5);
    g.set({3, 3}, 15);
    auto fv = filter(g, [](const int& v) { return v > 0; });
    int sum = 0;
    for (auto [pos, cell] : fv.iter()) sum += cell;
    EXPECT_EQ(sum, 20);
}

TEST(FilterView, IterCellsMutYieldsMutableFilteredValues) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 1);
    g.set({1, 1}, -1);
    g.set({2, 2}, 3);
    auto fv = filter(g, [](const int& v) { return v > 0; });
    for (int& cell : fv.iter_cells()) cell *= 10;
    EXPECT_EQ(g.get({0, 0})->get(), 10);
    EXPECT_EQ(g.get({2, 2})->get(), 30);
}

TEST(FilterView, IterMutYieldsMutableFilteredPairs) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 2);
    g.set({1, 1}, -2);
    auto fv = filter(g, [](const int& v) { return v > 0; });
    for (auto [pos, cell] : fv.iter()) cell += static_cast<int>(pos[0]);
    EXPECT_EQ(g.get({0, 0})->get(), 2);
}

// ────────────────────────────────────────────────────────────
// transform_view tests
// ────────────────────────────────────────────────────────────

namespace {
struct TransformViewCell {
    int value;
    int other;
};
}  // namespace

TEST(TransformView, GetProjectsReference) {
    dense_grid<2, TransformViewCell> g({4, 4});
    g.set({1, 2}, TransformViewCell{42, 7});

    auto tv = transform(g, [](const TransformViewCell& cell) -> const int& { return cell.value; });

    EXPECT_TRUE(tv.contains({1, 2}));
    auto r = tv.get({1, 2});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->get(), 42);
    EXPECT_EQ(tv.get({0, 0}).error(), grid_error::EmptyCell);
}

TEST(TransformView, GetCanReturnProjectedValue) {
    dense_grid<2, int> g({4, 4});
    g.set({1, 1}, 21);

    auto tv = transform(g, [](const int& value) { return value * 2; });

    auto r = tv.get({1, 1});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 42);
}

TEST(TransformView, GetMutProjectsMutableReference) {
    dense_grid<2, TransformViewCell> g({4, 4});
    g.set({0, 0}, TransformViewCell{10, 3});

    auto tv = transform(g, [](auto& cell) -> auto& { return cell.value; });
    static_assert(viewable_grid<decltype(tv)>);
    static_assert(iterable_grid<decltype(tv)>);

    auto r = tv.get({0, 0});
    ASSERT_TRUE(r.has_value());
    r->get() = 99;
    EXPECT_EQ(g.get({0, 0})->get().value, 99);
}

TEST(TransformView, IterCellsProjectsValues) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 2);
    g.set({1, 0}, 3);

    auto tv = transform(g, [](const int& value) { return value * 10; });

    std::vector<int> values;
    for (auto value : tv.iter_cells()) {
        values.push_back(value);
    }

    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0], 20);
    EXPECT_EQ(values[1], 30);
}

TEST(TransformView, IterPairsKeepPositions) {
    dense_grid<2, int> g({4, 4});
    g.set({2, 1}, 5);

    auto tv = transform(g, [](const int& value) { return value + 1; });

    auto it           = tv.iter().begin();
    auto [pos, value] = *it;
    EXPECT_EQ(pos[0], 2u);
    EXPECT_EQ(pos[1], 1u);
    EXPECT_EQ(value, 6);
}

// ────────────────────────────────────────────────────────────
// offset_view tests
// ────────────────────────────────────────────────────────────

TEST(OffsetView, GetUsesNewOriginAndDimensions) {
    dense_grid<2, int> g({6, 6});
    g.set({2, 3}, 42);
    g.set({3, 3}, 7);

    auto ov = offset(g, std::array<std::uint32_t, 2>{2, 3}, std::array<std::uint32_t, 2>{2, 2});
    static_assert(viewable_grid<decltype(ov)>);
    static_assert(iterable_grid<decltype(ov)>);

    auto dims = ov.dimensions();
    EXPECT_EQ(dims[0], 2u);
    EXPECT_EQ(dims[1], 2u);
    EXPECT_TRUE(ov.contains({0, 0}));
    EXPECT_TRUE(ov.contains({1, 0}));
    EXPECT_FALSE(ov.contains({2, 0}));
    EXPECT_EQ(ov.get({0, 0})->get(), 42);
    EXPECT_EQ(ov.get({1, 0})->get(), 7);
    EXPECT_EQ(ov.get({2, 0}).error(), grid_error::OutOfBounds);
}

TEST(OffsetView, MutationsDelegateToTranslatedPosition) {
    dense_grid<2, int> g({6, 6});
    auto ov = offset(g, std::array<std::uint32_t, 2>{2, 3}, std::array<std::uint32_t, 2>{2, 2});

    ASSERT_TRUE(ov.set({0, 1}, 99).has_value());
    EXPECT_EQ(g.get({2, 4})->get(), 99);

    auto r = ov.get({0, 1});
    ASSERT_TRUE(r.has_value());
    r->get() = 100;
    EXPECT_EQ(g.get({2, 4})->get(), 100);

    ASSERT_TRUE(ov.remove({0, 1}).has_value());
    EXPECT_FALSE(g.contains({2, 4}));
}

TEST(OffsetView, IterationReturnsViewRelativePositions) {
    dense_grid<2, int> g({6, 6});
    g.set({2, 3}, 11);
    g.set({3, 4}, 22);
    g.set({4, 4}, 33);

    auto ov = offset(g, std::array<std::uint32_t, 2>{2, 3}, std::array<std::uint32_t, 2>{2, 2});

    std::vector<std::array<std::uint32_t, 2>> positions;
    std::vector<int> values;
    for (auto&& [pos, value] : ov.iter()) {
        positions.push_back(pos);
        values.push_back(value);
    }

    ASSERT_EQ(positions.size(), 2u);
    EXPECT_EQ(positions[0], (std::array<std::uint32_t, 2>{0, 0}));
    EXPECT_EQ(positions[1], (std::array<std::uint32_t, 2>{1, 1}));
    EXPECT_EQ(values[0], 11);
    EXPECT_EQ(values[1], 22);
}

TEST(OffsetView, SupportsSignedOrigins) {
    tree_extendible_grid<2, int> g;
    g.set({-2, 3}, 5);

    auto ov = offset(g, std::array<std::int32_t, 2>{-2, 3}, std::array<std::uint32_t, 2>{3, 3});

    EXPECT_TRUE(ov.contains({0, 0}));
    EXPECT_EQ(ov.get({0, 0})->get(), 5);
    ASSERT_TRUE(ov.set({2, 2}, 9).has_value());
    EXPECT_EQ(g.get({0, 5})->get(), 9);
    EXPECT_EQ(ov.get({-1, 0}).error(), grid_error::OutOfBounds);
}

TEST(OffsetView, IterPosYieldsViewRelativePositions) {
    dense_grid<2, int> g({6, 6});
    g.set({2, 3}, 1);
    g.set({3, 4}, 2);

    auto ov = offset(g, std::array<std::uint32_t, 2>{2, 3}, std::array<std::uint32_t, 2>{2, 2});
    std::vector<std::array<std::uint32_t, 2>> positions;
    for (auto pos : ov.iter_pos()) positions.push_back(pos);
    ASSERT_EQ(positions.size(), 2u);
}

TEST(OffsetView, IterCellsYieldsValuesInWindow) {
    dense_grid<2, int> g({6, 6});
    g.set({2, 3}, 10);
    g.set({3, 4}, 20);

    auto ov = offset(g, std::array<std::uint32_t, 2>{2, 3}, std::array<std::uint32_t, 2>{2, 2});
    int sum = 0;
    for (const int& cell : ov.iter_cells()) sum += cell;
    EXPECT_EQ(sum, 30);
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
    EXPECT_TRUE(sv.get({0, 0}).has_value());
    EXPECT_EQ(sv.get({1, 1}).error(), grid_error::EmptyCell);
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
    g.set({0, 0}, 42);
    auto sv = shadow(g, [](const std::array<std::uint32_t, 2>&) { return true; });
    EXPECT_EQ(sv.get_unsafe({0, 0}), 42);
    sv.get_unsafe({0, 0}) = 88;
    EXPECT_EQ(sv.get_unsafe({0, 0}), 88);
}

TEST(ShadowView, IterPosYieldsShadowedPositions) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 1);
    g.set({1, 1}, 2);
    g.set({2, 2}, 3);
    auto sv = shadow(g, [](const std::array<std::uint32_t, 2>& p) { return p[0] <= 1; });
    std::vector<std::array<std::uint32_t, 2>> positions;
    for (auto pos : sv.iter_pos()) positions.push_back(pos);
    ASSERT_EQ(positions.size(), 2u);
}

TEST(ShadowView, IterCellsYieldsShadowedValues) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 10);
    g.set({1, 1}, 20);
    g.set({3, 3}, 30);
    auto sv = shadow(g, [](const std::array<std::uint32_t, 2>& p) { return p[0] <= 1; });
    int sum = 0;
    for (const int& cell : sv.iter_cells()) sum += cell;
    EXPECT_EQ(sum, 30);
}

TEST(ShadowView, IterYieldsShadowedPairs) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 5);
    g.set({1, 1}, 7);
    g.set({3, 3}, 9);
    auto sv = shadow(g, [](const std::array<std::uint32_t, 2>& p) { return p[0] <= 1; });
    int sum = 0;
    for (auto [pos, cell] : sv.iter()) sum += cell;
    EXPECT_EQ(sum, 12);
}

TEST(ShadowView, IterCellsMutYieldsMutableShadowedValues) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 1);
    g.set({1, 1}, 3);
    g.set({2, 2}, 5);
    auto sv = shadow(g, [](const std::array<std::uint32_t, 2>& p) { return p[0] <= 1; });
    for (int& cell : sv.iter_cells()) cell *= 10;
    EXPECT_EQ(g.get({0, 0})->get(), 10);
    EXPECT_EQ(g.get({1, 1})->get(), 30);
}

TEST(ShadowView, IterMutYieldsMutableShadowedPairs) {
    dense_grid<2, int> g({4, 4});
    g.set({0, 0}, 2);
    g.set({2, 2}, 4);
    auto sv = shadow(g, [](const std::array<std::uint32_t, 2>& p) { return p[0] == 0; });
    for (auto [pos, cell] : sv.iter()) cell += 10;
    EXPECT_EQ(g.get({0, 0})->get(), 12);
}
