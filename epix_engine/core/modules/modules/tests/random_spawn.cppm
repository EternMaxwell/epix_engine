module;

#include <gtest/gtest.h>

export module epix.core:tests.random_spawn;

import std;
import :world;
import :query;
import :bundle;

namespace {
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
    std::string x;
    S1(std::string_view sv = "") : x(sv) {}
};
struct S2 {
    int n;
    S2(int v = 0) : n(v) {}
};
}  // namespace

template <>
struct core::sparse_component<S1> : std::true_type {};
template <>
struct core::sparse_component<S2> : std::true_type {};

TEST(core, random_spawn) {
    using namespace core;

    auto registry = std::make_shared<TypeRegistry>();
    World world(0, registry);
    std::ios::sync_with_stdio(false);

    // bundles will be registered automatically by BundleSpawner::create<T>(world, tick)

    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    std::uniform_int_distribution<int> val_int(0, 1000);
    std::uniform_real_distribution<double> val_d(0.0, 1000.0);

    // randomly add components by iterating entities in World
    for (size_t idx = 0; idx < 2000; ++idx) {
        auto rmut = world.spawn();
        double p  = prob(rng);
        if (p < 0.4) {
            // add T1 via bundle
            rmut.insert_bundle(make_bundle<T1>(std::forward_as_tuple(val_int(rng))));
        }
        p = prob(rng);
        if (p < 0.35) {
            rmut.insert_bundle(make_bundle<T2>(std::forward_as_tuple(std::to_string(val_int(rng)))));
        }
        p = prob(rng);
        if (p < 0.25) {
            rmut.insert_bundle(make_bundle<T3>(std::forward_as_tuple(val_d(rng))));
        }
        p = prob(rng);
        if (p < 0.3) {
            rmut.insert_bundle(make_bundle<S1>(std::forward_as_tuple(std::to_string(val_int(rng)))));
        }
        p = prob(rng);
        if (p < 0.2) {
            rmut.insert_bundle(make_bundle<S2>(std::forward_as_tuple(val_int(rng))));
        }
    }

    // type id and type names
    // std::println(std::cout, "type ids: {}",
    //              std::views::iota(0u, registry->count()) | std::views::transform([&](uint32_t i) {
    //                  return std::format("({}, {})", i, registry->type_index(i).short_name());
    //              }));

    // check archetype and table counts
    std::println(std::cout, "Total: entities={}, archetypes={}, tables={}", world.entities_mut().total_count(),
                 world.archetypes_mut().size(), world.storage_mut().tables.table_count());
    // for (size_t i = 0; i < world.archetypes_mut().size(); ++i) {
    //     auto& arch = world.archetypes_mut().get_mut(i).value().get();
    //     std::println(std::cout, "Archetype {}: size {}, table {}, table_components: {}, sparse_components: {}",
    //                  arch.id().get(), arch.size(), arch.table_id().get(),
    //                  arch.table_components() | std::views::transform([](auto i) { return i.get(); }),
    //                  arch.sparse_components() | std::views::transform([](auto i) { return i.get(); }));
    // }
    std::println(std::cout, "Archetypes:");
    std::println(std::cout, "\ttable_ids:{}",
                 std::views::iota(0u, world.archetypes_mut().size()) | std::views::transform([&](uint32_t ai) {
                     auto&& archetype = world.archetypes_mut().get_mut(ai).value().get();
                     return std::format("{}:{}", ai, archetype.table_id().get());
                 }));
    std::println(std::cout, "\tsizes:{}",
                 std::views::iota(0u, world.archetypes_mut().size()) | std::views::transform([&](uint32_t ai) {
                     auto&& archetype = world.archetypes_mut().get_mut(ai).value().get();
                     return std::format("{}:{}", ai, archetype.size());
                 }));
    std::println(
        std::cout, "Table sizes -> {}",
        std::views::iota(0u, world.storage_mut().tables.table_count()) | std::views::transform([&](uint32_t ti) {
            auto& t = world.storage_mut().tables.get_mut(TableId(ti)).value().get();
            return std::format("{:02}:->{:03}", ti, t.size());
        }));

    // verify and print subset
    for (size_t idx = 0; idx < world.entities_mut().total_count() && idx < 10; ++idx) {
        auto opt = world.entities_mut().resolve_index(static_cast<uint32_t>(idx));
        EXPECT_TRUE(opt.has_value());
        Entity e = opt.value();
        EXPECT_TRUE(world.entities_mut().contains(e));
        auto r  = world.get_entity(e).value();
        auto a  = r.get<T1>();
        auto b  = r.get<T2>();
        auto c  = r.get<T3>();
        auto s1 = r.get<S1>();
        auto s2 = r.get<S2>();
        std::println(std::cout,
                     "entity:(g={:02},i={:02}),loc:(arch={:02},arch_idx={:02},table={:02},table_idx={:02}) -> T1: {}, "
                     "T2: \'{}\', T3: {}, S1: \'{}\', S2: {}",
                     e.generation, e.index, r.location().archetype_id.get(), r.location().archetype_idx.get(),
                     r.location().table_id.get(), r.location().table_idx.get(), a ? std::to_string(a->get().v) : "-",
                     b ? b->get().s : "-", c ? std::to_string(c->get().d) : "-", s1 ? s1->get().x : "-",
                     s2 ? std::to_string(s2->get().n) : "-");
    }

    // basic consistency checks: all entity refs that claim to contain component should be able to retrieve it via
    // get_mut
    for (size_t idx = 0; idx < world.entities_mut().total_count(); ++idx) {
        auto opt = world.entities_mut().resolve_index(static_cast<uint32_t>(idx));
        if (!opt) continue;
        Entity e = opt.value();
        if (!world.entities_mut().contains(e)) continue;
        auto ref = world.get_entity(e).value();
        if (ref.contains<T1>()) {
            auto mut = ref.get_ref<T1>();
            EXPECT_TRUE(mut.has_value());
        }
        if (ref.contains<T2>()) {
            auto mut = ref.get_ref<T2>();
            EXPECT_TRUE(mut.has_value());
        }
        if (ref.contains<T3>()) {
            auto mut = ref.get_ref<T3>();
            EXPECT_TRUE(mut.has_value());
        }
        if (ref.contains<S1>()) {
            auto mut = ref.get_ref<S1>();
            EXPECT_TRUE(mut.has_value());
        }
        if (ref.contains<S2>()) {
            auto mut = ref.get_ref<S2>();
            EXPECT_TRUE(mut.has_value());
        }
    }
}
