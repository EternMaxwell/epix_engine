// epix.tasks comprehensive test suite — covers all public API

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/use_awaitable.hpp>

import epix.tasks;
#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
using namespace epix::tasks;

// ─── Task<T> — basic lifecycle ───────────────────────────────────────────────

TEST(Task, DefaultConstructedIsFinished) {
    Task<int> t;
    EXPECT_TRUE(t.is_finished());
    EXPECT_FALSE(static_cast<bool>(t));
}

TEST(Task, MakeProducesUnfinishedTask) {
    auto [task, state] = Task<int>::make();
    EXPECT_FALSE(task.is_finished());
    EXPECT_TRUE(static_cast<bool>(task));
    state->set_value(0);  // fulfill to avoid dangling state
}

TEST(Task, BlockReturnsValue) {
    TaskPool pool{2};
    auto t = pool.spawn([]() { return 42; });
    auto v = t.block();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
}

TEST(Task, BlockRethrowsException) {
    TaskPool pool{1};
    auto t = pool.spawn([]() -> int { throw std::runtime_error("fail"); });
    EXPECT_THROW(t.block(), std::runtime_error);
}

TEST(Task, IsFinishedFalseBeforeDone) {
    auto [task, state] = Task<int>::make();
    EXPECT_FALSE(task.is_finished());
    state->set_value(0);
}

TEST(Task, IsFinishedTrueAfterBlock) {
    TaskPool pool{1};
    auto t = pool.spawn([]() { return 7; });
    t.block();
    EXPECT_TRUE(t.is_finished());
}

TEST(Task, DetachClearsHandle) {
    TaskPool pool{1};
    auto t = pool.spawn([]() { return 1; });
    EXPECT_TRUE(static_cast<bool>(t));
    t.detach();
    EXPECT_FALSE(static_cast<bool>(t));
    EXPECT_TRUE(t.is_finished());  // null state => is_finished
}

TEST(Task, DetachedTaskRunsToCompletion) {
    std::atomic<int> x{0};
    {
        TaskPool pool{1};
        pool.spawn([&]() { x.store(99); }).detach();
    }  // pool dtor joins threads => work completes
    EXPECT_EQ(x.load(), 99);
}

TEST(Task, BlockReturnsNulloptWhenNull) {
    Task<int> t;
    EXPECT_FALSE(t.block().has_value());
}

TEST(Task, OperatorBoolFalseAfterDone) {
    TaskPool pool{1};
    auto t = pool.spawn([]() { return 0; });
    t.block();
    EXPECT_FALSE(static_cast<bool>(t));
}

// ─── Task<T> — co_await ──────────────────────────────────────────────────────

// asio::awaitable<T> coroutines run on the pool.
// Task<T> is an async operation (has operator()(CompletionToken)),
// so `co_await task` works natively inside asio::awaitable<T>.

TEST(Task, CoAwaitTaskInAsioCoroutine) {
    TaskPool pool{2};
    auto coro = [&]() -> asio::awaitable<int> {
        // pool.spawn() returns Task<int>; co_await works directly
        int a = co_await pool.spawn([]() { return 3; });
        int b = co_await pool.spawn([]() { return 4; });
        co_return a + b;
    };
    auto v = pool.spawn(coro()).block();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 7);
}

TEST(Task, CoroutineReturnsValue) {
    TaskPool pool{2};
    auto v = pool.spawn([]() -> asio::awaitable<int> { co_return 10; }()).block();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 10);
}

TEST(Task, CoroutineSequentialWork) {
    TaskPool pool{2};
    auto coro = []() -> asio::awaitable<int> {
        int a = 3;
        int b = 4;
        co_return a + b;
    };
    auto v = pool.spawn(coro()).block();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 7);
}

TEST(Task, CoroutineExceptionPropagates) {
    TaskPool pool{1};
    auto coro = []() -> asio::awaitable<int> {
        throw std::runtime_error("boom");
        co_return 0;
    };
    EXPECT_THROW(pool.spawn(coro()).block(), std::runtime_error);
}

TEST(Task, CoAwaitAlreadyFinishedSkipsSuspend) {
    // Test that block() on a pre-fulfilled Task<T> returns immediately.
    auto [task, state] = Task<int>::make();
    state->set_value(55);
    EXPECT_TRUE(task.is_finished());
    auto v = task.block();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 55);
}

// ─── Task<void> ──────────────────────────────────────────────────────────────

TEST(TaskVoid, DefaultConstructedIsFinished) {
    Task<void> t;
    EXPECT_TRUE(t.is_finished());
    EXPECT_FALSE(static_cast<bool>(t));
}

TEST(TaskVoid, BlockCompletesWork) {
    TaskPool pool{1};
    std::atomic<int> x{0};
    auto t = pool.spawn([&]() { x.store(1); });
    t.block();
    EXPECT_EQ(x.load(), 1);
}

TEST(TaskVoid, BlockRethrowsException) {
    TaskPool pool{1};
    auto t = pool.spawn([]() { throw std::runtime_error("void fail"); });
    EXPECT_THROW(t.block(), std::runtime_error);
}

TEST(TaskVoid, IsFinishedAfterBlock) {
    TaskPool pool{1};
    auto t = pool.spawn([]() {});
    t.block();
    EXPECT_TRUE(t.is_finished());
}

TEST(TaskVoid, DetachRunsWork) {
    std::atomic<int> x{0};
    {
        TaskPool pool{1};
        pool.spawn([&]() { x.store(7); }).detach();
    }
    EXPECT_EQ(x.load(), 7);
}

TEST(TaskVoid, CoroutineCompletes) {
    TaskPool pool{2};
    std::atomic<int> x{0};
    auto coro = [&]() -> asio::awaitable<void> {
        // co_await Task<void> works natively inside asio::awaitable
        co_await pool.spawn([&]() { x.store(99); });
    };
    pool.spawn(coro()).block();
    EXPECT_EQ(x.load(), 99);
}

TEST(TaskVoid, CoroutineExceptionPropagates) {
    TaskPool pool{1};
    auto coro = [&]() -> asio::awaitable<void> {
        co_await pool.spawn([]() { throw std::runtime_error("void boom"); });
    };
    EXPECT_THROW(pool.spawn(coro()).block(), std::runtime_error);
}

// ─── TaskPool ────────────────────────────────────────────────────────────────

TEST(TaskPool, DefaultConstruct) {
    TaskPool pool;
    EXPECT_EQ(pool.thread_num(), std::thread::hardware_concurrency());
}

TEST(TaskPool, ExplicitThreadCount) {
    TaskPool pool{3};
    EXPECT_EQ(pool.thread_num(), 3u);
}

TEST(TaskPool, SpawnNonVoid) {
    TaskPool pool{2};
    auto v = pool.spawn([]() { return 42; }).block();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
}

TEST(TaskPool, SpawnVoid) {
    TaskPool pool{1};
    std::atomic<int> c{0};
    pool.spawn([&]() { c.fetch_add(1); }).block();
    EXPECT_EQ(c.load(), 1);
}

TEST(TaskPool, SpawnManyConcurrently) {
    TaskPool pool{4};
    std::vector<Task<int>> tasks;
    for (int i = 0; i < 16; ++i) tasks.push_back(pool.spawn([i]() { return i * i; }));
    for (int i = 0; i < 16; ++i) {
        auto v = tasks[i].block();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, i * i);
    }
}

TEST(TaskPool, ScopeEmpty) {
    TaskPool pool{2};
    EXPECT_TRUE(pool.scope<int>([](Scope<int>&) {}).empty());
}

TEST(TaskPool, ScopeCollectsResults) {
    TaskPool pool{2};
    auto r = pool.scope<int>([](Scope<int>& s) {
        s.spawn([]() { return 1; });
        s.spawn([]() { return 2; });
        s.spawn([]() { return 3; });
    });
    std::sort(r.begin(), r.end());
    ASSERT_EQ(r.size(), 3u);
    EXPECT_EQ(r[0], 1);
    EXPECT_EQ(r[1], 2);
    EXPECT_EQ(r[2], 3);
}

TEST(TaskPool, IoContextBackend) {
    TaskPool pool{2, TaskPoolBackend::IoContext};
    EXPECT_EQ(pool.thread_num(), 2u);
    auto v = pool.spawn([]() { return 55; }).block();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 55);
}

TEST(TaskPool, IoContextBackendVoid) {
    TaskPool pool{2, TaskPoolBackend::IoContext};
    std::atomic<int> x{0};
    pool.spawn([&x]() { x.store(77); }).block();
    EXPECT_EQ(x.load(), 77);
}

// ─── TaskPoolBuilder ─────────────────────────────────────────────────────────

TEST(TaskPoolBuilder, DefaultBuildsHWConcurrency) {
    auto pool = TaskPoolBuilder{}.build();
    EXPECT_EQ(pool.thread_num(), std::thread::hardware_concurrency());
}

TEST(TaskPoolBuilder, NumThreadsOverride) {
    auto pool = TaskPoolBuilder{}.num_threads(3).build();
    EXPECT_EQ(pool.thread_num(), 3u);
}

TEST(TaskPoolBuilder, CallbacksAccepted) {
    // Verify callbacks are invoked on the worker threads (requires IoContext backend)
    std::atomic<int> spawned{0}, destroyed{0};
    {
        auto pool = TaskPoolBuilder{}
                        .num_threads(2)
                        .on_thread_spawn([&spawned]() { spawned.fetch_add(1); })
                        .on_thread_destroy([&destroyed]() { destroyed.fetch_add(1); })
                        .build();
        EXPECT_EQ(pool.thread_num(), 2u);
        EXPECT_EQ(*pool.spawn([]() { return 1; }).block(), 1);
    }  // pool destroyed here — drain waits for threads to exit
    EXPECT_EQ(spawned.load(), 2);
    EXPECT_EQ(destroyed.load(), 2);
}

TEST(TaskPoolBuilder, ThreadNameAccepted) {
    auto pool = TaskPoolBuilder{}.num_threads(1).thread_name("test-pool").build();
    EXPECT_EQ(pool.thread_num(), 1u);
    EXPECT_EQ(*pool.spawn([]() { return 99; }).block(), 99);
}

TEST(TaskPoolBuilder, ExplicitIoContextBackend) {
    auto pool = TaskPoolBuilder{}.num_threads(2).backend(TaskPoolBackend::IoContext).build();
    EXPECT_EQ(pool.thread_num(), 2u);
    EXPECT_EQ(*pool.spawn([]() { return 7; }).block(), 7);
}

TEST(TaskPoolBuilder, ExplicitThreadPoolBackend) {
    auto pool = TaskPoolBuilder{}.num_threads(2).backend(TaskPoolBackend::ThreadPool).build();
    EXPECT_EQ(pool.thread_num(), 2u);
    EXPECT_EQ(*pool.spawn([]() { return 8; }).block(), 8);
}

// ─── Scope<T> ────────────────────────────────────────────────────────────────

TEST(Scope, SpawnAll) {
    TaskPool pool{2};
    auto r = pool.scope<int>([](Scope<int>& s) {
        for (int i = 0; i < 5; ++i) s.spawn([i]() { return i; });
    });
    std::sort(r.begin(), r.end());
    ASSERT_EQ(r.size(), 5u);
    for (int i = 0; i < 5; ++i) EXPECT_EQ(r[i], i);
}

TEST(Scope, SpawnOnScopeDelegates) {
    TaskPool pool{2};
    auto r = pool.scope<int>([](Scope<int>& s) {
        s.spawn_on_scope([]() { return 10; });
        s.spawn_on_scope([]() { return 20; });
    });
    std::sort(r.begin(), r.end());
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0], 10);
    EXPECT_EQ(r[1], 20);
}

TEST(Scope, MixedSpawnMethods) {
    TaskPool pool{2};
    auto r = pool.scope<int>([](Scope<int>& s) {
        s.spawn([]() { return 1; });
        s.spawn_on_scope([]() { return 2; });
        s.spawn([]() { return 3; });
    });
    std::sort(r.begin(), r.end());
    ASSERT_EQ(r.size(), 3u);
    EXPECT_EQ(r[0], 1);
    EXPECT_EQ(r[1], 2);
    EXPECT_EQ(r[2], 3);
}

// ─── Global singleton pools ───────────────────────────────────────────────────
// NOTE: each pool type uses static once_flag; initialised at most once per process.

TEST(GlobalPools, ComputeGetOrInit) {
    auto& p = ComputeTaskPool::get_or_init([]() { return TaskPool{2}; });
    EXPECT_EQ(p.thread_num(), 2u);
}

TEST(GlobalPools, ComputeSingleton) {
    auto& p1 = ComputeTaskPool::get_or_init([]() { return TaskPool{4}; });  // ignored
    auto& p2 = ComputeTaskPool::get();
    EXPECT_EQ(&p1, &p2);
}

TEST(GlobalPools, ComputeTryGetNonNull) {
    ComputeTaskPool::get_or_init([]() { return TaskPool{1}; });
    EXPECT_NE(ComputeTaskPool::try_get(), nullptr);
}

TEST(GlobalPools, ComputeSpawn) {
    auto v = ComputeTaskPool::get().spawn([]() { return 99; }).block();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 99);
}

TEST(GlobalPools, ComputeScope) {
    auto r = ComputeTaskPool::get().scope<int>([](Scope<int>& s) {
        s.spawn([]() { return 1; });
        s.spawn([]() { return 2; });
    });
    std::sort(r.begin(), r.end());
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0], 1);
    EXPECT_EQ(r[1], 2);
}

TEST(GlobalPools, AsyncComputeInit) {
    auto& p = AsyncComputeTaskPool::get_or_init([]() { return TaskPool{2}; });
    EXPECT_EQ(p.thread_num(), 2u);
    auto v = p.spawn([]() { return std::string("async"); }).block();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, "async");
}

TEST(GlobalPools, IoInit) {
    auto& p = IoTaskPool::get_or_init([]() { return TaskPool{2}; });
    auto v  = p.spawn([]() { return 42; }).block();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
}

// ─── now_or_never / check_ready ──────────────────────────────────────────────

TEST(Futures, NowOrNeverValueNotReady) {
    auto [task, state] = Task<int>::make();
    EXPECT_FALSE(now_or_never(task).has_value());
    state->set_value(0);
}

TEST(Futures, NowOrNeverValueReady) {
    TaskPool pool{1};
    auto t = pool.spawn([]() { return 42; });
    t.block();  // ensure done
    auto v = now_or_never(t);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
}

TEST(Futures, NowOrNeverVoidNotReady) {
    auto [task, state] = Task<void>::make();
    EXPECT_FALSE(now_or_never(task));
    state->set_done();
}

TEST(Futures, NowOrNeverVoidReady) {
    TaskPool pool{1};
    auto t = pool.spawn([]() {});
    t.block();
    EXPECT_TRUE(now_or_never(t));
}

TEST(Futures, CheckReadyValue) {
    TaskPool pool{1};
    auto t = pool.spawn([]() { return 5; });
    t.block();
    auto v = check_ready(t);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 5);
}

TEST(Futures, CheckReadyVoid) {
    TaskPool pool{1};
    auto t = pool.spawn([]() {});
    t.block();
    EXPECT_TRUE(check_ready(t));
}

// ─── ParallelSlice ───────────────────────────────────────────────────────────

TEST(ParallelSlice, ParChunkMap) {
    TaskPool pool{2};
    const std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8};
    auto r = par_chunk_map<int>(std::span<const int>(data), pool, 2, [](std::size_t, std::span<const int> c) {
        int s = 0;
        for (auto v : c) s += v;
        return s;
    });
    std::sort(r.begin(), r.end());
    ASSERT_EQ(r.size(), 4u);
    EXPECT_EQ(r[0], 3);
    EXPECT_EQ(r[1], 7);
    EXPECT_EQ(r[2], 11);
    EXPECT_EQ(r[3], 15);
}

TEST(ParallelSlice, ParChunkMapSingleChunk) {
    TaskPool pool{2};
    const std::vector<int> data = {1, 2, 3};
    auto r = par_chunk_map<int>(std::span<const int>(data), pool, 10, [](std::size_t, std::span<const int> c) {
        int s = 0;
        for (auto v : c) s += v;
        return s;
    });
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0], 6);
}

TEST(ParallelSlice, ParSplatMapSumsTotal) {
    TaskPool pool{2};
    std::vector<int> data(100);
    std::iota(data.begin(), data.end(), 0);
    auto r =
        par_splat_map<int>(std::span<const int>(data), pool, std::nullopt, [](std::size_t, std::span<const int> c) {
            int s = 0;
            for (auto v : c) s += v;
            return s;
        });
    int total = 0;
    for (auto v : r) total += v;
    EXPECT_EQ(total, 4950);
}

TEST(ParallelSlice, ParSplatMapMaxTasks1) {
    TaskPool pool{2};
    const std::vector<int> data = {1, 2, 3, 4, 5};
    auto r =
        par_splat_map<int>(std::span<const int>(data), pool, std::size_t{1}, [](std::size_t, std::span<const int> c) {
            int s = 0;
            for (auto v : c) s += v;
            return s;
        });
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0], 15);
}

TEST(ParallelSlice, ParChunkMapMut) {
    TaskPool pool{2};
    std::vector<int> data = {1, 2, 3, 4};
    auto r                = par_chunk_map_mut<int>(std::span<int>(data), pool, 2, [](std::size_t, std::span<int> c) {
        int s = 0;
        for (auto v : c) s += v;
        return s;
    });
    std::sort(r.begin(), r.end());
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0], 3);
    EXPECT_EQ(r[1], 7);
}

TEST(ParallelSlice, ParSplatMapMut) {
    TaskPool pool{2};
    std::vector<int> data(6, 1);
    auto r    = par_splat_map_mut<int>(std::span<int>(data), pool, std::nullopt, [](std::size_t, std::span<int> c) {
        int s = 0;
        for (auto v : c) s += v;
        return s;
    });
    int total = 0;
    for (auto v : r) total += v;
    EXPECT_EQ(total, 6);
}

// ─── ThreadExecutor ──────────────────────────────────────────────────────────

TEST(ThreadExecutor, SpawnNonVoid) {
    ThreadExecutor exec;
    auto v = exec.spawn([]() { return 123; }).block();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 123);
}

TEST(ThreadExecutor, SpawnVoid) {
    ThreadExecutor exec;
    std::atomic<int> x{0};
    exec.spawn([&]() { x.store(1); }).block();
    EXPECT_EQ(x.load(), 1);
}

TEST(ThreadExecutor, SpawnRethrowsException) {
    ThreadExecutor exec;
    auto t = exec.spawn([]() -> int { throw std::runtime_error("exec fail"); });
    EXPECT_THROW(t.block(), std::runtime_error);
}

TEST(ThreadExecutor, TickerOnOwnerThread) {
    ThreadExecutor exec;
    EXPECT_TRUE(exec.ticker().has_value());
}

TEST(ThreadExecutor, TickerNotOnOtherThread) {
    ThreadExecutor exec;
    std::optional<ThreadExecutorTicker> result;
    std::thread t([&]() {
        auto tmp = exec.ticker();
        if (tmp) result = std::move(tmp);
    });
    t.join();
    EXPECT_FALSE(result.has_value());
}

TEST(ThreadExecutor, IsSameSelf) {
    ThreadExecutor exec;
    EXPECT_TRUE(exec.is_same(exec));
}

TEST(ThreadExecutor, IsSameDifferent) {
    ThreadExecutor a, b;
    EXPECT_FALSE(a.is_same(b));
}

TEST(ThreadExecutor, SpawnRunsWork) {
    ThreadExecutor exec;
    std::atomic<bool> ran{false};
    exec.spawn([&]() { ran.store(true); }).block();
    EXPECT_TRUE(ran.load());
}

// ─── ThreadExecutorTicker ────────────────────────────────────────────────────

TEST(ThreadExecutorTicker, TryTickReturnsFalse) {
    ThreadExecutor exec;
    auto ticker = exec.ticker();
    ASSERT_TRUE(ticker.has_value());
    EXPECT_FALSE(ticker->try_tick());
}
