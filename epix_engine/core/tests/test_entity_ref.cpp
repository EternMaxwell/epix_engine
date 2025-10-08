#include <cassert>
#include <iostream>

#include "epix/core/bundle.hpp"
#include "epix/core/component.hpp"
#include "epix/core/entities.hpp"
#include "epix/core/storage.hpp"
#include "epix/core/world_cell.hpp"
// test-only helper: make_type_id_array is expected by EntityRef::remove; provide a small implementation
// before including entity_ref so the header can see it in this translation unit.
namespace epix::core {
namespace test_internal {
const type_system::TypeRegistry* __registry_for_make_type_id_array = nullptr;
}
template <typename... Ts>
inline std::array<type_system::TypeId, sizeof...(Ts)> make_type_id_array() {
    const auto* reg = test_internal::__registry_for_make_type_id_array;
    return {reg->template type_id<Ts>()...};
}
}  // namespace epix::core

#include "epix/core/type_system/type_registry.hpp"
#include "epix/core/world/entity_ref.hpp"

using namespace epix::core;
using namespace epix::core::archetype;

struct A {
    int x;
    A(int v) : x(v) {}
};
struct B {
    std::string s;
    B(std::string_view sv) : s(sv) {}
};
struct C {
    int n;
    C(int v) : n(v) {}
};
struct D {
    static constexpr StorageType storage_type() { return StorageType::SparseSet; }
    std::string s;
    D(std::string_view sv) : s(sv) {}
};

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
        Entity e     = wc.entities_mut().alloc();
        auto spawner = BundleSpawner::create<T>(wc, change_tick());
        // spawner.reserve_storage(1);
        spawner.spawn_non_exist(e, std::forward<T>(bundle));
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
    // set test registry pointer used by make_type_id_array
    epix::core::test_internal::__registry_for_make_type_id_array = registry.get();
    epix::core::World world(registry);

    // register components
    // TypeId ta = registry->type_id<A>();
    // TypeId tb = registry->type_id<B>();
    // TypeId tc = registry->type_id<C>();
    // TypeId td = registry->type_id<D>();
    // world.components_mut().emplace(ta, ComponentInfo(ta, ComponentDesc::from_type<A>()));
    // world.components_mut().emplace(tb, ComponentInfo(tb, ComponentDesc::from_type<B>()));
    // world.components_mut().emplace(tc, ComponentInfo(tc, ComponentDesc::from_type<C>()));
    // world.components_mut().emplace(td, ComponentInfo(td, ComponentDesc::from_type<D>()));

    // spawn an entity with A and B
    auto e = world.spawn(make_init_bundle<A, B>(std::forward_as_tuple(5), std::forward_as_tuple("hello"))).id();

    // read via EntityRef
    auto r  = world.get_ref(e).value();
    auto aa = r.get<A>();
    auto bb = r.get<B>();
    assert(aa.has_value());
    assert(bb.has_value());
    assert(aa->get().x == 5);
    assert(bb->get().s == "hello");

    // get mutable ref and insert C (table) and D (sparse)
    auto rmut = world.get_mut_ref(e).value();
    rmut.emplace<C, D>(std::forward_as_tuple(9), std::forward_as_tuple("world"));

    // read back via EntityRef (do not access internals)
    auto r2 = world.get_ref(e).value();
    auto ac = r2.get<C>();
    auto ad = r2.get<D>();
    assert(ac.has_value());
    assert(ad.has_value());
    // check values
    assert(ac->get().n == 9 || ac->get().n == 9);
    assert(ad->get().s == "world");

    // remove D via EntityRefMut
    rmut.remove<D>();
    auto maybe_d = world.get_ref(e).value().get<D>();
    assert(!maybe_d.has_value());

    std::cout << "test_entity_ref combined passed\n";
    return 0;
}
