module;

#include <gtest/gtest.h>

export module epix.core:tests.query_state;

import :query;
import :world;

import std;

namespace {
struct X {
    int v = 0;
};
}  // namespace

TEST(core, query_state) {
    using namespace core;

    auto registry = std::make_shared<TypeRegistry>();
    World wc(WorldId(1), std::move(registry));

    // QueryState::create_uninit should work even when no components are registered
    auto qs_uninit = QueryState<std::tuple<>>::create_uninit(wc);

    // create should also work and not throw
    auto qs = QueryState<std::tuple<>>::create(wc);

    // create_from_const_uninit and create_from_const should return value when no components referenced
    auto qs_const_uninit = QueryState<std::tuple<>>::create_from_const_uninit(wc);
    EXPECT_TRUE(qs_const_uninit.has_value());
    auto qs_const = QueryState<std::tuple<>>::create_from_const(wc);
    EXPECT_TRUE(qs_const.has_value());
}
