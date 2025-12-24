#include <cassert>
#include <iostream>

#include "epix/core/storage/sparse_array.hpp"

using namespace epix::core::storage;

int main() {
    SparseArray<size_t, int> sa;
    assert(!sa.contains(5));
    sa.insert(5, 42);
    assert(sa.contains(5));
    auto v = sa.get(5);
    assert(v.has_value());
    assert(v->get() == 42);

    auto removed = sa.remove(5);
    assert(removed.has_value());
    assert(!sa.contains(5));

    std::cout << "sparse_array tests passed\n";
    return 0;
}
