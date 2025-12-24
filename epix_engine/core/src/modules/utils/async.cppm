module;

#include <condition_variable>
#include <deque>
#include <optional>

export module epix.core:utils.async;

namespace core {
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

    operator bool() { return m_queue.operator bool(); }
    bool operator!() { return !m_queue; }

    template <typename... Args>
    void send(Args&&... args) const {
        if (!m_queue) return;
        m_queue->emplace(std::forward<Args>(args)...);
    }
};
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

    operator bool() const { return m_queue.operator bool(); }
    bool operator!() const { return !m_queue; }

    T receive() const {
        if (!m_queue) {
            throw std::runtime_error("Receiver is not initialized.");
        }
        return m_queue->pop();
    }
    std::optional<T> try_receive() const {
        if (!m_queue) {
            return std::nullopt;
        }
        return m_queue->try_pop();
    }
    Sender<T, Alloc> create_sender() const { return Sender<T, Alloc>(m_queue); }

    template <typename U, typename Alloc2>
    friend std::pair<Sender<U, Alloc2>, Receiver<U, Alloc2>> make_channel();
};

export template <std::movable T, typename Alloc = std::allocator<T>>
std::pair<Sender<T, Alloc>, Receiver<T, Alloc>> make_channel() {
    auto queue = std::make_shared<ConQueue<T, Alloc>>();
    return {Sender<T, Alloc>(queue), Receiver<T, Alloc>(queue)};
}
}  // namespace core