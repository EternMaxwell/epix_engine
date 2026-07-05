// epix.tasks API tests — module build variant

#include <gtest/gtest.h>

#ifndef EPIX_IMPORT_STD
#include <atomic>
#include <chrono>
#include <numeric>
#include <thread>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.tasks;

using namespace epix::tasks;
using namespace std::chrono_literals;

// ── TaskPool ────────────────────────────────────────────────────────────────

TEST(TaskPool, DefaultConstruct) {
    TaskPool pool;
    EXPECT_GE(pool.thread_num(), 1u);
}

TEST(TaskPool, BuilderNumThreads) {
    auto pool = TaskPoolBuilder{}.num_threads(2).build();
    EXPECT_EQ(pool.thread_num(), 2u);
}

TEST(TaskPool, SpawnAndCheckFinished) {
    TaskPool pool;
    auto task = pool.spawn([]() { return 42; });
    while (!task.is_finished()) std::this_thread::sleep_for(1ms);
    EXPECT_TRUE(task.is_finished());
}

TEST(TaskPool, DetachRunsWork) {
    std::atomic<int> x{0};
    {
        TaskPool pool;
        pool.spawn([&]() { x.store(99); }).detach();
    }
    EXPECT_EQ(x.load(), 99);
}

// ── Scope ────────────────────────────────────────────────────────────────────

TEST(Scope, CollectsResults) {
    TaskPool pool;
    auto results = pool.scope<int>([](Scope<int>& s) {
        s.spawn([]() { return 1; });
        s.spawn([]() { return 2; });
        s.spawn([]() { return 3; });
    });
    ASSERT_EQ(results.size(), 3u);
}

// ── ThreadExecutor ───────────────────────────────────────────────────────────

TEST(ThreadExecutor, SpawnAndRun) {
    ThreadExecutor exec;
    auto task = exec.spawn([]() { return 77; });
    while (!task.is_finished()) {
        if (auto t = exec.ticker()) t->try_tick();
    }
    EXPECT_TRUE(task.is_finished());
}

TEST(ThreadExecutor, TickerOnlyOnOwningThread) {
    ThreadExecutor exec;
    EXPECT_TRUE(exec.ticker().has_value());
    auto other = std::thread([&]() { EXPECT_FALSE(exec.ticker().has_value()); });
    other.join();
}

// ── Global pools ─────────────────────────────────────────────────────────────

TEST(GlobalPools, ComputeTaskPool) {
    auto& pool = ComputeTaskPool::get_or_init(TaskPoolBuilder{}.num_threads(2).build());
    auto task  = pool.spawn([]() { return 99; });
    while (!task.is_finished()) std::this_thread::sleep_for(1ms);
    EXPECT_TRUE(task.is_finished());
}

// ── par_range ────────────────────────────────────────────────────────────────

TEST(ParRange, Transform) {
    TaskPool pool;
    std::vector<int> data{1, 2, 3, 4, 5};
    auto doubled = make_par(data).transform<int>(pool, [](int x) { return x * 2; });
    ASSERT_EQ(doubled.size(), 5u);
    EXPECT_EQ(doubled[4], 10);
}

TEST(ParRange, Sum) {
    TaskPool pool;
    std::vector<int> data(100, 1);
    EXPECT_EQ(make_par(data).sum(pool), 100);
}

TEST(ParRange, Filter) {
    TaskPool pool;
    std::vector<int> data{1, 2, 3, 4, 5, 6};
    auto evens = make_par(data).filter(pool, [](int x) { return x % 2 == 0; });
    EXPECT_EQ(evens.size(), 3u);
}

// ── Slice ────────────────────────────────────────────────────────────────────

TEST(Slice, ParChunkMap) {
    TaskPool pool;
    std::vector<int> data(20, 0);
    std::iota(data.begin(), data.end(), 0);
    auto results =
        par_chunk_map<int>(pool, std::span{data}, 5, [](size_t, std::span<const int> chunk) { return chunk.size(); });
    ASSERT_EQ(results.size(), 4u);
}

// ── Behavior ─────────────────────────────────────────────────────────────────

TEST(Behavior, BusyManyTasks) {
    auto pool = TaskPoolBuilder{}.num_threads(4).build();
    pool.scope<int>([](Scope<int>& s) {
        for (int i = 0; i < 40; ++i) {
            s.spawn([i]() -> int {
                auto start = std::chrono::steady_clock::now();
                while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(10)) {}
                return i;
            });
        }
    });
}
