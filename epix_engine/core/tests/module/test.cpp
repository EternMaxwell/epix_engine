#include <gtest/gtest.h>

import epix.core;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    auto app = core::App::create();
    inst.print();
    app.add_sub_app(0);
    return RUN_ALL_TESTS();
}