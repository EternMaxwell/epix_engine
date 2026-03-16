module;

#include <gtest/gtest.h>

export module epix.core:tests.schedule;

import std;

import :schedule;

namespace {
struct Comp1 {
    int value = 0;
};

struct Comp2 {
    float value = 0.0f;
};

struct Comp3 {
    std::string value = "";
};

struct Comp4 {
    double value = 0.0;
};
}  // namespace
template <>
struct core::sparse_component<Comp4> : std::true_type {};

using namespace core;

namespace {
void spawn_entities(Commands commands) {
    static bool called = false;
    if (called) return;
    called = true;
    std::println(std::cout, "Spawning entities...");
    for (int i = 0; i < 20; i++) {
        auto entity = commands.spawn();
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dist(0.0f, 100.0f);
        std::uniform_int_distribution<int> coin(0, 1);
        if (coin(rng)) {
            entity.insert(Comp1{coin(rng) * 10});
        }
        if (coin(rng)) {
            entity.insert(Comp2{dist(rng)});
        }
        if (coin(rng)) {
            entity.insert(Comp3{"random_string" + std::to_string(dist(rng))});
        }
        if (coin(rng)) {
            entity.insert(Comp4{dist(rng) * 1.0});
        }
    }
}
void query_system1(Query<Item<Entity, EntityLocation, const Comp1&, const Comp3&>> query) {
    for (auto&& [entity, loc, comp1, comp3] : query.iter()) {
        std::println(std::cout, "System1 - Entity {}: archetype_id = {}, Comp1.value = {}, Comp3.value = {}",
                     entity.index, loc.archetype_id.get(), comp1.value, comp3.value);
    }
}
void query_system2(Query<Item<Entity, EntityLocation, const Comp2&, const Comp4&>> query) {
    for (auto&& [entity, loc, comp2, comp4] : query.iter()) {
        std::println(std::cout, "System2 - Entity {}: archetype_id = {}, Comp2.value = {}, Comp4.value = {}",
                     entity.index, loc.archetype_id.get(), comp2.value, comp4.value);
    }
}
}  // namespace

TEST(core, schedule) {
    World world(WorldId(1));
    {
        Schedule sched(0);

        // Create three named lambdas and reuse them so the SystemSetLabel constructed
        // from the same callable type/value will match.
        auto af = []() {};
        auto bf = []() {};
        auto cf = []() {};

        SetConfig sa = sets(af);
        SetConfig sb = sets(bf);
        SetConfig sc = sets(cf);

        sa.set_name("A");
        sb.set_name("B");
        sc.set_name("C");

        // Build config: C has parents A and B
        sc.in_set(af);
        sc.in_set(bf);

        // Add a dependency: A depends on B (A -> B)
        sa.after(bf);

        // Add configs to schedule (SetConfig is non-copyable, move it)
        sched.configure_sets(std::move(sa));
        sched.configure_sets(std::move(sb));
        sched.configure_sets(std::move(sc));

        // Prepare should detect parents with dependencies and return error
        auto res = sched.prepare(true);
        EXPECT_FALSE(res.has_value());
        auto err = res.error();
        EXPECT_EQ(err.type, SchedulePrepareError::Type::ParentsWithDeps);

        // Non-conflicting case: D has parents E and F with no deps between them
        Schedule sched2(1);
        auto ef      = []() {};
        auto ff      = []() {};
        auto df      = []() {};
        SetConfig se = sets(ef);
        SetConfig sf = sets(ff);
        SetConfig sd = sets(df);
        sd.in_set(ef);
        sd.in_set(ff);
        sched2.configure_sets(std::move(se));
        sched2.configure_sets(std::move(sf));
        sched2.configure_sets(std::move(sd));
        auto res2 = sched2.prepare(true);
        EXPECT_TRUE(res2.has_value());
    }

    // Now test execution
    Schedule exec_sched(0);

    exec_sched.add_systems(into(spawn_entities));
    exec_sched.add_systems(into(query_system1).after(spawn_entities));
    exec_sched.add_systems(into(query_system2).after(spawn_entities));

    auto pres = exec_sched.prepare(true);
    EXPECT_TRUE(pres.has_value());

    // Create dispatcher and execute
    auto registry2 = std::make_shared<TypeRegistry>();
    SystemDispatcher dispatcher(world, 2);
    exec_sched.initialize_systems(world);
    std::println(std::cout, "First execution:");
    exec_sched.execute(dispatcher);
    std::println(std::cout, "Since commands are deferred, spawned entities are not visible yet.");
    std::println(std::cout, "Second execution:");
    exec_sched.execute(dispatcher);
}
