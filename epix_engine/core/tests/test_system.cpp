#include <cassert>
#include <iostream>
#include <ostream>

#include "epix/core/bundleimpl.hpp"
#include "epix/core/query/fetch.hpp"
#include "epix/core/system/system.hpp"
#include "epix/core/type_system/type_registry.hpp"
#include "epix/core/world.hpp"

using namespace epix::core;
using namespace epix::core::system;

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
template <>
struct epix::core::sparse_component<C3> : std::true_type {};

void func2(query::Query<query::Item<Entity, query::Opt<const C1&>, query::Opt<const C2&>, query::Opt<const C3&>>> query,
           Res<resource1> res1) {
    std::println(std::cout, "sys2 running");
    for (auto&& [id, c1_opt, c2_opt, c3_opt] : query.iter()) {
        // opt of reference are of type std::optional<std::reference_wrapper<const T>>
        std::println(std::cout, "entity {}: C1: {}, C2: {}, C3: {}", id.index,
                     c1_opt.transform([](const C1& c1) { return std::to_string(c1.v); }).value_or("null"),
                     c2_opt.transform([](const C2& c2) { return std::to_string(c2.f); }).value_or("null"),
                     c3_opt.transform([](const C3& c3) { return std::to_string(c3.d); }).value_or("null"));
    }
    std::println(std::cout, "resource1: a = {}, b = {}", res1->a, res1->b);
}

int main() {
    auto registry = std::make_shared<type_system::TypeRegistry>();
    World world(WorldId(1), std::move(registry));

    int counter  = 0;
    auto lambda1 = [&](World& world) {
        std::println(std::cout, "sys1 running");
        world.spawn().insert(C1{10}, C2{3.14f});
        world.spawn().insert(C1{20}, C3{2.718});
        world.spawn().insert(C2{1.618f}, C3{0.5772});
        world.init_resource<resource1>();
        ++counter;
        return 123;
    };
    // simple function system with an empty-tuple input and a return value
    auto* sys  = make_system(lambda1);
    auto* sys2 = make_system(func2);

    std::println(std::cout, "system names:\n\t sys1: {}\n\t sys2: {}", sys->name(), sys2->name());
    std::println(std::cout, "default sets:\n\t sys1: {}\n\t sys2: {}",
                 sys->default_sets() | std::views::transform([](const schedule::SystemSetLabel& label) {
                     return std::format("(type: {}, extra: {})", label.type_index().name(), label.extra());
                 }),
                 sys2->default_sets() | std::views::transform([](const schedule::SystemSetLabel& label) {
                     return std::format("(type: {}, extra: {})", label.type_index().name(), label.extra());
                 }));
    assert(sys->default_sets()[0] != sys2->default_sets()[0]);

    // initialize should succeed
    auto access  = sys->initialize(world);
    auto access2 = sys2->initialize(world);
    std::println(std::cout, "Access conflicts: {}", access.get_conflicts(access2).to_string());

    // run should call the function and return value
    auto res  = sys->run({}, world);
    auto res2 = sys2->run({}, world);
    assert(res.has_value());
    assert(res.value() == 123);
    assert(counter == 1);

    // check type ids
    std::println(std::cout, "type ids: C1: {}, C2: {}, C3: {}, resource1: {}",
                 world.type_registry().type_id<C1>().get(), world.type_registry().type_id<C2>().get(),
                 world.type_registry().type_id<C3>().get(), world.type_registry().type_id<resource1>().get());
    using Bundle1 = bundle::InitializeBundle<std::tuple<C1, C2>, std::tuple<std::tuple<C1&&>, std::tuple<C2&&>>>;
    using Bundle2 = bundle::InitializeBundle<std::tuple<C1, C3>, std::tuple<std::tuple<C1&&>, std::tuple<C3&&>>>;
    using Bundle3 = bundle::InitializeBundle<std::tuple<C2, C3>, std::tuple<std::tuple<C2&&>, std::tuple<C3&&>>>;
    std::println(std::cout, "Bundle ids: Bundle1: {}, Bundle2: {}, Bundle3: {}",
                 world.type_registry().type_id<Bundle1>().get(), world.type_registry().type_id<Bundle2>().get(),
                 world.type_registry().type_id<Bundle3>().get());

    std::cout << "test_system passed\n";
    delete sys;
    return 0;
}
