#include <cassert>
#include <iostream>
#include <ostream>
#include <random>
#include <thread>

#include "epix/core/query/fetch.hpp"
#include "epix/core/schedule/schedule.hpp"
#include "epix/core/system/commands.hpp"
#include "epix/core/type_system/type_registry.hpp"
#include "epix/core/world.hpp"

using namespace epix::core;
using namespace epix::core::schedule;
using namespace epix::core::system;

// test components
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
template <>
struct epix::core::sparse_component<Comp4> : std::true_type {};

void spawn_entities(epix::core::system::Commands commands) {
    static bool called = false;
    if (called) return;
    called = true;
    std::println(std::cout, "Spawning entities...");
    for (int i = 0; i < 100; i++) {
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
void query_system1(query::Query<query::Item<Entity, EntityLocation, const Comp1&, const Comp3&>> query) {
    for (auto&& [entity, loc, comp1, comp3] : query.iter()) {
        std::println(std::cout, "System1 - Entity {}: archetype_id = {}, Comp1.value = {}, Comp3.value = {}",
                     entity.index, loc.archetype_id.get(), comp1.value, comp3.value);
    }
}
void query_system2(query::Query<query::Item<Entity, EntityLocation, const Comp2&, const Comp4&>> query) {
    for (auto&& [entity, loc, comp2, comp4] : query.iter()) {
        std::println(std::cout, "System2 - Entity {}: archetype_id = {}, Comp2.value = {}, Comp4.value = {}",
                     entity.index, loc.archetype_id.get(), comp2.value, comp4.value);
    }
}

int main() {
    World world(WorldId(1));
    {
        Schedule sched(0);

        // Create three named lambdas and reuse them so the SystemSetLabel constructed
        // from the same callable type/value will match.
        auto af = []() {};
        auto bf = []() {};
        auto cf = []() {};

        SetConfig sa = make_sets(af);
        SetConfig sb = make_sets(bf);
        SetConfig sc = make_sets(cf);

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
        assert(!res.has_value());
        auto err = res.error();
        assert(err.type == SchedulePrepareError::Type::ParentsWithDeps);
        std::cout << "test_schedule (conflict) passed\n";

        // Non-conflicting case: D has parents E and F with no deps between them
        Schedule sched2(1);
        auto ef      = []() {};
        auto ff      = []() {};
        auto df      = []() {};
        SetConfig se = make_sets(ef);
        SetConfig sf = make_sets(ff);
        SetConfig sd = make_sets(df);
        sd.in_set(ef);
        sd.in_set(ff);
        sched2.configure_sets(std::move(se));
        sched2.configure_sets(std::move(sf));
        sched2.configure_sets(std::move(sd));
        auto res2 = sched2.prepare(true);
        assert(res2.has_value());
        std::cout << "test_schedule (no conflict) passed\n";
    }

    // Now test execution
    Schedule exec_sched(0);

    exec_sched.add_systems(epix::core::into(spawn_entities));
    exec_sched.add_systems(epix::core::into(query_system1).after(spawn_entities));
    exec_sched.add_systems(epix::core::into(query_system2).after(spawn_entities));

    auto pres = exec_sched.prepare(true);
    assert(pres.has_value());

    // Create dispatcher and execute
    auto registry2 = std::make_shared<type_system::TypeRegistry>();
    schedule::SystemDispatcher dispatcher(world, 2);
    exec_sched.initialize_systems(world);
    std::println(std::cout, "First execution:");
    exec_sched.execute(dispatcher);
    std::println(std::cout, "Second execution:");
    exec_sched.execute(dispatcher);

    std::cout << "test_schedule execution order passed\n";

    return 0;
}
