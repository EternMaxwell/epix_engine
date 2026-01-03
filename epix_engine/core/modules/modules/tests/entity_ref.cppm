module;

#include <gtest/gtest.h>

export module epix.core:tests.entity_ref;

import :world;
import :bundle;

namespace {
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
}  // namespace

template <>
struct core::sparse_component<D> : std::true_type {};

using namespace core;

TEST(core, entity_ref) {
    auto registry = std::make_shared<TypeRegistry>();
    World world(0, registry);

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
    EXPECT_TRUE(aa.has_value());
    EXPECT_TRUE(bb.has_value());
    EXPECT_EQ(aa->get().x, 5);
    EXPECT_EQ(bb->get().s, "hello");

    // get mutable ref and insert C (table) and D (sparse)
    auto rmut = world.get_entity_mut(e).value();
    rmut.emplace<C, D>(std::forward_as_tuple(9), std::forward_as_tuple("world"));

    // read back via EntityRef (do not access internals)
    auto r2 = world.get_entity(e).value();
    auto ac = r2.get<C>();
    auto ad = r2.get<D>();
    EXPECT_TRUE(ac.has_value());
    EXPECT_TRUE(ad.has_value());
    // check values
    EXPECT_EQ(ac->get().n, 9);
    EXPECT_EQ(ad->get().s, "world");

    // remove D via EntityRefMut
    rmut.remove<D>();
    auto maybe_d = world.get_entity(e).value().get<D>();
    EXPECT_TRUE(!maybe_d.has_value());
}
