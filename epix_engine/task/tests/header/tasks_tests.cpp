// epix.tasks API tests — header build variant

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <epix/task.hpp>
#include <numeric>
#include <stdexec/execution.hpp>
#include <thread>
#include <vector>

using namespace epix::task;
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

TEST(TaskPool, BuilderThreadName) {
    auto pool = TaskPoolBuilder{}.num_threads(1).thread_name("Test").build();
    EXPECT_EQ(pool.thread_num(), 1u);
}

TEST(TaskPool, SpawnReturnsTask) {
    TaskPool pool;
    auto task = pool.spawn([]() { return 42; });
    EXPECT_TRUE(static_cast<bool>(task));
}

TEST(TaskPool, SpawnAndCheckFinished) {
    TaskPool pool;
    auto task = pool.spawn([]() { return 42; });
    while (!task.is_finished()) std::this_thread::sleep_for(1ms);
    EXPECT_TRUE(task.is_finished());
}

TEST(TaskPool, SpawnManyFinish) {
    TaskPool pool;
    std::vector<Task<int>> tasks;
    for (int i = 0; i < 10; ++i) tasks.push_back(pool.spawn([i]() { return i * i; }));
    for (auto& t : tasks) {
        while (!t.is_finished()) std::this_thread::sleep_for(1ms);
        EXPECT_TRUE(t.is_finished());
    }
}

TEST(TaskPool, DetachMayDropWorkWhenPoolDies) {
    std::atomic<int> x{0};
    {
        TaskPool pool;
        pool.spawn([&]() { x.store(99); }).detach();
    }
    SUCCEED();
}

// ── Scope ────────────────────────────────────────────────────────────────────

TEST(Scope, EmptyScope) {
    TaskPool pool;
    auto results = pool.scope<int>([](Scope<int>&) {});
    EXPECT_TRUE(results.empty());
}

TEST(Scope, CollectsResults) {
    TaskPool pool;
    auto results = pool.scope<int>([](Scope<int>& s) {
        s.spawn([]() { return 1; });
        s.spawn([]() { return 2; });
        s.spawn([]() { return 3; });
    });
    ASSERT_EQ(results.size(), 3u);
    std::sort(results.begin(), results.end());
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
    EXPECT_EQ(results[2], 3);
}

TEST(Scope, ManyTasks) {
    TaskPool pool;
    constexpr int N = 100;
    auto results    = pool.scope<int>([&](Scope<int>& s) {
        for (int i = 0; i < N; ++i) s.spawn([i]() { return i; });
    });
    EXPECT_EQ(results.size(), static_cast<size_t>(N));
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

TEST(ThreadExecutor, IsSame) {
    ThreadExecutor a;
    ThreadExecutor b;
    EXPECT_TRUE(a.is_same(a));
    EXPECT_FALSE(a.is_same(b));
}

// ── Global pools ─────────────────────────────────────────────────────────────

TEST(GlobalPools, ComputeTaskPoolGetOrInit) {
    auto& pool = ComputeTaskPool::get_or_init(TaskPoolBuilder{}.num_threads(2).build());
    EXPECT_GE(pool.thread_num(), 1u);
    auto task = pool.spawn([]() {});
    while (!task.is_finished()) std::this_thread::sleep_for(1ms);

    auto* same = ComputeTaskPool::try_get();
    EXPECT_EQ(same, &pool);
}

TEST(GlobalPools, IoTaskPool) {
    auto& pool = IoTaskPool::get_or_init(TaskPoolBuilder{}.num_threads(2).build());
    auto task  = pool.spawn([]() { return 99; });
    while (!task.is_finished()) std::this_thread::sleep_for(1ms);
    EXPECT_TRUE(task.is_finished());
}

TEST(GlobalPools, AsyncComputeTaskPool) {
    auto& pool = AsyncComputeTaskPool::get_or_init(TaskPoolBuilder{}.num_threads(1).build());
    auto task  = pool.spawn([]() { return 55; });
    while (!task.is_finished()) std::this_thread::sleep_for(1ms);
    EXPECT_TRUE(task.is_finished());
}

// ── par_range ────────────────────────────────────────────────────────────────

TEST(ParRange, ForEachMutates) {
    TaskPool pool;
    std::vector<int> data(100, 0);
    make_par(data).for_each(pool, [](int& x) { x = 1; });
    for (auto x : data) EXPECT_EQ(x, 1);
}

TEST(ParRange, Transform) {
    TaskPool pool;
    std::vector<int> data{1, 2, 3, 4, 5};
    auto doubled = make_par(data).transform<int>(pool, [](int x) { return x * 2; });
    ASSERT_EQ(doubled.size(), 5u);
    EXPECT_EQ(doubled[0], 2);
    EXPECT_EQ(doubled[4], 10);
}

TEST(ParRange, Filter) {
    TaskPool pool;
    std::vector<int> data{1, 2, 3, 4, 5, 6};
    auto evens = make_par(data).filter(pool, [](int x) { return x % 2 == 0; });
    EXPECT_EQ(evens.size(), 3u);
}

TEST(ParRange, Sum) {
    TaskPool pool;
    std::vector<int> data(100, 1);
    auto s = make_par(data).sum(pool);
    EXPECT_EQ(s, 100);
}

TEST(ParRange, Reduce) {
    TaskPool pool;
    std::vector<int> data{1, 2, 3, 4, 5};
    auto total = make_par(data).reduce(pool, 0, std::plus{});
    EXPECT_EQ(total, 15);
}

TEST(ParRange, AnyAll) {
    TaskPool pool;
    std::vector<int> data{1, 2, 3, 4, 5};
    EXPECT_TRUE(make_par(data).any(pool, [](int x) { return x > 3; }));
    EXPECT_FALSE(make_par(data).any(pool, [](int x) { return x > 10; }));
    EXPECT_TRUE(make_par(data).all(pool, [](int x) { return x > 0; }));
    EXPECT_FALSE(make_par(data).all(pool, [](int x) { return x > 2; }));
}

TEST(ParRange, Empty) {
    TaskPool pool;
    std::vector<int> data;
    EXPECT_EQ(make_par(data).sum(pool), 0);
    EXPECT_FALSE(make_par(data).any(pool, [](int) { return true; }));
    EXPECT_TRUE(make_par(data).all(pool, [](int) { return false; }));
}

TEST(ParRange, Collect) {
    std::vector<int> data{1, 2, 3};
    auto copy = make_par(data).collect<std::vector<int>>();
    ASSERT_EQ(copy.size(), 3u);
    EXPECT_EQ(copy[0], 1);
    EXPECT_EQ(copy[2], 3);
}

TEST(ParRange, FromSpan) {
    TaskPool pool;
    std::vector<int> data{10, 20, 30};
    auto s = make_par(std::span{data}).sum(pool);
    EXPECT_EQ(s, 60);
}

// ── par_chunk_map / par_splat_map ────────────────────────────────────────────

TEST(Slice, ParChunkMap) {
    TaskPool pool;
    std::vector<int> data(20, 0);
    std::iota(data.begin(), data.end(), 0);
    auto results =
        par_chunk_map<int>(pool, std::span{data}, 5, [](size_t, std::span<const int> chunk) { return chunk.size(); });
    ASSERT_EQ(results.size(), 4u);
    for (auto r : results) EXPECT_EQ(r, 5);
}

TEST(Slice, ParSplatMap) {
    TaskPool pool;
    std::vector<int> data(100, 0);
    std::iota(data.begin(), data.end(), 0);
    auto results = par_splat_map<int>(pool, std::span{data}, std::nullopt,
                                      [](size_t, std::span<const int> chunk) { return chunk.size(); });
    EXPECT_GT(results.size(), 0u);
}

TEST(Slice, Empty) {
    TaskPool pool;
    std::vector<int> data;
    auto results = par_chunk_map<int>(pool, std::span{data}, 2, [](size_t, std::span<const int>) { return 0; });
    EXPECT_TRUE(results.empty());
}

// ── Busy / Idle behavior tests ───────────────────────────────────────────────

TEST(Behavior, BusyManyTasks) {
    auto pool = TaskPoolBuilder{}.num_threads(4).build();
    auto t0   = std::chrono::steady_clock::now();
    pool.scope<int>([](Scope<int>& s) {
        for (int i = 0; i < 40; ++i) {
            s.spawn([i]() -> int {
                auto start = std::chrono::steady_clock::now();
                while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(10)) {
                    // spin
                }
                return i;
            });
        }
    });
    auto elapsed = std::chrono::steady_clock::now() - t0;
    // With 4 threads and 40 tasks of 10ms each, should finish well under 500ms
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 500);
}

TEST(Behavior, IdleSingleTask) {
    auto pool = TaskPoolBuilder{}.build();
    pool.scope<int>([](Scope<int>& s) {
        s.spawn([]() -> int {
            auto start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(50)) {
                // spin
            }
            return 0;
        });
    });
    // Single task should complete fine
}

// ── Sender-based tasks via TaskPool ────────────────────────────────────────

TEST(SenderTask, TaskPoolSpawnSender) {
    TaskPool pool;
    auto task = pool.spawn(STDEXEC::just(42));
    while (!task.is_finished()) std::this_thread::sleep_for(1ms);
    EXPECT_TRUE(task.is_finished());
}

TEST(SenderTask, TaskPoolSpawnVoidSender) {
    std::atomic<bool> ran{false};
    TaskPool pool;
    auto task = pool.spawn(STDEXEC::just() | STDEXEC::then([&ran] { ran.store(true); }));
    while (!task.is_finished()) std::this_thread::sleep_for(1ms);
    EXPECT_TRUE(ran.load());
}
