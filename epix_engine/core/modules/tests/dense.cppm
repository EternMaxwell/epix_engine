module;

#include <gtest/gtest.h>

export module epix.core:tests.dense;

import std;
import :storage;

using namespace core;

TEST(core, dense) {
    Dense d(meta::type_id<std::string>::type_info(), 2);
    EXPECT_EQ(d.len(), 0);

    d.push<std::string>({0, 0}, "a");
    d.push<std::string>({0, 0}, "b");
    EXPECT_EQ(d.len(), 2);
    auto s0 = d.get_as<std::string>(0);
    EXPECT_TRUE(s0.has_value());
    EXPECT_EQ(s0->get(), "a");

    // replace
    d.replace<std::string>(0, 0, "z");
    EXPECT_EQ(d.get_as<std::string>(0)->get(), "z");

    // swap_remove
    d.swap_remove(0);
    EXPECT_EQ(d.len(), 1);

    // get ticks
    auto ticks = d.get_ticks(0);
    EXPECT_TRUE(ticks.has_value());
}