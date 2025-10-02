#include <cassert>
#include <iostream>
#include <string>

#include "epix/core/storage/dense.hpp"
#include "epix/core/type_system/type_registry.hpp"

using namespace epix::core::storage;
using namespace epix::core::type_system;

int main() {
    const TypeInfo* ti = TypeInfo::get_info<std::string>();
    Dense d(ti, 2);
    assert(d.len() == 0);

    d.push<std::string>({0, 0}, "a");
    d.push<std::string>({0, 0}, "b");
    assert(d.len() == 2);
    auto s0 = d.get_as<std::string>(0);
    assert(s0.has_value());
    assert(s0->get() == "a");

    // replace
    d.replace<std::string>(0, 0, "z");
    assert(d.get_as<std::string>(0)->get() == "z");

    // swap_remove
    d.swap_remove(0);
    assert(d.len() == 1);

    // get ticks
    auto ticks = d.get_ticks(0);
    assert(ticks.has_value());

    std::cout << "dense tests passed\n";
    return 0;
}
