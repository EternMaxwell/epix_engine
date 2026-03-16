module;

#include <gtest/gtest.h>

export module epix.core:tests.sparse_array;

import :storage;

TEST(core, sparse_array) {
    using namespace core;

    SparseArray<size_t, int> sa;
    EXPECT_FALSE(sa.contains(5));
    sa.insert(5, 42);
    EXPECT_TRUE(sa.contains(5));
    auto v = sa.get(5);
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(v->get(), 42);

    auto removed = sa.remove(5);
    EXPECT_TRUE(removed.has_value());
    EXPECT_FALSE(sa.contains(5));
}