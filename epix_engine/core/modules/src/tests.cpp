module;

module epix.core;

import std;

import :tests;
import :tests.query.access;
import :tests.app_main;

[[nodiscard]] std::vector<core::tests::ForceBase> core::tests::force_link_tests() {
    return std::vector<ForceBase>{
        {core_access_Test{}},
    };
}