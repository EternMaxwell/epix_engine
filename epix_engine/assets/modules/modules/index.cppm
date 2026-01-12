module;

export module epix.assets:index;

import std;
import epix.core;

namespace assets {
export struct StrongHandle;
export template <typename T>
struct Handle;
struct AssetIndexAllocator;

using core::Receiver;
using core::Sender;

export struct AssetIndex {
   private:
    std::uint32_t index_;
    std::uint32_t generation_;

   protected:
    AssetIndex(std::uint32_t index, std::uint32_t generation) : index_(index), generation_(generation) {}

   public:
    AssetIndex(const AssetIndex&)            = default;
    AssetIndex(AssetIndex&&)                 = default;
    AssetIndex& operator=(const AssetIndex&) = default;
    AssetIndex& operator=(AssetIndex&&)      = default;

    std::uint32_t index() const { return index_; }
    std::uint32_t generation() const { return generation_; }

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
    mutable std::atomic<std::uint32_t> m_next = 0;
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

    AssetIndex reserve() const;
    void release(const AssetIndex& index) const;
    Receiver<AssetIndex> reserved_receiver() const;
};
}  // namespace assets