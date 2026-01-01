#include <gtest/gtest.h>

import epix.core;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    core::tests::force_link_tests();
    return RUN_ALL_TESTS();
}