#include <cassert>
#include <iostream>
#include <ostream>

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
template <>
struct epix::core::sparse_component<C3> : std::true_type {};

int main() {
    auto registry = std::make_shared<type_system::TypeRegistry>();
    World world(WorldId(1), std::move(registry));

    int counter = 0;
    // simple function system with an empty-tuple input and a return value
    auto* sys  = make_system([&](World& world) {
        std::println(std::cout, "sys1 running");
        world.spawn().insert(C1{10}, C2{3.14f});
        world.spawn().insert(C1{20}, C3{2.718});
        world.spawn().insert(C2{1.618f}, C3{0.5772});
        ++counter;
        return 123;
    });
    auto* sys2 = make_system(
        [&](query::Query<query::Item<Entity, query::Opt<const C1&>, query::Opt<const C2&>, query::Opt<const C3&>>>
                query) {
            std::println(std::cout, "sys2 running");
            for (auto&& [id, c1_opt, c2_opt, c3_opt] : query.iter()) {
                // opt of reference are of type std::optional<std::reference_wrapper<const T>>
                std::println(std::cout, "entity {}: C1: {}, C2: {}, C3: {}", id.index,
                             c1_opt.transform([](const C1& c1) { return std::to_string(c1.v); }).value_or("null"),
                             c2_opt.transform([](const C2& c2) { return std::to_string(c2.f); }).value_or("null"),
                             c3_opt.transform([](const C3& c3) { return std::to_string(c3.d); }).value_or("null"));
            }
        });

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

    std::cout << "test_system passed\n";
    delete sys;
    return 0;
}
