#pragma once


#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include <epix/common.hpp>
#endif

namespace epix::image::detail {

/** @brief A 2D size in integer pixels (`euclid::Size2D<i32, UnknownUnit>`). */
struct AtlasSize {
    std::int32_t width  = 0;
    std::int32_t height = 0;

    constexpr bool is_empty() const noexcept { return width <= 0 || height <= 0; }
    constexpr std::int32_t area() const noexcept { return width > 0 && height > 0 ? width * height : 0; }
    bool operator==(const AtlasSize&) const = default;
};

/** @brief A 2D point in integer pixels (`euclid::Point2D<i32, UnknownUnit>`). */
struct AtlasPoint {
    std::int32_t x = 0;
    std::int32_t y = 0;

    bool operator==(const AtlasPoint&) const = default;
};

/** @brief An integer rectangle (`euclid::Rect<i32, UnknownUnit>`). */
struct AtlasRectangle {
    AtlasPoint min{};
    AtlasPoint max{};

    static constexpr AtlasRectangle zero() noexcept { return {}; }
    constexpr AtlasSize size() const noexcept { return AtlasSize{max.x - min.x, max.y - min.y}; }
    constexpr std::int32_t area() const noexcept { return size().area(); }
    constexpr std::int32_t width() const noexcept { return max.x - min.x; }
    constexpr std::int32_t height() const noexcept { return max.y - min.y; }

    bool operator==(const AtlasRectangle&) const = default;
};

/** @brief Options that tweak the behaviour of the atlas allocator
 * (`guillotiere::AllocatorOptions`). */
struct AtlasAllocatorOptions {
    /** @brief Rounds rectangle sizes up to a multiple of this value. Default `(1, 1)`. */
    AtlasSize alignment{1, 1};
    /** @brief Value below which a size is considered small. Default `32`. */
    std::int32_t small_size_threshold = 32;
    /** @brief Value above which a size is considered large. Default `256`. */
    std::int32_t large_size_threshold = 256;
};

/** @brief A rectangle handed out by the allocator (`guillotiere::Allocation`). */
struct AtlasAllocation {
    std::uint32_t id = 0;
    AtlasRectangle rectangle;
};

/**
 * @brief A port of `guillotiere` 0.6.0's `AtlasAllocator`, which is what Bevy's
 * `DynamicTextureAtlasBuilder` allocates through.
 *
 * Only the construction and `allocate` paths are ported, because that is all the builder uses;
 * `deallocate`, `grow`, `rearrange` and the wireframe/serde surfaces are out of scope for this
 * module. The node tree, the three free-list buckets, the worst-fit/best-fit choice and the
 * guillotine split all keep the crate's exact behaviour, because the *order* in which rectangles
 * come back out is what makes an atlas layout identical to Bevy's.
 */
class AtlasAllocator {
   public:
    /** @brief Create an allocator for an atlas of `size`, with the crate's default options. */
    explicit AtlasAllocator(AtlasSize size);
    /** @brief Create an allocator with explicit options. */
    AtlasAllocator(AtlasSize size, const AtlasAllocatorOptions& options);

    /** @brief The total size of the atlas. */
    const AtlasSize& size() const noexcept { return size_; }

    /** @brief Allocate a rectangle, or nothing when no free rectangle fits. */
    std::optional<AtlasAllocation> allocate(AtlasSize requested_size);

   private:
    /** @brief A node index, with `kNone` standing in for Rust's `AllocIndex::NONE`. */
    static constexpr std::uint32_t kNone = 0xFFFFFFFFu;

    enum class Orientation {
        Vertical,
        Horizontal,
    };
    enum class NodeKind {
        Container,
        Alloc,
        Free,
        Unused,
    };
    struct Node {
        std::uint32_t parent       = kNone;
        std::uint32_t next_sibling = kNone;
        std::uint32_t prev_sibling = kNone;
        NodeKind kind              = NodeKind::Unused;
        Orientation orientation    = Orientation::Horizontal;
        AtlasRectangle rect;
    };

    static constexpr std::size_t kBucketSmall  = 0;
    static constexpr std::size_t kBucketMedium = 1;
    static constexpr std::size_t kBucketLarge  = 2;
    static constexpr std::size_t kBucketCount  = 3;

    static std::size_t free_list_for_size(std::int32_t small_threshold,
                                          std::int32_t large_threshold,
                                          const AtlasSize& size) noexcept;
    static Orientation flipped(Orientation orientation) noexcept {
        return orientation == Orientation::Vertical ? Orientation::Horizontal : Orientation::Vertical;
    }
    static void adjust_size(std::int32_t alignment, std::int32_t& size) noexcept;
    static void guillotine_rect(const AtlasRectangle& chosen_rect,
                                AtlasSize requested_size,
                                Orientation default_orientation,
                                AtlasRectangle& split_rect,
                                AtlasRectangle& leftover_rect,
                                Orientation& orientation) noexcept;

    std::uint32_t find_suitable_rect(const AtlasSize& requested_size);
    std::uint32_t new_node();
    void add_free_rect(std::uint32_t id, const AtlasSize& size);
    std::uint32_t alloc_id(std::uint32_t index) const noexcept;

    std::vector<Node> nodes_;
    std::array<std::vector<std::uint32_t>, kBucketCount> free_lists_;
    std::vector<std::uint32_t> generations_;
    std::uint32_t unused_nodes_ = kNone;
    AtlasSize alignment_{1, 1};
    std::int32_t small_size_threshold_ = 32;
    std::int32_t large_size_threshold_ = 256;
    AtlasSize size_{};
};

}  // namespace epix::image::detail
