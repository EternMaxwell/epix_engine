#pragma once

#include <epix/utils/async.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <epix/core.hpp>

namespace epix::assets {
struct StrongHandle;
template <typename T>
struct Handle;
struct AssetIndexAllocator;

using epix::utils::async::Receiver;
using epix::utils::async::Sender;

struct AssetIndex {
   private:
    uint32_t index_;
    uint32_t generation_;

   protected:
    AssetIndex(uint32_t index, uint32_t generation) : index_(index), generation_(generation) {}

   public:
    AssetIndex(const AssetIndex&)            = default;
    AssetIndex(AssetIndex&&)                 = default;
    AssetIndex& operator=(const AssetIndex&) = default;
    AssetIndex& operator=(AssetIndex&&)      = default;

    uint32_t index() const { return index_; }
    uint32_t generation() const { return generation_; }

    bool operator==(const AssetIndex& other) const                  = default;
    bool operator!=(const AssetIndex& other) const                  = default;
    std::strong_ordering operator<=>(const AssetIndex& other) const = default;

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
    AssetIndexAllocator();
    AssetIndexAllocator(const AssetIndexAllocator&)            = delete;
    AssetIndexAllocator(AssetIndexAllocator&&)                 = delete;
    AssetIndexAllocator& operator=(const AssetIndexAllocator&) = delete;
    AssetIndexAllocator& operator=(AssetIndexAllocator&&)      = delete;

    AssetIndex reserve();
    void release(const AssetIndex& index);
    Receiver<AssetIndex> reserved_receiver() const;
};
}  // namespace epix::assets