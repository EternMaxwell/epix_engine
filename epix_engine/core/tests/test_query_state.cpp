#include <cassert>
#include <iostream>

#include "epix/core/query/state.hpp"
#include "epix/core/world.hpp"

using namespace epix::core;
using namespace epix::core::query;

struct X {
    int v = 0;
};

int main() {
    auto registry = std::make_shared<type_system::TypeRegistry>();
    World wc(WorldId(1), std::move(registry));

    // QueryState::create_uninit should work even when no components are registered
    auto qs_uninit = QueryState<std::tuple<>>::create_uninit(wc);

    // create should also work and not throw
    auto qs = QueryState<std::tuple<>>::create(wc);

    // create_from_const_uninit and create_from_const should return value when no components referenced
    auto qs_const_uninit = QueryState<std::tuple<>>::create_from_const_uninit(wc);
    assert(qs_const_uninit.has_value());
    auto qs_const = QueryState<std::tuple<>>::create_from_const(wc);
    assert(qs_const.has_value());

    std::cout << "test_query_state passed\n";
    return 0;
}
