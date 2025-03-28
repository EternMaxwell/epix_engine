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

template <typename T>
struct Assets {
    std::vector<Entry<T>> m_assets;
    std::deque<uint32_t> m_free_indices;
    Receiver<DestructionEvent> m_event_receiver;
    std::function<void(T&&)> m_destruct_behaviour;
    std::shared_ptr<spdlog::logger> m_logger;

    Assets()
        : m_event_receiver(
              std::get<1>(index::channel::make_channel<DestructionEvent>())
          ) {
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

    template <typename... Args>
    Handle<T> emplace(Args&&... args) {
        std::size_t index;
        if (m_free_indices.empty()) {
            index = m_assets.size();
            m_assets.emplace_back(
                std::make_optional<T>(std::forward<Args>(args)...), 0, 1
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
            m_assets[index].ref_count = 1;
            return Handle<T>(std::make_shared<StrongHandle>(
                index, m_assets[index].generation,
                m_event_receiver.create_sender()
            ));
        }
    }

    std::optional<Handle<T>> get_strong_handle(const AssetIndex& index) {
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

    std::optional<std::reference_wrapper<const T>> get(const AssetIndex& index
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
    std::optional<std::reference_wrapper<T>> get_mut(const AssetIndex& index) {
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
        m_logger->trace("Handling events");
        while (auto event = m_event_receiver.try_receive()) {
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
                        m_destruct_behaviour(
                            std::move(m_assets[index.index].asset.value())
                        );
                    }
                    m_assets[index.index].asset = std::nullopt;
                    m_free_indices.push_back(index.index);
                }
            }
        }
        m_logger->trace("Finished handling events");
    }
};
}  // namespace epix::assets