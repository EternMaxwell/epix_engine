// ── Module version: unit tests for epix.async_task ──────────────────────
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>

import epix.async_task;

using namespace epix::async_task;

// ── Custom coroutine for native co_await tests ────────────────────────────
struct tp;
struct tc {
    using promise_type = tp;
    std::coroutine_handle<tp> h;
    tc() : h(nullptr) {}
    explicit tc(std::coroutine_handle<tp> h_) : h(h_) {}
    tc(tc&& o) : h(std::exchange(o.h, nullptr)) {}
    ~tc() {
        if (h) h.destroy();
    }
};
struct tp {
    std::exception_ptr e;
    tc get_return_object() { return tc{std::coroutine_handle<tp>::from_promise(*this)}; }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { e = std::current_exception(); }
};

// ── Test executor ────────────────────────────────────────────────────────
class TestExecutor {
   public:
    TestExecutor() : m_ioc(), m_work(asio::make_work_guard(m_ioc)) {}
    void schedule(Runnable r, ScheduleInfo i) {
        m_sc++;
        m_li = i;
        asio::post(m_ioc, [r = std::move(r)]() mutable { r.run(); });
    }
    void run_one() { m_ioc.poll_one(); }
    void run() { m_ioc.run(); }
    void stop() { m_work.reset(); }
    int schedule_count() const { return m_sc; }
    ScheduleInfo last_info() const { return m_li; }

   private:
    asio::io_context m_ioc;
    asio::executor_work_guard<asio::io_context::executor_type> m_work;
    int m_sc          = 0;
    ScheduleInfo m_li = ScheduleInfo::New;
};

// ── spawn basics ─────────────────────────────────────────────────────────
TEST(AsyncTaskTest, SpawnReturnsRunnableAndTask) {
    TestExecutor ex;
    auto [r, t] = spawn([] { return 42; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    EXPECT_FALSE(t.is_finished());
    EXPECT_FALSE(r.is_done());
}
TEST(AsyncTaskTest, SpawnVoidWork) {
    TestExecutor ex;
    bool c      = false;
    auto [r, t] = spawn([&c] { c = true; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    r.schedule();
    ex.run_one();
    EXPECT_TRUE(t.is_finished());
    EXPECT_TRUE(c);
}
TEST(AsyncTaskTest, ScheduleAndRunReturnsCorrectValue) {
    TestExecutor ex;
    auto [r, t] = spawn([] { return 99; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    r.schedule();
    ex.run_one();
    EXPECT_TRUE(t.is_finished());
    EXPECT_TRUE(r.is_done());
}

// ── Cancellation ─────────────────────────────────────────────────────────
TEST(AsyncTaskTest, CancelBeforeScheduleReturnsNullopt) {
    TestExecutor ex;
    auto [r, t] = spawn([] { return 10; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    std::optional<int> v;
    auto c = [&]() -> tc { v = co_await std::move(t).cancel(); }();
    while (!c.h.done()) c.h.resume();
    EXPECT_FALSE(v.has_value());
}
TEST(AsyncTaskTest, CancelVoidTaskReturnsFalse) {
    auto [r, t] = spawn([] {}, [](Runnable, ScheduleInfo) {});
    bool ok     = true;
    auto c      = [&]() -> tc { ok = co_await std::move(t).cancel(); }();
    while (!c.h.done()) c.h.resume();
    EXPECT_FALSE(ok);
}
TEST(AsyncTaskTest, CancelCompletedTaskReturnsValue) {
    TestExecutor ex;
    auto [r, t] = spawn([] { return 100; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    r.schedule();
    ex.run_one();
    EXPECT_TRUE(t.is_finished());
    std::optional<int> v;
    auto c = [&]() -> tc { v = co_await std::move(t).cancel(); }();
    while (!c.h.done()) c.h.resume();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 100);
}

// ── Detach ───────────────────────────────────────────────────────────────
TEST(AsyncTaskTest, DetachDoesNotCancel) {
    TestExecutor ex;
    std::atomic<bool> e{false};
    {
        auto [r, t] = spawn(
            [&e] {
                e.store(true);
                return 0;
            },
            [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
        r.schedule();
        t.detach();
    }
    ex.run_one();
    EXPECT_TRUE(e.load());
}
TEST(AsyncTaskTest, DestructorCancelsTask) {
    TestExecutor ex;
    bool e = false;
    {
        auto [r, t] = spawn(
            [&e] {
                e = true;
                return 0;
            },
            [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    }
    EXPECT_FALSE(e);
}
TEST(AsyncTaskTest, AlreadyDetachedTaskDtorIsSafe) {
    TestExecutor ex;
    auto [r, t] = spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    t.detach();
}
TEST(AsyncTaskTest, IsFinishedReportsCorrectly) {
    TestExecutor ex;
    auto [r, t] = spawn([] { return 5; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    EXPECT_FALSE(t.is_finished());
    r.schedule();
    ex.run_one();
    EXPECT_TRUE(t.is_finished());
    EXPECT_TRUE(r.is_done());
}
TEST(AsyncTaskTest, DefaultConstructedTaskIsFinished) {
    Task<int> t;
    EXPECT_TRUE(t.is_finished());
}

// ── Waker ────────────────────────────────────────────────────────────────
TEST(AsyncTaskTest, WakerWakesWithScheduleInfoWake) {
    TestExecutor ex;
    auto [r, t] = spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    auto w      = r.waker();
    std::move(w).wake();
    EXPECT_EQ(ex.last_info(), ScheduleInfo::Wake);
    EXPECT_GE(ex.schedule_count(), 1);
}
TEST(AsyncTaskTest, WakerWakeByRef) {
    TestExecutor ex;
    auto [r, t] = spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    auto w      = r.waker();
    w.wake_by_ref();
    EXPECT_EQ(ex.last_info(), ScheduleInfo::Wake);
    EXPECT_TRUE(static_cast<bool>(w));
    ex.run_one();
    EXPECT_TRUE(t.is_finished());
}
TEST(AsyncTaskTest, WakerIsCopyable) {
    TestExecutor ex;
    auto [r, t] = spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    auto w1     = r.waker();
    Waker w2    = w1;
    Waker w3(w1);
    w2 = w1;
    EXPECT_TRUE(static_cast<bool>(w1));
    EXPECT_TRUE(static_cast<bool>(w2));
    EXPECT_TRUE(static_cast<bool>(w3));
    EXPECT_EQ(w1, w2);
    std::move(w1).wake();
    EXPECT_EQ(ex.schedule_count(), 1);
}
TEST(AsyncTaskTest, WakeOnCompletedTaskIsNoOp) {
    TestExecutor ex;
    auto [r, t] = spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    r.schedule();
    ex.run_one();
    EXPECT_TRUE(t.is_finished());
    int b  = ex.schedule_count();
    auto w = r.waker();
    std::move(w).wake();
    EXPECT_EQ(ex.schedule_count(), b);
}
TEST(AsyncTaskTest, ScheduleInfoNewOnRunnableSchedule) {
    TestExecutor ex;
    auto [r, t] = spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    r.schedule();
    EXPECT_EQ(ex.last_info(), ScheduleInfo::New);
}

// ── Exception ────────────────────────────────────────────────────────────
TEST(AsyncTaskTest, ExceptionInWorkIsPropagated) {
    auto [r, t] = spawn([]() -> int { throw std::runtime_error("boom"); }, [](Runnable r, ScheduleInfo) { r.run(); });
    r.schedule();
    EXPECT_TRUE(t.is_finished());
}
TEST(AsyncTaskTest, FallibleTaskCatchesException) {
    auto [r, t] = spawn([]() -> int { throw std::runtime_error("fail"); }, [](Runnable r, ScheduleInfo) { r.run(); });
    r.schedule();
    auto ft = std::move(t).fallible();
    std::optional<int> v;
    auto c = [&]() -> tc { v = co_await std::move(ft); }();
    while (!c.h.done()) c.h.resume();
    EXPECT_FALSE(v.has_value());
}

// ── run() ────────────────────────────────────────────────────────────────
TEST(AsyncTaskTest, RunReturnsFalseWhenWorkCompletes) {
    auto [r, t] = spawn([] { return 1; }, [](Runnable, ScheduleInfo) {});
    r.schedule();
    EXPECT_FALSE(r.run());
}
TEST(AsyncTaskTest, RunReturnsFalseWhenNotScheduled) {
    auto [r, t] = spawn([] { return 1; }, [](Runnable, ScheduleInfo) {});
    EXPECT_FALSE(r.run());
}
TEST(AsyncTaskTest, RunReturnsFalseWhenAlreadyCompleted) {
    auto [r, t] = spawn([] { return 1; }, [](Runnable, ScheduleInfo) {});
    r.schedule();
    r.run();
    EXPECT_FALSE(r.run());
}
TEST(AsyncTaskTest, DuplicateScheduleIsSafe) {
    TestExecutor ex;
    auto [r, t] = spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    r.schedule();
    r.schedule();
    ex.run_one();
    EXPECT_TRUE(t.is_finished());
}
TEST(AsyncTaskTest, ScheduleAfterCompletionIsNoOp) {
    TestExecutor ex;
    auto [r, t] = spawn([] { return 1; }, [&ex](Runnable r, ScheduleInfo i) { ex.schedule(std::move(r), i); });
    r.schedule();
    ex.run_one();
    int b = ex.schedule_count();
    r.schedule();
    EXPECT_EQ(ex.schedule_count(), b);
}

// ── Default safety ───────────────────────────────────────────────────────
TEST(AsyncTaskTest, DefaultTaskIsSafe) {
    Task<int> t;
    EXPECT_FALSE(static_cast<bool>(t));
}
TEST(AsyncTaskTest, DefaultRunnableIsSafe) {
    Runnable r;
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_FALSE(r.run());
}
TEST(AsyncTaskTest, DefaultWakerIsSafe) {
    Waker w;
    EXPECT_FALSE(static_cast<bool>(w));
}
TEST(AsyncTaskTest, DefaultFallibleTaskIsSafe) {
    FallibleTask<int> ft;
    EXPECT_FALSE(static_cast<bool>(ft));
}

// ── Move semantics ───────────────────────────────────────────────────────
TEST(AsyncTaskTest, TaskMoveSemantics) {
    auto [r, t1] = spawn([] { return 42; }, [](Runnable, ScheduleInfo) {});
    Task<int> t2 = std::move(t1);
    EXPECT_FALSE(static_cast<bool>(t1));
    EXPECT_TRUE(static_cast<bool>(t2));
    r.schedule();
    r.run();
    EXPECT_TRUE(t2.is_finished());
}
TEST(AsyncTaskTest, RunnableMoveAndCopySemantics) {
    auto [r1, t] = spawn([] { return 1; }, [](Runnable, ScheduleInfo) {});
    Runnable r2  = std::move(r1);
    Runnable r3  = r2;
    EXPECT_TRUE(static_cast<bool>(r2));
    EXPECT_TRUE(static_cast<bool>(r3));
}
TEST(AsyncTaskTest, FallibleFallibleIsIdentity) {
    auto [r, t] = spawn([] { return 1; }, [](Runnable, ScheduleInfo) {});
    auto ft     = std::move(t).fallible();
    auto ft2    = std::move(ft).fallible();
    EXPECT_FALSE(ft2.is_finished());
}
TEST(AsyncTaskTest, ScheduleFunctionCalledWithCorrectInfo) {
    ScheduleInfo c = ScheduleInfo::Wake;
    auto [r, t]    = spawn([] { return 1; }, [&c](Runnable r, ScheduleInfo i) { c = i; });
    r.schedule();
    EXPECT_EQ(c, ScheduleInfo::New);
    auto w = r.waker();
    std::move(w).wake();
    EXPECT_EQ(c, ScheduleInfo::Wake);
}
TEST(AsyncTaskTest, ConcurrentScheduleAndAwait) {
    std::atomic<bool> d{false};
    auto [r, t] = spawn(
        [&d] {
            d.store(true);
            return 1;
        },
        [](Runnable r, ScheduleInfo) { std::thread([r = std::move(r)]() mutable { r.run(); }).detach(); });
    r.schedule();
    while (!d.load()) std::this_thread::yield();
    while (!t.is_finished()) std::this_thread::yield();
    EXPECT_TRUE(t.is_finished());
}
TEST(AsyncTaskTest, DetachedTaskStillRuns) {
    std::atomic<bool> ran{false};
    {
        auto [r, t] = spawn([&ran] { ran.store(true); }, [](Runnable r, ScheduleInfo) { r.run(); });
        r.schedule();
        t.detach();
    }
    EXPECT_TRUE(ran.load());
}

// ── Integration: schedule via io_context, drive via event loop ───────────
//
// The schedule function posts to asio::io_context.  When run() returns
// true, the handler re-posts itself for another poll.

TEST(AsyncTaskCoroutine, ScheduleOnIoContext) {
    asio::io_context ioc;
    auto [r, t] = spawn([]() -> asio::awaitable<int> { co_return 42; },
                        [&ioc](Runnable r, ScheduleInfo) {
                            auto runner = std::make_shared<Runnable>(std::move(r));
                            std::function<void()> poll;
                            poll = [runner, &ioc, &poll]() {
                                if (runner->run()) asio::post(ioc, poll);
                            };
                            asio::post(ioc, poll);
                        });
    r.schedule();
    ioc.run();
    EXPECT_TRUE(t.is_finished());
}

TEST(AsyncTaskCoroutine, ScheduleOnIoContextAndCoAwait) {
    asio::io_context ioc;
    auto [r, t] = spawn([]() -> asio::awaitable<int> { co_return 99; },
                        [&ioc](Runnable r, ScheduleInfo) {
                            auto runner = std::make_shared<Runnable>(std::move(r));
                            std::function<void()> poll;
                            poll = [runner, &ioc, &poll]() {
                                if (runner->run()) asio::post(ioc, poll);
                            };
                            asio::post(ioc, poll);
                        });
    r.schedule();
    ioc.run();
    ASSERT_TRUE(t.is_finished());
    int v  = 0;
    auto c = [&]() -> tc { v = co_await std::move(t); }();
    while (!c.h.done()) c.h.resume();
    EXPECT_EQ(v, 99);
}

TEST(AsyncTaskCoroutine, ScheduleVoidOnIoContext) {
    asio::io_context ioc;
    auto [r, t] = spawn([]() -> asio::awaitable<void> { co_return; },
                        [&ioc](Runnable r, ScheduleInfo) {
                            auto runner = std::make_shared<Runnable>(std::move(r));
                            std::function<void()> poll;
                            poll = [runner, &ioc, &poll]() {
                                if (runner->run()) asio::post(ioc, poll);
                            };
                            asio::post(ioc, poll);
                        });
    r.schedule();
    ioc.run();
    EXPECT_TRUE(t.is_finished());
}

// ── One-step advancement ──────────────────────────────────────────────────

void drive(Runnable& r) {
    while (r.run()) r.schedule();
}

TEST(AsyncTaskCoroutine, SpawnCoroutineAndDrive) {
    auto [r, t] = spawn([]() -> asio::awaitable<int> { co_return 42; }, [](Runnable, ScheduleInfo) {});
    r.schedule();
    drive(r);
    EXPECT_TRUE(t.is_finished());
}

TEST(AsyncTaskCoroutine, SpawnCoroutineAndCoAwait) {
    auto [r, t] = spawn([]() -> asio::awaitable<int> { co_return 99; }, [](Runnable, ScheduleInfo) {});
    r.schedule();
    drive(r);
    int v  = 0;
    auto c = [&]() -> tc { v = co_await std::move(t); }();
    while (!c.h.done()) c.h.resume();
    EXPECT_EQ(v, 99);
}

TEST(AsyncTaskCoroutine, SpawnCoroutineException) {
    auto [r, t] = spawn(
        []() -> asio::awaitable<int> {
            throw std::runtime_error("fail");
            co_return 0;
        },
        [](Runnable, ScheduleInfo) {});
    r.schedule();
    drive(r);
    EXPECT_TRUE(t.is_finished());
    auto ft = std::move(t).fallible();
    std::optional<int> v;
    auto c = [&]() -> tc { v = co_await std::move(ft); }();
    while (!c.h.done()) c.h.resume();
    EXPECT_FALSE(v.has_value());
}

TEST(AsyncTaskCoroutine, SpawnVoidCoroutine) {
    auto [r, t] = spawn([]() -> asio::awaitable<void> { co_return; }, [](Runnable, ScheduleInfo) {});
    r.schedule();
    drive(r);
    EXPECT_TRUE(t.is_finished());
}

TEST(AsyncTaskCoroutine, CancelCoroutine) {
    auto [r, t] = spawn([]() -> asio::awaitable<int> { co_return 42; }, [](Runnable, ScheduleInfo) {});
    r.schedule();
    drive(r);
    std::optional<int> v;
    auto c = [&]() -> tc { v = co_await std::move(t).cancel(); }();
    while (!c.h.done()) c.h.resume();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
}

TEST(AsyncTaskCoroutine, CoAwaitFallibleCoroutine) {
    auto [r, t] = spawn([]() -> asio::awaitable<int> { co_return 77; }, [](Runnable, ScheduleInfo) {});
    r.schedule();
    drive(r);
    std::optional<int> v;
    auto c = [&]() -> tc { v = co_await std::move(t).fallible(); }();
    while (!c.h.done()) c.h.resume();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 77);
}
