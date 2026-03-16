module;

#include <gtest/gtest.h>

export module epix.core:tests.system;

import std;
import :system;
import :world;

namespace {
struct C1 {
    int v = 0;
};
struct C2 {
    float f = 0.0f;
};
struct C3 {
    double d = 0.0;
};
struct resource1 {
    int a   = 42;
    float b = 10.0f;
};
}  // namespace
template <>
struct core::sparse_component<C3> : std::true_type {};

using namespace core;

namespace {
void func2(Query<Item<Entity, Opt<const C1&>, Opt<const C2&>, Opt<const C3&>>> query, Res<resource1> res1) {
    // std::println(std::cout, "sys2 running");
    std::array<std::size_t, 3> counts = {0, 0, 0};
    for (auto&& [id, c1_opt, c2_opt, c3_opt] : query.iter()) {
        // opt of reference are of type std::optional<std::reference_wrapper<const T>>
        std::println(std::cout, "entity {}: C1: {}, C2: {}, C3: {}", id.index,
                     c1_opt.transform([](const C1& c1) { return std::to_string(c1.v); }).value_or("null"),
                     c2_opt.transform([](const C2& c2) { return std::to_string(c2.f); }).value_or("null"),
                     c3_opt.transform([](const C3& c3) { return std::to_string(c3.d); }).value_or("null"));
        counts[0] += static_cast<std::size_t>(c1_opt.has_value());
        counts[1] += static_cast<std::size_t>(c2_opt.has_value());
        counts[2] += static_cast<std::size_t>(c3_opt.has_value());
    }
    EXPECT_EQ(counts[0], 2);  // two entities with C1
    EXPECT_EQ(counts[1], 2);  // two entities with C2
    EXPECT_EQ(counts[2], 2);  // two entities with C3
    EXPECT_EQ(res1->a, 42);
    EXPECT_EQ(res1->b, 10.0f);
    // std::println(std::cout, "resource1: a = {}, b = {}", res1->a, res1->b);
}
}  // namespace

TEST(core, system) {
    auto registry = std::make_shared<TypeRegistry>();
    World world(WorldId(1), registry);

    int counter  = 0;
    auto lambda1 = [&](World& world) {
        // std::println(std::cout, "sys1 running");
        world.spawn().insert(C1{10}, C2{3.14f});
        world.spawn().insert(C1{20}, C3{2.718});
        world.spawn().insert(C2{1.618f}, C3{0.5772});
        world.init_resource<resource1>();
        ++counter;
        return 123;
    };
    // simple function system with an empty-tuple input and a return value
    auto sys  = make_system_unique(lambda1);
    auto sys2 = make_system_unique(func2);

    // std::println(std::cout, "system names:\n\t sys1: {}\n\t sys2: {}", sys->name(), sys2->name());
    // std::println(std::cout, "default sets:\n\t sys1: {}\n\t sys2: {}",
    //              sys->default_sets() | std::views::transform([](const SystemSetLabel& label) {
    //                  return std::format("(type: {}, extra: {})", label.type_index().name(), label.extra());
    //              }),
    //              sys2->default_sets() | std::views::transform([](const SystemSetLabel& label) {
    //                  return std::format("(type: {}, extra: {})", label.type_index().name(), label.extra());
    //              }));
    EXPECT_NE(sys->default_sets()[0], sys2->default_sets()[0]);

    // initialize should succeed
    auto access  = sys->initialize(world);
    auto access2 = sys2->initialize(world);
    // std::println(std::cout, "Access conflicts: {}", access.get_conflicts(access2).to_string());
    auto conflict_ids = access.get_conflicts(access2).ids.iter_ones() | std::ranges::to<std::vector<TypeId>>();
    auto expected_ids = std::vector<TypeId>{registry->type_id<C1>(), registry->type_id<C2>(), registry->type_id<C3>(),
                                            registry->type_id<resource1>()};
    std::ranges::sort(expected_ids);
    EXPECT_EQ(conflict_ids, expected_ids);

    // run should call the function and return value
    auto res  = sys->run({}, world);
    auto res2 = sys2->run({}, world);
    EXPECT_TRUE(res.has_value());
    EXPECT_EQ(res.value(), 123);
    EXPECT_EQ(counter, 1);
}
