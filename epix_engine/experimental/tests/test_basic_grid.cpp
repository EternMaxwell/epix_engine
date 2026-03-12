#include <gtest/gtest.h>

import std;
import epix.experimental.basic_grid;

using namespace ext::grid;

// ============================================================
// packed_grid tests
// ============================================================

TEST(PackedGrid, ConstructionAndDimensions) {
    packed_grid<2, int> g({3, 4}, 0);
    EXPECT_EQ(g.dimension(0), 3);
    EXPECT_EQ(g.dimension(1), 4);
    auto dims = g.dimensions();
    EXPECT_EQ(dims[0], 3);
    EXPECT_EQ(dims[1], 4);
}

TEST(PackedGrid, DefaultValueInit) {
    packed_grid<2, int> g({2, 2}, 42);
    auto val = g.get({0, 0});
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->get(), 42);
}

TEST(PackedGrid, SetAndGet) {
    packed_grid<2, int> g({3, 3}, 0);
    EXPECT_TRUE(g.set({1, 2}, 99).has_value());
    auto val = g.get({1, 2});
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->get(), 99);
}

TEST(PackedGrid, GetMut) {
    packed_grid<2, int> g({2, 2}, 0);
    auto ref = g.get_mut({0, 1});
    ASSERT_TRUE(ref.has_value());
    ref->get() = 7;
    EXPECT_EQ(g.get({0, 1})->get(), 7);
}

TEST(PackedGrid, Reset) {
    packed_grid<2, int> g({2, 2}, 5);
    g.set({0, 0}, 100);
    EXPECT_EQ(g.get({0, 0})->get(), 100);
    EXPECT_TRUE(g.reset({0, 0}).has_value());
    EXPECT_EQ(g.get({0, 0})->get(), 5);
}

TEST(PackedGrid, SetDefault) {
    packed_grid<2, int> g({2, 2}, 0);
    g.set({0, 0}, 10);
    g.set_default(99);
    g.reset({0, 0});
    EXPECT_EQ(g.get({0, 0})->get(), 99);
}

TEST(PackedGrid, OutOfBounds) {
    packed_grid<2, int> g({2, 3}, 0);
    EXPECT_EQ(g.get({2, 0}).error(), grid_error::OutOfBounds);
    EXPECT_EQ(g.get({0, 3}).error(), grid_error::OutOfBounds);
    EXPECT_EQ(g.set({5, 5}, 1).error(), grid_error::OutOfBounds);
    EXPECT_EQ(g.reset({5, 0}).error(), grid_error::OutOfBounds);
}

TEST(PackedGrid, HigherDimensional) {
    packed_grid<3, int> g({2, 3, 4}, 0);
    auto ref = g.get_mut({1, 2, 3});
    ASSERT_TRUE(ref.has_value());
    ref->get() = 42;
    EXPECT_EQ(g.get({1, 2, 3})->get(), 42);
    EXPECT_EQ(g.get({0, 0, 0})->get(), 0);
}

// ============================================================
// dense_grid tests
// ============================================================

TEST(DenseGrid, ConstructionEmpty) {
    dense_grid<2, int> g({4, 4});
    EXPECT_EQ(g.dimension(0), 4);
    EXPECT_EQ(g.dimension(1), 4);
    EXPECT_FALSE(g.contains({0, 0}));
}

TEST(DenseGrid, SetAndGet) {
    dense_grid<2, int> g({3, 3});
    EXPECT_TRUE(g.set({1, 1}, 42).has_value());
    EXPECT_TRUE(g.contains({1, 1}));
    auto val = g.get({1, 1});
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->get(), 42);
}

TEST(DenseGrid, SetOverwrites) {
    dense_grid<2, int> g({3, 3});
    g.set({0, 0}, 1);
    g.set({0, 0}, 2);
    EXPECT_EQ(g.get({0, 0})->get(), 2);
}

TEST(DenseGrid, SetNew) {
    dense_grid<2, int> g({3, 3});
    EXPECT_TRUE(g.set_new({1, 0}, 10).has_value());
    EXPECT_EQ(g.set_new({1, 0}, 20).error(), grid_error::AlreadyOccupied);
    EXPECT_EQ(g.get({1, 0})->get(), 10);
}

TEST(DenseGrid, GetEmpty) {
    dense_grid<2, int> g({3, 3});
    auto val = g.get({0, 0});
    EXPECT_FALSE(val.has_value());
    EXPECT_EQ(val.error(), grid_error::EmptyCell);
}

TEST(DenseGrid, GetMut) {
    dense_grid<2, int> g({3, 3});
    g.set({0, 0}, 5);
    auto ref = g.get_mut({0, 0});
    ASSERT_TRUE(ref.has_value());
    ref->get() = 99;
    EXPECT_EQ(g.get({0, 0})->get(), 99);
}

TEST(DenseGrid, Remove) {
    dense_grid<2, int> g({3, 3});
    g.set({1, 1}, 42);
    EXPECT_TRUE(g.contains({1, 1}));
    EXPECT_TRUE(g.remove({1, 1}).has_value());
    EXPECT_FALSE(g.contains({1, 1}));
    EXPECT_EQ(g.remove({1, 1}).error(), grid_error::EmptyCell);
}

TEST(DenseGrid, Take) {
    dense_grid<2, int> g({3, 3});
    g.set({2, 2}, 77);
    auto taken = g.take({2, 2});
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(taken.value(), 77);
    EXPECT_FALSE(g.contains({2, 2}));
}

TEST(DenseGrid, OutOfBounds) {
    dense_grid<2, int> g({2, 2});
    EXPECT_EQ(g.set({3, 0}, 1).error(), grid_error::OutOfBounds);
    EXPECT_EQ(g.get({0, 5}).error(), grid_error::OutOfBounds);
}

TEST(DenseGrid, Iterators) {
    dense_grid<2, int> g({3, 3});
    g.set({0, 0}, 1);
    g.set({1, 1}, 2);
    g.set({2, 2}, 3);

    std::vector<int> values;
    for (auto& v : g.iter_cells()) {
        values.push_back(v);
    }
    EXPECT_EQ(values.size(), 3u);

    int sum = 0;
    for (auto& v : g.iter_cells()) {
        sum += v;
    }
    EXPECT_EQ(sum, 6);

    std::size_t pos_count = 0;
    for ([[maybe_unused]] auto& p : g.iter_pos()) {
        pos_count++;
    }
    EXPECT_EQ(pos_count, 3u);
}

TEST(DenseGrid, MultipleSetRemove) {
    dense_grid<2, int> g({4, 4});
    for (std::uint32_t i = 0; i < 4; i++) {
        for (std::uint32_t j = 0; j < 4; j++) {
            g.set({i, j}, static_cast<int>(i * 4 + j));
        }
    }
    for (std::uint32_t i = 0; i < 4; i++) {
        for (std::uint32_t j = 0; j < 4; j++) {
            EXPECT_TRUE(g.contains({i, j}));
            EXPECT_EQ(g.get({i, j})->get(), static_cast<int>(i * 4 + j));
        }
    }
    g.remove({0, 0});
    g.remove({3, 3});
    EXPECT_FALSE(g.contains({0, 0}));
    EXPECT_FALSE(g.contains({3, 3}));
    EXPECT_TRUE(g.contains({1, 1}));
}

// ============================================================
// sparse_grid tests
// ============================================================

TEST(SparseGrid, ConstructionEmpty) {
    sparse_grid<2, int> g({4, 4});
    EXPECT_EQ(g.dimension(0), 4);
    EXPECT_FALSE(g.contains({0, 0}));
}

TEST(SparseGrid, SetAndGet) {
    sparse_grid<2, int> g({3, 3});
    EXPECT_TRUE(g.set({1, 2}, 55).has_value());
    EXPECT_TRUE(g.contains({1, 2}));
    EXPECT_EQ(g.get({1, 2})->get(), 55);
}

TEST(SparseGrid, SetOverwrites) {
    sparse_grid<2, int> g({3, 3});
    g.set({0, 0}, 1);
    g.set({0, 0}, 2);
    EXPECT_EQ(g.get({0, 0})->get(), 2);
}

TEST(SparseGrid, SetNew) {
    sparse_grid<2, int> g({3, 3});
    EXPECT_TRUE(g.set_new({1, 0}, 10).has_value());
    EXPECT_EQ(g.set_new({1, 0}, 20).error(), grid_error::AlreadyOccupied);
    EXPECT_EQ(g.get({1, 0})->get(), 10);
}

TEST(SparseGrid, GetEmpty) {
    sparse_grid<2, int> g({3, 3});
    EXPECT_EQ(g.get({0, 0}).error(), grid_error::EmptyCell);
}

TEST(SparseGrid, GetMut) {
    sparse_grid<2, int> g({3, 3});
    g.set({0, 0}, 5);
    g.get_mut({0, 0})->get() = 99;
    EXPECT_EQ(g.get({0, 0})->get(), 99);
}

TEST(SparseGrid, Remove) {
    sparse_grid<2, int> g({3, 3});
    g.set({1, 1}, 42);
    EXPECT_TRUE(g.remove({1, 1}).has_value());
    EXPECT_FALSE(g.contains({1, 1}));
    EXPECT_EQ(g.remove({1, 1}).error(), grid_error::EmptyCell);
}

TEST(SparseGrid, Take) {
    sparse_grid<2, int> g({3, 3});
    g.set({2, 2}, 77);
    auto taken = g.take({2, 2});
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(taken.value(), 77);
    EXPECT_FALSE(g.contains({2, 2}));
}

TEST(SparseGrid, IndexRecycling) {
    sparse_grid<2, int> g({4, 4});
    g.set({0, 0}, 1);
    g.set({1, 1}, 2);
    g.set({2, 2}, 3);
    g.remove({1, 1});
    // After removal and re-insertion, the recycled slot should be reused
    g.set({3, 3}, 4);
    EXPECT_TRUE(g.contains({0, 0}));
    EXPECT_FALSE(g.contains({1, 1}));
    EXPECT_TRUE(g.contains({2, 2}));
    EXPECT_TRUE(g.contains({3, 3}));
    EXPECT_EQ(g.get({3, 3})->get(), 4);
}

TEST(SparseGrid, OutOfBounds) {
    sparse_grid<2, int> g({2, 2});
    EXPECT_EQ(g.set({3, 0}, 1).error(), grid_error::OutOfBounds);
    EXPECT_EQ(g.get({0, 5}).error(), grid_error::OutOfBounds);
}

TEST(SparseGrid, Iterators) {
    sparse_grid<2, int> g({3, 3});
    g.set({0, 0}, 10);
    g.set({1, 1}, 20);
    g.set({2, 2}, 30);

    int sum = 0;
    for (auto& v : g.iter_cells()) {
        sum += v;
    }
    EXPECT_EQ(sum, 60);

    g.remove({1, 1});
    sum = 0;
    for (auto& v : g.iter_cells()) {
        sum += v;
    }
    EXPECT_EQ(sum, 40);
}

// ============================================================
// dense_extendible_grid tests
// ============================================================

TEST(DenseExtendibleGrid, ConstructionAndDimensions) {
    dense_extendible_grid<2, int> g({4, 4});
    EXPECT_EQ(g.dimension(0), 4);
    EXPECT_EQ(g.dimension(1), 4);
    EXPECT_FALSE(g.contains({0, 0}));
}

TEST(DenseExtendibleGrid, SetAndGet) {
    dense_extendible_grid<2, int> g({4, 4});
    EXPECT_TRUE(g.set({1, 1}, 42).has_value());
    EXPECT_TRUE(g.contains({1, 1}));
    EXPECT_EQ(g.get({1, 1})->get(), 42);
}

TEST(DenseExtendibleGrid, SetNew) {
    dense_extendible_grid<2, int> g({4, 4});
    EXPECT_TRUE(g.set_new({0, 0}, 10).has_value());
    EXPECT_EQ(g.set_new({0, 0}, 20).error(), grid_error::AlreadyOccupied);
    EXPECT_EQ(g.get({0, 0})->get(), 10);
}

TEST(DenseExtendibleGrid, GetMut) {
    dense_extendible_grid<2, int> g({4, 4});
    g.set({0, 0}, 5);
    g.get_mut({0, 0})->get() = 99;
    EXPECT_EQ(g.get({0, 0})->get(), 99);
}

TEST(DenseExtendibleGrid, NegativeCoordinates) {
    dense_extendible_grid<2, int> g({4, 4});
    // Extend to cover negative range
    g.extend({-2, -2}, {4, 4});
    EXPECT_TRUE(g.set({-1, -1}, 77).has_value());
    EXPECT_TRUE(g.contains({-1, -1}));
    EXPECT_EQ(g.get({-1, -1})->get(), 77);
}

TEST(DenseExtendibleGrid, ExtendAndAccess) {
    dense_extendible_grid<2, int> g({2, 2});
    g.set({0, 0}, 1);
    g.extend({-3, -3}, {5, 5});
    // Old data should still be accessible
    EXPECT_EQ(g.get({0, 0})->get(), 1);
    // New range should be accessible
    EXPECT_TRUE(g.set({4, 4}, 99).has_value());
    EXPECT_EQ(g.get({4, 4})->get(), 99);
    EXPECT_TRUE(g.set({-2, -2}, 50).has_value());
    EXPECT_EQ(g.get({-2, -2})->get(), 50);
}

TEST(DenseExtendibleGrid, Iterators) {
    dense_extendible_grid<2, int> g({4, 4});
    g.set({0, 0}, 1);
    g.set({1, 1}, 2);
    g.set({2, 2}, 3);

    int sum = 0;
    for (auto& v : g.iter_cells()) {
        sum += v;
    }
    EXPECT_EQ(sum, 6);

    std::size_t count = 0;
    for ([[maybe_unused]] auto& p : g.iter_pos()) {
        count++;
    }
    EXPECT_EQ(count, 3u);
}

// ============================================================
// tree_extendible_grid tests
// ============================================================

TEST(TreeExtendibleGrid, DefaultConstruction) {
    tree_extendible_grid<2, int> g;
    EXPECT_EQ(g.count(), 0u);
}

TEST(TreeExtendibleGrid, SetAndGet) {
    tree_extendible_grid<2, int> g;
    EXPECT_TRUE(g.set({0, 0}, 42).has_value());
    EXPECT_TRUE(g.contains({0, 0}));
    EXPECT_EQ(g.get({0, 0})->get(), 42);
    EXPECT_EQ(g.count(), 1u);
}

TEST(TreeExtendibleGrid, SetOverwrites) {
    tree_extendible_grid<2, int> g;
    g.set({0, 0}, 1);
    g.set({0, 0}, 2);
    EXPECT_EQ(g.get({0, 0})->get(), 2);
    EXPECT_EQ(g.count(), 1u);
}

TEST(TreeExtendibleGrid, SetNew) {
    tree_extendible_grid<2, int> g;
    EXPECT_TRUE(g.set_new({1, 1}, 10).has_value());
    EXPECT_EQ(g.set_new({1, 1}, 20).error(), grid_error::AlreadyOccupied);
    EXPECT_EQ(g.get({1, 1})->get(), 10);
}

TEST(TreeExtendibleGrid, GetEmpty) {
    tree_extendible_grid<2, int> g;
    auto result = g.get({0, 0});
    EXPECT_FALSE(result.has_value());
}

TEST(TreeExtendibleGrid, GetMut) {
    tree_extendible_grid<2, int> g;
    g.set({0, 0}, 5);
    g.get_mut({0, 0})->get() = 99;
    EXPECT_EQ(g.get({0, 0})->get(), 99);
}

TEST(TreeExtendibleGrid, Remove) {
    tree_extendible_grid<2, int> g;
    g.set({1, 1}, 42);
    EXPECT_TRUE(g.remove({1, 1}).has_value());
    EXPECT_FALSE(g.contains({1, 1}));
    EXPECT_EQ(g.count(), 0u);
    EXPECT_EQ(g.remove({1, 1}).error(), grid_error::EmptyCell);
}

TEST(TreeExtendibleGrid, Take) {
    tree_extendible_grid<2, int> g;
    g.set({2, 3}, 77);
    auto taken = g.take({2, 3});
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(taken.value(), 77);
    EXPECT_FALSE(g.contains({2, 3}));
    EXPECT_EQ(g.count(), 0u);
}

TEST(TreeExtendibleGrid, AutoExtend) {
    tree_extendible_grid<2, int> g;
    // Insert at a large coordinate; tree should extend automatically
    EXPECT_TRUE(g.set({100, 200}, 55).has_value());
    EXPECT_TRUE(g.contains({100, 200}));
    EXPECT_EQ(g.get({100, 200})->get(), 55);
    EXPECT_GE(g.coverage(), 201u);
}

TEST(TreeExtendibleGrid, MultipleInserts) {
    tree_extendible_grid<2, int> g;
    for (std::uint32_t i = 0; i < 10; i++) {
        for (std::uint32_t j = 0; j < 10; j++) {
            g.set({i, j}, static_cast<int>(i * 10 + j));
        }
    }
    EXPECT_EQ(g.count(), 100u);
    for (std::uint32_t i = 0; i < 10; i++) {
        for (std::uint32_t j = 0; j < 10; j++) {
            EXPECT_EQ(g.get({i, j})->get(), static_cast<int>(i * 10 + j));
        }
    }
}

TEST(TreeExtendibleGrid, Iterators) {
    tree_extendible_grid<2, int> g;
    g.set({0, 0}, 1);
    g.set({1, 1}, 2);
    g.set({2, 2}, 3);

    int sum = 0;
    for (auto& v : g.iter_cells()) {
        sum += v;
    }
    EXPECT_EQ(sum, 6);

    std::size_t count = 0;
    for ([[maybe_unused]] auto& p : g.iter_pos()) {
        count++;
    }
    EXPECT_EQ(count, 3u);
}

TEST(TreeExtendibleGrid, CustomChildCount) {
    tree_extendible_grid<2, int, 4> g;
    g.set({0, 0}, 1);
    g.set({15, 15}, 2);
    EXPECT_EQ(g.count(), 2u);
    EXPECT_EQ(g.get({0, 0})->get(), 1);
    EXPECT_EQ(g.get({15, 15})->get(), 2);
}

TEST(TreeExtendibleGrid, HigherDimensional) {
    tree_extendible_grid<3, int> g;
    auto ref = g.get({1, 2, 3});
    EXPECT_FALSE(ref.has_value());
}
