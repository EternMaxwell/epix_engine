#pragma once

#include <concepts>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <tuple>
#include <type_traits>

namespace epix::utils::async {
template <typename T>
struct Mutex {
   private:
    mutable std::mutex m_mutex;
    union {
        T m_value;
    };

   public:
    template <typename... Args>
    Mutex(Args&&... args) {
        new (&m_value) T(std::forward<Args>(args)...);
    }
    Mutex(const T& value) { new (&m_value) T(value); }
    Mutex(T&& value) { new (&m_value) T(std::move(value)); }
    Mutex(const Mutex&) = delete;
    Mutex(Mutex&& other) {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        new (&m_value) T(std::move(other.m_value));
    }
    Mutex& operator=(const Mutex&) = delete;
    Mutex& operator=(Mutex&& other) noexcept {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        m_value = std::move(other.m_value);
        return *this;
    }
    ~Mutex() {
        std::lock_guard lock(m_mutex);
        m_value.~T();
    }
    Mutex clone() const {
        std::lock_guard lock(m_mutex);
        return Mutex(m_value);
    }

    std::unique_lock<std::mutex> lock() const {
        return std::unique_lock<std::mutex>(m_mutex);
    }
    T& get() { return m_value; }
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
template <typename T, typename Alloc = std::allocator<T>>
struct ConQueue {
    using value_type     = T;
    using container_type = std::deque<T>;
    using size_type      = typename container_type::size_type;

   private:
    container_type m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;

   public:
    ConQueue()                           = default;
    ConQueue(const ConQueue&)            = delete;
    ConQueue(ConQueue&&)                 = delete;
    ConQueue& operator=(const ConQueue&) = delete;
    ConQueue& operator=(ConQueue&&)      = delete;
    ~ConQueue()                          = default;

    template <typename... Args>
    void emplace(Args&&... args) {
        std::unique_lock lock(m_mutex);
        m_queue.emplace_back(std::forward<Args>(args)...);
        m_cv.notify_one();
    }
    T pop() {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty(); });
        T value = std::move(m_queue.front());
        m_queue.pop_front();
        return std::move(value);
    }
    std::optional<T> try_pop() {
        std::unique_lock lock(m_mutex);
        if (m_queue.empty()) return std::nullopt;
        T value = std::move(m_queue.front());
        m_queue.pop_front();
        return std::move(value);
    }
    bool empty() {
        std::unique_lock lock(m_mutex);
        return m_queue.empty();
    }
};
template <std::movable T, typename Alloc = std::allocator<T>>
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

    operator bool() { return m_queue.operator bool(); }
    bool operator!() { return !m_queue; }

    template <typename... Args>
    void send(Args&&... args) const {
        if (!m_queue) return;
        m_queue->emplace(std::forward<Args>(args)...);
    }
};
template <std::movable T, typename Alloc = std::allocator<T>>
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

    operator bool() { return m_queue.operator bool(); }
    bool operator!() { return !m_queue; }

    T receive() {
        if (!m_queue) {
            throw std::runtime_error("Receiver is not initialized.");
        }
        return m_queue->pop();
    }
    std::optional<T> try_receive() {
        if (!m_queue) {
            return std::nullopt;
        }
        return m_queue->try_pop();
    }
    Sender<T, Alloc> create_sender() const { return Sender<T, Alloc>(m_queue); }

    template <typename U, typename Alloc2>
    friend std::pair<Sender<U, Alloc2>, Receiver<U, Alloc2>> make_channel();
};

template <std::movable T, typename Alloc = std::allocator<T>>
std::pair<Sender<T, Alloc>, Receiver<T, Alloc>> make_channel() {
    auto queue = std::make_shared<ConQueue<T, Alloc>>();
    return {Sender<T, Alloc>(queue), Receiver<T, Alloc>(queue)};
}

template <typename T>
struct RwLock {
    struct ReadGuard
        : private std::pair<std::shared_lock<std::shared_mutex>, const T*> {
        using base_type =
            std::pair<std::shared_lock<std::shared_mutex>, const T*>;

       private:
        ReadGuard(std::shared_lock<std::shared_mutex>&& lock, const T* ptr)
            : base_type(std::move(lock), ptr) {}

       public:
        ReadGuard(const ReadGuard&) = delete;
        ReadGuard(ReadGuard&& other) noexcept : base_type(std::move(other)) {
            other.second = nullptr;  // Prevent double deletion
        }
        ReadGuard& operator=(const ReadGuard&) = delete;
        ReadGuard& operator=(ReadGuard&& other) noexcept {
            if (this != &other) {
                this->first  = std::move(other.first);
                this->second = other.second;
                other.second = nullptr;  // Prevent double deletion
            }
            return *this;
        }

        ~ReadGuard() = default;

        const T& operator*() const& { return *this->second; }
        const T* operator->() const& { return this->second; }

        friend struct RwLock<T>;
        template <typename... Ts>
        friend auto scoped_read(const RwLock<Ts>&... lock);
    };
    struct WriteGuard
        : private std::pair<std::unique_lock<std::shared_mutex>, T*> {
        using base_type = std::pair<std::unique_lock<std::shared_mutex>, T*>;

       private:
        WriteGuard(std::unique_lock<std::shared_mutex>&& lock, T* ptr)
            : base_type(std::move(lock), ptr) {}

       public:
        WriteGuard(const WriteGuard&) = delete;
        WriteGuard(WriteGuard&& other) noexcept : base_type(std::move(other)) {
            other.second = nullptr;  // Prevent double deletion
        }
        WriteGuard& operator=(const WriteGuard&) = delete;
        WriteGuard& operator=(WriteGuard&& other) noexcept {
            if (this != &other) {
                this->first  = std::move(other.first);
                this->second = other.second;
                other.second = nullptr;  // Prevent double deletion
            }
            return *this;
        }

        ~WriteGuard() = default;

        T& operator*() & { return *this->second; }
        T* operator->() & { return this->second; }

        friend struct RwLock<T>;
        template <typename... Ts>
        friend auto scoped_write(const RwLock<Ts>&... lock);
    };

   private:
    mutable std::shared_mutex m_mutex;
    mutable T m_value;

   public:
    template <typename... Args>
        requires(!((sizeof...(Args) == 1) &&
                   (std::same_as<RwLock<T>, std::decay_t<Args>> || ...))) &&
                std::constructible_from<T, Args...>
    RwLock(Args&&... args) : m_value(std::forward<Args>(args)...) {}
    RwLock(const RwLock&) = delete;
    RwLock(RwLock&& other) noexcept
        : m_value((other.m_mutex.lock(), std::move(other.m_value))) {
        other.m_mutex.unlock();
    }
    RwLock& operator=(const RwLock&) = delete;
    RwLock& operator=(RwLock&& other) noexcept {
        if (this != &other) {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_value = std::move(other.m_value);
        }
        return *this;
    }

    ~RwLock() = default;

    ReadGuard read() const {
        return ReadGuard(std::shared_lock<std::shared_mutex>(m_mutex),
                         &m_value);
    }
    WriteGuard write() const {
        return WriteGuard(std::unique_lock<std::shared_mutex>(m_mutex),
                          &m_value);
    }
    std::optional<ReadGuard> try_read() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex, std::defer_lock);
        if (lock.try_lock()) {
            return ReadGuard(std::move(lock), &m_value);
        }
        return std::nullopt;
    }
    std::optional<WriteGuard> try_write() const {
        std::unique_lock<std::shared_mutex> lock(m_mutex, std::defer_lock);
        if (lock.try_lock()) {
            return WriteGuard(std::move(lock), &m_value);
        }
        return std::nullopt;
    }
    std::shared_lock<std::shared_mutex> defer_read() const {
        return std::shared_lock<std::shared_mutex>(m_mutex, std::defer_lock);
    }
    std::unique_lock<std::shared_mutex> defer_write() const {
        return std::unique_lock<std::shared_mutex>(m_mutex, std::defer_lock);
    }
    const T& read(std::shared_lock<std::shared_mutex>& lock) const {
        if (!lock.owns_lock()) {
            lock.lock();
        }
        return m_value;
    }
    const T* try_read(std::shared_lock<std::shared_mutex>& lock) const {
        if (!lock.owns_lock() && !lock.try_lock()) {
            return nullptr;
        }
        return &m_value;
    }
    T& write(std::unique_lock<std::shared_mutex>& lock) const {
        if (!lock.owns_lock()) {
            lock.lock();
        }
        return m_value;
    }
    T* try_write(std::unique_lock<std::shared_mutex>& lock) const {
        if (!lock.owns_lock() && !lock.try_lock()) {
            return nullptr;
        }
        return &m_value;
    }

    template <typename... Ts>
    friend auto scoped_read(const RwLock<Ts>&... lock);
    template <typename... Ts>
    friend auto scoped_write(const RwLock<Ts>&... lock);
};
template <typename V, typename L>
struct MultiGuard;
template <typename... Vs, typename L>
struct MultiGuard<std::tuple<Vs...>, L> {
   private:
    std::tuple<Vs*...> m_values;
    L m_lock;

   public:
    MultiGuard(std::tuple<Vs*...>&& values, L&& lock)
        : m_values(std::move(values)), m_lock(std::move(lock)) {}
    MultiGuard(const MultiGuard&)            = delete;
    MultiGuard(MultiGuard&&)                 = default;
    MultiGuard& operator=(const MultiGuard&) = delete;
    MultiGuard& operator=(MultiGuard&&)      = default;
    ~MultiGuard()                            = default;

    std::tuple<Vs&...> operator*() & {
        return std::apply([](Vs*... vs) { return std::tie(*vs...); }, m_values);
    }
    std::tuple<const Vs&...> operator*() const& {
        return std::apply([](const Vs*... vs) { return std::tie(*vs...); },
                          m_values);
    }
};

template <typename... T>
struct MultiLock {
    std::tuple<std::decay_t<T>...> m_locks;
    MultiLock(T&&... locks) : m_locks(std::forward<T>(locks)...) {
        std::apply([](auto&... locks) { (locks.lock(), ...); }, m_locks);
    }
    MultiLock(const MultiLock&)            = delete;
    MultiLock(MultiLock&&)                 = default;
    MultiLock& operator=(const MultiLock&) = delete;
    MultiLock& operator=(MultiLock&&)      = default;
    ~MultiLock()                           = default;
};

template <typename... Ts>
auto scoped_write(const RwLock<Ts>&... lock) {
    MultiLock final_lock(lock.defer_write()...);
    return MultiGuard<std::tuple<Ts...>, decltype(final_lock)>(
        std::make_tuple(&lock.m_value...), std::move(final_lock));
}
template <typename... Ts>
auto scoped_read(const RwLock<Ts>&... lock) {
    MultiLock final_lock(lock.defer_read()...);
    return MultiGuard<std::tuple<const Ts...>, decltype(final_lock)>(
        std::make_tuple(&lock.m_value...), std::move(final_lock));
}
}  // namespace epix::utils::async
namespace epix::async {
using namespace utils::async;
}