// Combined multithreaded tests for TypeRegistry:
// 1) stability test: concurrent reads of already-registered types
// 2) many-first-add test: concurrent first-time registrations of many compile-time types
#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "epix/core/type_system/type_registry.hpp"

using namespace epix::core::type_system;

// --- stability test --------------------------------------------------------
void stability_test() {
    auto registry = std::make_shared<TypeRegistry>();

    // baseline ids in main thread
    size_t id_int    = registry->type_id<int>();
    size_t id_str    = registry->type_id<std::string>();
    size_t id_double = registry->type_id<double>();
    size_t id_ll     = registry->type_id<long long>();

    const TypeInfo* ti_int    = registry->type_info(id_int);
    const TypeInfo* ti_str    = registry->type_info(id_str);
    const TypeInfo* ti_double = registry->type_info(id_double);
    const TypeInfo* ti_ll     = registry->type_info(id_ll);

    const size_t threads    = std::max<size_t>(2, std::thread::hardware_concurrency());
    const size_t iterations = 10000;

    std::vector<std::thread> ths;
    ths.reserve(threads);

    std::atomic<bool> fail{false};

    for (size_t t = 0; t < threads; ++t) {
        ths.emplace_back([&, t]() {
            for (size_t i = 0; i < iterations; ++i) {
                // alternating access pattern
                size_t a = registry->type_id<int>();
                size_t b = registry->type_id<std::string>();
                size_t c = registry->type_id<double>();
                size_t d = registry->type_id<long long>();

                if (a != id_int || b != id_str || c != id_double || d != id_ll) {
                    fail.store(true);
                    return;
                }

                // also verify type_info pointers are consistent
                const TypeInfo* pa = registry->type_info(a);
                const TypeInfo* pb = registry->type_info(b);
                const TypeInfo* pc = registry->type_info(c);
                const TypeInfo* pd = registry->type_info(d);

                if (pa != ti_int || pb != ti_str || pc != ti_double || pd != ti_ll) {
                    fail.store(true);
                    return;
                }
            }
        });
    }

    for (auto& th : ths) th.join();
    assert(!fail.load());
    std::cout << "type_registry multithreaded stability test passed\n";
}

// --- many-first-add test --------------------------------------------------
template <size_t N>
struct CTType {
    char pad[N % 16 + 1];
};

template <size_t... Is>
void register_all_impl(TypeRegistry* registry, size_t* out, std::index_sequence<Is...>) {
    ((out[Is] = registry->type_id<CTType<Is>>()), ...);
}

void many_first_add_test() {
    constexpr size_t TYPE_COUNT = 64;  // number of distinct compile-time types
    constexpr size_t REPS       = 3;   // repeat to increase stress
    const size_t THREADS        = std::max<size_t>(2, std::thread::hardware_concurrency());

    auto registry = std::make_shared<TypeRegistry>();

    std::vector<std::vector<size_t>> ids(THREADS, std::vector<size_t>(TYPE_COUNT, static_cast<size_t>(-1)));
    std::atomic<bool> start{false};
    std::vector<std::thread> ths;
    ths.reserve(THREADS);

    for (size_t t = 0; t < THREADS; ++t) {
        ths.emplace_back([&, t]() {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            for (size_t rep = 0; rep < REPS; ++rep) {
                register_all_impl(registry.get(), ids[t].data(), std::make_index_sequence<TYPE_COUNT>{});
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (auto& th : ths) th.join();

    for (size_t i = 0; i < TYPE_COUNT; ++i) {
        size_t ref = ids[0][i];
        for (size_t t = 1; t < THREADS; ++t) {
            if (ids[t][i] != ref) {
                std::cerr << "Mismatch for type " << i << ": " << ids[t][i] << " != " << ref << "\n";
                assert(false);
            }
        }
        const TypeInfo* ti = registry->type_info(ref);
        assert(ti != nullptr);
    }

    std::cout << "type_registry many-first-add multithread test passed\n";
}

int main() {
    stability_test();
    many_first_add_test();
    return 0;
}
