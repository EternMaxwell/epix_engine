#include <cassert>
#include <iostream>

#include "epix/core/storage/bitvector.hpp"

using namespace epix::core::storage;

int main() {
    // basic construction
    bit_vector b(130, false);
    assert(b.size() == 130);
    assert(!b.any());
    assert(b.count() == 0);

    // set bits
    b.set(0);
    b.set(64);
    b.set(129);
    assert(b.test(0));
    assert(b.test(64));
    assert(b.test(129));
    assert(b.count() == 3);

    // flip a bit
    b.flip(64);
    assert(!b.test(64));

    // set all and reset
    b.set_all();
    assert(b.any());
    assert(b.count() == 130);
    b.reset();
    assert(b.none());

    // intersects and subset
    bit_vector a(130, false), c(130, false);
    a.set(5);
    a.set(7);
    c.set(7);
    assert(a.intersects(c));
    assert(a.find_first_intersection(c) == 7);
    // c contains {7} which is a subset of a {5,7}
    assert(c.is_subset_of(a) == true);

    // find next
    a.set(10);
    a.set(20);
    size_t f = a.find_first();
    assert(f == 5);
    size_t n = a.find_next(f);
    assert(n == 7);

    std::cout << "bit_vector tests passed\n";
    return 0;
}
