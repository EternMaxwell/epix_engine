#include <cassert>
#include <iostream>

#include "epix/core/entities.hpp"
#include "epix/core/world.hpp"
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
using namespace epix::core::bundle;

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
    std::string s;
    D(std::string_view sv) : s(sv) {}
};

template <>
struct epix::core::sparse_component<D> : std::true_type {};

int main() {
    auto registry = std::make_shared<type_system::TypeRegistry>();
    epix::core::World world(0, registry);

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
    auto e = world.spawn(make_bundle<A, B>(std::forward_as_tuple(5), std::forward_as_tuple("hello"))).id();

    // read via EntityRef
    auto r  = world.get_entity(e).value();
    auto aa = r.get<A>();
    auto bb = r.get<B>();
    assert(aa.has_value());
    assert(bb.has_value());
    assert(aa->get().x == 5);
    assert(bb->get().s == "hello");

    // get mutable ref and insert C (table) and D (sparse)
    auto rmut = world.get_entity_mut(e).value();
    rmut.emplace<C, D>(std::forward_as_tuple(9), std::forward_as_tuple("world"));

    // read back via EntityRef (do not access internals)
    auto r2 = world.get_entity(e).value();
    auto ac = r2.get<C>();
    auto ad = r2.get<D>();
    assert(ac.has_value());
    assert(ad.has_value());
    // check values
    assert(ac->get().n == 9 || ac->get().n == 9);
    assert(ad->get().s == "world");

    // remove D via EntityRefMut
    rmut.remove<D>();
    auto maybe_d = world.get_entity(e).value().get<D>();
    assert(!maybe_d.has_value());

    std::cout << "test_entity_ref combined passed\n";
    return 0;
}
