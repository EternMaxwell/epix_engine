#include <cassert>
#include <iostream>
#include <string>

#include "epix/core/storage/sparse_set.hpp"

using namespace epix::core::storage;

int main() {
    SparseSet<int, std::string> s;
    assert(s.empty());

    s.emplace(42, "hello");
    assert(s.size() == 1);
    assert(s.contains(42));
    auto v = s.get(42);
    assert(v.has_value());
    assert(v->get() == "hello");

    // emplace replace
    s.emplace(42, "world");
    auto v2 = s.get(42);
    assert(v2.has_value());
    assert(v2->get() == "world");

    // remove
    bool removed = s.remove(42);
    assert(removed);
    assert(!s.contains(42));

    std::cout << "sparse_set tests passed\n";
    return 0;
}
