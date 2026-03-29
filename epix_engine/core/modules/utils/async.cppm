module;

export module epix.utils:async;

import std;

namespace epix::utils {
/** @brief Policy controlling sender behavior when a bounded ConQueue is full. */
export enum class OverflowPolicy {
    Block,       ///< Block the sender until space is available.
    DropOldest,  ///< Drop the oldest element in the queue to make room.
    DropNewest,  ///< Discard the new element (do not enqueue).
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

   public:
    ConQueue() = default;
    ConQueue(std::size_t capacity, OverflowPolicy policy = OverflowPolicy::Block)
        : m_capacity(capacity), m_overflow_policy(policy) {}
    ConQueue(const ConQueue&)            = delete;
    ConQueue(ConQueue&&)                 = delete;
    ConQueue& operator=(const ConQueue&) = delete;
    ConQueue& operator=(ConQueue&&)      = delete;
    ~ConQueue()                          = default;

    /** @brief Construct an element in-place at the back of the queue.
     * @tparam Args Constructor argument types.
     * @param args Arguments forwarded to T's constructor.
     */
    template <typename... Args>
    void emplace(Args&&... args) {
        std::unique_lock lock(m_mutex);
        if (m_capacity.has_value()) {
            auto capacity = *m_capacity;
            if (m_queue.size() >= capacity) {
                switch (m_overflow_policy) {
                    case OverflowPolicy::Block:
                        m_cv.wait(lock, [this, capacity] { return m_queue.size() < capacity; });
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
    T pop() {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty(); });
        T value = std::move(m_queue.front());
        m_queue.pop_front();
        m_cv.notify_all();  // wake any sender waiting for space (Block policy)
        return value;
    }
    /** @brief Try to remove and return the front element without blocking.
     * @return The front element, or std::nullopt if the queue is empty.
     */
    std::optional<T> try_pop() {
        std::unique_lock lock(m_mutex);
        if (m_queue.empty()) return std::nullopt;
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
};
/** @brief Thread-safe MPSC sender that pushes values into a shared ConQueue.
 * @tparam T Element type (must be movable).
 * @tparam Alloc Allocator type.
 */
export template <std::movable T, typename Alloc = std::allocator<T>>
struct Sender {
    using queue_type = ConQueue<T, Alloc>;

   private:
    mutable std::shared_ptr<queue_type> m_queue;

   public:
    Sender(const std::shared_ptr<queue_type>& queue) : m_queue(queue) {}
    Sender()                         = default;
    Sender(const Sender&)            = default;
    Sender(Sender&&)                 = default;
    Sender& operator=(const Sender&) = default;
    Sender& operator=(Sender&&)      = default;
    ~Sender()                        = default;

    /** @brief Check whether this sender is connected to a queue. */
    operator bool() { return m_queue.operator bool(); }
    /** @brief Check whether this sender is disconnected. */
    bool operator!() { return !m_queue; }

    /** @brief Send a value by constructing it in-place into the queue.
     * @tparam Args Constructor argument types.
     * @param args Arguments forwarded to T's constructor.
     */
    template <typename... Args>
    void send(Args&&... args) const {
        if (!m_queue) return;
        m_queue->emplace(std::forward<Args>(args)...);
    }
};
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
    T receive() const {
        if (!m_queue) {
            throw std::runtime_error("Receiver is not initialized.");
        }
        return m_queue->pop();
    }
    /** @brief Try to receive a value without blocking.
     * @return The received value, or std::nullopt if unavailable.
     */
    std::optional<T> try_receive() const {
        if (!m_queue) {
            return std::nullopt;
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

/** @brief Sender end of a one-shot broadcast channel backed by std::promise.
 *
 * Sends a single value to ALL receivers independently.
 * Duplicate sends are silently ignored.
 * @tparam T Value type (must be copy-constructible).
 */
export template <typename T>
struct BroadcastSender {
   private:
    std::shared_ptr<std::promise<T>> m_promise;

   public:
    BroadcastSender() = default;
    BroadcastSender(std::shared_ptr<std::promise<T>> promise) : m_promise(std::move(promise)) {}
    BroadcastSender(const BroadcastSender&)            = default;
    BroadcastSender(BroadcastSender&&)                 = default;
    BroadcastSender& operator=(const BroadcastSender&) = default;
    BroadcastSender& operator=(BroadcastSender&&)      = default;
    ~BroadcastSender()                                 = default;

    operator bool() const { return m_promise != nullptr; }
    bool operator!() const { return m_promise == nullptr; }

    /** @brief Send a value to all receivers, blocking until stored.
     *  Silently ignores duplicate sends (promise already fulfilled).
     */
    void send(T value) const {
        if (!m_promise) return;
        try {
            m_promise->set_value(std::move(value));
        } catch (const std::future_error&) {}
    }
};

/** @brief Receiver end of a one-shot broadcast channel backed by std::shared_future.
 *
 * Cloneable: every copy independently receives the same value.
 * receive() blocks until the sender sends, then returns the value;
 * subsequent calls return the cached value immediately.
 * @tparam T Value type (must be copy-constructible).
 */
export template <typename T>
struct BroadcastReceiver {
   private:
    std::shared_future<T> m_future;

   public:
    BroadcastReceiver() = default;
    BroadcastReceiver(std::shared_future<T> future) : m_future(std::move(future)) {}
    BroadcastReceiver(const BroadcastReceiver&)            = default;
    BroadcastReceiver(BroadcastReceiver&&)                 = default;
    BroadcastReceiver& operator=(const BroadcastReceiver&) = default;
    BroadcastReceiver& operator=(BroadcastReceiver&&)      = default;
    ~BroadcastReceiver()                                   = default;

    operator bool() const { return m_future.valid(); }
    bool operator!() const { return !m_future.valid(); }

    /** @brief Block until the value is available, then return it.
     *  May be called multiple times; always returns the same value.
     */
    T receive() const { return m_future.get(); }
};

/** @brief Create a one-shot broadcast channel pair.
 *
 * The sender sets the value exactly once; all current and future receivers
 * independently receive that same value via receive().
 * @tparam T Element type (must be copy-constructible).
 * @return A pair of (BroadcastSender, BroadcastReceiver).
 */
export template <typename T>
std::pair<BroadcastSender<T>, BroadcastReceiver<T>> make_broadcast_channel() {
    auto promise = std::make_shared<std::promise<T>>();
    auto future  = std::shared_future<T>(promise->get_future());
    return {BroadcastSender<T>(std::move(promise)), BroadcastReceiver<T>(std::move(future))};
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
}  // namespace utils