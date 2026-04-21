module;

#include <asio/awaitable.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>

export module epix.async_channel;

import std;

namespace epix::async_channel {

// ── Error types (match Rust async_channel) ──

export template <typename T>
struct SendError {
    T msg;
};

export struct RecvError {};

export enum class TryRecvError {
    Empty,
    Closed,
};

export template <typename T>
struct TrySendError {
    enum Kind { Full, Closed };
    Kind kind;
    T msg;
    bool is_full() const { return kind == Full; }
    bool is_closed() const { return kind == Closed; }
};

// ── Forward declarations ──

export template <typename T>
struct Sender;
export template <typename T>
struct Receiver;
export template <typename T>
struct WeakSender;
export template <typename T>
struct WeakReceiver;

// ── Shared channel state ──

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

// ── Sender ──

export template <typename T>
struct Sender {
   private:
    std::shared_ptr<Channel<T>> m_ch;

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
    Sender() = default;
    explicit Sender(std::shared_ptr<Channel<T>> ch) : m_ch(std::move(ch)) { inc(); }
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

    explicit operator bool() const { return m_ch != nullptr; }

    std::expected<void, TrySendError<T>> try_send(T msg) const {
        if (!m_ch) return std::unexpected(TrySendError<T>{TrySendError<T>::Closed, std::move(msg)});
        std::lock_guard lk(m_ch->mtx);
        if (m_ch->closed || m_ch->receiver_count == 0)
            return std::unexpected(TrySendError<T>{TrySendError<T>::Closed, std::move(msg)});
        if (m_ch->cap && m_ch->queue.size() >= *m_ch->cap)
            return std::unexpected(TrySendError<T>{TrySendError<T>::Full, std::move(msg)});
        m_ch->queue.push_back(std::move(msg));
        m_ch->cv.notify_one();
        return {};
    }

    asio::awaitable<std::expected<void, SendError<T>>> send(T msg) const {
        if (!m_ch) co_return std::unexpected(SendError<T>{std::move(msg)});
        while (true) {
            {
                std::lock_guard lk(m_ch->mtx);
                if (m_ch->closed || m_ch->receiver_count == 0) co_return std::unexpected(SendError<T>{std::move(msg)});
                bool has_space = !m_ch->cap || m_ch->queue.size() < *m_ch->cap;
                if (has_space) {
                    m_ch->queue.push_back(std::move(msg));
                    m_ch->cv.notify_one();
                    co_return std::expected<void, SendError<T>>{};
                }
            }
            co_await asio::post(asio::use_awaitable);
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

    std::optional<std::size_t> capacity() const {
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

    WeakSender<T> downgrade() const;

    bool same_channel(const Sender& o) const { return m_ch == o.m_ch; }

    template <typename U>
    friend struct Receiver;
    template <typename U>
    friend struct WeakSender;
};

// ── Receiver ──

export template <typename T>
struct Receiver {
   private:
    std::shared_ptr<Channel<T>> m_ch;

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
    Receiver() = default;
    explicit Receiver(std::shared_ptr<Channel<T>> ch) : m_ch(std::move(ch)) { inc(); }
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

    explicit operator bool() const { return m_ch != nullptr; }

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

    asio::awaitable<std::expected<T, RecvError>> recv() const {
        if (!m_ch) co_return std::unexpected(RecvError{});
        while (true) {
            {
                std::lock_guard lk(m_ch->mtx);
                if (!m_ch->queue.empty()) {
                    T val = std::move(m_ch->queue.front());
                    m_ch->queue.pop_front();
                    m_ch->cv.notify_one();
                    co_return val;
                }
                if (m_ch->closed || m_ch->sender_count == 0) co_return std::unexpected(RecvError{});
            }
            co_await asio::post(asio::use_awaitable);
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

    std::optional<std::size_t> capacity() const {
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

    WeakReceiver<T> downgrade() const;

    bool same_channel(const Receiver& o) const { return m_ch == o.m_ch; }

    template <typename U>
    friend struct Sender;
    template <typename U>
    friend struct WeakReceiver;
};

// ── WeakSender ──

export template <typename T>
struct WeakSender {
   private:
    std::weak_ptr<Channel<T>> m_ch;

   public:
    WeakSender() = default;
    explicit WeakSender(std::weak_ptr<Channel<T>> ch) : m_ch(std::move(ch)) {}
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

export template <typename T>
struct WeakReceiver {
   private:
    std::weak_ptr<Channel<T>> m_ch;

   public:
    WeakReceiver() = default;
    explicit WeakReceiver(std::weak_ptr<Channel<T>> ch) : m_ch(std::move(ch)) {}
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
WeakSender<T> Sender<T>::downgrade() const {
    return WeakSender<T>(m_ch);
}

template <typename T>
WeakReceiver<T> Receiver<T>::downgrade() const {
    return WeakReceiver<T>(m_ch);
}

// ── Factory functions ──

export template <typename T>
std::pair<Sender<T>, Receiver<T>> unbounded() {
    auto ch = std::make_shared<Channel<T>>();
    return {Sender<T>(ch), Receiver<T>(ch)};
}

export template <typename T>
std::pair<Sender<T>, Receiver<T>> bounded(std::size_t cap) {
    auto ch = std::make_shared<Channel<T>>();
    ch->cap = cap;
    return {Sender<T>(ch), Receiver<T>(ch)};
}

}  // namespace epix::async_channel
