module;

#include <asio/awaitable.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>

export module epix.async_broadcast;

import std;

namespace epix::async_broadcast {

// ── Error types (match Rust async_broadcast) ──

export template <typename T>
struct SendError {
    T msg;
};

export enum class RecvError {
    Closed,
};

export template <typename T>
struct TrySendError {
    enum Kind { Full, Closed, Inactive };
    Kind kind;
    T msg;
    bool is_full() const { return kind == Full; }
    bool is_closed() const { return kind == Closed; }
    bool is_inactive() const { return kind == Inactive; }
};

export enum class TryRecvError {
    Empty,
    Closed,
};

// ── Forward declarations ──

export template <typename T>
struct Sender;
export template <typename T>
struct Receiver;
export template <typename T>
struct InactiveReceiver;

// ── Shared broadcast state ──

template <typename T>
struct BroadcastState {
    mutable std::mutex mtx;
    std::deque<T> queue;
    std::size_t head_index     = 0;
    std::size_t capacity       = 0;
    bool overflow              = false;
    std::size_t sender_count   = 0;
    std::size_t receiver_count = 0;
    bool closed                = false;
};

// ── Sender ──

export template <typename T>
struct Sender {
   private:
    std::shared_ptr<BroadcastState<T>> m_st;

    void inc() {
        if (m_st) {
            std::lock_guard lk(m_st->mtx);
            ++m_st->sender_count;
        }
    }
    void dec() {
        if (!m_st) return;
        std::lock_guard lk(m_st->mtx);
        if (m_st->sender_count > 0) --m_st->sender_count;
    }

   public:
    Sender() = default;
    explicit Sender(std::shared_ptr<BroadcastState<T>> st) : m_st(std::move(st)) { inc(); }
    Sender(const Sender& o) : m_st(o.m_st) { inc(); }
    Sender(Sender&& o) noexcept : m_st(std::exchange(o.m_st, nullptr)) {}
    Sender& operator=(const Sender& o) {
        if (this != &o && m_st != o.m_st) {
            dec();
            m_st = o.m_st;
            inc();
        }
        return *this;
    }
    Sender& operator=(Sender&& o) noexcept {
        if (this != &o) {
            dec();
            m_st = std::exchange(o.m_st, nullptr);
        }
        return *this;
    }
    ~Sender() { dec(); }

    explicit operator bool() const { return m_st != nullptr; }

    void set_overflow(bool v) const {
        if (!m_st) return;
        std::lock_guard lk(m_st->mtx);
        m_st->overflow = v;
    }

    std::expected<void, TrySendError<T>> try_broadcast(T msg) const {
        if (!m_st) return std::unexpected(TrySendError<T>{TrySendError<T>::Closed, std::move(msg)});
        std::lock_guard lk(m_st->mtx);
        if (m_st->closed) return std::unexpected(TrySendError<T>{TrySendError<T>::Closed, std::move(msg)});
        if (m_st->receiver_count == 0)
            return std::unexpected(TrySendError<T>{TrySendError<T>::Inactive, std::move(msg)});
        if (m_st->queue.size() >= m_st->capacity) {
            if (m_st->overflow) {
                m_st->queue.pop_front();
                m_st->head_index++;
            } else {
                return std::unexpected(TrySendError<T>{TrySendError<T>::Full, std::move(msg)});
            }
        }
        m_st->queue.push_back(std::move(msg));
        return {};
    }

    asio::awaitable<std::expected<void, SendError<T>>> broadcast(T msg) const {
        if (!m_st) co_return std::unexpected(SendError<T>{std::move(msg)});
        while (true) {
            {
                std::lock_guard lk(m_st->mtx);
                if (m_st->closed) co_return std::unexpected(SendError<T>{std::move(msg)});
                bool has_space = m_st->queue.size() < m_st->capacity || m_st->overflow;
                if (has_space) {
                    if (m_st->queue.size() >= m_st->capacity && m_st->overflow) {
                        m_st->queue.pop_front();
                        m_st->head_index++;
                    }
                    m_st->queue.push_back(std::move(msg));
                    co_return std::expected<void, SendError<T>>{};
                }
            }
            co_await asio::post(asio::use_awaitable);
        }
    }

    bool close() const {
        if (!m_st) return false;
        std::lock_guard lk(m_st->mtx);
        if (m_st->closed) return false;
        m_st->closed = true;
        return true;
    }

    bool is_closed() const {
        if (!m_st) return true;
        std::lock_guard lk(m_st->mtx);
        return m_st->closed;
    }

    bool is_empty() const {
        if (!m_st) return true;
        std::lock_guard lk(m_st->mtx);
        return m_st->queue.empty();
    }

    bool is_full() const {
        if (!m_st) return true;
        std::lock_guard lk(m_st->mtx);
        return m_st->queue.size() >= m_st->capacity;
    }

    std::size_t len() const {
        if (!m_st) return 0;
        std::lock_guard lk(m_st->mtx);
        return m_st->queue.size();
    }

    std::size_t capacity_val() const {
        if (!m_st) return 0;
        return m_st->capacity;
    }

    std::size_t receiver_count() const {
        if (!m_st) return 0;
        std::lock_guard lk(m_st->mtx);
        return m_st->receiver_count;
    }

    std::size_t sender_count() const {
        if (!m_st) return 0;
        std::lock_guard lk(m_st->mtx);
        return m_st->sender_count;
    }

    Receiver<T> new_receiver() const;

    template <typename U>
    friend struct Receiver;
    template <typename U>
    friend struct InactiveReceiver;
};

// ── Receiver ──

export template <typename T>
struct Receiver {
   private:
    std::shared_ptr<BroadcastState<T>> m_st;
    std::size_t m_cursor = 0;

    void inc() {
        if (m_st) {
            std::lock_guard lk(m_st->mtx);
            ++m_st->receiver_count;
        }
    }
    void dec() {
        if (!m_st) return;
        std::lock_guard lk(m_st->mtx);
        if (m_st->receiver_count > 0) --m_st->receiver_count;
    }

   public:
    Receiver() = default;
    explicit Receiver(std::shared_ptr<BroadcastState<T>> st, std::size_t cursor)
        : m_st(std::move(st)), m_cursor(cursor) {
        inc();
    }
    Receiver(const Receiver& o) : m_st(o.m_st), m_cursor(o.m_cursor) { inc(); }
    Receiver(Receiver&& o) noexcept : m_st(std::exchange(o.m_st, nullptr)), m_cursor(o.m_cursor) {}
    Receiver& operator=(const Receiver& o) {
        if (this != &o) {
            if (m_st != o.m_st) {
                dec();
                m_st = o.m_st;
                inc();
            }
            m_cursor = o.m_cursor;
        }
        return *this;
    }
    Receiver& operator=(Receiver&& o) noexcept {
        if (this != &o) {
            dec();
            m_st     = std::exchange(o.m_st, nullptr);
            m_cursor = o.m_cursor;
        }
        return *this;
    }
    ~Receiver() { dec(); }

    explicit operator bool() const { return m_st != nullptr; }

    std::expected<T, TryRecvError> try_recv() {
        if (!m_st) return std::unexpected(TryRecvError::Closed);
        std::lock_guard lk(m_st->mtx);
        std::size_t tail = m_st->head_index + m_st->queue.size();
        if (m_cursor < m_st->head_index) m_cursor = m_st->head_index;
        if (m_cursor < tail) {
            std::size_t offset = m_cursor - m_st->head_index;
            T val              = m_st->queue[offset];
            ++m_cursor;
            return val;
        }
        if (m_st->closed || m_st->sender_count == 0) return std::unexpected(TryRecvError::Closed);
        return std::unexpected(TryRecvError::Empty);
    }

    asio::awaitable<std::expected<T, RecvError>> recv() {
        if (!m_st) co_return std::unexpected(RecvError::Closed);
        while (true) {
            {
                std::lock_guard lk(m_st->mtx);
                std::size_t tail = m_st->head_index + m_st->queue.size();
                if (m_cursor < m_st->head_index) m_cursor = m_st->head_index;
                if (m_cursor < tail) {
                    std::size_t offset = m_cursor - m_st->head_index;
                    T val              = m_st->queue[offset];
                    ++m_cursor;
                    co_return val;
                }
                if (m_st->closed || m_st->sender_count == 0) co_return std::unexpected(RecvError::Closed);
            }
            co_await asio::post(asio::use_awaitable);
        }
    }

    bool close() const {
        if (!m_st) return false;
        std::lock_guard lk(m_st->mtx);
        if (m_st->closed) return false;
        m_st->closed = true;
        return true;
    }

    bool is_closed() const {
        if (!m_st) return true;
        std::lock_guard lk(m_st->mtx);
        return m_st->closed;
    }

    bool is_empty() const {
        if (!m_st) return true;
        std::lock_guard lk(m_st->mtx);
        std::size_t tail = m_st->head_index + m_st->queue.size();
        std::size_t cur  = m_cursor < m_st->head_index ? m_st->head_index : m_cursor;
        return cur >= tail;
    }

    bool is_full() const {
        if (!m_st) return true;
        std::lock_guard lk(m_st->mtx);
        return m_st->queue.size() >= m_st->capacity;
    }

    std::size_t len() const {
        if (!m_st) return 0;
        std::lock_guard lk(m_st->mtx);
        return m_st->queue.size();
    }

    std::size_t capacity_val() const {
        if (!m_st) return 0;
        return m_st->capacity;
    }

    std::size_t receiver_count() const {
        if (!m_st) return 0;
        std::lock_guard lk(m_st->mtx);
        return m_st->receiver_count;
    }

    std::size_t sender_count() const {
        if (!m_st) return 0;
        std::lock_guard lk(m_st->mtx);
        return m_st->sender_count;
    }

    InactiveReceiver<T> deactivate();

    Sender<T> new_sender() const;

    template <typename U>
    friend struct Sender;
    template <typename U>
    friend struct InactiveReceiver;
};

// ── InactiveReceiver ──

export template <typename T>
struct InactiveReceiver {
   private:
    std::shared_ptr<BroadcastState<T>> m_st;
    std::size_t m_cursor = 0;

   public:
    InactiveReceiver() = default;
    InactiveReceiver(std::shared_ptr<BroadcastState<T>> st, std::size_t cursor)
        : m_st(std::move(st)), m_cursor(cursor) {}
    InactiveReceiver(const InactiveReceiver&)            = default;
    InactiveReceiver(InactiveReceiver&&)                 = default;
    InactiveReceiver& operator=(const InactiveReceiver&) = default;
    InactiveReceiver& operator=(InactiveReceiver&&)      = default;

    Receiver<T> activate() {
        if (!m_st) return {};
        return Receiver<T>(m_st, m_cursor);
    }

    template <typename U>
    friend struct Receiver;
};

// ── Deferred implementations ──

template <typename T>
Receiver<T> Sender<T>::new_receiver() const {
    if (!m_st) return {};
    std::lock_guard lk(m_st->mtx);
    std::size_t cursor = m_st->head_index + m_st->queue.size();
    return Receiver<T>(m_st, cursor);
}

template <typename T>
InactiveReceiver<T> Receiver<T>::deactivate() {
    if (!m_st) return {};
    dec();
    auto st = std::exchange(m_st, nullptr);
    return InactiveReceiver<T>(std::move(st), m_cursor);
}

template <typename T>
Sender<T> Receiver<T>::new_sender() const {
    if (!m_st) return {};
    return Sender<T>(m_st);
}

// ── Factory function ──

export template <typename T>
std::pair<Sender<T>, Receiver<T>> broadcast(std::size_t cap) {
    auto st      = std::make_shared<BroadcastState<T>>();
    st->capacity = cap;
    Sender<T> s(st);
    Receiver<T> r(st, 0);
    return {std::move(s), std::move(r)};
}

}  // namespace epix::async_broadcast
