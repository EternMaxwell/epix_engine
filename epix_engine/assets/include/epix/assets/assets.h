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
struct Handle;
struct StrongHandle;
struct AssetIndexAllocator;
template <typename T>
using Sender = index::channel::Sender<T>;
template <typename T>
using Receiver = index::channel::Receiver<T>;

struct AssetIndex {
    uint32_t index;
    uint32_t generation;

   protected:
    AssetIndex(uint32_t index, uint32_t generation)
        : index(index), generation(generation) {}

   public:
    AssetIndex(const AssetIndex&)            = default;
    AssetIndex(AssetIndex&&)                 = default;
    AssetIndex& operator=(const AssetIndex&) = default;
    AssetIndex& operator=(AssetIndex&&)      = default;

    friend struct StrongHandle;
    template <typename T>
    friend struct Handle;
    friend struct AssetIndexAllocator;
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
        : event_sender(event_sender), index(index, generation) {}
    StrongHandle(const StrongHandle&) = delete;
    StrongHandle(StrongHandle&& other) : index(other.index) {
        other.event_sender = Sender<DestructionEvent>();
    }

    StrongHandle& operator=(const StrongHandle&)  = delete;
    StrongHandle& operator=(StrongHandle&& other) = delete;

    ~StrongHandle() {
        if (event_sender) {
            event_sender.send(DestructionEvent{index});
        }
    }
};
template <typename T>
struct Handle {
   private:
    std::variant<std::shared_ptr<StrongHandle>, AssetIndex> ref;

   public:
    Handle(const std::shared_ptr<StrongHandle>& handle) : ref(handle) {}
    Handle(const AssetIndex& index) : ref(index) {}

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
};

template <typename T>
struct AssetStorage {
   private:
    std::vector<std::optional<Entry<T>>> m_storage;
    uint32_t m_size;

   public:
    AssetStorage() : m_size(0) {}
    AssetStorage(const AssetStorage&)            = delete;
    AssetStorage(AssetStorage&&)                 = delete;
    AssetStorage& operator=(const AssetStorage&) = delete;
    AssetStorage& operator=(AssetStorage&&)      = delete;

    uint32_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }

    /**
     * @brief Insert an asset into the storage. If the asset already exists, it
     * will be replaced.
     *
     * @param index The index and generation of the asset to insert.
     * @param args The arguments to construct the asset.
     * @return std::optional<bool> True if the asset was replaced, false if it
     * was inserted. If the generation is different, std::nullopt is returned.
     */
    template <typename... Args>
    std::optional<bool> insert(const AssetIndex& index, Args&&... args) {
        if (index.index >= m_storage.size()) {
            m_storage.resize(index.index + 1);
        }
        if (!m_storage[index.index]) {
            m_storage[index.index]             = Entry<T>();
            m_storage[index.index]->generation = index.generation;
        } else if (m_storage[index.index]->generation != index.generation) {
            return std::nullopt;
        }
        bool res = m_storage[index.index]->asset.has_value();
        m_storage[index.index]->asset =
            std::make_optional<T>(std::forward<Args>(args)...);
        m_size++;
        return res;
    }

    std::optional<T> pop(const AssetIndex& index) {
        if (index.index < m_storage.size() && m_storage[index.index] &&
            m_storage[index.index]->generation == index.generation) {
            auto asset = std::move(m_storage[index.index]->asset.value());
            m_storage[index.index]->asset = std::nullopt;
            m_size--;
            return std::move(asset);
        } else {
            return std::nullopt;
        }
    }

    bool valid(const AssetIndex& index) const {
        return index.index < m_storage.size() && m_storage[index.index] &&
               m_storage[index.index]->generation == index.generation;
    }

    bool contains(const AssetIndex& index) const {
        return index.index < m_storage.size() && m_storage[index.index] &&
               m_storage[index.index]->asset.has_value() &&
               m_storage[index.index]->generation == index.generation;
    }

    bool remove(const AssetIndex& index) {
        if (index.index < m_storage.size() && m_storage[index.index] &&
            m_storage[index.index]->generation == index.generation) {
            m_storage[index.index]->asset = std::nullopt;
            m_size--;
            return true;
        } else {
            return false;
        }
    }

    bool remove_dereferenced(const AssetIndex& index) {
        if (index.index < m_storage.size() && m_storage[index.index] &&
            m_storage[index.index]->generation == index.generation) {
            m_storage[index.index] = std::nullopt;
            m_size--;
            return true;
        } else {
            return false;
        }
    }

    std::optional<std::reference_wrapper<T>> get(const AssetIndex& index) {
        if (index.index < m_storage.size() && m_storage[index.index] &&
            m_storage[index.index]->generation == index.generation &&
            m_storage[index.index]->asset) {
            return std::make_optional<std::reference_wrapper<T>>(
                m_storage[index.index]->asset.value()
            );
        } else {
            return std::nullopt;
        }
    }

    std::optional<std::reference_wrapper<const T>> get(const AssetIndex& index
    ) const {
        if (index.index < m_storage.size() && m_storage[index.index] &&
            m_storage[index.index]->generation == index.generation &&
            m_storage[index.index]->asset) {
            return std::make_optional<std::reference_wrapper<const T>>(
                m_storage[index.index]->asset.value()
            );
        } else {
            return std::nullopt;
        }
    }
};

struct AssetIndexAllocator {
   private:
    std::atomic<uint32_t> m_next = 0;
    Sender<AssetIndex> m_free_indices_sender;
    Receiver<AssetIndex> m_free_indices_receiver;

   public:
    AssetIndexAllocator()
        : m_free_indices_receiver(
              std::get<1>(index::channel::make_channel<AssetIndex>())
          ) {
        m_free_indices_sender = m_free_indices_receiver.create_sender();
    }
    AssetIndexAllocator(const AssetIndexAllocator&)            = delete;
    AssetIndexAllocator(AssetIndexAllocator&&)                 = delete;
    AssetIndexAllocator& operator=(const AssetIndexAllocator&) = delete;
    AssetIndexAllocator& operator=(AssetIndexAllocator&&)      = delete;

    AssetIndex reserve() {
        if (auto index = m_free_indices_receiver.try_receive()) {
            return AssetIndex(index->index, index->generation + 1);
        } else {
            uint32_t i = m_next.fetch_add(1, std::memory_order_relaxed);
            return AssetIndex(i, 0);
        }
    }
    void release(const AssetIndex& index) { m_free_indices_sender.send(index); }
};

template <typename T>
struct HandleProvider {
    AssetIndexAllocator m_allocator;
    Sender<DestructionEvent> m_event_sender;
    Receiver<DestructionEvent> m_event_receiver;
    std::vector<uint32_t> m_ref_counts;

    HandleProvider()
        : m_event_receiver(
              std::get<1>(index::channel::make_channel<DestructionEvent>())
          ) {
        m_event_sender = m_event_receiver.create_sender();
    }
    HandleProvider(const HandleProvider&)            = delete;
    HandleProvider(HandleProvider&&)                 = delete;
    HandleProvider& operator=(const HandleProvider&) = delete;
    HandleProvider& operator=(HandleProvider&&)      = delete;

    Handle<T> reserve() {
        auto index = m_allocator.reserve();
        reference(index.index);
        return Handle<T>(std::make_shared<StrongHandle>(index, m_event_sender));
    }

    void reference(uint32_t index) {
        if (index >= m_ref_counts.size()) {
            m_ref_counts.resize(index + 1, 0);
        }
        m_ref_counts[index]++;
    }

    void release(const Handle<T>& handle) { m_allocator.release(handle); }
    void release(const AssetIndex& index) { m_allocator.release(index); }

    void handle_events(const std::function<void(const AssetIndex&)>& callback) {
        while (auto event = m_event_receiver.try_receive()) {
            if (event->index.index < m_ref_counts.size() &&
                m_ref_counts[event->index.index] > 0) {
                m_ref_counts[event->index.index]--;
                if (m_ref_counts[event->index.index] == 0) {
                    callback(event->index);
                    release(event->index);
                }
            }
        }
    }
};

template <typename T>
struct Assets {
   private:
    AssetStorage<T> m_assets;
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
        m_logger->trace(
            "Emplacing asset at {} with gen {}", index.index, index.generation
        );
        auto res = m_assets.insert(index, std::forward<Args>(args)...);
        if (!res) {
            m_logger->error(
                "Failed to emplace asset at {} with gen {}, generation "
                "mismatch",
                index.index, index.generation
            );
        } else if (res.value()) {
            m_logger->debug(
                "Replaced asset at {} with gen {}", index.index,
                index.generation
            );
        } else {
            m_logger->debug(
                "Inserted asset at {} with gen {}", index.index,
                index.generation
            );
        }
        return handle;
    }

    bool valid(const AssetIndex& index) const {
        return m_assets.contains(index);
    }

    std::optional<Handle<T>> get_strong_handle(const AssetIndex& index) {
        if (valid(index)) {
            m_handle_provider->reference(index.index);
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
        return m_assets.get(index);
    }
    std::optional<std::reference_wrapper<T>> get_mut(const AssetIndex& index) {
        return m_assets.get(index);
    }
    bool remove(const AssetIndex& index) {
        if (valid(index)) {
            m_logger->trace(
                "Force removing asset at {} with gen {}, current ref count is "
                "{}",
                index.index, index.generation, m_assets[index.index].ref_count
            );
            return m_assets.remove(index);
        } else {
            return false;
        }
    }
    std::optional<T> pop(const AssetIndex& index) {
        if (valid(index)) {
            m_logger->trace(
                "Force popping asset at {} with gen {}, current ref count is "
                "{}",
                index.index, index.generation, m_assets[index.index].ref_count
            );
            return std::move(m_assets.pop(index));
        } else {
            return std::nullopt;
        }
    }

    void handle_events() {
        m_logger->trace("Handling events");
        m_handle_provider->handle_events([this](const AssetIndex& index) {
            // this index now has 0 references, we can destroy the asset
            m_logger->debug(
                "Asset at {} with gen {} has 0 references, destroying it",
                index.index, index.generation
            );
            auto asset = m_assets.get(index);
            if (asset) {
                m_logger->debug(
                    "Destroying asset at {} with gen {}", index.index,
                    index.generation
                );
                if (m_destruct_behaviour) {
                    m_destruct_behaviour(asset.value().get());
                }
            } else {
                m_logger->error(
                    "Failed to destroy asset at {} with gen {}, asset not "
                    "found",
                    index.index, index.generation
                );
            }
            m_assets.remove_dereferenced(index);
        });
        m_logger->trace("Finished handling events");
    }
};
}  // namespace epix::assets