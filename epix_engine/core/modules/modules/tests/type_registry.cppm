module;

#include <gtest/gtest.h>

export module epix.core:tests.type_registry;

import std;
import :type_registry;

using namespace core;

namespace {
template <size_t N>
struct CTType {
    char pad[N % 16 + 1];
};

template <size_t... Is>
void register_all_impl(TypeRegistry* registry, size_t* out, std::index_sequence<Is...>) {
    ((out[Is] = registry->type_id<CTType<Is>>()), ...);
}
}  // namespace

TEST(core, type_registry) {
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
            EXPECT_EQ(ids[t][i], ref);
        }
    }
    EXPECT_EQ(registry->count(), TYPE_COUNT);
}
