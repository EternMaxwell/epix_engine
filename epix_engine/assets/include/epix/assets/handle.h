#pragma once

#include "index.h"

namespace epix::assets {
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
struct HandleProvider {
    AssetIndexAllocator m_allocator;
    Sender<DestructionEvent> m_event_sender;
    Receiver<DestructionEvent> m_event_receiver;
    std::vector<uint32_t> m_ref_counts;
    Receiver<AssetIndex> m_reserved;
    Sender<AssetIndex> m_reserved_sender;

    HandleProvider()
        : m_event_receiver(
              std::get<1>(epix::utils::async::make_channel<DestructionEvent>())
          ),
          m_reserved(std::get<1>(epix::utils::async::make_channel<AssetIndex>())
          ) {
        m_event_sender    = m_event_receiver.create_sender();
        m_reserved_sender = m_reserved.create_sender();
    }
    HandleProvider(const HandleProvider&)            = delete;
    HandleProvider(HandleProvider&&)                 = delete;
    HandleProvider& operator=(const HandleProvider&) = delete;
    HandleProvider& operator=(HandleProvider&&)      = delete;

    Handle<T> reserve() {
        auto index = m_allocator.reserve();
        m_reserved_sender.send(index);
        reference(index.index);
        return Handle<T>(std::make_shared<StrongHandle>(index, m_event_sender));
    }

    void reference(uint32_t index) {
        if (index >= m_ref_counts.size()) {
            m_ref_counts.resize(index + 1, 0);
        }
        m_ref_counts[index]++;
    }

    uint32_t ref_count(uint32_t index) {
        if (m_ref_counts.size() > index) {
            return m_ref_counts[index];
        }
        return 0;
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
}  // namespace epix::assets