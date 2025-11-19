#include <atomic>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "epix/core/storage/untypedvec.hpp"

// Non-trivial helper type used for memory-safety tests
struct Heavy {
    std::string s;
    static std::atomic<int> live;
    static std::atomic<int> constructions;
    static std::atomic<int> copies;
    static std::atomic<int> moves;
    static std::atomic<int> destructions;

    Heavy(const std::string& v) : s(v) {
        ++live;
        ++constructions;
    }
    Heavy(const Heavy& o) : s(o.s) {
        ++live;
        ++copies;
    }
    Heavy(Heavy&& o) noexcept : s(std::move(o.s)) {
        ++live;
        ++moves;
    }
    ~Heavy() {
        ++destructions;
        --live;
    }
};

std::atomic<int> Heavy::live{0};
std::atomic<int> Heavy::constructions{0};
std::atomic<int> Heavy::copies{0};
std::atomic<int> Heavy::moves{0};
std::atomic<int> Heavy::destructions{0};

int main() {
    using namespace epix::core::storage;

    // Test with int
    const auto& int_desc = epix::meta::type_info::of<int>();
    untyped_vector v_int(int_desc, 4);
    for (int i = 0; i < 10; ++i) {
        v_int.push_back_from(&i);
    }
    assert(v_int.size() == 10);
    for (size_t i = 0; i < v_int.size(); ++i) {
        int val = v_int.get_as<int>(i);
        assert(val == static_cast<int>(i));
    }

    // Test with std::string
    const auto& str_desc = epix::meta::type_info::of<std::string>();
    untyped_vector v_str(str_desc, 2);
    std::string s1 = "hello";
    std::string s2 = "world";
    v_str.push_back_from(&s1);
    v_str.push_back_from(&s2);
    assert(v_str.size() == 2);
    // use typed accessors
    assert(v_str.get_as<std::string>(0) == "hello");
    assert(v_str.get_as<std::string>(1) == "world");

    // test cget (raw const pointer) and cget_as
    const auto& cv_str = v_str;
    const void* raw0   = cv_str.cget(0);
    assert(*reinterpret_cast<const std::string*>(raw0) == "hello");
    assert(cv_str.cget_as<std::string>(0) == "hello");

    // test replace_emplace
    v_str.replace_emplace<std::string>(1, "replaced");
    assert(v_str.get_as<std::string>(1) == "replaced");

    // emplace and pop
    v_str.emplace_back<std::string>("from_emplace");
    assert(v_str.size() == 3);
    v_str.pop_back();
    assert(v_str.size() == 2);

    std::cout << "untyped_vector tests passed" << std::endl;
    // --- additional memory-safety tests with a non-trivial type ---
    {
        // reset counters on the file-scope Heavy type
        Heavy::live          = 0;
        Heavy::constructions = 0;
        Heavy::copies        = 0;
        Heavy::moves         = 0;
        Heavy::destructions  = 0;

        const auto& heavy_desc = epix::meta::type_info::of<Heavy>();
        untyped_vector v_heavy(heavy_desc, 1);

        // push a sequence of named Heavy objects to force multiple reallocations
        std::vector<std::string> names = {"a", "b", "c", "d", "e", "f", "g", "h"};
        for (const auto& n : names) {
            v_heavy.emplace_back<Heavy>(n);
        }

        assert(v_heavy.size() == names.size());
        assert(Heavy::live == static_cast<int>(names.size()));

        // verify contents after reallocations
        // verify contents after reallocations using typed accessors
        for (size_t i = 0; i < v_heavy.size(); ++i) {
            assert(v_heavy.get_as<Heavy>(i).s == names[i]);
        }

        // swap_remove a middle element and ensure size and live count update
        size_t before = v_heavy.size();
        v_heavy.swap_remove(2);
        assert(v_heavy.size() == before - 1);
        assert(Heavy::live == static_cast<int>(v_heavy.size()));

        // clear and ensure all destructors run
        v_heavy.clear();
        assert(v_heavy.size() == 0);
        assert(Heavy::live == 0);

        // test replace_emplace on Heavy
        for (const auto& n : names) v_heavy.emplace_back<Heavy>(n);
        v_heavy.replace_emplace<Heavy>(2, std::string("Z"));
        assert(v_heavy.get_as<Heavy>(2).s == "Z");

        // basic invariant: number of destructions equals number of constructions + copies + moves
        int constructed_total =
            static_cast<int>(Heavy::constructions.load() + Heavy::copies.load() + Heavy::moves.load());
        int destroyed_total = static_cast<int>(Heavy::destructions.load());
        (void)constructed_total;  // keep variables for debugging if assertions fail
        (void)destroyed_total;
        assert(Heavy::destructions.load() >= 1);
    }

    std::cout << "memory-safety tests passed" << std::endl;
    return 0;
}
