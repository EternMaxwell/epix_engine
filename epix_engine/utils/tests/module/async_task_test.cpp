// ── Module version: unit tests for epix.async_task ───────────────────────

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>

import epix.async_task;

using namespace epix::async_task;

// ── Test helpers ─────────────────────────────────────────────────────────

/// Simple executor: posts Runnable via asio::io_context.
class TestExecutor {
   public:
    TestExecutor() : m_ioc(), m_work(asio::make_work_guard(m_ioc)) {}

    void schedule(Runnable r, ScheduleInfo info) {
        m_schedule_count++;
        m_last_info = info;
        asio::post(m_ioc, [r = std::move(r)]() mutable { r.run(); });
    }

    void run_one() { m_ioc.poll_one(); }

    void run_for(std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) { m_ioc.run_for(timeout); }

    void run() { m_ioc.run(); }

    void stop() { m_work.reset(); }

    int schedule_count() const { return m_schedule_count; }
    ScheduleInfo last_info() const { return m_last_info; }

   private:
    asio::io_context m_ioc;
    asio::executor_work_guard<asio::io_context::executor_type> m_work;
    int m_schedule_count     = 0;
    ScheduleInfo m_last_info = ScheduleInfo::New;
};

// ── spawn() basics ───────────────────────────────────────────────────────

TEST(AsyncTaskModuleTest, SpawnReturnsRunnableAndTask) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 42; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    EXPECT_FALSE(task.is_finished());
    EXPECT_FALSE(runnable.is_done());
}

TEST(AsyncTaskModuleTest, SpawnVoidWork) {
    TestExecutor ex;
    bool called = false;
    auto [runnable, task] =
        spawn([&called] { called = true; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();
    ex.run_one();

    EXPECT_TRUE(task.is_finished());
    EXPECT_TRUE(called);
}

// ── Basic execution flow ─────────────────────────────────────────────────

TEST(AsyncTaskModuleTest, ScheduleAndRunReturnsCorrectValue) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 99; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();
    ex.run_one();

    EXPECT_TRUE(task.is_finished());
    EXPECT_TRUE(runnable.is_done());
}

TEST(AsyncTaskModuleTest, CoAwaitTaskWithAsio) {
    asio::io_context ioc;
    int result = 0;

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            auto [runnable, task] =
                spawn([] { return 7; },
                      [&ioc](Runnable r, ScheduleInfo) { asio::post(ioc, [r = std::move(r)]() mutable { r.run(); }); });
            runnable.schedule();
            result = co_await std::move(task);
        },
        asio::detached);

    ioc.run();

    EXPECT_EQ(result, 7);
}

TEST(AsyncTaskModuleTest, CoAwaitFallibleTask) {
    asio::io_context ioc;
    std::optional<int> result;

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            auto [runnable, task] =
                spawn([] { return 42; },
                      [&ioc](Runnable r, ScheduleInfo) { asio::post(ioc, [r = std::move(r)]() mutable { r.run(); }); });
            runnable.schedule();
            auto ft = std::move(task).fallible();
            result  = co_await std::move(ft);
        },
        asio::detached);

    ioc.run();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

// ── Cancellation ─────────────────────────────────────────────────────────

TEST(AsyncTaskModuleTest, CancelBeforeScheduleReturnsNullopt) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 10; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    asio::io_context ioc;
    std::optional<int> result;
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { result = co_await std::move(task).cancel(); }, asio::detached);
    ioc.run();

    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(runnable.is_done());
}

TEST(AsyncTaskModuleTest, CancelVoidTaskReturnsFalse) {
    TestExecutor ex;
    auto [runnable, task] = spawn([] {}, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    asio::io_context ioc;
    bool result = true;
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { result = co_await std::move(task).cancel(); }, asio::detached);
    ioc.run();

    EXPECT_FALSE(result);
}

TEST(AsyncTaskModuleTest, CancelCompletedTaskReturnsValue) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 100; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();
    ex.run_one();
    EXPECT_TRUE(task.is_finished());

    asio::io_context ioc;
    std::optional<int> result;
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { result = co_await std::move(task).cancel(); }, asio::detached);
    ioc.run();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 100);
}

// ── Detach ───────────────────────────────────────────────────────────────

TEST(AsyncTaskModuleTest, DetachDoesNotCancel) {
    TestExecutor ex;
    std::atomic<bool> executed{false};

    {
        auto [runnable, task] = spawn(
            [&executed] {
                executed.store(true);
                return 0;
            },
            [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });
        runnable.schedule();
        task.detach();
    }

    ex.run_one();
    EXPECT_TRUE(executed.load());
}

// ── Destructor behaviour ─────────────────────────────────────────────────

TEST(AsyncTaskModuleTest, DestructorCancelsTask) {
    TestExecutor ex;
    bool executed = false;

    {
        auto [runnable, task] = spawn(
            [&executed] {
                executed = true;
                return 0;
            },
            [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });
    }

    EXPECT_FALSE(executed);
}

// ── is_finished() ────────────────────────────────────────────────────────

TEST(AsyncTaskModuleTest, IsFinishedReportsCorrectly) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 5; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    EXPECT_FALSE(task.is_finished());
    runnable.schedule();
    ex.run_one();
    EXPECT_TRUE(task.is_finished());
}

// ── FallibleTask ─────────────────────────────────────────────────────────

TEST(AsyncTaskModuleTest, FallibleTaskCoAwaitOnSuccess) {
    asio::io_context ioc;
    std::optional<std::string> result;

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            auto [runnable, task] =
                spawn([] -> std::string { return "hello"; },
                      [&ioc](Runnable r, ScheduleInfo) { asio::post(ioc, [r = std::move(r)]() mutable { r.run(); }); });
            runnable.schedule();
            auto ft = std::move(task).fallible();
            result  = co_await std::move(ft);
        },
        asio::detached);

    ioc.run();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "hello");
}

TEST(AsyncTaskModuleTest, FallibleTaskCancelReturnsNullopt) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 5; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    auto ft = std::move(task).fallible();

    asio::io_context ioc;
    std::optional<int> result;
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { result = co_await std::move(ft).cancel(); }, asio::detached);
    ioc.run();

    EXPECT_FALSE(result.has_value());
}

TEST(AsyncTaskModuleTest, FallibleVoidCoAwait) {
    asio::io_context ioc;
    bool success = false;

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            auto [runnable, task] =
                spawn([] {},
                      [&ioc](Runnable r, ScheduleInfo) { asio::post(ioc, [r = std::move(r)]() mutable { r.run(); }); });
            runnable.schedule();
            auto ft = std::move(task).fallible();
            success = co_await std::move(ft);
        },
        asio::detached);

    ioc.run();

    EXPECT_TRUE(success);
}

// ── Waker behaviour ──────────────────────────────────────────────────────

TEST(AsyncTaskModuleTest, WakerWakesWithScheduleInfoWake) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    auto w = runnable.waker();
    std::move(w).wake();

    EXPECT_EQ(ex.last_info(), ScheduleInfo::Wake);
    EXPECT_GE(ex.schedule_count(), 1);
}

TEST(AsyncTaskModuleTest, WakerWakeByRef) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    auto w = runnable.waker();
    w.wake_by_ref();

    EXPECT_EQ(ex.last_info(), ScheduleInfo::Wake);
    EXPECT_TRUE(static_cast<bool>(w));
    ex.run_one();
    EXPECT_TRUE(task.is_finished());
}

// ── ScheduleInfo ─────────────────────────────────────────────────────────

TEST(AsyncTaskModuleTest, ScheduleInfoNewOnRunnableSchedule) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();
    EXPECT_EQ(ex.last_info(), ScheduleInfo::New);
}

// ── Exception handling ───────────────────────────────────────────────────

TEST(AsyncTaskModuleTest, FallibleTaskCatchesException) {
    asio::io_context ioc;
    std::optional<int> result;

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            auto [runnable, task] =
                spawn([]() -> int { throw std::runtime_error("fail"); },
                      [&ioc](Runnable r, ScheduleInfo) { asio::post(ioc, [r = std::move(r)]() mutable { r.run(); }); });
            runnable.schedule();
            auto ft = std::move(task).fallible();
            result  = co_await std::move(ft);
        },
        asio::detached);

    ioc.run();
    EXPECT_FALSE(result.has_value());
}

// ── Default-constructed handles ──────────────────────────────────────────

TEST(AsyncTaskModuleTest, DefaultHandlesAreSafe) {
    Task<int> t;
    EXPECT_TRUE(t.is_finished());
    t.detach();

    Task<void> tv;
    EXPECT_TRUE(tv.is_finished());
    tv.detach();

    Runnable r;
    EXPECT_TRUE(r.is_done());
    EXPECT_FALSE(r.run());
    r.schedule();

    FallibleTask<int> ft;
    EXPECT_TRUE(ft.is_finished());
    ft.detach();

    FallibleTask<void> ftv;
    EXPECT_TRUE(ftv.is_finished());
    ftv.detach();

    Waker w;
    std::move(w).wake();
    w.wake_by_ref();
}

// ── Move semantics ───────────────────────────────────────────────────────

TEST(AsyncTaskModuleTest, TaskMoveSemantics) {
    TestExecutor ex;
    auto [runnable, task1] =
        spawn([] { return 42; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    Task<int> task2 = std::move(task1);
    EXPECT_FALSE(task2.is_finished());

    runnable.schedule();
    ex.run_one();
    EXPECT_TRUE(task2.is_finished());
}

// ── Co-await Task<void> ───────────────────────────────────────────────────

TEST(AsyncTaskModuleTest, CoAwaitVoidTask) {
    asio::io_context ioc;
    bool completed = false;

    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            auto [runnable, task] =
                spawn([&completed] { completed = true; },
                      [&ioc](Runnable r, ScheduleInfo) { asio::post(ioc, [r = std::move(r)]() mutable { r.run(); }); });
            runnable.schedule();
            co_await std::move(task);
        },
        asio::detached);

    ioc.run();

    EXPECT_TRUE(completed);
}

// ── Schedule after completion ────────────────────────────────────────────

TEST(AsyncTaskModuleTest, ScheduleAfterCompletionIsNoOp) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();
    ex.run_one();
    EXPECT_TRUE(task.is_finished());

    int before = ex.schedule_count();
    runnable.schedule();
    EXPECT_EQ(ex.schedule_count(), before);
}

// ── Duplicate schedule is safe ───────────────────────────────────────────

TEST(AsyncTaskModuleTest, DuplicateScheduleIsSafe) {
    TestExecutor ex;
    std::atomic<int> count{0};
    auto [runnable, task] = spawn(
        [&count] {
            count.fetch_add(1);
            return 1;
        },
        [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();
    runnable.schedule();
    ex.run_one();
    ex.run_one();

    EXPECT_EQ(count.load(), 1);
    EXPECT_TRUE(task.is_finished());
}
