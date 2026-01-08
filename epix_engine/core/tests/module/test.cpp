#include <gtest/gtest.h>

import std;
import epix.core;

void terminate_handle() {
    try {
        if (std::current_exception()) {
            std::rethrow_exception(std::current_exception());
        }
    } catch (const std::exception& e) {
        std::println(std::cerr, "Terminated due to unhandled exception: {}", e.what());
    } catch (...) {
        std::println(std::cerr, "Terminated due to unknown unhandled exception");
    }
    auto bt = std::stacktrace::current();
    std::println(std::cerr, "Stack trace at termination:\n{}", bt);
    std::exit(1);
}

int main(int argc, char** argv) {
    std::set_terminate(terminate_handle);
    ::testing::InitGoogleTest(&argc, argv);
    auto tests = core::tests::force_link_tests();
    return RUN_ALL_TESTS();
}