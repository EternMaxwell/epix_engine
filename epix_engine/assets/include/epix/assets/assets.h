#pragma once

#include <concurrentqueue.h>
#include <epix/common.h>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>

namespace epix::assets {
template <typename T>
struct conqueue {
   private:
    std::deque<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;

   public:
    conqueue()                           = default;
    conqueue(const conqueue&)            = delete;
    conqueue(conqueue&&)                 = delete;
    conqueue& operator=(const conqueue&) = delete;
    conqueue& operator=(conqueue&&)      = delete;

    template <typename... Args>
    void emplace(Args&&... args) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.emplace_back(std::forward<Args>(args)...);
        m_cv.notify_one();
    }
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return std::nullopt;
        } else {
            T value = std::move(m_queue.front());
            m_queue.pop_front();
            return std::make_optional(std::move(value));
        }
    }
    T pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty(); });
        T value = std::move(m_queue.front());
        m_queue.pop_front();
        return std::move(value);
    }
};

template <typename T>
struct Sender {
    std::shared_ptr<conqueue<T>> queue;

    Sender(const std::shared_ptr<conqueue<T>>& queue) : queue(queue) {}
    Sender() = default;

    template <typename... Args>
    void send(Args&&... args) {
        queue->emplace(std::forward<Args>(args)...);
    }

    operator bool() const { return queue != nullptr; }
    bool operator!() const { return queue == nullptr; }
};

template <typename T>
struct Receiver {
    std::shared_ptr<conqueue<T>> queue;

    Receiver(const std::shared_ptr<conqueue<T>>& queue) : queue(queue) {}
    Receiver() = default;

    std::optional<T> try_receive() { return std::move(queue->try_pop()); }
    T receive() { return std::move(queue->pop()); }

    Sender<T> create_sender() { return Sender<T>(queue); }

    operator bool() const { return queue != nullptr; }
    bool operator!() const { return queue == nullptr; }
};

template <typename T>
std::tuple<Sender<T>, Receiver<T>> make_channel() {
    auto queue = std::make_shared<conqueue<T>>();
    return std::make_tuple(Sender<T>(queue), Receiver<T>(queue));
}

struct AssetIndex {
    uint32_t index      = -1;
    uint32_t generation = -1;
};
struct AssetEvent {
    enum class Type { ADDED, REMOVED, MODIFIED, UNUSED };

    Type type;
    AssetIndex index;
};
struct DestructionEvent {
    AssetIndex index;
};
struct StrongHandle {
    AssetIndex index;
    Sender<DestructionEvent> event_sender;

    StrongHandle(
        const AssetIndex& index, const Sender<DestructionEvent>& event_sender
    )
        : index(index), event_sender(event_sender) {}
    StrongHandle(
        uint32_t index,
        uint32_t generation,
        const Sender<DestructionEvent>& event_sender
    )
        : event_sender(event_sender) {
        this->index.index      = index;
        this->index.generation = generation;
    }
    StrongHandle(const StrongHandle&) = delete;
    StrongHandle(StrongHandle&& other) : index(other.index) {
        other.index        = AssetIndex();
        other.event_sender = Sender<DestructionEvent>();
    }

    StrongHandle& operator=(const StrongHandle&) = delete;
    StrongHandle& operator=(StrongHandle&& other) {
        index              = other.index;
        other.index        = AssetIndex();
        event_sender       = other.event_sender;
        other.event_sender = Sender<DestructionEvent>();
        return *this;
    }

    ~StrongHandle() {
        if (event_sender) {
            event_sender.send(DestructionEvent{index});
        }
    }
};
template <typename T>
struct Handle {
    std::variant<std::shared_ptr<StrongHandle>, AssetIndex> ref;

    Handle(const std::shared_ptr<StrongHandle>& handle) : ref(handle) {}
    Handle(const AssetIndex<T>& index) : ref(index) {}

    Handle(const Handle& other) {
        ref = other.operator epix::assets::AssetIndex();
    }
    Handle(Handle&& other) {
        if (other.is_strong()) {
            ref       = std::get<std::shared_ptr<StrongHandle>>(other.ref);
            other.ref = AssetIndex();
        } else {
            ref = other.operator epix::assets::AssetIndex();
        }
    }
    Handle& operator=(const Handle& other) {
        ref = other.operator epix::assets::AssetIndex();
        return *this;
    }
    Handle& operator=(Handle&& other) {
        if (other.is_strong()) {
            ref       = std::get<std::shared_ptr<StrongHandle>>(other.ref);
            other.ref = AssetIndex();
        } else {
            ref = other.operator epix::assets::AssetIndex();
        }
        return *this;
    }

    bool is_strong() const {
        return std::holds_alternative<std::shared_ptr<StrongHandle>>(ref);
    }
    bool is_weak() const { return std::holds_alternative<AssetIndex>(ref); }

    operator AssetIndex() const {
        if (is_strong()) {
            return std::get<std::shared_ptr<StrongHandle>>(ref)->index;
        } else {
            return std::get<AssetIndex<T>>(ref);
        }
    }
};

template <typename T>
struct Entry {
    std::optional<T> asset = std::nullopt;
    uint32_t generation    = 0;
    uint32_t ref_count     = 0;
};

template <typename T>
struct Assets {
    std::vector<Entry<T>> m_assets;
    std::deque<uint32_t> m_free_indices;
    Receiver<DestructionEvent> m_event_receiver;
    std::function<void(T&&)> m_destruct_behaviour;

    Assets() {
        m_assets.reserve(16);
        m_free_indices.reserve(16);
        m_event_receiver = std::get<1>(make_channel<DestructionEvent>());
    }
    Assets(const Assets&)            = delete;
    Assets(Assets&&)                 = delete;
    Assets& operator=(const Assets&) = delete;
    Assets& operator=(Assets&&)      = delete;

    void set_destruct_behaviour(std::function<void(T&&)> behaviour) {
        m_destruct_behaviour = behaviour;
    }

    template <typename... Args>
    Handle<T> emplace(Args&&... args) {
        std::size_t index;
        if (m_free_indices.empty()) {
            index = m_assets.size();
            m_assets.emplace_back(
                std::make_optional<T>(std::forward<Args>(args)...), 0, 0
            );
            return Handle<T>(std::make_shared<StrongHandle>(
                index, 0, m_event_receiver.create_sender()
            ));
        } else {
            index = m_free_indices.front();
            m_free_indices.pop_front();
            m_assets[index].generation++;
            m_assets[index].asset =
                std::make_optional<T>(std::forward<Args>(args)...);
            return Handle<T>(std::make_shared<StrongHandle>(
                index, m_assets[index].generation,
                m_event_receiver.create_sender()
            ));
        }
    }

    std::optional<Handle<T>> get_strong_handle(const AssetIndex<T>& index) {
        if (index.index < m_assets.size() &&
            m_assets[index.index].generation == index.generation &&
            m_assets[index.index].asset) {
            m_assets[index.index].ref_count++;
            return std::make_optional<Handle<T>>(std::make_shared<StrongHandle>(
                index.index, index.generation, m_event_receiver.create_sender()
            ));
        } else {
            return std::nullopt;
        }
    }

    std::optional<std::reference_wrapper<const T>> get(
        const AssetIndex<T>& index
    ) const {
        if (index.index < m_assets.size() &&
            m_assets[index.index].generation == index.generation &&
            m_assets[index.index].asset) {
            return std::make_optional<std::reference_wrapper<const T>>(
                m_assets[index.index].asset.value()
            );
        } else {
            return std::nullopt;
        }
    }
    std::optional<std::reference_wrapper<T>> get_mut(const AssetIndex<T>& index
    ) {
        if (index.index < m_assets.size() &&
            m_assets[index.index].generation == index.generation &&
            m_assets[index.index].asset) {
            return std::make_optional<std::reference_wrapper<T>>(
                m_assets[index.index].asset.value()
            );
        } else {
            return std::nullopt;
        }
    }

    void handle_events() {
        while (auto event = m_event_receiver.receive()) {
            auto& index = event->index;
            if (index.index < m_assets.size() &&
                m_assets[index.index].generation == index.generation &&
                m_assets[index.index].asset) {
                m_assets[index.index].ref_count--;
                if (m_assets[index.index].ref_count == 0) {
                    if (m_destruct_behaviour) {
                        m_destruct_behaviour(
                            std::move(m_assets[index.index].asset.value())
                        );
                    }
                    m_assets[index.index].asset = std::nullopt;
                    m_free_indices.push_back(index.index);
                }
            }
        }
    }
};
}  // namespace epix::assets