#pragma once

// #include <concurrentqueue.h>
#include <epix/common.h>
#include <index/concurrent/channel.h>
#include <index/traits/variant.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>

namespace epix::assets {
template <typename T>
using Sender = index::channel::Sender<T>;
template <typename T>
using Receiver = index::channel::Receiver<T>;

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
    Handle(const AssetIndex& index) : ref(index) {}
    Handle() : ref(AssetIndex()) {}

    Handle(const Handle& other)            = default;
    Handle(Handle&& other)                 = default;
    Handle& operator=(const Handle& other) = default;
    Handle& operator=(Handle&& other)      = default;

    bool is_strong() const {
        return std::holds_alternative<std::shared_ptr<StrongHandle>>(ref);
    }
    bool is_weak() const { return std::holds_alternative<AssetIndex>(ref); }

    Handle<T> weak() const { return Handle<T>(operator const AssetIndex&()); }

    operator const AssetIndex&() const {
        if (is_strong()) {
            return std::get<std::shared_ptr<StrongHandle>>(ref)->index;
        } else {
            return std::get<AssetIndex>(ref);
        }
    }
    operator AssetIndex&() {
        if (is_strong()) {
            return std::get<std::shared_ptr<StrongHandle>>(ref)->index;
        } else {
            return std::get<AssetIndex>(ref);
        }
    }
};

template <typename T>
struct Entry {
    std::optional<T> asset = std::nullopt;
    uint32_t generation    = 0;
    uint32_t ref_count     = 0;
};

struct AssetIndexAllocator {
    std::atomic<uint32_t> m_next = 0;
    Sender<AssetIndex> m_free_indices_sender;
    Receiver<AssetIndex> m_free_indices_receiver;

    AssetIndexAllocator()
        : m_free_indices_receiver(
              std::get<1>(index::channel::make_channel<AssetIndex>())
          ),
          m_free_indices_sender(m_free_indices_receiver.create_sender()) {}
    AssetIndexAllocator(const AssetIndexAllocator&)            = delete;
    AssetIndexAllocator(AssetIndexAllocator&&)                 = delete;
    AssetIndexAllocator& operator=(const AssetIndexAllocator&) = delete;
    AssetIndexAllocator& operator=(AssetIndexAllocator&&)      = delete;

    AssetIndex reserve() {
        if (auto index = m_free_indices_receiver.try_receive()) {
            index->generation++;
            return index.value();
        } else {
            uint32_t i = m_next.fetch_add(1, std::memory_order_relaxed);
            return AssetIndex{i, 0};
        }
    }
    void release(const AssetIndex& index) { m_free_indices_sender.send(index); }
};

template <typename T>
struct HandleProvider {
    AssetIndexAllocator m_allocator;
    Sender<DestructionEvent> m_event_sender;
    Receiver<DestructionEvent> m_event_receiver;

    HandleProvider()
        : m_event_receiver(
              std::get<1>(index::channel::make_channel<DestructionEvent>())
          ),
          m_event_sender(m_event_receiver.create_sender()) {}
    HandleProvider(const HandleProvider&)            = delete;
    HandleProvider(HandleProvider&&)                 = delete;
    HandleProvider& operator=(const HandleProvider&) = delete;
    HandleProvider& operator=(HandleProvider&&)      = delete;

    Handle<T> reserve() {
        auto index = m_allocator.reserve();
        return Handle<T>(std::make_shared<StrongHandle>(index, m_event_sender));
    }

    void release(const Handle<T>& handle) { m_allocator.release(handle); }
    void release(const AssetIndex& index) { m_allocator.release(index); }
};

template <typename T>
struct Assets {
   private:
    std::vector<Entry<T>> m_assets;
    std::shared_ptr<HandleProvider<T>> m_handle_provider;
    std::function<void(T&)> m_destruct_behaviour;
    std::shared_ptr<spdlog::logger> m_logger;

   public:
    Assets() : m_handle_provider(std::make_shared<HandleProvider<T>>()) {
        m_logger =
            spdlog::default_logger()->clone(typeid(decltype(*this)).name());
    }
    Assets(const Assets&)            = delete;
    Assets(Assets&&)                 = delete;
    Assets& operator=(const Assets&) = delete;
    Assets& operator=(Assets&&)      = delete;

    void set_destruct_behaviour(std::function<void(T&&)> behaviour) {
        m_destruct_behaviour = behaviour;
    }
    void set_log_level(spdlog::level::level_enum level) {
        m_logger->set_level(level);
    }
    void set_log_label(const std::string& label) {
        m_logger = m_logger->clone(label);
    }

    std::shared_ptr<HandleProvider<T>> get_handle_provider() {
        return m_handle_provider;
    }

    template <typename... Args>
    Handle<T> emplace(Args&&... args) {
        Handle<T> handle = m_handle_provider->reserve();
        AssetIndex index = handle;
        if (index.index >= m_assets.size()) {
            m_assets.resize(index.index + 1);
        }
        m_assets[index.index].asset =
            std::make_optional<T>(std::forward<Args>(args)...);
        m_assets[index.index].generation = index.generation;
        m_assets[index.index].ref_count  = 1;
        m_logger->trace(
            "Emplaced asset at {} with gen {}", index.index, index.generation
        );
        return handle;
    }

    bool valid(const AssetIndex& index) const {
        return index.index < m_assets.size() &&
               m_assets[index.index].generation == index.generation &&
               m_assets[index.index].asset;
    }

    std::optional<Handle<T>> get_strong_handle(const AssetIndex& index) {
        if (valid(index)) {
            m_assets[index.index].ref_count++;
            return std::make_optional<Handle<T>>(std::make_shared<StrongHandle>(
                index.index, index.generation,
                m_handle_provider->m_event_receiver.create_sender()
            ));
        } else {
            return std::nullopt;
        }
    }

    std::optional<std::reference_wrapper<const T>> get(const AssetIndex& index
    ) const {
        if (valid(index)) {
            return std::make_optional<std::reference_wrapper<const T>>(
                m_assets[index.index].asset.value()
            );
        } else {
            return std::nullopt;
        }
    }
    std::optional<std::reference_wrapper<T>> get_mut(const AssetIndex& index) {
        if (valid(index)) {
            return std::make_optional<std::reference_wrapper<T>>(
                m_assets[index.index].asset.value()
            );
        } else {
            return std::nullopt;
        }
    }
    std::optional<T> remove(const AssetIndex& index) {
        if (valid(index)) {
            m_logger->trace(
                "Force removing asset at {} with gen {}, current ref count is "
                "{}",
                index.index, index.generation, m_assets[index.index].ref_count
            );
            auto asset = std::move(m_assets[index.index].asset.value());
            m_assets[index.index].asset = std::nullopt;
            m_handle_provider->release(index);
            return asset;
        } else {
            return std::nullopt;
        }
    }

    void handle_events() {
        m_logger->trace("Handling events");
        while (auto event = m_handle_provider->m_event_receiver.try_receive()) {
            auto& index = event->index;
            if (index.index < m_assets.size() &&
                m_assets[index.index].generation == index.generation &&
                m_assets[index.index].asset) {
                m_logger->trace(
                    "Decrease ref count of asset at {} with gen {}, current "
                    "ref count is {}",
                    index.index, index.generation,
                    m_assets[index.index].ref_count
                );
                m_assets[index.index].ref_count--;
                m_logger->trace(
                    "Ref count of asset at {} with gen {} is now {}",
                    index.index, index.generation,
                    m_assets[index.index].ref_count
                );
                if (m_assets[index.index].ref_count == 0) {
                    m_logger->trace(
                        "Ref count of asset at {} with gen {} is 0, "
                        "destructing",
                        index.index, index.generation
                    );
                    if (m_destruct_behaviour) {
                        m_destruct_behaviour(m_assets[index.index].asset.value()
                        );
                    }
                    m_assets[index.index].asset = std::nullopt;
                    m_handle_provider->release(index);
                }
            }
        }
        m_logger->trace("Finished handling events");
    }
};
}  // namespace epix::assets