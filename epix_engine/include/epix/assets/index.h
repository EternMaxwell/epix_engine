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
    EPIX_API AssetIndex(uint32_t index, uint32_t generation);

   public:
    AssetIndex(const AssetIndex&)            = default;
    AssetIndex(AssetIndex&&)                 = default;
    AssetIndex& operator=(const AssetIndex&) = default;
    AssetIndex& operator=(AssetIndex&&)      = default;

    bool operator==(const AssetIndex& other) const;
    bool operator!=(const AssetIndex& other) const;

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
    Receiver<AssetIndex> m_reserved;
    Sender<AssetIndex> m_reserved_sender;

   public:
    EPIX_API AssetIndexAllocator();
    AssetIndexAllocator(const AssetIndexAllocator&)            = delete;
    AssetIndexAllocator(AssetIndexAllocator&&)                 = delete;
    AssetIndexAllocator& operator=(const AssetIndexAllocator&) = delete;
    AssetIndexAllocator& operator=(AssetIndexAllocator&&)      = delete;

    EPIX_API AssetIndex reserve();
    EPIX_API void release(const AssetIndex& index);
    EPIX_API Receiver<AssetIndex> reserved_receiver() const;
};
}  // namespace epix::assets