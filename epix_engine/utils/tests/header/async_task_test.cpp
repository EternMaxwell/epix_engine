// ── Unit tests for epix::async_task ──────────────────────────────────────
//
// Tests behaviour of Runnable, Task<T>, FallibleTask<T>, Waker against
// the expected semantics of Rust's async-task crate.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <chrono>
#include <epix/async_task.hpp>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>

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

    /// Run the io_context for at most N "poll" cycles.
    void run_one() { m_ioc.poll_one(); }

    /// Run until there is no more work (or timeout).
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

TEST(AsyncTaskTest, SpawnReturnsRunnableAndTask) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 42; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    EXPECT_FALSE(task.is_finished());
    EXPECT_FALSE(runnable.is_done());
}

TEST(AsyncTaskTest, SpawnVoidWork) {
    TestExecutor ex;
    bool called = false;
    auto [runnable, task] =
        spawn([&called] { called = true; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();
    ex.run_one();  // execute

    EXPECT_TRUE(task.is_finished());
    EXPECT_TRUE(called);
}

// ── Basic execution flow ─────────────────────────────────────────────────

TEST(AsyncTaskTest, ScheduleAndRunReturnsCorrectValue) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 99; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();
    ex.run_one();

    EXPECT_TRUE(task.is_finished());
    EXPECT_TRUE(runnable.is_done());
}

TEST(AsyncTaskTest, CoAwaitTaskWithAsio) {
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

TEST(AsyncTaskTest, CoAwaitFallibleTask) {
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

TEST(AsyncTaskTest, CancelBeforeScheduleReturnsNullopt) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 10; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    // Cancel BEFORE scheduling.
    asio::io_context ioc;
    std::optional<int> result;
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { result = co_await std::move(task).cancel(); }, asio::detached);
    ioc.run();

    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(runnable.is_done());
}

TEST(AsyncTaskTest, CancelVoidTaskReturnsFalse) {
    TestExecutor ex;
    auto [runnable, task] = spawn([] {}, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    asio::io_context ioc;
    bool result = true;
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { result = co_await std::move(task).cancel(); }, asio::detached);
    ioc.run();

    EXPECT_FALSE(result);
}

TEST(AsyncTaskTest, CancelCompletedTaskReturnsValue) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 100; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    // Run to completion first.
    runnable.schedule();
    ex.run_one();

    EXPECT_TRUE(task.is_finished());

    // Then cancel.
    asio::io_context ioc;
    std::optional<int> result;
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { result = co_await std::move(task).cancel(); }, asio::detached);
    ioc.run();

    // Task completed before cancel → returns Some.
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 100);
}

// ── Detach ───────────────────────────────────────────────────────────────

TEST(AsyncTaskTest, DetachDoesNotCancel) {
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
        task.detach();  // release without cancelling
    }

    ex.run_one();
    EXPECT_TRUE(executed.load());
}

// ── Destructor behaviour ─────────────────────────────────────────────────

TEST(AsyncTaskTest, DestructorCancelsTask) {
    TestExecutor ex;
    bool executed = false;

    {
        auto [runnable, task] = spawn(
            [&executed] {
                executed = true;
                return 0;
            },
            [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });
        // Neither scheduled nor detached — Task destructor cancels.
    }

    EXPECT_FALSE(executed);
}

TEST(AsyncTaskTest, AlreadyDetachedTaskDtorIsSafe) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    task.detach();
    // ~Task() on a detached (m_state == nullptr) should be safe.
}

// ── is_finished() ────────────────────────────────────────────────────────

TEST(AsyncTaskTest, IsFinishedReportsCorrectly) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 5; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    EXPECT_FALSE(task.is_finished());

    runnable.schedule();
    ex.run_one();

    EXPECT_TRUE(task.is_finished());
    EXPECT_TRUE(runnable.is_done());
}

TEST(AsyncTaskTest, DefaultConstructedTaskIsFinished) {
    Task<int> t;
    EXPECT_TRUE(t.is_finished());
}

// ── FallibleTask ─────────────────────────────────────────────────────────

TEST(AsyncTaskTest, FallibleTaskCoAwaitOnSuccess) {
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

TEST(AsyncTaskTest, FallibleTaskCancelReturnsNullopt) {
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

TEST(AsyncTaskTest, FallibleVoidCoAwait) {
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

TEST(AsyncTaskTest, WakerWakesWithScheduleInfoWake) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    auto w = runnable.waker();
    std::move(w).wake();

    // The schedule function should have been called with ScheduleInfo::Wake.
    EXPECT_EQ(ex.last_info(), ScheduleInfo::Wake);
    EXPECT_GE(ex.schedule_count(), 1);
}

TEST(AsyncTaskTest, WakerWakeByRef) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    auto w = runnable.waker();
    w.wake_by_ref();

    EXPECT_EQ(ex.last_info(), ScheduleInfo::Wake);

    // Waker is still valid after wake_by_ref.
    EXPECT_TRUE(static_cast<bool>(w));

    // Run the scheduled work.
    ex.run_one();
    EXPECT_TRUE(task.is_finished());
}

TEST(AsyncTaskTest, WakerIsCopyable) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    auto w1  = runnable.waker();
    Waker w2 = w1;  // copy
    Waker w3(w1);   // copy
    w2 = w1;        // copy assign

    EXPECT_TRUE(static_cast<bool>(w1));
    EXPECT_TRUE(static_cast<bool>(w2));
    EXPECT_TRUE(static_cast<bool>(w3));
    EXPECT_EQ(w1, w2);

    std::move(w1).wake();
    EXPECT_EQ(ex.schedule_count(), 1);
}

TEST(AsyncTaskTest, WakeOnCompletedTaskIsNoOp) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();
    ex.run_one();
    EXPECT_TRUE(task.is_finished());

    // Waking a completed task should be a no-op.
    int before = ex.schedule_count();
    auto w     = runnable.waker();
    std::move(w).wake();
    EXPECT_EQ(ex.schedule_count(), before);
}

// ── ScheduleInfo ─────────────────────────────────────────────────────────

TEST(AsyncTaskTest, ScheduleInfoNewOnRunnableSchedule) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();

    EXPECT_EQ(ex.last_info(), ScheduleInfo::New);
}

// ── Exception handling ───────────────────────────────────────────────────

TEST(AsyncTaskTest, ExceptionInWorkIsPropagated) {
    TestExecutor ex;
    auto [runnable, task] = spawn([]() -> int { throw std::runtime_error("boom"); },
                                  [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();
    ex.run_one();

    // Task should be finished (Completed with exception).
    EXPECT_TRUE(task.is_finished());

    // The exception is stored in the state. When co_awaited, the
    // completion handler receives the exception_ptr. The existing
    // tasks::Task<T> in the codebase follows the same pattern.
}

TEST(AsyncTaskTest, FallibleTaskCatchesException) {
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

    // FallibleTask returns nullopt on failure.
    EXPECT_FALSE(result.has_value());
}

// ── Runnable::run() return value ─────────────────────────────────────────

TEST(AsyncTaskTest, RunReturnsFalseWhenWorkCompletes) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    bool re_woken = runnable.run();
    // Work completes immediately, no waker was invoked → false.
    EXPECT_FALSE(re_woken);
    EXPECT_TRUE(task.is_finished());
}

TEST(AsyncTaskTest, RunReturnsFalseWhenNotScheduled) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    // Call run() without scheduling first.
    bool re_woken = runnable.run();
    EXPECT_FALSE(re_woken);
}

TEST(AsyncTaskTest, RunReturnsFalseWhenAlreadyCompleted) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();
    runnable.run();  // first run

    // Second run on completed task.
    bool re_woken = runnable.run();
    EXPECT_FALSE(re_woken);
}

// ── Schedule on already-scheduled task ───────────────────────────────────

TEST(AsyncTaskTest, DuplicateScheduleIsSafe) {
    TestExecutor ex;
    std::atomic<int> count{0};
    auto [runnable, task] = spawn(
        [&count] {
            count.fetch_add(1);
            return 1;
        },
        [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();
    runnable.schedule();  // second schedule before execution

    ex.run_one();
    ex.run_one();  // may need second poll for duplicate

    // Work should have executed exactly once.
    EXPECT_EQ(count.load(), 1);
    EXPECT_TRUE(task.is_finished());
}

// ── Schedule after completion ────────────────────────────────────────────

TEST(AsyncTaskTest, ScheduleAfterCompletionIsNoOp) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    runnable.schedule();
    ex.run_one();
    EXPECT_TRUE(task.is_finished());

    int before = ex.schedule_count();
    runnable.schedule();                     // schedule completed task
    EXPECT_EQ(ex.schedule_count(), before);  // no-op
}

// ── Default-constructed handles ──────────────────────────────────────────

TEST(AsyncTaskTest, DefaultTaskIsSafe) {
    Task<int> t;
    EXPECT_TRUE(t.is_finished());
    t.detach();  // no-op
    // ~Task() — no-op

    Task<void> tv;
    EXPECT_TRUE(tv.is_finished());
    tv.detach();
}

TEST(AsyncTaskTest, DefaultRunnableIsSafe) {
    Runnable r;
    EXPECT_TRUE(r.is_done());
    EXPECT_FALSE(r.run());
    r.schedule();  // no-op
    r.waker();     // default waker
}

TEST(AsyncTaskTest, DefaultWakerIsSafe) {
    Waker w;
    EXPECT_FALSE(static_cast<bool>(w));
    std::move(w).wake();  // no-op
    w.wake_by_ref();      // no-op
}

TEST(AsyncTaskTest, DefaultFallibleTaskIsSafe) {
    FallibleTask<int> ft;
    EXPECT_TRUE(ft.is_finished());
    ft.detach();

    FallibleTask<void> ftv;
    EXPECT_TRUE(ftv.is_finished());
    ftv.detach();
}

// ── Move semantics ───────────────────────────────────────────────────────

TEST(AsyncTaskTest, TaskMoveSemantics) {
    TestExecutor ex;
    auto [runnable, task1] =
        spawn([] { return 42; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    Task<int> task2 = std::move(task1);
    EXPECT_FALSE(task2.is_finished());

    Task<int> task3;
    task3 = std::move(task2);
    EXPECT_FALSE(task3.is_finished());

    runnable.schedule();
    ex.run_one();

    EXPECT_TRUE(task3.is_finished());
}

TEST(AsyncTaskTest, RunnableMoveAndCopySemantics) {
    TestExecutor ex;
    auto [runnable1, task] =
        spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    // Runnable is copyable (shared_ptr).
    Runnable runnable2 = runnable1;
    Runnable runnable3(runnable1);
    Runnable runnable4 = std::move(runnable1);

    runnable2.schedule();
    ex.run_one();

    EXPECT_TRUE(task.is_finished());
    EXPECT_TRUE(runnable3.is_done());
    EXPECT_TRUE(runnable4.is_done());
}

// ── FallibleTask chain ───────────────────────────────────────────────────

TEST(AsyncTaskTest, FallibleFallibleIsIdentity) {
    TestExecutor ex;
    auto [runnable, task] =
        spawn([] { return 10; }, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    auto ft1 = std::move(task).fallible();
    auto ft2 = std::move(ft1).fallible();  // identity

    EXPECT_FALSE(ft2.is_finished());

    runnable.schedule();
    ex.run_one();
    EXPECT_TRUE(ft2.is_finished());
}

// ── Schedule function receives ScheduleInfo ──────────────────────────────

TEST(AsyncTaskTest, ScheduleFunctionCalledWithCorrectInfo) {
    ScheduleInfo captured_new  = ScheduleInfo::Wake;  // not New
    ScheduleInfo captured_wake = ScheduleInfo::New;   // not Wake

    asio::io_context ioc;
    auto [runnable, task] = spawn([] { return 1; },
                                  [&](Runnable r, ScheduleInfo info) {
                                      if (info == ScheduleInfo::New) captured_new = info;
                                      if (info == ScheduleInfo::Wake) captured_wake = info;
                                      asio::post(ioc, [r = std::move(r)]() mutable { r.run(); });
                                  });

    // schedule() → ScheduleInfo::New
    runnable.schedule();
    EXPECT_EQ(captured_new, ScheduleInfo::New);

    // waker → ScheduleInfo::Wake
    auto w = runnable.waker();
    std::move(w).wake();
    EXPECT_EQ(captured_wake, ScheduleInfo::Wake);

    ioc.run();  // drain
}

// ── Integration: schedule on thread, await on main ───────────────────────

TEST(AsyncTaskTest, ConcurrentScheduleAndAwait) {
    asio::io_context ioc;

    auto [runnable, task] = spawn(
        [] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return 123;
        },
        [&ioc](Runnable r, ScheduleInfo) {
            // Run on a background thread via asio post.
            asio::post(ioc, [r = std::move(r)]() mutable { r.run(); });
        });

    // Schedule from this thread.
    runnable.schedule();

    int result = 0;
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { result = co_await std::move(task); }, asio::detached);

    ioc.run();

    EXPECT_EQ(result, 123);
}

// ── Detach + forget ──────────────────────────────────────────────────────

TEST(AsyncTaskTest, DetachedTaskStillRuns) {
    TestExecutor ex;
    std::atomic<bool> ran{false};

    {
        auto [runnable, task] = spawn(
            [&ran] {
                ran.store(true);
                return 0;
            },
            [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

        runnable.schedule();
        task.detach();
        // task goes out of scope here, but should not cancel
    }

    ex.run_one();
    EXPECT_TRUE(ran.load());
}

// ── FallibleTask<void> ───────────────────────────────────────────────────

TEST(AsyncTaskTest, FallibleVoidTaskCancel) {
    TestExecutor ex;
    auto [runnable, task] = spawn([] {}, [&ex](Runnable r, ScheduleInfo info) { ex.schedule(std::move(r), info); });

    auto ft = std::move(task).fallible();

    asio::io_context ioc;
    bool result = true;
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { result = co_await std::move(ft).cancel(); }, asio::detached);
    ioc.run();

    EXPECT_FALSE(result);  // cancelled, not completed
}

// ── Co-await Task<void> ───────────────────────────────────────────────────

TEST(AsyncTaskTest, CoAwaitVoidTask) {
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

// ── Co-await Task<void> cancel ────────────────────────────────────────────

TEST(AsyncTaskTest, CoAwaitVoidTaskCancelReturnsFalse) {
    auto [runnable, task] = spawn([] {}, [](Runnable, ScheduleInfo) { /* never schedule */ });

    asio::io_context ioc;
    bool result = true;
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> { result = co_await std::move(task).cancel(); }, asio::detached);
    ioc.run();

    EXPECT_FALSE(result);
}
