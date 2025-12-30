#include <gtest/gtest.h>

import epix.core;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    auto app = core::App::create();
    return RUN_ALL_TESTS();
}