#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <epix/async_channel.hpp>
#include <epix/core.hpp>
#include <functional>
#endif
#include <epix/assets/concepts.hpp>

namespace epix::assets {
EPIX_EXPORT struct StrongHandle;
/** @brief Typed handle to an asset of type T. */
EPIX_EXPORT template <typename T>
struct Handle;
struct AssetIndexAllocator;

/** @brief Generational index into an asset storage.
 *  Pairs a slot index with a generation counter so stale references
 *  can be detected after the slot is recycled. */
EPIX_EXPORT struct AssetIndex {
   private:
    std::uint32_t index_;
    std::uint32_t generation_;

   protected:
    AssetIndex(std::uint32_t index, std::uint32_t generation) noexcept : index_(index), generation_(generation) {}

   public:
    AssetIndex(const AssetIndex&) noexcept            = default;
    AssetIndex(AssetIndex&&) noexcept                 = default;
    AssetIndex& operator=(const AssetIndex&) noexcept = default;
    AssetIndex& operator=(AssetIndex&&) noexcept      = default;

    /** @brief Get the slot index. */
    std::uint32_t index() const noexcept { return index_; }
    /** @brief Get the generation counter. */
    std::uint32_t generation() const noexcept { return generation_; }

    bool operator==(const AssetIndex& other) const noexcept                  = default;
    bool operator!=(const AssetIndex& other) const noexcept                  = default;
    std::strong_ordering operator<=>(const AssetIndex& other) const noexcept = default;

    friend struct StrongHandle;
    template <typename T>
    friend struct Handle;
    friend struct AssetIndexAllocator;
    template <Asset T>
    friend struct Assets;
};

struct AssetIndexAllocator {
   private:
    mutable std::atomic<std::uint32_t> m_next = 0;
    epix::async_channel::Sender<AssetIndex> m_free_indices_sender;
    epix::async_channel::Receiver<AssetIndex> m_free_indices_receiver;
    epix::async_channel::Receiver<AssetIndex> m_reserved;
    epix::async_channel::Sender<AssetIndex> m_reserved_sender;

   public:
    AssetIndexAllocator();
    AssetIndexAllocator(const AssetIndexAllocator&)            = delete;
    AssetIndexAllocator(AssetIndexAllocator&&)                 = delete;
    AssetIndexAllocator& operator=(const AssetIndexAllocator&) = delete;
    AssetIndexAllocator& operator=(AssetIndexAllocator&&)      = delete;

    AssetIndex reserve() const;
    void release(const AssetIndex& index) const;
    epix::async_channel::Receiver<AssetIndex> reserved_receiver() const noexcept;
};
}  // namespace epix::assets

namespace std {
template <>
struct hash<epix::assets::AssetIndex> {
    std::size_t operator()(const epix::assets::AssetIndex& index) const noexcept {
        return std::hash<uint64_t>()((static_cast<uint64_t>(index.index()) << 32) |
                                     static_cast<uint64_t>(index.generation()));
    }
};
}  // namespace std