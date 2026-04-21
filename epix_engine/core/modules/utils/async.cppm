module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#endif
export module epix.utils:async;
#ifdef EPIX_IMPORT_STD
import std;
#endif
namespace epix::utils {
/** @brief Policy controlling sender behavior when a bounded ConQueue is full. */
export enum class OverflowPolicy {
    Block,       ///< Block the sender until space is available.
    DropOldest,  ///< Drop the oldest element in the queue to make room.
    DropNewest,  ///< Discard the new element (do not enqueue).
};

/** @brief Receive-side error state for async channels. */
export enum class ReceiveError {
    Empty,
    Closed,
};

/**
 * @brief A thread-safe queue implementation using std::deque and
 * std::shared_mutex.
 *
 * This queue allows multiple threads to read from it concurrently while
 * ensuring that only one thread can write to it at a time. It provides methods
 * for adding and removing elements from both ends of the queue, as well as
 * methods for accessing the front and back elements.
 *
 * However, differenct threads can still modify the same element in the queue at
 * the same time. It is only guaranteed that the element references are always
 * valid.
 *
 * @tparam T The type of elements stored in the queue.
 * @tparam Alloc The allocator type used for memory management. Defaults to
 * std::allocator<T>.
 */
export template <typename T, typename Alloc = std::allocator<T>>
struct ConQueue {
    using value_type     = T;
    using container_type = std::deque<T>;
    using size_type      = typename container_type::size_type;

   private:
    container_type m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::optional<std::size_t> m_capacity;
    OverflowPolicy m_overflow_policy = OverflowPolicy::Block;
    std::size_t m_sender_count       = 0;
    bool m_closed                    = false;

   public:
    ConQueue() = default;
    ConQueue(std::size_t capacity, OverflowPolicy policy = OverflowPolicy::Block)
        : m_capacity(capacity), m_overflow_policy(policy) {}
    ConQueue(const ConQueue&)            = delete;
    ConQueue(ConQueue&&)                 = delete;
    ConQueue& operator=(const ConQueue&) = delete;
    ConQueue& operator=(ConQueue&&)      = delete;
    ~ConQueue()                          = default;

    void add_sender() {
        std::unique_lock lock(m_mutex);
        ++m_sender_count;
    }

    void remove_sender() {
        std::unique_lock lock(m_mutex);
        if (m_sender_count == 0) return;
        --m_sender_count;
        if (m_sender_count == 0) {
            m_cv.notify_all();
        }
    }

    void close() {
        std::unique_lock lock(m_mutex);
        m_closed = true;
        m_cv.notify_all();
    }

    /** @brief Construct an element in-place at the back of the queue.
     * @tparam Args Constructor argument types.
     * @param args Arguments forwarded to T's constructor.
     */
    template <typename... Args>
    void emplace(Args&&... args) {
        std::unique_lock lock(m_mutex);
        if (m_closed) return;
        if (m_capacity.has_value()) {
            auto capacity = *m_capacity;
            if (m_queue.size() >= capacity) {
                switch (m_overflow_policy) {
                    case OverflowPolicy::Block:
                        m_cv.wait(lock, [this, capacity] { return m_closed || m_queue.size() < capacity; });
                        if (m_closed) return;
                        break;
                    case OverflowPolicy::DropOldest:
                        m_queue.pop_front();
                        break;
                    case OverflowPolicy::DropNewest:
                        return;
                }
            }
        }
        m_queue.emplace_back(std::forward<Args>(args)...);
        m_cv.notify_all();
    }
    /** @brief Remove and return the front element, blocking until one is
     * available. */
    std::expected<T, ReceiveError> pop() {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty() || m_sender_count == 0 || m_closed; });
        if (m_queue.empty()) {
            return std::unexpected(ReceiveError::Closed);
        }
        T value = std::move(m_queue.front());
        m_queue.pop_front();
        m_cv.notify_all();  // wake any sender waiting for space (Block policy)
        return value;
    }
    /** @brief Try to remove and return the front element without blocking.
     * @return The front element, or std::nullopt if the queue is empty.
     */
    std::expected<T, ReceiveError> try_pop() {
        std::unique_lock lock(m_mutex);
        if (m_queue.empty()) {
            return std::unexpected((m_sender_count == 0 || m_closed) ? ReceiveError::Closed : ReceiveError::Empty);
        }
        T value = std::move(m_queue.front());
        m_queue.pop_front();
        m_cv.notify_all();  // wake any sender waiting for space (Block policy)
        return value;
    }
    /** @brief Check whether the queue is empty. */
    bool empty() {
        std::unique_lock lock(m_mutex);
        return m_queue.empty();
    }

    /** @brief Check whether any sender is still connected. */
    bool has_senders() {
        std::unique_lock lock(m_mutex);
        return m_sender_count > 0;
    }
};

export template <std::movable T, typename Alloc = std::allocator<T>>
struct WeakSender;

/** @brief Thread-safe MPSC sender that pushes values into a shared ConQueue.
 * @tparam T Element type (must be movable).
 * @tparam Alloc Allocator type.
 */
export template <std::movable T, typename Alloc = std::allocator<T>>
struct Sender {
    using queue_type = ConQueue<T, Alloc>;

   private:
    mutable std::shared_ptr<queue_type> m_queue;

    static void connect(const std::shared_ptr<queue_type>& queue) {
        if (queue) queue->add_sender();
    }

    static void disconnect(const std::shared_ptr<queue_type>& queue) {
        if (queue) queue->remove_sender();
    }

   public:
    Sender(const std::shared_ptr<queue_type>& queue) : m_queue(queue) { connect(m_queue); }
    Sender() = default;
    Sender(const Sender& other) : m_queue(other.m_queue) { connect(m_queue); }
    Sender(Sender&& other) noexcept : m_queue(std::exchange(other.m_queue, nullptr)) {}
    Sender& operator=(const Sender& other) {
        if (this == &other || m_queue == other.m_queue) return *this;
        disconnect(m_queue);
        m_queue = other.m_queue;
        connect(m_queue);
        return *this;
    }
    Sender& operator=(Sender&& other) noexcept {
        if (this == &other) return *this;
        disconnect(m_queue);
        m_queue = std::exchange(other.m_queue, nullptr);
        return *this;
    }
    ~Sender() { disconnect(m_queue); }

    /** @brief Check whether this sender is connected to a queue. */
    operator bool() const { return m_queue.operator bool(); }
    /** @brief Check whether this sender is disconnected. */
    bool operator!() const { return !m_queue; }

    /** @brief Send a value by constructing it in-place into the queue.
     * @tparam Args Constructor argument types.
     * @param args Arguments forwarded to T's constructor.
     */
    template <typename... Args>
    void send(Args&&... args) const {
        if (!m_queue) return;
        m_queue->emplace(std::forward<Args>(args)...);
    }

    void close() const {
        if (!m_queue) return;
        m_queue->close();
    }

    WeakSender<T, Alloc> downgrade() const;
};

/** @brief Weak sender handle that does not keep a channel open by itself. */
export template <std::movable T, typename Alloc>
struct WeakSender {
    using queue_type = ConQueue<T, Alloc>;

   private:
    std::weak_ptr<queue_type> m_queue;

   public:
    WeakSender() = default;
    WeakSender(const std::shared_ptr<queue_type>& queue) : m_queue(queue) {}

    std::optional<Sender<T, Alloc>> upgrade() const {
        auto queue = m_queue.lock();
        if (!queue) return std::nullopt;
        if (!queue->has_senders()) return std::nullopt;
        return Sender<T, Alloc>(queue);
    }
};

template <std::movable T, typename Alloc>
WeakSender<T, Alloc> Sender<T, Alloc>::downgrade() const {
    return WeakSender<T, Alloc>(m_queue);
}
/** @brief Thread-safe receiver that pops values from a shared ConQueue.
 *
 * Supports blocking receive and non-blocking try_receive.
 * @tparam T Element type (must be movable).
 * @tparam Alloc Allocator type.
 */
export template <std::movable T, typename Alloc = std::allocator<T>>
struct Receiver {
    using queue_type = ConQueue<T, Alloc>;

   private:
    std::shared_ptr<queue_type> m_queue;

   public:
    Receiver(const std::shared_ptr<queue_type>& queue) : m_queue(queue) {}
    Receiver()                           = default;
    Receiver(const Receiver&)            = default;
    Receiver(Receiver&&)                 = default;
    Receiver& operator=(const Receiver&) = default;
    Receiver& operator=(Receiver&&)      = default;
    ~Receiver()                          = default;

    /** @brief Check whether this receiver is connected to a queue. */
    operator bool() const { return m_queue.operator bool(); }
    /** @brief Check whether this receiver is disconnected. */
    bool operator!() const { return !m_queue; }

    /** @brief Block until a value is available, then return it.
     * @throws std::runtime_error If the receiver is not initialized.
     */
    std::expected<T, ReceiveError> receive() const {
        if (!m_queue) {
            return std::unexpected(ReceiveError::Closed);
        }
        return m_queue->pop();
    }
    /** @brief Try to receive a value without blocking.
     * @return The received value, or std::nullopt if unavailable.
     */
    std::expected<T, ReceiveError> try_receive() const {
        if (!m_queue) {
            return std::unexpected(ReceiveError::Closed);
        }
        return m_queue->try_pop();
    }
    /** @brief Create a new Sender connected to the same queue. */
    Sender<T, Alloc> create_sender() const { return Sender<T, Alloc>(m_queue); }

    template <typename U, typename Alloc2>
    friend std::pair<Sender<U, Alloc2>, Receiver<U, Alloc2>> make_channel();
};

/** @brief Create a Sender/Receiver channel pair backed by a shared ConQueue.
 * @tparam T Element type.
 * @return A pair of (Sender, Receiver).
 */
export template <std::movable T, typename Alloc = std::allocator<T>>
std::pair<Sender<T, Alloc>, Receiver<T, Alloc>> make_channel() {
    auto queue = std::make_shared<ConQueue<T, Alloc>>();
    return {Sender<T, Alloc>(queue), Receiver<T, Alloc>(queue)};
}
/** @brief Create a bounded Sender/Receiver channel pair backed by a shared ConQueue.
 * @tparam T Element type.
 * @param capacity Maximum number of elements the queue can hold.
 * @param policy Overflow policy when the queue is full.
 * @return A pair of (Sender, Receiver).
 */
export template <std::movable T, typename Alloc = std::allocator<T>>
std::pair<Sender<T, Alloc>, Receiver<T, Alloc>> make_channel(std::size_t capacity,
                                                             OverflowPolicy policy = OverflowPolicy::Block) {
    auto queue = std::make_shared<ConQueue<T, Alloc>>(capacity, policy);
    return {Sender<T, Alloc>(queue), Receiver<T, Alloc>(queue)};
}

template <typename T>
struct BroadcastCursor {
    std::size_t next_index = 0;
};

template <typename T>
struct BroadcastQueue {
   private:
    using cursor_type = BroadcastCursor<T>;

    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv;
    std::deque<T> m_messages;
    std::vector<std::weak_ptr<cursor_type>> m_cursors;
    std::size_t m_front_index    = 0;
    std::size_t m_next_index     = 0;
    std::size_t m_sender_count   = 0;
    std::size_t m_receiver_count = 0;
    bool m_closed                = false;

    void prune_consumed_locked() {
        std::size_t min_next_index = m_next_index;
        bool has_live_cursor       = false;

        auto out = m_cursors.begin();
        for (auto it = m_cursors.begin(); it != m_cursors.end(); ++it) {
            if (auto cursor = it->lock()) {
                has_live_cursor = true;
                min_next_index  = std::min(min_next_index, cursor->next_index);
                *out++          = *it;
            }
        }
        m_cursors.erase(out, m_cursors.end());

        if (!has_live_cursor) {
            min_next_index = m_next_index;
        }

        while (m_front_index < min_next_index && !m_messages.empty()) {
            m_messages.pop_front();
            ++m_front_index;
        }
    }

    std::expected<T, ReceiveError> receive_locked(cursor_type& cursor) {
        if (cursor.next_index < m_front_index) {
            cursor.next_index = m_front_index;
        }
        if (cursor.next_index >= m_next_index) {
            return std::unexpected(ReceiveError::Closed);
        }

        auto offset = cursor.next_index - m_front_index;
        T value     = m_messages[offset];
        ++cursor.next_index;
        prune_consumed_locked();
        return value;
    }

   public:
    using cursor_ptr = std::shared_ptr<cursor_type>;

    BroadcastQueue()                                 = default;
    BroadcastQueue(const BroadcastQueue&)            = delete;
    BroadcastQueue(BroadcastQueue&&)                 = delete;
    BroadcastQueue& operator=(const BroadcastQueue&) = delete;
    BroadcastQueue& operator=(BroadcastQueue&&)      = delete;
    ~BroadcastQueue()                                = default;

    void add_sender() {
        std::unique_lock lock(m_mutex);
        if (m_closed) return;
        ++m_sender_count;
    }

    void remove_sender() {
        std::unique_lock lock(m_mutex);
        if (m_sender_count == 0) return;
        --m_sender_count;
        if (m_sender_count == 0) {
            m_cv.notify_all();
        }
    }

    void add_receiver() {
        std::unique_lock lock(m_mutex);
        ++m_receiver_count;
    }

    void remove_receiver() {
        std::unique_lock lock(m_mutex);
        if (m_receiver_count == 0) return;
        --m_receiver_count;
        if (m_receiver_count == 0) {
            m_closed = true;
            prune_consumed_locked();
            m_cv.notify_all();
        }
    }

    void close() {
        std::unique_lock lock(m_mutex);
        m_closed = true;
        prune_consumed_locked();
        m_cv.notify_all();
    }

    cursor_ptr create_cursor(std::size_t next_index = 0) {
        std::unique_lock lock(m_mutex);
        auto cursor        = std::make_shared<cursor_type>();
        cursor->next_index = std::max(next_index, m_front_index);
        m_cursors.push_back(cursor);
        return cursor;
    }

    cursor_ptr clone_cursor(const cursor_ptr& other) {
        std::unique_lock lock(m_mutex);
        auto cursor = std::make_shared<cursor_type>();
        if (other) {
            cursor->next_index = std::max(other->next_index, m_front_index);
        } else {
            cursor->next_index = m_front_index;
        }
        m_cursors.push_back(cursor);
        return cursor;
    }

    void push(T value) {
        std::unique_lock lock(m_mutex);
        if (m_closed) return;
        m_messages.emplace_back(std::move(value));
        ++m_next_index;
        m_cv.notify_all();
    }

    std::expected<T, ReceiveError> receive(const cursor_ptr& cursor) {
        if (!cursor) {
            return std::unexpected(ReceiveError::Closed);
        }

        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [&] { return cursor->next_index < m_next_index || m_sender_count == 0 || m_closed; });
        if (cursor->next_index < m_next_index) {
            return receive_locked(*cursor);
        }
        return std::unexpected(ReceiveError::Closed);
    }

    std::expected<T, ReceiveError> try_receive(const cursor_ptr& cursor) {
        if (!cursor) {
            return std::unexpected(ReceiveError::Closed);
        }

        std::unique_lock lock(m_mutex);
        if (cursor->next_index < m_next_index) {
            return receive_locked(*cursor);
        }
        return std::unexpected((m_sender_count == 0 || m_closed) ? ReceiveError::Closed : ReceiveError::Empty);
    }
};

/** @brief Sender end of a multi-message broadcast channel.
 *
 * Every sent value is delivered to all receivers independently.
 * When the last sender disconnects, waiting receivers observe Closed once all
 * buffered messages have been consumed.
 * @tparam T Value type (must be copy-constructible).
 */
export template <typename T>
struct BroadcastSender {
   private:
    using queue_type = BroadcastQueue<T>;

    std::shared_ptr<queue_type> m_queue;

    static void connect(const std::shared_ptr<queue_type>& queue) {
        if (queue) queue->add_sender();
    }

    static void disconnect(const std::shared_ptr<queue_type>& queue) {
        if (queue) queue->remove_sender();
    }

   public:
    BroadcastSender() = default;
    BroadcastSender(const std::shared_ptr<queue_type>& queue) : m_queue(queue) { connect(m_queue); }
    BroadcastSender(const BroadcastSender& other) : m_queue(other.m_queue) { connect(m_queue); }
    BroadcastSender(BroadcastSender&& other) noexcept : m_queue(std::exchange(other.m_queue, nullptr)) {}
    BroadcastSender& operator=(const BroadcastSender& other) {
        if (this == &other || m_queue == other.m_queue) return *this;
        disconnect(m_queue);
        m_queue = other.m_queue;
        connect(m_queue);
        return *this;
    }
    BroadcastSender& operator=(BroadcastSender&& other) noexcept {
        if (this == &other) return *this;
        disconnect(m_queue);
        m_queue = std::exchange(other.m_queue, nullptr);
        return *this;
    }
    ~BroadcastSender() { disconnect(m_queue); }

    operator bool() const { return m_queue != nullptr; }
    bool operator!() const { return m_queue == nullptr; }

    void send(T value) const {
        if (!m_queue) return;
        m_queue->push(std::move(value));
    }

    void close() const {
        if (!m_queue) return;
        m_queue->close();
    }
};

/** @brief Receiver end of a multi-message broadcast channel.
 *
 * Receiver copies keep their own read cursor and each independently observe the
 * same sequence of values from their copy point onward.
 * @tparam T Value type (must be copy-constructible).
 */
export template <typename T>
struct BroadcastReceiver {
   private:
    using queue_type  = BroadcastQueue<T>;
    using cursor_type = typename queue_type::cursor_ptr;

    std::shared_ptr<queue_type> m_queue;
    cursor_type m_cursor;

    static void connect(const std::shared_ptr<queue_type>& queue) {
        if (queue) queue->add_receiver();
    }

    static void disconnect(const std::shared_ptr<queue_type>& queue) {
        if (queue) queue->remove_receiver();
    }

   public:
    BroadcastReceiver() = default;
    BroadcastReceiver(std::shared_ptr<queue_type> queue, cursor_type cursor)
        : m_queue(std::move(queue)), m_cursor(std::move(cursor)) {
        connect(m_queue);
    }
    BroadcastReceiver(const BroadcastReceiver& other) : m_queue(other.m_queue) {
        if (m_queue) {
            m_cursor = m_queue->clone_cursor(other.m_cursor);
            connect(m_queue);
        }
    }
    BroadcastReceiver(BroadcastReceiver&& other) noexcept
        : m_queue(std::exchange(other.m_queue, nullptr)), m_cursor(std::exchange(other.m_cursor, nullptr)) {}
    BroadcastReceiver& operator=(const BroadcastReceiver& other) {
        if (this == &other) return *this;
        disconnect(m_queue);
        m_queue = other.m_queue;
        m_cursor.reset();
        if (m_queue) {
            m_cursor = m_queue->clone_cursor(other.m_cursor);
            connect(m_queue);
        }
        return *this;
    }
    BroadcastReceiver& operator=(BroadcastReceiver&& other) noexcept {
        if (this == &other) return *this;
        disconnect(m_queue);
        m_queue  = std::exchange(other.m_queue, nullptr);
        m_cursor = std::exchange(other.m_cursor, nullptr);
        return *this;
    }
    ~BroadcastReceiver() { disconnect(m_queue); }

    operator bool() const { return m_queue != nullptr && m_cursor != nullptr; }
    bool operator!() const { return !m_queue || !m_cursor; }

    std::expected<T, ReceiveError> receive() const {
        if (!m_queue || !m_cursor) {
            return std::unexpected(ReceiveError::Closed);
        }
        return m_queue->receive(m_cursor);
    }

    std::expected<T, ReceiveError> try_receive() const {
        if (!m_queue || !m_cursor) {
            return std::unexpected(ReceiveError::Closed);
        }
        return m_queue->try_receive(m_cursor);
    }
};

/** @brief Create a multi-message broadcast channel pair.
 *
 * All receiver copies observe the same sequence independently. If all senders
 * disconnect before another value arrives, receivers return Closed instead of
 * blocking forever.
 * @tparam T Element type (must be copy-constructible).
 * @return A pair of (BroadcastSender, BroadcastReceiver).
 */
export template <typename T>
std::pair<BroadcastSender<T>, BroadcastReceiver<T>> make_broadcast_channel() {
    auto queue  = std::make_shared<BroadcastQueue<T>>();
    auto cursor = queue->create_cursor();
    return {BroadcastSender<T>(queue), BroadcastReceiver<T>(queue, std::move(cursor))};
}
/** @brief Simple mutex wrapper that pairs a std::mutex with the data it protects.
 *
 * Access the protected data via lock() which returns a Guard, or via
 * with_lock() which takes a callable.
 * @tparam T The protected value type.
 */
export template <typename T>
struct Mutex {
    /** @brief RAII guard that locks the mutex and provides access to the
     * protected value. */
    struct Guard {
       private:
        std::unique_lock<std::mutex> const lock;

       public:
        /** @brief Reference to the mutex-protected value. */
        T& ref;

        /** @brief Construct a guard, locking the given mutex. */
        Guard(std::mutex& mtx, T& val) : lock(mtx), ref(val) {}
        Guard(const Guard&)            = delete;
        Guard(Guard&&)                 = default;
        Guard& operator=(const Guard&) = delete;
        Guard& operator=(Guard&&)      = delete;

        /** @brief Access the protected value by pointer. */
        T* operator->() { return &ref; }
        /** @brief Dereference the guard to get the protected value. */
        T& operator*() { return ref; }
        /** @brief Access the protected value by const pointer. */
        const T* operator->() const { return &ref; }
        /** @brief Dereference the guard to get a const reference. */
        const T& operator*() const { return ref; }
        /** @brief Implicit conversion to a mutable reference. */
        operator T&() { return ref; }
        /** @brief Implicit conversion to a const reference. */
        operator const T&() const { return ref; }
    };

   private:
    mutable std::mutex m_mutex;
    union {
        mutable T m_value;
    };

   public:
    /** @brief Construct the protected value in-place.
     * @tparam Args Constructor argument types.
     * @param args Arguments forwarded to T's constructor.
     */
    template <typename... Args>
    Mutex(Args&&... args)
        requires std::constructible_from<T, Args...>
    {
        new (&m_value) T(std::forward<Args>(args)...);
    }
    Mutex(const Mutex&) = delete;
    /** @brief Move-construct by locking both mutexes and moving the value. */
    Mutex(Mutex&& other)
        requires std::move_constructible<T>
    {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        new (&m_value) T(std::move(other.m_value));
    }
    Mutex& operator=(const Mutex&) = delete;
    /** @brief Move-assign by locking both mutexes and moving the value. */
    Mutex& operator=(Mutex&& other)
        requires std::is_move_assignable<T>::value
    {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        m_value = std::move(other.m_value);
        return *this;
    }
    ~Mutex() {
        std::lock_guard lock(m_mutex);
        m_value.~T();
    }
    /** @brief Create a copy of the Mutex by cloning the protected value. */
    Mutex clone() const
        requires std::copy_constructible<T>
    {
        std::lock_guard lock(m_mutex);
        return Mutex(m_value);
    }

    /** @brief Execute a callable while holding the lock.
     * @tparam Func A callable accepting T&.
     * @param func The callable to invoke with the protected value.
     * @return The result of invoking func.
     */
    template <std::invocable<T&> Func>
    auto with_lock(Func&& func) const -> std::invoke_result_t<Func, T&> {
        std::lock_guard lock(m_mutex);
        return func(m_value);
    }

    /** @brief Lock the mutex and return a Guard that provides access to the
     * value. */
    Guard lock() const { return Guard(m_mutex, m_value); }
};
export template <typename T>
struct RwLock {
   public:
    struct ReadGuard {
       private:
        std::shared_lock<std::shared_mutex> const lock;

       public:
        const T& ref;

        ReadGuard(std::shared_mutex& mtx, const T& val) : lock(mtx), ref(val) {}
        ReadGuard(const ReadGuard&)            = delete;
        ReadGuard(ReadGuard&&)                 = default;
        ReadGuard& operator=(const ReadGuard&) = delete;
        ReadGuard& operator=(ReadGuard&&)      = delete;

        const T* operator->() const { return &ref; }
        const T& operator*() const { return ref; }
        operator const T&() const { return ref; }
    };
    struct WriteGuard {
       private:
        std::unique_lock<std::shared_mutex> const lock;

       public:
        T& ref;

        WriteGuard(std::shared_mutex& mtx, T& val) : lock(mtx), ref(val) {}
        WriteGuard(const WriteGuard&)            = delete;
        WriteGuard(WriteGuard&&)                 = default;
        WriteGuard& operator=(const WriteGuard&) = delete;
        WriteGuard& operator=(WriteGuard&&)      = delete;

        T* operator->() { return &ref; }
        T& operator*() { return ref; }
        const T* operator->() const { return &ref; }
        const T& operator*() const { return ref; }
        operator T&() { return ref; }
        operator const T&() const { return ref; }
    };

   private:
    mutable std::shared_mutex m_mutex;
    union {
        mutable T m_value;
    };

   public:
    template <typename... Args>
    RwLock(Args&&... args)
        requires std::constructible_from<T, Args...>
    {
        new (&m_value) T(std::forward<Args>(args)...);
    }
    RwLock(const RwLock&) = delete;
    RwLock(RwLock&& other)
        requires std::move_constructible<T>
    {
        std::unique_lock lock(m_mutex, std::defer_lock);
        std::unique_lock other_lock(other.m_mutex, std::defer_lock);
        std::lock(lock, other_lock);
        new (&m_value) T(std::move(other.m_value));
    }
    RwLock& operator=(const RwLock&) = delete;
    RwLock& operator=(RwLock&& other)
        requires std::is_move_assignable<T>::value
    {
        std::unique_lock lock(m_mutex, std::defer_lock);
        std::unique_lock other_lock(other.m_mutex, std::defer_lock);
        std::lock(lock, other_lock);
        m_value = std::move(other.m_value);
        return *this;
    }
    RwLock clone() const
        requires std::copy_constructible<T>
    {
        std::shared_lock lock(m_mutex);
        return RwLock(m_value);
    }
    ~RwLock() {
        std::unique_lock lock(m_mutex);
        m_value.~T();
    }

    ReadGuard read() const { return ReadGuard(m_mutex, m_value); }
    WriteGuard write() const { return WriteGuard(m_mutex, m_value); }
};
}  // namespace epix::utils