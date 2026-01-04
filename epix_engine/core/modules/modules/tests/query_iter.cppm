module;

#include <gtest/gtest.h>

export module epix.core:tests.query_iter;

import std;

import :query;
import :world;
import :bundle;

namespace {
struct P {
    int a;
    P(int v) : a(v) {}
};
}  // namespace

TEST(core, query_iter) {
    using namespace core;

    World wc(0);

    // spawn a couple entities with P
    for (int i = 0; i < 5; ++i) {
        wc.spawn(make_bundle<P>(std::forward_as_tuple(i)));
        wc.spawn(make_bundle<std::string, P>(std::forward_as_tuple("entity"), std::forward_as_tuple(i)));
        wc.spawn(make_bundle<int>(std::forward_as_tuple(i)));
    }
    wc.flush();

    // Create QueryState for Ref<P>
    auto qs0 = wc.query<Entity>();
    auto qs1 = wc.query_filtered<Item<Entity, Opt<Mut<std::string>>>, Filter<With<P>, Without<int>>>();
    auto qs2 = wc.query<Mut<std::string>>();
    auto qs3 = wc.query_filtered<Item<Entity, Mut<P>>, Without<std::string>>();

    EXPECT_EQ(std::ranges::distance(qs0.iter(wc)), 15);
    EXPECT_EQ(std::ranges::distance(qs1.iter(wc)), 10);
    EXPECT_EQ(std::ranges::distance(qs2.iter(wc)), 5);
    EXPECT_EQ(std::ranges::distance(qs3.iter(wc)), 5);
}