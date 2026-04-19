// epix.tasks comprehensive test suite — covers all public API

#include <gtest/gtest.h>
#include <stdexec/execution.hpp>
#include <exec/task.hpp>
#include <exec/static_thread_pool.hpp>

import epix.tasks;
import std;

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

TEST(Task, CoAwaitReturnsValue) {
    TaskPool pool{2};
    auto coro = [&]() -> exec::task<int> {
        co_return co_await pool.spawn([]() { return 10; });
    };
    auto r = stdexec::sync_wait(stdexec::on(pool.get_scheduler(), coro()));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<0>(*r), 10);
}

TEST(Task, CoAwaitChainedTasks) {
    TaskPool pool{2};
    auto coro = [&]() -> exec::task<int> {
        int a = co_await pool.spawn([]() { return 3; });
        int b = co_await pool.spawn([]() { return 4; });
        co_return a + b;
    };
    auto r = stdexec::sync_wait(stdexec::on(pool.get_scheduler(), coro()));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<0>(*r), 7);
}

TEST(Task, CoAwaitExceptionPropagates) {
    TaskPool pool{1};
    auto coro = [&]() -> exec::task<int> {
        co_return co_await pool.spawn([]() -> int { throw std::runtime_error("boom"); });
    };
    EXPECT_THROW(
        stdexec::sync_wait(stdexec::on(pool.get_scheduler(), coro())),
        std::runtime_error
    );
}

TEST(Task, CoAwaitAlreadyFinishedSkipsSuspend) {
    auto [task, state] = Task<int>::make();
    state->set_value(55);
    TaskPool pool{1};
    auto coro = [t = std::move(task)]() mutable -> exec::task<int> {
        co_return co_await std::move(t);
    };
    auto r = stdexec::sync_wait(stdexec::on(pool.get_scheduler(), coro()));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(std::get<0>(*r), 55);
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

TEST(TaskVoid, CoAwaitCompletes) {
    TaskPool pool{2};
    std::atomic<int> x{0};
    auto coro = [&]() -> exec::task<void> {
        co_await pool.spawn([&]() { x.store(99); });
    };
    stdexec::sync_wait(stdexec::on(pool.get_scheduler(), coro()));
    EXPECT_EQ(x.load(), 99);
}

TEST(TaskVoid, CoAwaitExceptionPropagates) {
    TaskPool pool{1};
    auto coro = [&]() -> exec::task<void> {
        co_await pool.spawn([]() { throw std::runtime_error("void boom"); });
    };
    EXPECT_THROW(
        stdexec::sync_wait(stdexec::on(pool.get_scheduler(), coro())),
        std::runtime_error
    );
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
    for (int i = 0; i < 16; ++i)
        tasks.push_back(pool.spawn([i]() { return i * i; }));
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
    EXPECT_EQ(r[0], 1); EXPECT_EQ(r[1], 2); EXPECT_EQ(r[2], 3);
}

TEST(TaskPool, GetSchedulerUsable) {
    TaskPool pool{1};
    std::atomic<bool> ran{false};
    stdexec::sync_wait(
        stdexec::schedule(pool.get_scheduler())
        | stdexec::then([&]() { ran.store(true); })
    );
    EXPECT_TRUE(ran.load());
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
    // Verify the builder fluent API compiles and pool works correctly
    auto pool = TaskPoolBuilder{}
        .num_threads(2)
        .on_thread_spawn([]() {})
        .on_thread_destroy([]() {})
        .build();
    EXPECT_EQ(pool.thread_num(), 2u);
    EXPECT_EQ(*pool.spawn([]() { return 1; }).block(), 1);
}

TEST(TaskPoolBuilder, ThreadNameAccepted) {
    auto pool = TaskPoolBuilder{}.num_threads(1).thread_name("test-pool").build();
    EXPECT_EQ(pool.thread_num(), 1u);
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
    EXPECT_EQ(r[0], 10); EXPECT_EQ(r[1], 20);
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
    EXPECT_EQ(r[0], 1); EXPECT_EQ(r[1], 2); EXPECT_EQ(r[2], 3);
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

TEST(GlobalPools, ComputePoolAccessor) {
    TaskPool& inner = ComputeTaskPool::get().pool();
    EXPECT_GT(inner.thread_num(), 0u);
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
    EXPECT_EQ(r[0], 1); EXPECT_EQ(r[1], 2);
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
    auto v = p.spawn([]() { return 42; }).block();
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
    auto r = par_chunk_map<int>(std::span<const int>(data), pool, 2,
        [](std::size_t, std::span<const int> c) {
            int s = 0; for (auto v : c) s += v; return s;
        });
    std::sort(r.begin(), r.end());
    ASSERT_EQ(r.size(), 4u);
    EXPECT_EQ(r[0], 3); EXPECT_EQ(r[1], 7); EXPECT_EQ(r[2], 11); EXPECT_EQ(r[3], 15);
}

TEST(ParallelSlice, ParChunkMapSingleChunk) {
    TaskPool pool{2};
    const std::vector<int> data = {1, 2, 3};
    auto r = par_chunk_map<int>(std::span<const int>(data), pool, 10,
        [](std::size_t, std::span<const int> c) {
            int s = 0; for (auto v : c) s += v; return s;
        });
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0], 6);
}

TEST(ParallelSlice, ParSplatMapSumsTotal) {
    TaskPool pool{2};
    std::vector<int> data(100);
    std::iota(data.begin(), data.end(), 0);
    auto r = par_splat_map<int>(std::span<const int>(data), pool, std::nullopt,
        [](std::size_t, std::span<const int> c) {
            int s = 0; for (auto v : c) s += v; return s;
        });
    int total = 0; for (auto v : r) total += v;
    EXPECT_EQ(total, 4950);
}

TEST(ParallelSlice, ParSplatMapMaxTasks1) {
    TaskPool pool{2};
    const std::vector<int> data = {1, 2, 3, 4, 5};
    auto r = par_splat_map<int>(std::span<const int>(data), pool, std::size_t{1},
        [](std::size_t, std::span<const int> c) {
            int s = 0; for (auto v : c) s += v; return s;
        });
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0], 15);
}

TEST(ParallelSlice, ParChunkMapMut) {
    TaskPool pool{2};
    std::vector<int> data = {1, 2, 3, 4};
    auto r = par_chunk_map_mut<int>(std::span<int>(data), pool, 2,
        [](std::size_t, std::span<int> c) {
            int s = 0; for (auto v : c) s += v; return s;
        });
    std::sort(r.begin(), r.end());
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0], 3); EXPECT_EQ(r[1], 7);
}

TEST(ParallelSlice, ParSplatMapMut) {
    TaskPool pool{2};
    std::vector<int> data(6, 1);
    auto r = par_splat_map_mut<int>(std::span<int>(data), pool, std::nullopt,
        [](std::size_t, std::span<int> c) {
            int s = 0; for (auto v : c) s += v; return s;
        });
    int total = 0; for (auto v : r) total += v;
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

TEST(ThreadExecutor, GetSchedulerUsable) {
    ThreadExecutor exec;
    std::atomic<bool> ran{false};
    stdexec::sync_wait(
        stdexec::schedule(exec.get_scheduler())
        | stdexec::then([&]() { ran.store(true); })
    );
    EXPECT_TRUE(ran.load());
}

// ─── ThreadExecutorTicker ────────────────────────────────────────────────────

TEST(ThreadExecutorTicker, TryTickReturnsFalse) {
    ThreadExecutor exec;
    auto ticker = exec.ticker();
    ASSERT_TRUE(ticker.has_value());
    EXPECT_FALSE(ticker->try_tick());
}
