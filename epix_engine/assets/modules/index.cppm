module;

export module epix.assets:index;

import std;
import epix.core;
import :concepts;

namespace epix::assets {
export struct StrongHandle;
/** @brief Typed handle to an asset of type T. */
export template <typename T>
struct Handle;
struct AssetIndexAllocator;

using core::Receiver;
using core::Sender;

/** @brief Generational index into an asset storage.
 *  Pairs a slot index with a generation counter so stale references
 *  can be detected after the slot is recycled. */
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

    /** @brief Get the slot index. */
    std::uint32_t index() const { return index_; }
    /** @brief Get the generation counter. */
    std::uint32_t generation() const { return generation_; }

    bool operator==(const AssetIndex& other) const                  = default;
    bool operator!=(const AssetIndex& other) const                  = default;
    std::strong_ordering operator<=>(const AssetIndex& other) const = default;

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
}  // namespace epix::assets

namespace std {
template <>
struct hash<epix::assets::AssetIndex> {
    std::size_t operator()(const epix::assets::AssetIndex& index) const {
        return std::hash<uint64_t>()((static_cast<uint64_t>(index.index()) << 32) |
                                     static_cast<uint64_t>(index.generation()));
    }
};
}  // namespace std