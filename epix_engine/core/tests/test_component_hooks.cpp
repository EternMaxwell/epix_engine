#include <cassert>
#include <iostream>
#include <random>

#include "epix/core/bundleimpl.hpp"
#include "epix/core/component.hpp"
#include "epix/core/type_system/type_registry.hpp"
#include "epix/core/world.hpp"
#include "epix/core/world/entity_ref.hpp"

using namespace epix::core;
using namespace epix::core::bundle;

// Simple components with static hooks that increment counters in the world via world->components_mut().
struct C1 {
    int v;
    C1(int x) : v(x) {}
    static inline int inserted  = 0;
    static inline int removed   = 0;
    static inline int replaced  = 0;
    static inline int added     = 0;
    static inline int despawned = 0;
    static void on_insert(World& w, HookContext ctx) { ++inserted; }
    static void on_remove(World& w, HookContext ctx) { ++removed; }
    static void on_add(World& w, HookContext ctx) { ++added; }
    static void on_replace(World& w, HookContext ctx) { ++replaced; }
    static void on_despawn(World& w, HookContext ctx) { ++despawned; }
};
struct C2 {
    float f;
    C2(float x) : f(x) {}
    static inline int inserted  = 0;
    static inline int removed   = 0;
    static inline int replaced  = 0;
    static inline int added     = 0;
    static inline int despawned = 0;
    static void on_insert(World& w, HookContext ctx) { ++inserted; }
    static void on_remove(World& w, HookContext ctx) { ++removed; }
    static void on_add(World& w, HookContext ctx) { ++added; }
    static void on_replace(World& w, HookContext ctx) { ++replaced; }
    static void on_despawn(World& w, HookContext ctx) { ++despawned; }
};

int main() {
    auto registry = std::make_shared<type_system::TypeRegistry>();
    World world(WorldId(1), std::move(registry));

    // We'll use the same random engine as existing randomized tests.
    std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    std::bernoulli_distribution coin(0.5);

    const int N = 200;

    // Track expected counts
    int expected_inserted_c1  = 0;
    int expected_removed_c1   = 0;
    int expected_replaced_c1  = 0;
    int expected_added_c1     = 0;
    int expected_despawned_c1 = 0;
    int expected_inserted_c2  = 0;
    int expected_removed_c2   = 0;
    int expected_replaced_c2  = 0;
    int expected_added_c2     = 0;
    int expected_despawned_c2 = 0;

    // Randomized sequence: for each entity, randomly spawn some components (via bundles)
    // spawn entities and randomly add components via World::spawn
    std::vector<Entity> spawned_entities;
    for (int i = 0; i < N; ++i) {
        auto rmut = world.spawn();
        spawned_entities.push_back(rmut.id());
        bool add1 = coin(rng);
        bool add2 = coin(rng);
        if (add1 && add2) {
            rmut.insert_bundle(make_init_bundle<C1, C2>(std::make_tuple(1), std::make_tuple(2.0f)));
            expected_inserted_c1 += 1;
            expected_inserted_c2 += 1;
            expected_added_c1 += 1;
            expected_added_c2 += 1;
        } else if (add1) {
            rmut.insert_bundle(make_init_bundle<C1>(std::make_tuple(1)));
            expected_inserted_c1 += 1;
            expected_added_c1 += 1;
        } else if (add2) {
            rmut.insert_bundle(make_init_bundle<C2>(std::make_tuple(2.0f)));
            expected_inserted_c2 += 1;
            expected_added_c2 += 1;
        }
    }

    // randomly replace or add new.
    for (auto e : spawned_entities) {
        bool rep1      = coin(rng);
        bool rep2      = coin(rng);
        auto maybe_mut = world.get_entity_mut(e);
        if (!maybe_mut) continue;
        auto mut       = *maybe_mut;
        bool replaced1 = mut.contains<C1>() && rep1;
        bool replaced2 = mut.contains<C2>() && rep2;
        if (replaced1 && replaced2) {
            mut.insert_bundle(make_init_bundle<C1, C2>(std::make_tuple(3), std::make_tuple(4.0f)));
            expected_replaced_c1 += 1;
            expected_replaced_c2 += 1;
            expected_inserted_c1 += 1;
            expected_inserted_c2 += 1;
        } else if (replaced1) {
            mut.insert_bundle(make_init_bundle<C1>(std::make_tuple(3)));
            expected_replaced_c1 += 1;
            expected_inserted_c1 += 1;
        } else if (replaced2) {
            mut.insert_bundle(make_init_bundle<C2>(std::make_tuple(4.0f)));
            expected_replaced_c2 += 1;
            expected_inserted_c2 += 1;
        }
    }

    // Now randomly remove some components from existing entities
    for (auto e : spawned_entities) {
        bool rem1      = coin(rng);
        bool rem2      = coin(rng);
        auto maybe_mut = world.get_entity_mut(e);
        if (!maybe_mut) continue;
        auto mut = *maybe_mut;
        if (rem1 && mut.contains<C1>()) {
            mut.remove<C1>();
            expected_removed_c1 += 1;
        }
        if (rem2 && mut.contains<C2>()) {
            mut.remove<C2>();
            expected_removed_c2 += 1;
        }
    }

    // Finally randomly despawn some entities
    for (auto e : spawned_entities) {
        bool despawn   = coin(rng);
        auto maybe_mut = world.get_entity_mut(e);
        if (!maybe_mut) continue;
        auto mut = *maybe_mut;
        if (despawn) {
            if (mut.contains<C1>()) {
                expected_despawned_c1 += 1;
                expected_removed_c1 += 1;  // despawn also triggers remove
            }
            if (mut.contains<C2>()) {
                expected_despawned_c2 += 1;
                expected_removed_c2 += 1;  // despawn also triggers remove
            }
            mut.despawn();
        }
    }

    std::println(std::cout,
                 "Expected:\n\tC1: inserted {}, removed {}, replaced {}, added {}, despawned {}\n\tC2: inserted {}, "
                 "removed {}, replaced {}, added {}, despawned {}",
                 expected_inserted_c1, expected_removed_c1, expected_replaced_c1, expected_added_c1,
                 expected_despawned_c1, expected_inserted_c2, expected_removed_c2, expected_replaced_c2,
                 expected_added_c2, expected_despawned_c2);
    std::println(std::cout,
                 "Actual:\n\tC1: inserted {}, removed {}, replaced {}, added {}, despawned {}\n\tC2: inserted {}, "
                 "removed {}, replaced {}, added {}, despawned {}",
                 C1::inserted, C1::removed, C1::replaced, C1::added, C1::despawned, C2::inserted, C2::removed,
                 C2::replaced, C2::added, C2::despawned);

    // Compare hook counters
    if (C1::inserted != expected_inserted_c1) {
        std::cerr << "C1 inserted mismatch: " << C1::inserted << " vs " << expected_inserted_c1 << "\n";
        return 2;
    }
    if (C1::removed != expected_removed_c1) {
        std::cerr << "C1 removed mismatch: " << C1::removed << " vs " << expected_removed_c1 << "\n";
        return 3;
    }
    if (C2::inserted != expected_inserted_c2) {
        std::cerr << "C2 inserted mismatch: " << C2::inserted << " vs " << expected_inserted_c2 << "\n";
        return 4;
    }
    if (C2::removed != expected_removed_c2) {
        std::cerr << "C2 removed mismatch: " << C2::removed << " vs " << expected_removed_c2 << "\n";
        return 5;
    }
    if (C1::replaced != expected_replaced_c1) {
        std::cerr << "C1 replaced mismatch: " << C1::replaced << " vs " << expected_replaced_c1 << "\n";
        return 6;
    }
    if (C2::replaced != expected_replaced_c2) {
        std::cerr << "C2 replaced mismatch: " << C2::replaced << " vs " << expected_replaced_c2 << "\n";
        return 7;
    }
    if (C1::added != expected_added_c1) {
        std::cerr << "C1 added mismatch: " << C1::added << " vs " << expected_added_c1 << "\n";
        return 8;
    }
    if (C2::added != expected_added_c2) {
        std::cerr << "C2 added mismatch: " << C2::added << " vs " << expected_added_c2 << "\n";
        return 9;
    }
    if (C1::despawned != expected_despawned_c1) {
        std::cerr << "C1 despawned mismatch: " << C1::despawned << " vs " << expected_despawned_c1 << "\n";
        return 10;
    }
    if (C2::despawned != expected_despawned_c2) {
        std::cerr << "C2 despawned mismatch: " << C2::despawned << " vs " << expected_despawned_c2 << "\n";
        return 11;
    }

    std::cout << "test_component_hooks passed\n";
    return 0;
}
