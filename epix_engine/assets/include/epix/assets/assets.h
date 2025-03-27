#pragma once

#include <concurrentqueue.h>
#include <epix/common.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>

namespace epix::assets {

template <typename T>
struct Sender {
    std::shared_ptr<moodycamel::ConcurrentQueue<T>> queue;

    Sender(const std::shared_ptr<moodycamel::ConcurrentQueue<T>>& queue)
        : queue(queue) {}
    Sender() = default;

    template <typename... Args>
    void send(Args&&... args) {
        queue->enqueue(T(std::forward<Args>(args)...));
    }
    void send(const T& msg) { queue->enqueue(msg); }
    void send(T&& msg) { queue->enqueue(std::move(msg)); }
};

template <typename T>
struct Receiver {
    std::shared_ptr<moodycamel::ConcurrentQueue<T>> queue;

    Receiver(const std::shared_ptr<moodycamel::ConcurrentQueue<T>>& queue)
        : queue(queue) {}
    Receiver() = default;

    std::optional<T> receive() {
        T msg;
        if (queue->try_dequeue(msg)) {
            return std::make_optional(std::move(msg));
        } else {
            return std::nullopt;
        }
    }

    Sender<T> create_sender() { return Sender<T>(queue); }
};

template <typename T>
std::tuple<Sender<T>, Receiver<T>> make_channel() {
    auto queue = std::make_shared<moodycamel::ConcurrentQueue<T>>();
    return std::make_tuple(Sender<T>(queue), Receiver<T>(queue));
}

struct AssetIndex {
    uint32_t index;
    uint32_t generation;
};
struct AssetEvent {
    enum class Type { ADDED, REMOVED, MODIFIED, UNUSED };

    Type type;
    AssetIndex index;
};
struct StrongHandle {
    AssetIndex index;
    Sender<AssetEvent> event_sender;

    StrongHandle(
        const AssetIndex& index, const Sender<AssetEvent>& event_sender
    )
        : index(index), event_sender(event_sender) {}
    StrongHandle(
        uint32_t index,
        uint32_t generation,
        const Sender<AssetEvent>& event_sender
    )
        : event_sender(event_sender) {
        this->index.index      = index;
        this->index.generation = generation;
    }
    StrongHandle(const StrongHandle&) = delete;
    StrongHandle(StrongHandle&& other) : index(other.index) {
        other.index.index      = -1;
        other.index.generation = -1;
        other.event_sender     = Sender<AssetEvent>();
    }
};
template <typename T>
struct Handle {
    std::variant<std::shared_ptr<StrongHandle>, AssetIndex> ref;

    Handle(const std::shared_ptr<StrongHandle>& handle) : ref(handle) {}
    Handle(const AssetIndex<T>& index) : ref(index) {}

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
    std::optional<T> asset;
    uint32_t generation;
    uint32_t ref_count;
};

template <typename T>
struct Assets {
    std::vector<Entry<T>> m_assets;
    std::deque<std::size_t> m_free_indices;
    Receiver<AssetEvent> m_event_receiver;

    Assets() {
        m_assets.reserve(16);
        m_free_indices.reserve(16);
        m_event_receiver = std::get<1>(make_channel<AssetEvent>());
    }
    Assets(const Assets&)            = delete;
    Assets(Assets&&)                 = delete;
    Assets& operator=(const Assets&) = delete;
    Assets& operator=(Assets&&)      = delete;

    template <typename... Args>
    Handle<T> emplace(Args&&... args) {
        std::size_t index;
        if (m_free_indices.empty()) {
            index = m_assets.size();
            m_assets.emplace_back(
                std::make_optional<T>(std::forward<Args>(args)...), 0
            );
            return Handle<T>(
                StrongHandle(index, 0, m_event_receiver.create_sender())
            );
        } else {
            index = m_free_indices.front();
            m_free_indices.pop_front();
            m_assets[index].generation++;
            m_assets[index].asset =
                std::make_optional<T>(std::forward<Args>(args)...);
        }
    }

    StrongHandle get_strong_handle(const AssetIndex<T>& index) {
        return StrongHandle(index, m_event_receiver.create_sender());
    }
};
}  // namespace epix::assets