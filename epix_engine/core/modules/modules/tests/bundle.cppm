module;

#include <gtest/gtest.h>

export module epix.core:tests.bundle;

import std;

import :bundle;

using namespace core;

struct A {
    int x;
    A(int v) : x(v) {}
};

struct B {
    std::string s;
    B(std::string_view v) : s(v) {}
};

struct C {
    double d;
    std::vector<int> v;
    C(double dd, std::initializer_list<int> il) : d(dd), v(il) {}
};

struct M {
    std::unique_ptr<int> p;
    M(std::unique_ptr<int> q) : p(std::move(q)) {}
};

template <typename T, typename... Args>
void construct(void* ptr, Args&&... args) {
    auto bundle = make_bundle<T>(std::forward_as_tuple(std::forward<Args>(args)...));
    bundle.write(std::span<void*>{&ptr, 1});
}

template <typename... T, typename... ArgTuples>
    requires(sizeof...(T) == sizeof...(ArgTuples))
void construct_multi(auto&& ptr, ArgTuples&&... args) {
    auto bundle = make_bundle<T...>(std::forward<ArgTuples>(args)...);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // simulate work
    bundle.write(ptr);
}

TEST(core, bundle) {
    alignas(A) std::byte storage_a[sizeof(A)];
    alignas(B) std::byte storage_b[sizeof(B)];
    construct<A>(&storage_a, 42);
    construct<B>(&storage_b, "hello");
    A& a = *reinterpret_cast<A*>(&storage_a);
    B& b = *reinterpret_cast<B*>(&storage_b);
    // std::println(std::cout, "A.x = {}, B.s = {}", a.x, b.s);

    EXPECT_EQ(a.x, 42);
    EXPECT_EQ(b.s, "hello");

    a.~A();
    b.~B();

    // --- construct multiple types at once ---
    alignas(A) std::byte storage_a2[sizeof(A)];
    alignas(B) std::byte storage_b2[sizeof(B)];
    alignas(C) std::byte storage_c2[sizeof(C)];

    std::array<void*, 3> ptrs{&storage_a2, &storage_b2, &storage_c2};
    construct_multi<A, B, C>(ptrs, std::forward_as_tuple(7), std::forward_as_tuple("multi"),
                             std::forward_as_tuple(3.14, std::initializer_list<int>{1, 2, 3}));

    A& a2 = *reinterpret_cast<A*>(&storage_a2);
    B& b2 = *reinterpret_cast<B*>(&storage_b2);
    C& c2 = *reinterpret_cast<C*>(&storage_c2);
    // std::println(std::cout, "A.x = {}, B.s = {}, C.d = {}, C.v = {}", a2.x, b2.s, c2.d, c2.v | std::views::all);
    EXPECT_EQ(a2.x, 7);
    EXPECT_EQ(b2.s, "multi");
    EXPECT_EQ(c2.d, 3.14);
    EXPECT_TRUE((c2.v.size() == 3 && c2.v[0] == 1 && c2.v[1] == 2 && c2.v[2] == 3));

    // destroy
    a2.~A();
    b2.~B();
    c2.~C();

    // --- move-only temporary args ---
    alignas(M) std::byte storage_m[sizeof(M)];
    auto mb = make_bundle<M>(std::make_tuple(std::make_unique<int>(99)));
    std::array<void*, 1> ptrs_m{reinterpret_cast<void*>(&storage_m)};
    std::span<void*> mv(ptrs_m.data(), ptrs_m.size());
    mb.write(mv);
    M& m = *reinterpret_cast<M*>(&storage_m);
    EXPECT_TRUE(m.p && *m.p == 99);
    m.~M();
}
