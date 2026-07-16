#include <gtest/gtest.h>

#include <epix/ecs.hpp>
#include <memory>
#include <tuple>
#include <utility>

namespace {
struct X {
    int v = 0;
};
}  // namespace

TEST(ecs, query_state) {
    using namespace epix::ecs;

    World wc(WorldId(1));

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
