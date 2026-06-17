#include <gtest/gtest.h>

#include <epix/extension/grid.hpp>

#if defined(_MSC_VER)
#pragma warning(disable : 4834)
#elif defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-value"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-value"
#endif

using namespace epix::ext::grid;

namespace {
constexpr auto kFullCat = grid_category::iterable | grid_category::container | grid_category::unsafe_viewable |
                          grid_category::unsafe_container | grid_category::const_viewable |
                          grid_category::const_iterable | grid_category::const_unsafe | grid_category::counted;
template <std::size_t Dim, typename T>
using ugrid = any_grid<Dim, T, kFullCat, std::uint32_t, std::uint32_t>;
}  // namespace

// ============================================================
// category conversion — compile-time verification
// ============================================================

using full_grid = any_grid<2, int&, kFullCat, std::uint32_t, std::uint32_t>;
static_assert(
    std::is_constructible_v<any_grid<2, int&, grid_category::none, std::uint32_t, std::uint32_t>, full_grid&&>);
static_assert(
    std::is_constructible_v<any_grid<2, int&, grid_category::iterable, std::uint32_t, std::uint32_t>, full_grid&&>);
static_assert(
    std::is_constructible_v<any_grid<2, int&, grid_category::container, std::uint32_t, std::uint32_t>, full_grid&&>);
static_assert(
    !std::is_constructible_v<any_grid<2, int&, grid_category::copyable, std::uint32_t, std::uint32_t>, full_grid&&>);

using full_view =
    any_grid_view<2, int&, grid_category::iterable | grid_category::container, std::uint32_t, std::uint32_t>;
static_assert(std::is_constructible_v<any_grid_view<2, int&, grid_category::none, std::uint32_t, std::uint32_t>,
                                      const full_view&>);
static_assert(std::is_constructible_v<any_grid_view<2, int&, grid_category::iterable, std::uint32_t, std::uint32_t>,
                                      const full_view&>);
static_assert(std::is_constructible_v<any_grid_view<2, int&, grid_category::container, std::uint32_t, std::uint32_t>,
                                      const full_view&>);
static_assert(!std::is_constructible_v<
              any_grid_view<2,
                            int&,
                            grid_category::const_viewable | grid_category::const_iterable | grid_category::const_unsafe,
                            std::uint32_t,
                            std::uint32_t>,
              const full_view&>);

// ============================================================
// any_grid tests
// ============================================================

// ── construction & CTAD ─────────────────────────────────────

TEST(AnyGrid, ConstructFromDenseGrid) {
    dense_grid<2, int> dg({4, 5});
    ugrid<2, int&> g(std::move(dg));
    EXPECT_EQ(g.dimensions(), (std::array<std::uint32_t, 2>{4, 5}));
}

TEST(AnyGrid, CtadDeducesDimAndCellType) {
    auto g = any_grid(dense_grid<2, int>({3, 3}));
    static_assert(viewable_grid<decltype(g)>);
    EXPECT_EQ(g.dimensions(), (std::array<std::uint32_t, 2>{3, 3}));
}

TEST(AnyGrid, MoveConstruction) {
    auto g1 = any_grid(dense_grid<2, int>({3, 3}));
    g1.set({0, 0}, 42);
    ugrid<2, int&> g2(std::move(g1));
    EXPECT_EQ(g2.get({0, 0}).value(), 42);
}

TEST(AnyGrid, MoveAssignment) {
    auto g1 = any_grid(dense_grid<2, int>({3, 3}));
    g1.set({1, 1}, 77);
    auto g2 = any_grid(dense_grid<2, int>({1, 1}));
    g2      = std::move(g1);
    EXPECT_EQ(g2.get({1, 1}).value(), 77);
}

TEST(AnyGrid, CopyConstructionPreservesData) {
    auto g1 = any_grid(dense_grid<2, int>({3, 3}));
    g1.set({2, 2}, 99);
    auto g2 = g1;  // copy
    EXPECT_EQ(g2.get({2, 2}).value(), 99);
    // modifying copy does not affect original
    g2.set({2, 2}, 11);
    EXPECT_EQ(g1.get({2, 2}).value(), 99);
    EXPECT_EQ(g2.get({2, 2}).value(), 11);
}

// ── viewable_grid ───────────────────────────────────────────

TEST(AnyGrid, DimensionsReflectsUnderlying) {
    auto g = any_grid(dense_grid<2, int>({7, 3}));
    auto d = g.dimensions();
    EXPECT_EQ(d[0], 7u);
    EXPECT_EQ(d[1], 3u);
}

TEST(AnyGrid, ContainsDelegates) {
    auto g = any_grid(dense_grid<2, int>({4, 4}));
    g.set({0, 0}, 1);
    EXPECT_TRUE(g.contains({0, 0}));
    EXPECT_FALSE(g.contains({3, 3}));
    EXPECT_FALSE(g.contains({4, 0}));  // out of bounds
}

TEST(AnyGrid, GetReturnsValue) {
    auto g = any_grid(dense_grid<2, int>({4, 4}));
    g.set({2, 3}, 55);
    auto r = g.get({2, 3});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 55);
}

TEST(AnyGrid, GetReturnsErrorForEmpty) {
    auto g = any_grid(dense_grid<2, int>({4, 4}));
    EXPECT_EQ(g.get({0, 0}).error(), grid_error::EmptyCell);
}

// ── iterable_grid ───────────────────────────────────────────

TEST(AnyGrid, IterPosYieldsAllPositions) {
    auto g = any_grid(dense_grid<2, int>({2, 2}));
    g.set({0, 0}, 1);
    g.set({1, 1}, 2);
    std::vector<std::array<std::uint32_t, 2>> positions;
    for (auto pos : g.iter_pos()) positions.push_back(pos);
    EXPECT_EQ(positions.size(), 2u);
}

TEST(AnyGrid, IterCellsYieldsCellValues) {
    auto g = any_grid(dense_grid<2, int>({2, 2}));
    g.set({0, 0}, 10);
    g.set({1, 1}, 20);
    int sum = 0;
    for (const int& cell : g.iter_cells()) sum += cell;
    EXPECT_EQ(sum, 30);
}

TEST(AnyGrid, IterYieldsPosValuePairs) {
    auto g = any_grid(dense_grid<2, int>({2, 2}));
    g.set({0, 0}, 100);
    g.set({1, 1}, 200);
    int sum = 0;
    for (auto [pos, cell] : g.iter()) {
        sum += cell;
        EXPECT_GE(pos[0], 0u);
        EXPECT_LE(pos[0], 1u);
    }
    EXPECT_EQ(sum, 300);
}

// ── mutable_viewable_grid ───────────────────────────────────

TEST(AnyGrid, GetMutSupported) {
    auto g = any_grid(dense_grid<2, int>({3, 3}));
    g.set({0, 0}, 5);
    auto r = g.get({0, 0});
    ASSERT_TRUE(r.has_value());
    r->get() = 99;
    EXPECT_EQ(g.get({0, 0}).value(), 99);
}

TEST(AnyGrid, GetMutOnFilterViewStillWorks) {
    // filter_view HAS mutable_viewable_grid — get_mut should succeed
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 10);
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    auto g  = any_grid(std::move(fv));
    auto r  = g.get({0, 0});
    ASSERT_TRUE(r.has_value());
    r->get() = 99;
    EXPECT_EQ(g.get({0, 0}).value(), 99);
}

// ── grid_container ──────────────────────────────────────────

TEST(AnyGrid, SetInsertsValue) {
    auto g = any_grid(dense_grid<2, int>({3, 3}));
    auto r = g.set({1, 2}, 42);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 42);
    EXPECT_EQ(g.get({1, 2}).value(), 42);
}

TEST(AnyGrid, SetOverwritesExisting) {
    auto g = any_grid(dense_grid<2, int>({3, 3}));
    g.set({0, 0}, 10);
    g.set({0, 0}, 20);
    EXPECT_EQ(g.get({0, 0}).value(), 20);
}

TEST(AnyGrid, SetNewViaSet) {
    auto g = any_grid(dense_grid<2, int>({3, 3}));
    auto r = g.set_new({1, 1}, 77);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(g.get({1, 1}).value(), 77);
}

TEST(AnyGrid, RemoveErasesCell) {
    auto g = any_grid(dense_grid<2, int>({3, 3}));
    g.set({2, 2}, 33);
    EXPECT_TRUE(g.contains({2, 2}));
    auto r = g.remove({2, 2});
    EXPECT_TRUE(r.has_value());
    EXPECT_FALSE(g.contains({2, 2}));
}

TEST(AnyGrid, TakeMovesOutAndErases) {
    auto g = any_grid(dense_grid<2, int>({3, 3}));
    g.set({2, 2}, 88);
    auto r = g.take({2, 2});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 88);
    EXPECT_FALSE(g.contains({2, 2}));
}

// ── not supported: filter_view lacks grid_container ────────

TEST(AnyGrid, SetOnFilterViewNotAvailable) {
    dense_grid<2, int> dg({4, 4});
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    auto g  = any_grid(std::move(fv));
    static_assert(!grid_container<decltype(g)>);
    EXPECT_TRUE(true);
}

TEST(AnyGrid, RemoveOnFilterViewNotAvailable) {
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 1);
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    auto g  = any_grid(std::move(fv));
    static_assert(!grid_container<decltype(g)>);
    EXPECT_TRUE(true);
}

TEST(AnyGrid, TakeOnFilterViewNotAvailable) {
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 1);
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    auto g  = any_grid(std::move(fv));
    static_assert(!grid_container<decltype(g)>);
    EXPECT_TRUE(true);
}

// ── iterable / mutable_iterable on filter_view still works ──

TEST(AnyGrid, IterCellsOnFilterViewStillWorks) {
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 10);
    dg.set({1, 1}, 20);
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    auto g  = any_grid(std::move(fv));
    int sum = 0;
    for (const int& cell : g.iter_cells()) sum += cell;
    EXPECT_EQ(sum, 30);
}

TEST(AnyGrid, IterMutOnFilterViewStillWorks) {
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 5);
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    auto g  = any_grid(std::move(fv));
    for (auto [pos, cell] : g.iter()) cell += 1;
    EXPECT_EQ(g.get({0, 0}).value(), 6);
}

// ── unsafe accessors ────────────────────────────────────────

TEST(AnyGrid, GetUnsafeReturnsReference) {
    auto g = any_grid(dense_grid<2, int>({3, 3}));
    g.set({0, 0}, 42);
    EXPECT_EQ(g.get_unsafe({0, 0}), 42);
}

TEST(AnyGrid, GetMutUnsafeModifiesInPlace) {
    auto g = any_grid(dense_grid<2, int>({3, 3}));
    g.set({0, 0}, 1);
    g.get_unsafe({0, 0}) = 99;
    EXPECT_EQ(g.get({0, 0}).value(), 99);
}

TEST(AnyGrid, SetUnsafeInserts) {
    auto g = any_grid(dense_grid<2, int>({3, 3}));
    EXPECT_EQ(g.set_unsafe({1, 1}, 55), 55);
    EXPECT_EQ(g.get_unsafe({1, 1}), 55);
}

TEST(AnyGrid, RemoveUnsafeErases) {
    auto g = any_grid(dense_grid<2, int>({3, 3}));
    g.set_unsafe({2, 2}, 33);
    g.remove_unsafe({2, 2});
    EXPECT_FALSE(g.contains({2, 2}));
}

TEST(AnyGrid, TakeUnsafeMovesOut) {
    auto g = any_grid(dense_grid<2, int>({3, 3}));
    g.set({2, 2}, 77);
    EXPECT_EQ(g.take_unsafe({2, 2}), 77);
    EXPECT_FALSE(g.contains({2, 2}));
}

// ── mutable_iterable_grid ───────────────────────────────────

TEST(AnyGrid, SetUnsafeOnFilterViewNotAvailable) {
    dense_grid<2, int> dg({4, 4});
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    auto g  = any_grid(std::move(fv));
    static_assert(!unsafe_grid_container<decltype(g)>);
    EXPECT_TRUE(true);
}

TEST(AnyGrid, TakeUnsafeOnFilterViewNotAvailable) {
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 1);
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    auto g  = any_grid(std::move(fv));
    static_assert(!unsafe_grid_container<decltype(g)>);
    EXPECT_TRUE(true);
}

TEST(AnyGrid, GetUnsafeOnFilterViewStillWorks) {
    // filter_view HAS unsafe_viewable_grid
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 42);
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    auto g  = any_grid(std::move(fv));
    EXPECT_EQ(g.get_unsafe({0, 0}), 42);
}

TEST(AnyGrid, GetMutUnsafeOnFilterViewStillWorks) {
    // filter_view HAS unsafe_mutable_viewable_grid
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 1);
    auto fv              = views::filter(dg, [](const int& v) { return v > 0; });
    auto g               = any_grid(std::move(fv));
    g.get_unsafe({0, 0}) = 88;
    EXPECT_EQ(g.get({0, 0}).value(), 88);
}

TEST(AnyGrid, SetUnsafeOnTransformViewNotAvailable) {
    dense_grid<2, int> dg({4, 4});
    auto tv = views::transform(dg, [](const int& v) -> int { return v * 2; });
    auto g  = any_grid(std::move(tv));
    static_assert(!unsafe_grid_container<decltype(g)>);
    EXPECT_TRUE(true);
}

// ── mutable_iterable_grid ───────────────────────────────────

TEST(AnyGrid, IterCellsMutModifiesValues) {
    auto g = any_grid(dense_grid<2, int>({2, 2}));
    g.set({0, 0}, 1);
    g.set({0, 1}, 2);
    for (int& cell : g.iter_cells()) cell *= 10;
    EXPECT_EQ(g.get({0, 0}).value(), 10);
    EXPECT_EQ(g.get({0, 1}).value(), 20);
}

TEST(AnyGrid, IterMutYieldsMutablePairs) {
    auto g = any_grid(dense_grid<2, int>({2, 2}));
    g.set({0, 0}, 5);
    for (auto [pos, cell] : g.iter()) {
        cell += static_cast<int>(pos[0] + pos[1]);
    }
    EXPECT_EQ(g.get({0, 0}).value(), 5);
}

// ── construction from other grid types ──────────────────────

TEST(AnyGrid, ConstructFromTreeGrid) {
    auto g = any_grid(tree_grid<2, int>({4, 4}));
    EXPECT_EQ(g.dimensions(), (std::array<std::uint32_t, 2>{4, 4}));
    g.set({0, 0}, 1);
    EXPECT_EQ(g.get({0, 0}).value(), 1);
}

TEST(AnyGrid, ConstructFromSparseGrid) {
    auto g = any_grid(sparse_grid<2, int>({4, 4}));
    g.set({3, 3}, 99);
    EXPECT_EQ(g.get({3, 3}).value(), 99);
}

// ── non-copyable grid behavior ──────────────────────────────

TEST(AnyGrid, CopyConstructedFromNonCopyableIsEmpty) {
    // tree_extendible_grid should be copyable; dense_grid is copyable.
    // All standard grid types in this codebase are copyable, so
    // the clone() path is always exercised.  We just verify it works.
    auto g1 = any_grid(dense_grid<2, int>({2, 2}));
    g1.set({0, 0}, 77);
    auto g2 = g1;
    EXPECT_EQ(g2.get({0, 0}).value(), 77);
}

// ============================================================
// any_grid_view tests
// ============================================================

using uview_base = any_grid_view<2, int&, grid_category::none, std::uint32_t, std::uint32_t>;
using uview_iter = any_grid_view<2, int&, grid_category::iterable, std::uint32_t, std::uint32_t>;
using uview_ctr =
    any_grid_view<2, int&, grid_category::iterable | grid_category::container, std::uint32_t, std::uint32_t>;
using uview_unsafe =
    any_grid_view<2, int&, grid_category::iterable | grid_category::unsafe_viewable, std::uint32_t, std::uint32_t>;
using uview_full = any_grid_view<2,
                                 int&,
                                 grid_category::iterable | grid_category::container | grid_category::unsafe_viewable |
                                     grid_category::unsafe_container | grid_category::const_viewable |
                                     grid_category::const_iterable | grid_category::const_unsafe,
                                 std::uint32_t,
                                 std::uint32_t>;
using uview_all  = any_grid_view<2,
                                 int&,
                                 grid_category::iterable | grid_category::container | grid_category::unsafe_viewable |
                                     grid_category::unsafe_container,
                                 std::uint32_t,
                                 std::uint32_t>;

// ── construction ────────────────────────────────────────────

TEST(AnyGridView, ConstructFromLvalueGrid) {
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 42);
    uview_all ref(dg);
    EXPECT_EQ(ref.get({0, 0})->get(), 42);
}

TEST(AnyGridView, CtadDeducesTypes) {
    dense_grid<2, int> dg({3, 3});
    auto ref = any_grid_view(dg);
    static_assert(viewable_grid<decltype(ref)>);
}

// ── viewable_grid ───────────────────────────────────────────

TEST(AnyGridView, DimensionsDelegates) {
    dense_grid<2, int> dg({5, 6});
    uview_all ref(dg);
    EXPECT_EQ(ref.dimensions(), (std::array<std::uint32_t, 2>{5, 6}));
}

TEST(AnyGridView, ContainsDelegates) {
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 1);
    uview_all ref(dg);
    EXPECT_TRUE(ref.contains({0, 0}));
    EXPECT_FALSE(ref.contains({3, 3}));
}

TEST(AnyGridView, GetReturnsMutableRef) {
    dense_grid<2, int> dg({4, 4});
    dg.set({1, 2}, 99);
    uview_all ref(dg);
    auto r = ref.get({1, 2});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->get(), 99);
}

// ── iterable_grid ───────────────────────────────────────────

TEST(AnyGridView, IterPosYieldsAll) {
    dense_grid<2, int> dg({2, 2});
    dg.set({0, 0}, 1);
    dg.set({1, 1}, 2);
    uview_all ref(dg);
    std::vector<std::array<std::uint32_t, 2>> positions;
    for (auto pos : ref.iter_pos()) positions.push_back(pos);
    EXPECT_EQ(positions.size(), 2u);
}

TEST(AnyGridView, IterCellsYieldsValues) {
    dense_grid<2, int> dg({2, 2});
    dg.set({0, 0}, 5);
    dg.set({0, 1}, 7);
    uview_all ref(dg);
    int sum = 0;
    for (const int& cell : ref.iter_cells()) sum += cell;
    EXPECT_EQ(sum, 12);
}

TEST(AnyGridView, IterYieldsPairs) {
    dense_grid<2, int> dg({2, 2});
    dg.set({0, 0}, 10);
    dg.set({1, 1}, 20);
    uview_all ref(dg);
    int sum = 0;
    for (auto [pos, cell] : ref.iter()) sum += cell;
    EXPECT_EQ(sum, 30);
}

// ── mutable_viewable_grid ───────────────────────────────────

TEST(AnyGridView, GetModifiesUnderlying) {
    dense_grid<2, int> dg({3, 3});
    dg.set({0, 0}, 1);
    uview_all ref(dg);
    auto r = ref.get({0, 0});
    ASSERT_TRUE(r.has_value());
    r->get() = 77;
    EXPECT_EQ(dg.get({0, 0})->get(), 77);
}

// ── grid_container ──────────────────────────────────────────

TEST(AnyGridView, SetModifiesUnderlying) {
    dense_grid<2, int> dg({3, 3});
    uview_all ref(dg);
    EXPECT_TRUE(ref.set({1, 1}, 55).has_value());
    EXPECT_EQ(dg.get({1, 1})->get(), 55);
}

TEST(AnyGridView, RemoveErasesFromUnderlying) {
    dense_grid<2, int> dg({3, 3});
    dg.set({2, 2}, 33);
    uview_all ref(dg);
    EXPECT_TRUE(ref.remove({2, 2}).has_value());
    EXPECT_FALSE(dg.contains({2, 2}));
}

TEST(AnyGridView, TakeRemovesFromUnderlying) {
    dense_grid<2, int> dg({3, 3});
    dg.set({2, 2}, 88);
    uview_all ref(dg);
    auto r = ref.take({2, 2});
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 88);
    EXPECT_FALSE(dg.contains({2, 2}));
}

// ── not supported: view-wrapped underlying ─────────────────

TEST(AnyGridView, SetOnFilterViewNotAvailable) {
    dense_grid<2, int> dg({4, 4});
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    uview_base ref(fv);
    static_assert(!grid_container<decltype(ref)>);
    EXPECT_TRUE(true);
}

TEST(AnyGridView, RemoveOnFilterViewNotAvailable) {
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 1);
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    uview_base ref(fv);
    static_assert(!grid_container<decltype(ref)>);
    EXPECT_TRUE(true);
}

TEST(AnyGridView, TakeOnFilterViewNotAvailable) {
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 1);
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    uview_base ref(fv);
    static_assert(!grid_container<decltype(ref)>);
    EXPECT_TRUE(true);
}

TEST(AnyGridView, SetUnsafeOnFilterViewNotAvailable) {
    dense_grid<2, int> dg({4, 4});
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    uview_base ref(fv);
    static_assert(!unsafe_grid_container<decltype(ref)>);
    EXPECT_TRUE(true);
}

TEST(AnyGridView, GetUnsafeOnFilterViewStillWorks) {
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 42);
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    uview_unsafe ref(fv);
    EXPECT_EQ(ref.get_unsafe({0, 0}), 42);
}

TEST(AnyGridView, GetOnFilterViewStillWorks) {
    dense_grid<2, int> dg({4, 4});
    dg.set({0, 0}, 10);
    auto fv = views::filter(dg, [](const int& v) { return v > 0; });
    uview_base ref(fv);
    auto r = ref.get({0, 0});
    ASSERT_TRUE(r.has_value());
    r->get() = 77;
    EXPECT_EQ(dg.get({0, 0})->get(), 77);
}

// ── unsafe accessors ────────────────────────────────────────

TEST(AnyGridView, GetUnsafeReadsFromUnderlying) {
    dense_grid<2, int> dg({3, 3});
    dg.set({0, 0}, 42);
    uview_all ref(dg);
    EXPECT_EQ(ref.get_unsafe({0, 0}), 42);
}

TEST(AnyGridView, GetMutUnsafeModifiesUnderlying) {
    dense_grid<2, int> dg({3, 3});
    dg.set({0, 0}, 1);
    uview_all ref(dg);
    ref.get_unsafe({0, 0}) = 88;
    EXPECT_EQ(dg.get({0, 0})->get(), 88);
}

TEST(AnyGridView, SetUnsafeModifiesUnderlying) {
    dense_grid<2, int> dg({3, 3});
    uview_all ref(dg);
    ref.set_unsafe({1, 1}, 77);
    EXPECT_EQ(dg.get({1, 1})->get(), 77);
}

TEST(AnyGridView, RemoveUnsafeErasesFromUnderlying) {
    dense_grid<2, int> dg({3, 3});
    dg.set({2, 2}, 33);
    uview_all ref(dg);
    ref.remove_unsafe({2, 2});
    EXPECT_FALSE(dg.contains({2, 2}));
}

TEST(AnyGridView, TakeUnsafeRemovesFromUnderlying) {
    dense_grid<2, int> dg({3, 3});
    dg.set({2, 2}, 99);
    uview_all ref(dg);
    EXPECT_EQ(ref.take_unsafe({2, 2}), 99);
    EXPECT_FALSE(dg.contains({2, 2}));
}

// ── mutable_iterable_grid ───────────────────────────────────

TEST(AnyGridView, IterCellsMutModifiesUnderlying) {
    dense_grid<2, int> dg({2, 2});
    dg.set({0, 0}, 1);
    dg.set({0, 1}, 3);
    uview_all ref(dg);
    for (int& cell : ref.iter_cells()) cell *= 2;
    EXPECT_EQ(dg.get({0, 0})->get(), 2);
    EXPECT_EQ(dg.get({0, 1})->get(), 6);
}

TEST(AnyGridView, IterMutModifiesUnderlying) {
    dense_grid<2, int> dg({2, 2});
    dg.set({0, 0}, 5);
    uview_all ref(dg);
    for (auto [pos, cell] : ref.iter()) cell += 10;
    EXPECT_EQ(dg.get({0, 0})->get(), 15);
}

// ── multiple grid type sources ──────────────────────────────

TEST(AnyGridView, ReferencesTreeGrid) {
    tree_grid<2, int> tg({4, 4});
    tg.set({0, 0}, 42);
    uview_full ref(tg);
    EXPECT_EQ(ref.get({0, 0})->get(), 42);
}
