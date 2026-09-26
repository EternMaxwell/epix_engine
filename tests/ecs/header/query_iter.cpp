#include <gtest/gtest.h>

#include <algorithm>
#include <epix/ecs.hpp>
#include <ranges>
#include <string>

namespace {
struct P {
    int a;
    P(int v) : a(v) {}
};

struct ArchetypalProbe {
    static inline std::size_t filter_calls = 0;
};

struct DynamicProbe {
    static inline std::size_t filter_calls = 0;
};
}  // namespace

namespace epix::ecs {
template <typename Probe>
struct ProbeWorldQuery {
    struct Fetch {};
    using State = std::tuple<>;

    static Fetch init_fetch(World&, const State&, Tick, Tick) noexcept { return {}; }
    static void set_archetype(Fetch&, const State&, const Archetype&, Table&) noexcept {}
    static void set_access(State&, const FilteredAccess&) noexcept {}
    static void update_access(const State&, FilteredAccess&) noexcept {}
    static State init_state(World&) noexcept { return {}; }
    static std::optional<State> get_state(const Components&) noexcept { return State{}; }
    static bool matches_component_set(const State&, internal::contains_component_fn auto&&) noexcept { return true; }
};

template <>
struct WorldQuery<ArchetypalProbe> : ProbeWorldQuery<ArchetypalProbe> {};
template <>
struct QueryFilter<ArchetypalProbe> {
    static constexpr bool archetypal = true;
    static bool filter_fetch(WorldQuery<ArchetypalProbe>::Fetch&, Entity, TableRow) noexcept {
        ++ArchetypalProbe::filter_calls;
        return true;
    }
};

template <>
struct WorldQuery<DynamicProbe> : ProbeWorldQuery<DynamicProbe> {};
template <>
struct QueryFilter<DynamicProbe> {
    static constexpr bool archetypal = false;
    static bool filter_fetch(WorldQuery<DynamicProbe>::Fetch&, Entity entity, TableRow) noexcept {
        ++DynamicProbe::filter_calls;
        return entity.index % 2 == 0;
    }
};
}  // namespace epix::ecs

using ArchetypalQueryRange = epix::ecs::QueryIter<epix::ecs::Entity, ArchetypalProbe>;
using DynamicQueryRange    = epix::ecs::QueryIter<epix::ecs::Entity, DynamicProbe>;
using MutableQueryRange    = epix::ecs::QueryIter<epix::ecs::Mut<P>, epix::ecs::Filter<>>;
static_assert(std::ranges::view<ArchetypalQueryRange>);
static_assert(std::ranges::borrowed_range<ArchetypalQueryRange>);
static_assert(std::ranges::input_range<ArchetypalQueryRange>);
static_assert(std::ranges::input_range<const ArchetypalQueryRange>);
static_assert(std::ranges::sized_range<ArchetypalQueryRange>);
static_assert(!std::ranges::common_range<ArchetypalQueryRange>);
static_assert(std::ranges::input_range<DynamicQueryRange>);
static_assert(!std::ranges::sized_range<DynamicQueryRange>);
static_assert(std::ranges::input_range<MutableQueryRange>);
static_assert(!std::ranges::range<const MutableQueryRange>);

TEST(ecs, query_iter) {
    using namespace epix::ecs;

    World wc(0);

    // spawn a couple entities with P
    for (int i = 0; i < 5; ++i) {
        wc.spawn(make_bundle<P>(std::forward_as_tuple(i)));
        wc.spawn(make_bundle<std::string, P>(std::forward_as_tuple("entity"), std::forward_as_tuple(i)));
        wc.spawn(make_bundle<int>(std::forward_as_tuple(i)));
    }
    wc.flush();

    // Create QueryState for Ref<P>
    auto qs0 = wc.query<Entity>();
    auto qs1 = wc.query_filtered<Item<Entity, Opt<Mut<std::string>>>, Filter<With<P>, Without<int>>>();
    auto qs2 = wc.query<Mut<std::string>>();
    auto qs3 = wc.query_filtered<Item<Entity, Mut<P>>, Without<std::string>>();

    EXPECT_EQ(std::ranges::distance(qs0.iter(wc)), 15);
    EXPECT_EQ(std::ranges::distance(qs1.iter(wc)), 10);
    EXPECT_EQ(std::ranges::distance(qs2.iter(wc)), 5);
    EXPECT_EQ(std::ranges::distance(qs3.iter(wc)), 5);
}

TEST(ecs, query_iter_ranges_and_archetypal_fast_path) {
    using namespace epix::ecs;

    World world(0);
    for (int i = 0; i < 5; ++i) {
        world.spawn(make_bundle<P>(std::forward_as_tuple(i)));
        world.spawn(make_bundle<std::string, P>(std::forward_as_tuple("entity"), std::forward_as_tuple(i)));
        world.spawn(make_bundle<int>(std::forward_as_tuple(i)));
    }
    world.flush();

    auto archetypal = world.query_filtered<Entity, ArchetypalProbe>();
    auto dynamic    = world.query_filtered<Entity, DynamicProbe>();

    ArchetypalProbe::filter_calls = 0;
    auto archetypal_range         = archetypal.iter(world);
    EXPECT_EQ(archetypal_range.size(), 15u);
    EXPECT_EQ(archetypal_range.max_remaining(), 15u);
    auto cursor = archetypal_range.begin();
    ASSERT_NE(cursor, archetypal_range.end());
    EXPECT_EQ(cursor.max_remaining(), 15u);
    auto independent_cursor = archetypal_range.begin();
    EXPECT_EQ(*cursor, *independent_cursor);
    ++cursor;
    EXPECT_NE(*cursor, *independent_cursor);
    EXPECT_EQ(cursor.max_remaining(), 14u);
    EXPECT_EQ(std::ranges::distance(archetypal_range), 15);
    EXPECT_EQ(
        std::ranges::distance(archetypal_range | std::views::transform([](Entity entity) { return entity.index; })),
        15);
    EXPECT_EQ(ArchetypalProbe::filter_calls, 0u);

    DynamicProbe::filter_calls = 0;
    auto dynamic_range         = dynamic.iter(world);
    EXPECT_EQ(dynamic_range.max_remaining(), 15u);
    EXPECT_EQ(std::ranges::distance(dynamic_range), 8);
    EXPECT_EQ(DynamicProbe::filter_calls, 15u);
}
