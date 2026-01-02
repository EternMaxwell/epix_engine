module;

module epix.core;

import std;

import :tests;
import :tests.query.access;
import :tests.app_main;
import :tests.archetype;
import :tests.bitvector;
import :tests.bundle_info;
import :tests.bundle_inserter;
import :tests.bundle;
import :tests.component_hooks;

[[nodiscard]] std::vector<core::tests::ForceBase> core::tests::force_link_tests() {
    using namespace core;
    return std::vector<ForceBase>{
        {core_access_Test{}},    {core_app_main_Test{}},        {core_archetype_Test{}},
        {core_bitvector_Test{}}, {core_bundle_info_Test{}},     {core_bundle_inserter_Test{}},
        {core_bundle_Test{}},    {core_component_hooks_Test{}},
    };
}