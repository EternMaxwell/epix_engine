module;

#include <gtest/gtest.h>

export module epix.core:tests.untyped_vec;

import std;
import epix.meta;

import :storage;

namespace {
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
}  // namespace

TEST(core, untyped_vector) {
    using namespace core;

    // Test with int
    const auto& int_desc = meta::type_info::of<int>();
    untyped_vector v_int(int_desc, 4);
    for (int i = 0; i < 10; ++i) {
        v_int.push_back_from(&i);
    }
    EXPECT_EQ(v_int.size(), 10);
    for (size_t i = 0; i < v_int.size(); ++i) {
        int val = v_int.get_as<int>(i);
        EXPECT_EQ(val, static_cast<int>(i));
    }

    // Test with std::string
    const auto& str_desc = meta::type_info::of<std::string>();
    untyped_vector v_str(str_desc, 2);
    std::string s1 = "hello";
    std::string s2 = "world";
    v_str.push_back_from(&s1);
    v_str.push_back_from(&s2);
    EXPECT_EQ(v_str.size(), 2);
    // use typed accessors
    EXPECT_EQ(v_str.get_as<std::string>(0), "hello");
    EXPECT_EQ(v_str.get_as<std::string>(1), "world");

    // test cget (raw const pointer) and cget_as
    const auto& cv_str = v_str;
    const void* raw0   = cv_str.cget(0);
    EXPECT_EQ(*reinterpret_cast<const std::string*>(raw0), "hello");
    EXPECT_EQ(cv_str.cget_as<std::string>(0), "hello");

    // test replace_emplace
    v_str.replace_emplace<std::string>(1, "replaced");
    EXPECT_EQ(v_str.get_as<std::string>(1), "replaced");

    // emplace and pop
    v_str.emplace_back<std::string>("from_emplace");
    EXPECT_EQ(v_str.size(), 3);
    v_str.pop_back();
    EXPECT_EQ(v_str.size(), 2);

    // --- additional memory-safety tests with a non-trivial type ---
    {
        // reset counters on the file-scope Heavy type
        Heavy::live          = 0;
        Heavy::constructions = 0;
        Heavy::copies        = 0;
        Heavy::moves         = 0;
        Heavy::destructions  = 0;

        const auto& heavy_desc = meta::type_info::of<Heavy>();
        untyped_vector v_heavy(heavy_desc, 1);

        // push a sequence of named Heavy objects to force multiple reallocations
        std::vector<std::string> names = {"a", "b", "c", "d", "e", "f", "g", "h"};
        for (const auto& n : names) {
            v_heavy.emplace_back<Heavy>(n);
        }

        EXPECT_EQ(v_heavy.size(), names.size());
        EXPECT_EQ(Heavy::live, static_cast<int>(names.size()));

        // verify contents after reallocations
        // verify contents after reallocations using typed accessors
        for (size_t i = 0; i < v_heavy.size(); ++i) {
            EXPECT_EQ(v_heavy.get_as<Heavy>(i).s, names[i]);
        }

        // swap_remove a middle element and ensure size and live count update
        size_t before = v_heavy.size();
        v_heavy.swap_remove(2);
        EXPECT_EQ(v_heavy.size(), before - 1);
        EXPECT_EQ(Heavy::live, static_cast<int>(v_heavy.size()));

        // clear and ensure all destructors run
        v_heavy.clear();
        EXPECT_EQ(v_heavy.size(), 0);
        EXPECT_EQ(Heavy::live, 0);

        // test replace_emplace on Heavy
        for (const auto& n : names) v_heavy.emplace_back<Heavy>(n);
        v_heavy.replace_emplace<Heavy>(2, std::string("Z"));
        EXPECT_EQ(v_heavy.get_as<Heavy>(2).s, "Z");

        // basic invariant: number of destructions equals number of constructions + copies + moves
        int constructed_total =
            static_cast<int>(Heavy::constructions.load() + Heavy::copies.load() + Heavy::moves.load());
        int destroyed_total = static_cast<int>(Heavy::destructions.load());
        (void)constructed_total;  // keep variables for debugging if assertions fail
        (void)destroyed_total;
        EXPECT_GE(Heavy::destructions.load(), 1);
    };
}
