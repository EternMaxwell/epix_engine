#pragma once

#ifndef EPIX_CXX_MODULE
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <epix/common.hpp>
#include <expected>
#include <mutex>
#include <optional>
#include <stdexec/execution.hpp>
#include <utility>
#endif

namespace epix::async_channel {

namespace internal {
template <typename T>
struct Channel {
    mutable std::mutex mtx;
    mutable std::condition_variable cv;
    std::deque<T> queue;
    std::optional<std::size_t> cap;  // nullopt = unbounded
    std::size_t sender_count   = 0;
    std::size_t receiver_count = 0;
    bool closed                = false;
};

}  // namespace internal

// ── Error types (match Rust async_channel) ──

EPIX_EXPORT template <typename T>
struct SendError {
    T msg;
};

EPIX_EXPORT struct RecvError {};

EPIX_EXPORT enum class TryRecvError {
    Empty,
    Closed,
};

EPIX_EXPORT template <typename T>
struct TrySendError {
   public:
    enum Kind { Full, Closed };

    Kind kind;
    T msg;

    TrySendError(Kind kind, T msg) : kind(kind), msg(std::move(msg)) {}
    static TrySendError full(T msg) { return TrySendError(Full, std::move(msg)); }
    static TrySendError closed(T msg) { return TrySendError(Closed, std::move(msg)); }

    bool is_full() const noexcept { return kind == Full; }
    bool is_closed() const noexcept { return kind == Closed; }
};

// ── Forward declarations ──

EPIX_EXPORT template <typename T>
struct Sender;
EPIX_EXPORT template <typename T>
struct Receiver;
EPIX_EXPORT template <typename T>
struct WeakSender;
EPIX_EXPORT template <typename T>
struct WeakReceiver;
// ── Sender ──

EPIX_EXPORT template <typename T>
struct Sender {
   private:
    std::shared_ptr<internal::Channel<T>> m_ch;

    void inc() {
        if (m_ch) {
            std::lock_guard lk(m_ch->mtx);
            ++m_ch->sender_count;
        }
    }
    void dec() {
        if (!m_ch) return;
        std::unique_lock lk(m_ch->mtx);
        if (m_ch->sender_count > 0) --m_ch->sender_count;
        if (m_ch->sender_count == 0) m_ch->cv.notify_all();
    }

   public:
    Sender() noexcept = default;
    explicit Sender(std::shared_ptr<internal::Channel<T>> ch) : m_ch(std::move(ch)) { inc(); }
    Sender(const Sender& o) : m_ch(o.m_ch) { inc(); }
    Sender(Sender&& o) noexcept : m_ch(std::exchange(o.m_ch, nullptr)) {}
    Sender& operator=(const Sender& o) {
        if (this != &o && m_ch != o.m_ch) {
            dec();
            m_ch = o.m_ch;
            inc();
        }
        return *this;
    }
    Sender& operator=(Sender&& o) noexcept {
        if (this != &o) {
            dec();
            m_ch = std::exchange(o.m_ch, nullptr);
        }
        return *this;
    }
    ~Sender() { dec(); }

    explicit operator bool() const noexcept { return m_ch != nullptr; }

    std::expected<void, TrySendError<T>> try_send(T msg) const {
        if (!m_ch) return std::unexpected(TrySendError<T>::closed(std::move(msg)));
        std::lock_guard lk(m_ch->mtx);
        if (m_ch->closed || m_ch->receiver_count == 0) return std::unexpected(TrySendError<T>::closed(std::move(msg)));
        if (m_ch->cap && m_ch->queue.size() >= *m_ch->cap)
            return std::unexpected(TrySendError<T>::full(std::move(msg)));
        m_ch->queue.push_back(std::move(msg));
        m_ch->cv.notify_one();
        return {};
    }

    STDEXEC::task<std::expected<void, SendError<T>>> send(T msg) const {
        if (!m_ch) co_return std::unexpected(SendError<T>{std::move(msg)});
        while (true) {
            bool sent   = false;
            bool closed = false;
            {
                std::lock_guard lk(m_ch->mtx);
                closed         = m_ch->closed || m_ch->receiver_count == 0;
                bool has_space = !m_ch->cap || m_ch->queue.size() < *m_ch->cap;
                if (!closed && has_space) {
                    m_ch->queue.push_back(std::move(msg));
                    sent = true;
                }
            }
            if (closed) co_return std::unexpected(SendError<T>{std::move(msg)});
            if (sent) {
                m_ch->cv.notify_one();
                co_return std::expected<void, SendError<T>>{};
            }
            auto scheduler = co_await STDEXEC::read_env(STDEXEC::get_scheduler);
            co_await (STDEXEC::just(scheduler)
                      | STDEXEC::let_value([](auto sch) { return STDEXEC::schedule(sch); }));
        }
    }

    std::expected<void, SendError<T>> send_blocking(T msg) const {
        if (!m_ch) return std::unexpected(SendError<T>{std::move(msg)});
        std::unique_lock lk(m_ch->mtx);
        m_ch->cv.wait(lk, [&] {
            if (m_ch->closed || m_ch->receiver_count == 0) return true;
            return !m_ch->cap || m_ch->queue.size() < *m_ch->cap;
        });
        if (m_ch->closed || m_ch->receiver_count == 0) return std::unexpected(SendError<T>{std::move(msg)});
        m_ch->queue.push_back(std::move(msg));
        m_ch->cv.notify_one();
        return {};
    }

    bool close() const {
        if (!m_ch) return false;
        std::lock_guard lk(m_ch->mtx);
        if (m_ch->closed) return false;
        m_ch->closed = true;
        m_ch->cv.notify_all();
        return true;
    }

    bool is_closed() const {
        if (!m_ch) return true;
        std::lock_guard lk(m_ch->mtx);
        return m_ch->closed || m_ch->receiver_count == 0;
    }

    bool is_empty() const {
        if (!m_ch) return true;
        std::lock_guard lk(m_ch->mtx);
        return m_ch->queue.empty();
    }

    bool is_full() const {
        if (!m_ch) return true;
        std::lock_guard lk(m_ch->mtx);
        return m_ch->cap && m_ch->queue.size() >= *m_ch->cap;
    }

    std::size_t len() const {
        if (!m_ch) return 0;
        std::lock_guard lk(m_ch->mtx);
        return m_ch->queue.size();
    }

    std::optional<std::size_t> capacity() const noexcept {
        if (!m_ch) return std::nullopt;
        return m_ch->cap;
    }

    std::size_t receiver_count() const {
        if (!m_ch) return 0;
        std::lock_guard lk(m_ch->mtx);
        return m_ch->receiver_count;
    }

    std::size_t sender_count() const {
        if (!m_ch) return 0;
        std::lock_guard lk(m_ch->mtx);
        return m_ch->sender_count;
    }

    WeakSender<T> downgrade() const noexcept;

    bool same_channel(const Sender& o) const noexcept { return m_ch == o.m_ch; }

    template <typename U>
    friend struct Receiver;
    template <typename U>
    friend struct WeakSender;
};

// ── Receiver ──

EPIX_EXPORT template <typename T>
struct Receiver {
   private:
    std::shared_ptr<internal::Channel<T>> m_ch;

    void inc() {
        if (m_ch) {
            std::lock_guard lk(m_ch->mtx);
            ++m_ch->receiver_count;
        }
    }
    void dec() {
        if (!m_ch) return;
        std::unique_lock lk(m_ch->mtx);
        if (m_ch->receiver_count > 0) --m_ch->receiver_count;
        if (m_ch->receiver_count == 0) m_ch->cv.notify_all();
    }

   public:
    Receiver() noexcept = default;
    explicit Receiver(std::shared_ptr<internal::Channel<T>> ch) : m_ch(std::move(ch)) { inc(); }
    Receiver(const Receiver& o) : m_ch(o.m_ch) { inc(); }
    Receiver(Receiver&& o) noexcept : m_ch(std::exchange(o.m_ch, nullptr)) {}
    Receiver& operator=(const Receiver& o) {
        if (this != &o && m_ch != o.m_ch) {
            dec();
            m_ch = o.m_ch;
            inc();
        }
        return *this;
    }
    Receiver& operator=(Receiver&& o) noexcept {
        if (this != &o) {
            dec();
            m_ch = std::exchange(o.m_ch, nullptr);
        }
        return *this;
    }
    ~Receiver() { dec(); }

    explicit operator bool() const noexcept { return m_ch != nullptr; }

    std::expected<T, TryRecvError> try_recv() const {
        if (!m_ch) return std::unexpected(TryRecvError::Closed);
        std::lock_guard lk(m_ch->mtx);
        if (!m_ch->queue.empty()) {
            T val = std::move(m_ch->queue.front());
            m_ch->queue.pop_front();
            m_ch->cv.notify_one();
            return val;
        }
        if (m_ch->closed || m_ch->sender_count == 0) return std::unexpected(TryRecvError::Closed);
        return std::unexpected(TryRecvError::Empty);
    }

    STDEXEC::task<std::expected<T, RecvError>> recv() const {
        if (!m_ch) co_return std::unexpected(RecvError{});
        while (true) {
            std::optional<T> value;
            bool closed = false;
            {
                std::lock_guard lk(m_ch->mtx);
                if (!m_ch->queue.empty()) {
                    value = std::move(m_ch->queue.front());
                    m_ch->queue.pop_front();
                } else {
                    closed = m_ch->closed || m_ch->sender_count == 0;
                }
            }
            if (value) {
                m_ch->cv.notify_one();
                co_return std::move(*value);
            }
            if (closed) co_return std::unexpected(RecvError{});
            auto scheduler = co_await STDEXEC::read_env(STDEXEC::get_scheduler);
            co_await (STDEXEC::just(scheduler)
                      | STDEXEC::let_value([](auto sch) { return STDEXEC::schedule(sch); }));
        }
    }

    std::expected<T, RecvError> recv_blocking() const {
        if (!m_ch) return std::unexpected(RecvError{});
        std::unique_lock lk(m_ch->mtx);
        m_ch->cv.wait(lk, [&] { return !m_ch->queue.empty() || m_ch->closed || m_ch->sender_count == 0; });
        if (!m_ch->queue.empty()) {
            T val = std::move(m_ch->queue.front());
            m_ch->queue.pop_front();
            m_ch->cv.notify_one();
            return val;
        }
        return std::unexpected(RecvError{});
    }

    bool close() const {
        if (!m_ch) return false;
        std::lock_guard lk(m_ch->mtx);
        if (m_ch->closed) return false;
        m_ch->closed = true;
        m_ch->cv.notify_all();
        return true;
    }

    bool is_closed() const {
        if (!m_ch) return true;
        std::lock_guard lk(m_ch->mtx);
        return m_ch->closed || m_ch->sender_count == 0;
    }

    bool is_empty() const {
        if (!m_ch) return true;
        std::lock_guard lk(m_ch->mtx);
        return m_ch->queue.empty();
    }

    bool is_full() const {
        if (!m_ch) return true;
        std::lock_guard lk(m_ch->mtx);
        return m_ch->cap && m_ch->queue.size() >= *m_ch->cap;
    }

    std::size_t len() const {
        if (!m_ch) return 0;
        std::lock_guard lk(m_ch->mtx);
        return m_ch->queue.size();
    }

    std::optional<std::size_t> capacity() const noexcept {
        if (!m_ch) return std::nullopt;
        return m_ch->cap;
    }

    std::size_t receiver_count() const {
        if (!m_ch) return 0;
        std::lock_guard lk(m_ch->mtx);
        return m_ch->receiver_count;
    }

    std::size_t sender_count() const {
        if (!m_ch) return 0;
        std::lock_guard lk(m_ch->mtx);
        return m_ch->sender_count;
    }

    WeakReceiver<T> downgrade() const noexcept;

    bool same_channel(const Receiver& o) const noexcept { return m_ch == o.m_ch; }

    template <typename U>
    friend struct Sender;
    template <typename U>
    friend struct WeakReceiver;
};

// ── WeakSender ──

EPIX_EXPORT template <typename T>
struct WeakSender {
   private:
    std::weak_ptr<internal::Channel<T>> m_ch;

   public:
    WeakSender() noexcept = default;
    explicit WeakSender(std::weak_ptr<internal::Channel<T>> ch) noexcept : m_ch(std::move(ch)) {}
    WeakSender(const WeakSender&)            = default;
    WeakSender(WeakSender&&)                 = default;
    WeakSender& operator=(const WeakSender&) = default;
    WeakSender& operator=(WeakSender&&)      = default;

    std::optional<Sender<T>> upgrade() const {
        auto ch = m_ch.lock();
        if (!ch) return std::nullopt;
        {
            std::lock_guard lk(ch->mtx);
            if (ch->closed) return std::nullopt;
        }
        // Lock released before constructing Sender to avoid deadlock:
        // Sender(ch) calls inc() which also acquires ch->mtx.
        return Sender<T>(ch);
    }
};

// ── WeakReceiver ──

EPIX_EXPORT template <typename T>
struct WeakReceiver {
   private:
    std::weak_ptr<internal::Channel<T>> m_ch;

   public:
    WeakReceiver() noexcept = default;
    explicit WeakReceiver(std::weak_ptr<internal::Channel<T>> ch) noexcept : m_ch(std::move(ch)) {}
    WeakReceiver(const WeakReceiver&)            = default;
    WeakReceiver(WeakReceiver&&)                 = default;
    WeakReceiver& operator=(const WeakReceiver&) = default;
    WeakReceiver& operator=(WeakReceiver&&)      = default;

    std::optional<Receiver<T>> upgrade() const {
        auto ch = m_ch.lock();
        if (!ch) return std::nullopt;
        {
            std::lock_guard lk(ch->mtx);
            if (ch->closed) return std::nullopt;
        }
        // Lock released before constructing Receiver to avoid deadlock:
        // Receiver(ch) calls inc() which also acquires ch->mtx.
        return Receiver<T>(ch);
    }
};

// ── Deferred downgrade implementations ──

template <typename T>
WeakSender<T> Sender<T>::downgrade() const noexcept {
    return WeakSender<T>(m_ch);
}

template <typename T>
WeakReceiver<T> Receiver<T>::downgrade() const noexcept {
    return WeakReceiver<T>(m_ch);
}

// ── Factory functions ──

EPIX_EXPORT template <typename T>
std::pair<Sender<T>, Receiver<T>> unbounded() {
    auto ch = std::make_shared<internal::Channel<T>>();
    return {Sender<T>(ch), Receiver<T>(ch)};
}

EPIX_EXPORT template <typename T>
std::pair<Sender<T>, Receiver<T>> bounded(std::size_t cap) {
    auto ch = std::make_shared<internal::Channel<T>>();
    ch->cap = cap;
    return {Sender<T>(ch), Receiver<T>(ch)};
}

}  // namespace epix::async_channel
