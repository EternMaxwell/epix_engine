module;

#include <gtest/gtest.h>

export module epix.core:tests.sparse_set;

import std;

import :storage;

TEST(core, sparse_set) {
    using namespace core;

    SparseSet<int, std::string> s;
    EXPECT_TRUE(s.empty());

    s.emplace(42, "hello");
    EXPECT_EQ(s.size(), 1);
    EXPECT_TRUE(s.contains(42));
    auto v = s.get(42);
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(v->get(), "hello");

    // emplace replace
    s.emplace(42, "world");
    auto v2 = s.get(42);
    EXPECT_TRUE(v2.has_value());
    EXPECT_EQ(v2->get(), "world");

    // remove
    bool removed = s.remove(42);
    EXPECT_TRUE(removed);
    EXPECT_FALSE(s.contains(42));
}
