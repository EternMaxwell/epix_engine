#include <algorithm>
#include <cassert>
#include <iostream>
#include <unordered_set>
#include <vector>

#include "epix/core/storage/bitvector.hpp"

using namespace epix::core::storage;

int main() {
    // basic construction and queries
    bit_vector b(130, false);
    assert(b.size() == 130);
    assert(b.count_ones() == 0);

    // set bits and basic tests
    b.set(0);
    b.set(64);
    b.set(129);
    assert(b.test(0));
    assert(b.test(64));
    assert(b.test(129));
    assert(b.count_ones() == 3);

    // toggle a bit
    b.toggle(64);
    assert(!b.test(64));

    // set_range to set all bits, then reset
    b.set_range(0, b.size());
    assert(b.count_ones() == b.size());
    assert(b.contains_all_in_range(0, b.size()));
    b.reset();
    assert(b.count_ones() == 0);

    // intersects, intersection view and intersect_count
    bit_vector a(130, false), c(130, false);
    a.set(5);
    a.set(7);
    c.set(7);
    assert(a.intersect(c));
    assert(a.intersect_count(c) == 1);
    auto iv = a.intersection(c);
    auto v  = iv | std::ranges::to<std::vector>();
    assert(v.size() == 1 && v[0] == 7);

    // iter_ones and iter_zeros (ranges)
    a.set(10);
    a.set(20);
    auto ones_view = a.iter_ones();
    auto it        = ones_view.begin();
    assert(*it == 5);
    ++it;
    assert(*it == 7);

    // bit ops and assigns
    bit_vector x(16, false), y(16, false);
    x.set(1);
    x.set(3);
    x.set(5);
    y.set(3);
    y.set(4);
    y.set(5);
    auto andv = x.bit_and(y);
    bit_vector exp_and(16, false);
    exp_and.set(3);
    exp_and.set(5);
    assert(andv == exp_and);

    auto orv = x.bit_or(y);
    bit_vector exp_or(16, false);
    exp_or.set(1);
    exp_or.set(3);
    exp_or.set(4);
    exp_or.set(5);
    assert(orv == exp_or);

    auto xorv = x.bit_xor(y);
    bit_vector exp_xor(16, false);
    exp_xor.set(1);
    exp_xor.set(4);
    assert(xorv == exp_xor);

    // assign variants
    bit_vector xa = x;
    xa.bit_or_assign(y);
    assert(xa == exp_or);
    xa = x;
    xa.bit_and_assign(y);
    assert(xa == exp_and);
    xa = x;
    xa.bit_xor_assign(y);
    assert(xa == exp_xor);

    // contains_any_in_range and contains_all_in_range
    bit_vector r(32, false);
    r.set_range(4, 8);                      // set 4..7
    assert(r.contains_any_in_range(0, 5));  // intersects at 4
    assert(r.contains_all_in_range(4, 8));

    // iter_zeros: create small vector and check zeros
    bit_vector small(8, false);
    small.set(1);
    small.set(3);
    small.set(6);
    auto zeros                         = small.iter_zeros() | std::ranges::to<std::vector>();
    std::vector<size_t> expected_zeros = {0, 2, 4, 5, 7};
    assert(zeros == expected_zeros);

    // equality / inequality
    bit_vector p(10, false), q(10, false);
    p.set(2);
    q.set(2);
    assert(p == q);
    q.set(3);
    assert(p != q);

    // auto-grow behavior
    bit_vector smallgrow(4, false);
    smallgrow.set(10);
    assert(smallgrow.size() == 11);
    assert(smallgrow.test(10));

    // std::hash usage in unordered_set
    std::unordered_set<bit_vector> set;
    set.insert(p);
    assert(set.find(p) != set.end());

    std::cout << "bit_vector tests passed\n";
    return 0;
}
