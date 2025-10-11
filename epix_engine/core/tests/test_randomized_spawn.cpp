#include <cassert>
#include <iostream>
#include <random>
#include <tuple>

#include "epix/core/bundle.hpp"
#include "epix/core/bundleimpl.hpp"
#include "epix/core/type_system/type_registry.hpp"
#include "epix/core/world/entity_ref.hpp"
#include "epix/core/world_cell.hpp"

using namespace epix::core;
using namespace epix::core::archetype;

struct T1 {
    int v;
    T1(int x = 0) : v(x) {}
};
struct T2 {
    std::string s;
    T2(std::string_view sv = "") : s(sv) {}
};
struct T3 {
    double d;
    T3(double dd = 0) : d(dd) {}
};
struct S1 {
    static constexpr StorageType storage_type() { return StorageType::SparseSet; }
    std::string x;
    S1(std::string_view sv = "") : x(sv) {}
};
struct S2 {
    static constexpr StorageType storage_type() { return StorageType::SparseSet; }
    int n;
    S2(int v = 0) : n(v) {}
};

// small test World wrapper so we can create EntityRef / EntityRefMut
namespace epix::core {
struct World {
    World(std::shared_ptr<type_system::TypeRegistry> registry) : wc(WorldId(1), std::move(registry)) {}
    Components& components_mut() { return wc.components_mut(); }
    Storage& storage_mut() { return wc.storage_mut(); }
    Entities& entities_mut() { return wc.entities_mut(); }
    archetype::Archetypes& archetypes_mut() { return wc.archetypes_mut(); }
    Bundles& bundles_mut() { return wc.bundles_mut(); }
    const type_system::TypeRegistry& type_registry() const { return wc.type_registry(); }
    Tick change_tick() const { return wc.change_tick(); }
    Tick last_change_tick() const { return wc.last_change_tick(); }
    void flush() { wc.flush(); }

    template <bundle::is_bundle T>
    EntityRefMut spawn(T&& bundle) {
        auto e       = wc.entities_mut().alloc();
        auto spawner = BundleSpawner::create<T>(wc, change_tick());
        spawner.reserve_storage(1);
        spawner.spawn_non_exist(e, std::forward<T>(bundle));
        return EntityRefMut(e, &wc);
    }
    EntityRefMut spawn() {
        auto e = wc.entities_mut().reserve_entity();
        wc.flush_entities();
        return EntityRefMut(e, &wc);
    }

    std::optional<EntityRef> get_ref(Entity e) { return EntityRef(e, &wc); }
    std::optional<EntityRefMut> get_mut_ref(Entity e) { return EntityRefMut(e, &wc); }

   private:
    WorldCell wc;
};
}  // namespace epix::core

int main() {
    auto registry = std::make_shared<type_system::TypeRegistry>();
    epix::core::World world(registry);
    std::ios::sync_with_stdio(false);

    // bundles will be registered automatically by BundleSpawner::create<T>(world, tick)

    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    std::uniform_int_distribution<int> val_int(0, 1000);
    std::uniform_real_distribution<double> val_d(0.0, 1000.0);

    // randomly add components by iterating entities in WorldCell
    for (size_t idx = 0; idx < 2000; ++idx) {
        auto rmut = world.spawn();
        double p  = prob(rng);
        if (p < 0.4) {
            // add T1 via bundle
            rmut.insert_bundle(make_init_bundle<T1>(std::forward_as_tuple(val_int(rng))));
        }
        p = prob(rng);
        if (p < 0.35) {
            rmut.insert_bundle(make_init_bundle<T2>(std::forward_as_tuple(std::to_string(val_int(rng)))));
        }
        p = prob(rng);
        if (p < 0.25) {
            rmut.insert_bundle(make_init_bundle<T3>(std::forward_as_tuple(val_d(rng))));
        }
        p = prob(rng);
        if (p < 0.3) {
            rmut.insert_bundle(make_init_bundle<S1>(std::forward_as_tuple(std::to_string(val_int(rng)))));
        }
        p = prob(rng);
        if (p < 0.2) {
            rmut.insert_bundle(make_init_bundle<S2>(std::forward_as_tuple(val_int(rng))));
        }
    }

    // check archetype and table counts
    std::println(std::cout, "Total entities: {}", world.entities_mut().total_count());
    std::println(std::cout, "Total archetypes: {}", world.archetypes_mut().size());
    std::println(std::cout, "Total tables: {}", world.storage_mut().tables.table_count());
    for (size_t i = 0; i < world.archetypes_mut().size(); ++i) {
        auto& arch = world.archetypes_mut().get_mut(i).value().get();
        std::println(std::cout, "Archetype {}: size {}, table {}, table_components: {}, sparse_components: {}",
                     arch.id().get(), arch.size(), arch.table_id().get(),
                     arch.table_components() | std::views::transform([](auto i) { return i.get(); }),
                     arch.sparse_components() | std::views::transform([](auto i) { return i.get(); }));
    }
    for (size_t i = 0; i < world.storage_mut().tables.table_count(); ++i) {
        auto& table = world.storage_mut().tables.get_mut(TableId(i)).value().get();
        std::println(std::cout, "Table {}: size {}", i, table.size());
    }

    // verify and print subset
    for (size_t idx = 0; idx < world.entities_mut().total_count() && idx < 100; ++idx) {
        auto opt = world.entities_mut().resolve_index(static_cast<uint32_t>(idx));
        if (!opt) continue;
        Entity e = opt.value();
        if (!world.entities_mut().contains(e)) continue;
        auto r  = world.get_ref(e).value();
        auto a  = r.get<T1>();
        auto b  = r.get<T2>();
        auto c  = r.get<T3>();
        auto s1 = r.get<S1>();
        auto s2 = r.get<S2>();
        std::println(std::cout, "entity : (gen {}, idx {}), loc: (arch {}, arch_idx {}, table {}, table_idx {})",
                     e.generation, e.index, r.location().archetype_id.get(), r.location().archetype_idx.get(),
                     r.location().table_id.get(), r.location().table_idx.get());
        std::println(std::cout, "\tT1: {}, T2: \'{}\', T3: {}, S1: \'{}\', S2: {}",
                     a ? std::to_string(a->get().v) : "-", b ? b->get().s : "-", c ? std::to_string(c->get().d) : "-",
                     s1 ? s1->get().x : "-", s2 ? std::to_string(s2->get().n) : "-");
    }

    // basic consistency checks: all entity refs that claim to contain component should be able to retrieve it via
    // get_mut
    for (size_t idx = 0; idx < world.entities_mut().total_count(); ++idx) {
        auto opt = world.entities_mut().resolve_index(static_cast<uint32_t>(idx));
        if (!opt) continue;
        Entity e = opt.value();
        if (!world.entities_mut().contains(e)) continue;
        auto ref = world.get_ref(e).value();
        if (ref.contains<T1>()) {
            auto mut = ref.get_ref<T1>();
            assert(mut.has_value());
        }
        if (ref.contains<T2>()) {
            auto mut = ref.get_ref<T2>();
            assert(mut.has_value());
        }
        if (ref.contains<T3>()) {
            auto mut = ref.get_ref<T3>();
            assert(mut.has_value());
        }
        if (ref.contains<S1>()) {
            auto mut = ref.get_ref<S1>();
            assert(mut.has_value());
        }
        if (ref.contains<S2>()) {
            auto mut = ref.get_ref<S2>();
            assert(mut.has_value());
        }
    }

    std::cout << "test_randomized_spawn passed\n";
    return 0;
}
