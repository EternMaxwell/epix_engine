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

namespace {
constexpr auto kFullCat = grid_category::iterable | grid_category::container | grid_category::unsafe_viewable |
                          grid_category::unsafe_container | grid_category::const_viewable |
                          grid_category::const_iterable | grid_category::const_unsafe;
using ugrid             = untyped_grid<2, kFullCat, std::uint32_t, std::uint32_t>;
}  // namespace

// ============================================================
// untyped_grid — truly type-erased void* grid tests
// ============================================================

TEST(UntypedGrid, ConstructFromDenseGrid) {
    dense_grid<2, int> dg({3, 3});
    dg.set({0, 0}, 42);
    ugrid g(std::move(dg));
    EXPECT_EQ(g.dimensions(), (std::array<std::uint32_t, 2>{3, 3}));
}

TEST(UntypedGrid, GetReturnsVoidPointer) {
    dense_grid<2, int> dg({3, 3});
    dg.set({0, 0}, 42);
    ugrid g(std::move(dg));
    auto r = g.get({0, 0});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*static_cast<int*>(*r), 42);
}

TEST(UntypedGrid, GetOnEmptyReturnsError) {
    dense_grid<2, int> dg({3, 3});
    ugrid g(std::move(dg));
    auto r = g.get({0, 0});
    EXPECT_FALSE(r.has_value());
}

TEST(UntypedGrid, ContainsDelegates) {
    dense_grid<2, int> dg({4, 4});
    dg.set({1, 2}, 7);
    ugrid g(std::move(dg));
    EXPECT_TRUE(g.contains({1, 2}));
    EXPECT_FALSE(g.contains({3, 3}));
}

TEST(UntypedGrid, SetInsertsValue) {
    dense_grid<2, int> dg({3, 3});
    ugrid g(std::move(dg));
    int val = 99;
    auto r  = g.set({1, 1}, &val);
    ASSERT_TRUE(r.has_value());
    auto gr = g.get({1, 1});
    ASSERT_TRUE(gr.has_value());
    EXPECT_EQ(*static_cast<int*>(*gr), 99);
}

TEST(UntypedGrid, SetNewConstructsInPlace) {
    dense_grid<2, int> dg({3, 3});
    ugrid g(std::move(dg));
    int val = 33;
    auto r  = g.set_new({0, 2}, &val);
    ASSERT_TRUE(r.has_value());
    auto gr = g.get({0, 2});
    ASSERT_TRUE(gr.has_value());
    EXPECT_EQ(*static_cast<int*>(*gr), 33);
}

TEST(UntypedGrid, RemoveErasesCell) {
    dense_grid<2, int> dg({3, 3});
    dg.set({1, 1}, 42);
    ugrid g(std::move(dg));
    auto r = g.remove({1, 1});
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(g.contains({1, 1}));
}

TEST(UntypedGrid, ClearRemovesAll) {
    dense_grid<2, int> dg({3, 3});
    dg.set({0, 0}, 1);
    dg.set({2, 2}, 2);
    ugrid g(std::move(dg));
    g.clear();
    EXPECT_FALSE(g.contains({0, 0}));
    EXPECT_FALSE(g.contains({2, 2}));
}

TEST(UntypedGrid, GetUnsafeReturnsVoidPointer) {
    dense_grid<2, int> dg({3, 3});
    dg.set({0, 0}, 99);
    ugrid g(std::move(dg));
    void* p = g.get_unsafe({0, 0});
    EXPECT_EQ(*static_cast<int*>(p), 99);
}

TEST(UntypedGrid, SetUnsafeModifiesInPlace) {
    dense_grid<2, int> dg({3, 3});
    ugrid g(std::move(dg));
    int val = 123;
    void* p = g.set_unsafe({1, 1}, &val);
    EXPECT_EQ(*static_cast<int*>(p), 123);
}

TEST(UntypedGrid, RemoveUnsafeErases) {
    dense_grid<2, int> dg({3, 3});
    dg.set({1, 1}, 42);
    ugrid g(std::move(dg));
    g.remove_unsafe({1, 1});
    EXPECT_FALSE(g.contains({1, 1}));
}

TEST(UntypedGrid, IterPosYieldsAllPositions) {
    dense_grid<2, int> dg({2, 2});
    dg.set({0, 0}, 1);
    dg.set({0, 1}, 2);
    dg.set({1, 0}, 3);
    dg.set({1, 1}, 4);
    ugrid g(std::move(dg));
    int count = 0;
    for (auto pos : g.iter_pos()) {
        (void)pos;
        ++count;
    }
    EXPECT_EQ(count, 4);
}

TEST(UntypedGrid, IterCellsYieldsVoidPointers) {
    dense_grid<2, int> dg({2, 2});
    dg.set({0, 0}, 10);
    dg.set({0, 1}, 20);
    dg.set({1, 0}, 30);
    dg.set({1, 1}, 40);
    ugrid g(std::move(dg));
    int sum = 0;
    for (void* p : g.iter_cells()) {
        sum += *static_cast<int*>(p);
    }
    EXPECT_EQ(sum, 100);
}

TEST(UntypedGrid, IterYieldsPosVoidPointerPairs) {
    dense_grid<2, int> dg({2, 2});
    dg.set({0, 1}, 99);
    ugrid g(std::move(dg));
    bool found = false;
    for (auto [pos, p] : g.iter()) {
        if (pos[0] == 0 && pos[1] == 1) {
            EXPECT_EQ(*static_cast<int*>(p), 99);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(UntypedGrid, MoveConstruction) {
    dense_grid<2, int> dg({3, 3});
    dg.set({0, 0}, 42);
    ugrid g1(std::move(dg));
    ugrid g2(std::move(g1));
    auto r = g2.get({0, 0});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*static_cast<int*>(*r), 42);
}

TEST(UntypedGrid, CtadDeducesTypes) {
    auto g = untyped_grid(dense_grid<2, int>({3, 3}));
    EXPECT_EQ(g.dimensions(), (std::array<std::uint32_t, 2>{3, 3}));
}
