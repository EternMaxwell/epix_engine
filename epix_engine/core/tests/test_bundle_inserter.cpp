#include <cassert>
#include <iostream>

#include "epix/core/bundleimpl.hpp"
#include "epix/core/component.hpp"
#include "epix/core/entities.hpp"
#include "epix/core/storage.hpp"
#include "epix/core/type_system/type_registry.hpp"
#include "epix/core/world.hpp"

using namespace epix::core;
using namespace epix::core::archetype;

struct X {
    int v;
    X(int vv) : v(vv) {}
};
struct Y {
    std::string s;
    Y(std::string_view sv) : s(sv) {}
};
struct Z {
    static constexpr StorageType storage_type() { return StorageType::SparseSet; }
    double d;
    Z(double dd) : d(dd) {}
};
struct W {
    static constexpr StorageType storage_type() { return StorageType::SparseSet; }
    std::string s;
    W(std::string_view sv) : s(sv) {}
};

int main() {
    auto registry = std::make_shared<type_system::TypeRegistry>();
    World world(WorldId(1), registry);

    // register component infos
    TypeId tx = registry->type_id<X>();
    TypeId ty = registry->type_id<Y>();
    world.components_mut().register_info<X>();
    world.components_mut().register_info<Y>();

    // create a bundle type and register
    using MyBundle = InitializeBundle<std::tuple<X, Y>, std::tuple<std::tuple<int>, std::tuple<std::string_view>>>;
    auto spawner   = BundleSpawner::create<MyBundle>(world, 123);

    // reserve and spawn an entity
    Entity e = world.entities_mut().alloc();
    spawner.reserve_storage(1);
    auto loc =
        spawner.spawn_non_exist(e, make_init_bundle<X, Y>(std::forward_as_tuple(11), std::forward_as_tuple("hi")));
    auto table_id_before = loc.table_id;
    std::println(std::cout, "entity : (gen {}, idx {}), loc: (arch {}, arch_idx {}, table {}, table_idx {})",
                 e.generation, e.index, loc.archetype_id.get(), loc.archetype_idx.get(), loc.table_id.get(),
                 loc.table_idx.get());
    // check data
    {
        auto& table = world.storage_mut().tables.get_mut(loc.table_id).value().get();
        auto& x     = table.get_dense(tx).value().get().get_as<X>(loc.table_idx).value().get();
        auto& y     = table.get_dense(ty).value().get().get_as<Y>(loc.table_idx).value().get();
        std::println(std::cout, "X.v = {}, Y.s = {}", x.v, y.s);
        assert(x.v == 11);
        assert(y.s == "hi");
    }

    auto inserter = BundleInserter::create<
        InitializeBundle<std::tuple<Z, W>, std::tuple<std::tuple<double>, std::tuple<std::string_view>>>>(
        world, loc.archetype_id, 123);
    loc = inserter.insert(e, loc, make_init_bundle<Z, W>(std::forward_as_tuple(3.14), std::forward_as_tuple("hello")),
                          true);
    std::println(std::cout, "after insert bundle, loc: (arch {}, arch_idx {}, table {}, table_idx {})",
                 loc.archetype_id.get(), loc.archetype_idx.get(), loc.table_id.get(), loc.table_idx.get());
    // check data
    {
        auto& table2 = world.storage_mut().tables.get_mut(loc.table_id).value().get();
        auto& x2     = table2.get_dense(registry->type_id<X>()).value().get().get_as<X>(loc.table_idx).value().get();
        auto& y2     = table2.get_dense(registry->type_id<Y>()).value().get().get_as<Y>(loc.table_idx).value().get();
        auto& z2 =
            world.storage_mut().sparse_sets.get_mut(registry->type_id<Z>()).value().get().get_as<Z>(e).value().get();
        auto& w2 =
            world.storage_mut().sparse_sets.get_mut(registry->type_id<W>()).value().get().get_as<W>(e).value().get();
        std::println(std::cout, "X.v = {}, Y.s = {}, Z.d = {}, W.s = {}", x2.v, y2.s, z2.d, w2.s);
        assert(x2.v == 11);
        assert(y2.s == "hi");
        assert(z2.d == 3.14);
        assert(w2.s == "hello");
        // auto& table = storage.tables.get_mut(table_id_before).value().get();
        // assert(table.size() == 0);  // moved out
    }

    std::cout << "test_bundle_inserter passed\n";
    return 0;
}
