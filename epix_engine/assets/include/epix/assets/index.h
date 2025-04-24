#pragma once

#include <epix/common.h>
#include <epix/utils/async.h>
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
struct StrongHandle;
template <typename T>
struct Handle;
struct AssetIndexAllocator;

using epix::utils::async::Receiver;
using epix::utils::async::Sender;

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

struct AssetIndexAllocator {
   private:
    std::atomic<uint32_t> m_next = 0;
    Sender<AssetIndex> m_free_indices_sender;
    Receiver<AssetIndex> m_free_indices_receiver;

   public:
    AssetIndexAllocator()
        : m_free_indices_receiver(
              std::get<1>(epix::utils::async::make_channel<AssetIndex>())
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
}  // namespace epix::assets