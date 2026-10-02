#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <epix/common.hpp>
#include <limits>
#include <optional>
#include <vector>
#endif

// Faithful C++ port of the `offset-allocator` crate v0.2.0 (MIT), which is the
// allocator Bevy 0.18 uses inside every general mesh slab (Sebastian
// Aaltonen's OffsetAllocator: hard-real-time free-list allocator with a
// piecewise-logarithmic size-class distribution). The port keeps the exact
// algorithm and semantics of the Rust source in
// offset-allocator-0.2.0/src/{lib,small_float,ext}.rs: `Allocator::new(size)`,
// `allocate(size)`, `free(Allocation)`, `allocation_size`, `storage_report`,
// and `min_allocator_size`. Only the u32 node-index instantiation is ported,
// which is what Bevy uses.

namespace epix::render::mesh::offset_allocator {

/** @brief Bin-class constants (offset-allocator small_float.rs). */
inline constexpr std::uint32_t kMantissaBits     = 3;
inline constexpr std::uint32_t kMantissaValue    = 1u << kMantissaBits;  // 8
inline constexpr std::uint32_t kMantissaMask     = kMantissaValue - 1;   // 7
inline constexpr std::uint32_t kNumTopBins       = 32;
inline constexpr std::uint32_t kBinsPerLeaf      = 8;
inline constexpr std::uint32_t kNumLeafBins      = kNumTopBins * kBinsPerLeaf;  // 256
inline constexpr std::uint32_t kNoNodeIndex      = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::uint32_t kDefaultMaxAllocs = 128 * 1024;

/** @brief Round `size` up to the smallest bin index that fits it
 * (uint_to_float_round_up). */
EPIX_API std::uint32_t uint_to_float_round_up(std::uint32_t size);

/** @brief Round `size` down to the largest bin index that fits in it
 * (uint_to_float_round_down). */
EPIX_API std::uint32_t uint_to_float_round_down(std::uint32_t size);

/** @brief Largest object size a given bin can hold (float_to_uint). */
inline std::uint32_t float_to_uint(std::uint32_t float_value) {
    const std::uint32_t exponent = float_value >> kMantissaBits;
    const std::uint32_t mantissa = float_value & kMantissaMask;
    if (exponent == 0) {
        return mantissa;
    }
    return (mantissa | kMantissaValue) << (exponent - 1);
}

/** @brief Minimum allocator capacity needed to hold an object of the given
 * size (offset-allocator ext::min_allocator_size). */
inline std::uint32_t min_allocator_size(std::uint32_t needed_object_size) {
    return float_to_uint(uint_to_float_round_up(needed_object_size));
}

/** @brief A single allocation (offset-allocator `Allocation<u32>`). */
struct Allocation {
    /** @brief Location of this allocation within the buffer (in units). */
    std::uint32_t offset = 0;
    /** @brief The node index associated with this allocation (private handle). */
    std::uint32_t metadata = kNoNodeIndex;

    bool operator==(const Allocation&) const noexcept = default;
};

/** @brief Summary of the allocator state (offset-allocator `StorageReport`). */
struct StorageReport {
    std::uint32_t total_free_space    = 0;
    std::uint32_t largest_free_region = 0;
};

/**
 * @brief Hard-real-time offset allocator over a single contiguous space
 * (offset-allocator `Allocator<u32>`).
 *
 * Space is handed out in units (slots in Bevy's usage). The allocator uses
 * 32 top bins / 256 leaf bins with a piecewise-logarithmic size-class
 * distribution, so freed gaps of any size are reused, and adjacent free nodes
 * are merged back together.
 */
EPIX_EXPORT class EPIX_API Allocator {
   public:
    /** @brief Create an allocator over `size` units with the default
     * reasonable maximum allocation count (Bevy passes the slot capacity). */
    explicit Allocator(std::uint32_t size)
        : Allocator(size, std::min<std::uint32_t>(kDefaultMaxAllocs, std::numeric_limits<std::uint32_t>::max() - 1)) {}
    /** @brief Create an allocator over `size` units with the given maximum
     * allocation count (must be < u32::MAX - 1). */
    Allocator(std::uint32_t size, std::uint32_t max_allocs) {
        // Bevy offset-allocator `with_max_allocs`: `assert!(max_allocs <
        // NI::MAX - 1)` is an unconditional panic, so this check must not be
        // compiled out by NDEBUG.
        if (max_allocs >= std::numeric_limits<std::uint32_t>::max() - 1) {
            std::abort();
        }
        initialize(size, max_allocs);
    }

    /** @brief Allocate `size` units; returns nullopt when no contiguous space
     * or node remains. */
    std::optional<Allocation> allocate(std::uint32_t size);

    /** @brief Free an allocation, merging it with adjacent free nodes.
     * Freeing the same allocation twice is unspecified (matches the crate). */
    void free(Allocation allocation);

    /** @brief Used size (in units, may exceed the requested size due to
     * rounding) of an allocation. */
    std::uint32_t allocation_size(Allocation allocation) const {
        if (allocation.metadata >= nodes_.size()) return 0;
        return nodes_[allocation.metadata].data_size;
    }

    /** @brief Free-space summary (offset-allocator `storage_report`). */
    StorageReport storage_report() const;

    /** @brief Clear all allocations, restoring the whole space as one free
     * node. */
    void reset();

    /** @brief Whether the allocator has no live allocations. */
    bool is_empty() const noexcept { return storage_report().total_free_space == size_; }

   private:
    static constexpr std::uint32_t kTopBinsShift = 3;
    static constexpr std::uint32_t kLeafBinsMask = 7;

    struct Node {
        std::uint32_t data_offset   = 0;
        std::uint32_t data_size     = 0;
        std::uint32_t bin_list_prev = kNoNodeIndex;
        std::uint32_t bin_list_next = kNoNodeIndex;
        std::uint32_t neighbor_prev = kNoNodeIndex;
        std::uint32_t neighbor_next = kNoNodeIndex;
        bool used                   = false;
    };

    /** @brief Lowest set bit at or after `start_bit_index`, or nullopt. */
    static std::optional<std::uint32_t> find_lowest_bit_set_after(std::uint32_t bit_mask,
                                                                  std::uint32_t start_bit_index) {
        if (start_bit_index >= 32) return std::nullopt;
        const std::uint32_t mask_after = ~((1u << start_bit_index) - 1);
        const std::uint32_t bits_after = bit_mask & mask_after;
        if (bits_after == 0) return std::nullopt;
        return static_cast<std::uint32_t>(std::countr_zero(bits_after));
    }

    void initialize(std::uint32_t size, std::uint32_t max_allocs) {
        size_       = size;
        max_allocs_ = max_allocs;
        reset();
    }

    /** @brief Insert a free node (taken from the freelist) on top of the bin
     * list for `size`, returning its node index. */
    std::uint32_t insert_node_into_bin(std::uint32_t size, std::uint32_t data_offset);

    /** @brief Untie a node from its bin list and return it to the freelist. */
    void remove_node_from_bin(std::uint32_t node_index);

    std::uint32_t size_          = 0;
    std::uint32_t max_allocs_    = 0;
    std::uint32_t free_storage_  = 0;
    std::uint32_t used_bins_top_ = 0;
    std::array<std::uint8_t, kNumTopBins> used_bins_{};
    std::array<std::uint32_t, kNumLeafBins> bin_indices_{};
    std::vector<Node> nodes_;
    std::vector<std::uint32_t> free_nodes_;
    std::uint32_t free_offset_ = 0;
};

}  // namespace epix::render::mesh::offset_allocator
