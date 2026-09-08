#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
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
inline constexpr std::uint32_t kMantissaBits         = 3;
inline constexpr std::uint32_t kMantissaValue        = 1u << kMantissaBits;  // 8
inline constexpr std::uint32_t kMantissaMask         = kMantissaValue - 1;   // 7
inline constexpr std::uint32_t kNumTopBins           = 32;
inline constexpr std::uint32_t kBinsPerLeaf          = 8;
inline constexpr std::uint32_t kNumLeafBins          = kNumTopBins * kBinsPerLeaf;  // 256
inline constexpr std::uint32_t kNoNodeIndex          = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::uint32_t kDefaultMaxAllocs     = 128 * 1024;

/** @brief Round `size` up to the smallest bin index that fits it
 * (uint_to_float_round_up). */
inline std::uint32_t uint_to_float_round_up(std::uint32_t size) {
    std::uint32_t exp       = 0;
    std::uint32_t mantissa  = 0;
    if (size < kMantissaValue) {
        // Denormal: 0..(kMantissaValue-1)
        mantissa = size;
    } else {
        const std::uint32_t highest_set_bit   = 31u - std::countl_zero(size);
        const std::uint32_t mantissa_start_bit = highest_set_bit - kMantissaBits;
        exp                                     = mantissa_start_bit + 1;
        mantissa                                = (size >> mantissa_start_bit) & kMantissaMask;
        const std::uint32_t low_bits_mask      = (1u << mantissa_start_bit) - 1;
        // Round up!
        if ((size & low_bits_mask) != 0) {
            mantissa += 1;
        }
    }
    // + allows mantissa -> exp overflow for round up.
    return (exp << kMantissaBits) + mantissa;
}

/** @brief Round `size` down to the largest bin index that fits in it
 * (uint_to_float_round_down). */
inline std::uint32_t uint_to_float_round_down(std::uint32_t size) {
    std::uint32_t exp      = 0;
    std::uint32_t mantissa = 0;
    if (size < kMantissaValue) {
        mantissa = size;
    } else {
        const std::uint32_t highest_set_bit   = 31u - std::countl_zero(size);
        const std::uint32_t mantissa_start_bit = highest_set_bit - kMantissaBits;
        exp                                     = mantissa_start_bit + 1;
        mantissa                                = (size >> mantissa_start_bit) & kMantissaMask;
    }
    return (exp << kMantissaBits) | mantissa;
}

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
class Allocator {
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
    std::optional<Allocation> allocate(std::uint32_t size) {
        if (free_offset_ == 0) {
            return std::nullopt;
        }
        // Round up to bin index so alloc >= bin: the min bin index that fits.
        const std::uint32_t min_bin_index     = uint_to_float_round_up(size);
        const std::uint32_t min_top_bin_index = min_bin_index >> kTopBinsShift;
        const std::uint32_t min_leaf_bin_index = min_bin_index & kLeafBinsMask;

        std::uint32_t top_bin_index = min_top_bin_index;
        std::optional<std::uint32_t> leaf_bin_index;
        // If the top bin exists, scan its leaf bins (can fail -> no space).
        if ((used_bins_top_ & (1u << top_bin_index)) != 0) {
            leaf_bin_index = find_lowest_bit_set_after(used_bins_[top_bin_index], min_leaf_bin_index);
        }
        // If no space in the top bin, search the higher top bins.
        if (!leaf_bin_index) {
            const auto top = find_lowest_bit_set_after(used_bins_top_, min_top_bin_index + 1);
            if (!top) return std::nullopt;
            top_bin_index = *top;
            // All leaf bins here fit (top bin was rounded up); start at bit 0.
            leaf_bin_index = static_cast<std::uint32_t>(std::countr_zero(used_bins_[top_bin_index]));
        }

        const std::uint32_t bin_index = (top_bin_index << kTopBinsShift) | *leaf_bin_index;
        // Pop the top node of the bin: bin top = node.next.
        const std::uint32_t node_index = bin_indices_[bin_index];
        auto& node                     = nodes_[node_index];
        const std::uint32_t node_total_size = node.data_size;
        node.data_size                       = size;
        node.used                            = true;
        bin_indices_[bin_index]              = node.bin_list_next;
        if (node.bin_list_next != kNoNodeIndex) {
            nodes_[node.bin_list_next].bin_list_prev = kNoNodeIndex;
        }
        free_storage_ -= node_total_size;

        // Bin empty?
        if (bin_indices_[bin_index] == kNoNodeIndex) {
            used_bins_[top_bin_index] &= static_cast<std::uint8_t>(~(1u << *leaf_bin_index));
            if (used_bins_[top_bin_index] == 0) {
                used_bins_top_ &= ~(1u << top_bin_index);
            }
        }

        // Push the remainder back into a lower bin and link it next to us.
        const std::uint32_t remainder_size = node_total_size - size;
        if (remainder_size > 0) {
            const std::uint32_t data_offset     = nodes_[node_index].data_offset;
            const std::uint32_t neighbor_next   = nodes_[node_index].neighbor_next;
            const std::uint32_t new_node_index  = insert_node_into_bin(remainder_size, data_offset + size);
            if (neighbor_next != kNoNodeIndex) {
                nodes_[neighbor_next].neighbor_prev = new_node_index;
            }
            nodes_[new_node_index].neighbor_prev = node_index;
            nodes_[new_node_index].neighbor_next = neighbor_next;
            nodes_[node_index].neighbor_next     = new_node_index;
        }
        return Allocation{nodes_[node_index].data_offset, node_index};
    }

    /** @brief Free an allocation, merging it with adjacent free nodes.
     * Freeing the same allocation twice is unspecified (matches the crate). */
    void free(Allocation allocation) {
        const std::uint32_t node_index = allocation.metadata;
        std::uint32_t offset           = nodes_[node_index].data_offset;
        std::uint32_t size             = nodes_[node_index].data_size;
        // Bevy offset-allocator `free`: `assert!(used)` is an unconditional
        // panic on double-free, so this check must not be compiled out by
        // NDEBUG.
        if (!nodes_[node_index].used) {
            std::abort();
        }

        // Merge with the previous contiguous free node.
        const std::uint32_t node_neighbor_prev = nodes_[node_index].neighbor_prev;
        if (node_neighbor_prev != kNoNodeIndex && !nodes_[node_neighbor_prev].used) {
            offset = nodes_[node_neighbor_prev].data_offset;
            size += nodes_[node_neighbor_prev].data_size;
            remove_node_from_bin(node_neighbor_prev);
            nodes_[node_index].neighbor_prev = nodes_[node_neighbor_prev].neighbor_prev;
        }
        // Merge with the next contiguous free node.
        const std::uint32_t node_neighbor_next = nodes_[node_index].neighbor_next;
        if (node_neighbor_next != kNoNodeIndex && !nodes_[node_neighbor_next].used) {
            size += nodes_[node_neighbor_next].data_size;
            remove_node_from_bin(node_neighbor_next);
            nodes_[node_index].neighbor_next = nodes_[node_neighbor_next].neighbor_next;
        }

        const std::uint32_t final_neighbor_next = nodes_[node_index].neighbor_next;
        const std::uint32_t final_neighbor_prev = nodes_[node_index].neighbor_prev;

        // Return the node to the freelist (stack).
        free_offset_ += 1;
        free_nodes_[free_offset_] = node_index;

        // Insert the combined (now free) node into a bin and reconnect its
        // neighbors.
        const std::uint32_t combined_node_index = insert_node_into_bin(size, offset);
        if (final_neighbor_next != kNoNodeIndex) {
            nodes_[combined_node_index].neighbor_next = final_neighbor_next;
            nodes_[final_neighbor_next].neighbor_prev = combined_node_index;
        }
        if (final_neighbor_prev != kNoNodeIndex) {
            nodes_[combined_node_index].neighbor_prev = final_neighbor_prev;
            nodes_[final_neighbor_prev].neighbor_next = combined_node_index;
        }
    }

    /** @brief Used size (in units, may exceed the requested size due to
     * rounding) of an allocation. */
    std::uint32_t allocation_size(Allocation allocation) const {
        if (allocation.metadata >= nodes_.size()) return 0;
        return nodes_[allocation.metadata].data_size;
    }

    /** @brief Free-space summary (offset-allocator `storage_report`). */
    StorageReport storage_report() const {
        std::uint32_t largest_free_region = 0;
        std::uint32_t free_storage        = 0;
        // Out of allocations -> zero free space.
        if (free_offset_ > 0) {
            free_storage = free_storage_;
            if (used_bins_top_ > 0) {
                const std::uint32_t top_bin_index = 31u - std::countl_zero(used_bins_top_);
                const std::uint32_t leaf_bin_index =
                    31u - std::countl_zero(static_cast<std::uint32_t>(used_bins_[top_bin_index]));
                largest_free_region = float_to_uint((top_bin_index << kTopBinsShift) | leaf_bin_index);
                assert(free_storage >= largest_free_region);
            }
        }
        return StorageReport{free_storage, largest_free_region};
    }

    /** @brief Clear all allocations, restoring the whole space as one free
     * node. */
    void reset() {
        free_storage_   = 0;
        used_bins_top_  = 0;
        free_offset_    = max_allocs_ - 1;
        used_bins_.fill(0);
        bin_indices_.fill(kNoNodeIndex);
        nodes_.assign(max_allocs_, Node{});
        free_nodes_.resize(max_allocs_);
        // Freelist is a stack; nodes in inverse order so [0] pops first.
        for (std::uint32_t i = 0; i < max_allocs_; ++i) {
            free_nodes_[i] = max_allocs_ - i - 1;
        }
        // Start state: the whole storage is one big node.
        insert_node_into_bin(size_, 0);
    }

    /** @brief Whether the allocator has no live allocations. */
    bool is_empty() const noexcept { return storage_report().total_free_space == size_; }

   private:
    static constexpr std::uint32_t kTopBinsShift = 3;
    static constexpr std::uint32_t kLeafBinsMask = 7;

    struct Node {
        std::uint32_t data_offset  = 0;
        std::uint32_t data_size    = 0;
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
        size_      = size;
        max_allocs_ = max_allocs;
        reset();
    }

    /** @brief Insert a free node (taken from the freelist) on top of the bin
     * list for `size`, returning its node index. */
    std::uint32_t insert_node_into_bin(std::uint32_t size, std::uint32_t data_offset) {
        const std::uint32_t bin_index      = uint_to_float_round_down(size);
        const std::uint32_t top_bin_index  = bin_index >> kTopBinsShift;
        const std::uint32_t leaf_bin_index = bin_index & kLeafBinsMask;

        // Bin was empty before?
        if (bin_indices_[bin_index] == kNoNodeIndex) {
            used_bins_[top_bin_index] |= static_cast<std::uint8_t>(1u << leaf_bin_index);
            used_bins_top_ |= 1u << top_bin_index;
        }

        // Take a freelist node and insert on top of the bin linked list.
        const std::uint32_t top_node_index = bin_indices_[bin_index];
        const std::uint32_t free_offset    = free_offset_;
        const std::uint32_t node_index     = free_nodes_[free_offset];
        free_offset_ -= 1;
        nodes_[node_index] = Node{.data_offset   = data_offset,
                                  .data_size     = size,
                                  .bin_list_prev = kNoNodeIndex,
                                  .bin_list_next = top_node_index,
                                  .neighbor_prev = kNoNodeIndex,
                                  .neighbor_next = kNoNodeIndex,
                                  .used          = false};
        if (top_node_index != kNoNodeIndex) {
            nodes_[top_node_index].bin_list_prev = node_index;
        }
        bin_indices_[bin_index] = node_index;
        free_storage_ += size;
        return node_index;
    }

    /** @brief Untie a node from its bin list and return it to the freelist. */
    void remove_node_from_bin(std::uint32_t node_index) {
        const Node node = nodes_[node_index];
        if (node.bin_list_prev != kNoNodeIndex) {
            // Middle node: unlink from the list.
            nodes_[node.bin_list_prev].bin_list_next = node.bin_list_next;
            if (node.bin_list_next != kNoNodeIndex) {
                nodes_[node.bin_list_next].bin_list_prev = node.bin_list_prev;
            }
        } else {
            // First node in a bin: find the bin and fix the head.
            const std::uint32_t bin_index      = uint_to_float_round_down(node.data_size);
            const std::uint32_t top_bin_index  = bin_index >> kTopBinsShift;
            const std::uint32_t leaf_bin_index = bin_index & kLeafBinsMask;
            bin_indices_[bin_index]            = node.bin_list_next;
            if (node.bin_list_next != kNoNodeIndex) {
                nodes_[node.bin_list_next].bin_list_prev = kNoNodeIndex;
            }
            if (bin_indices_[bin_index] == kNoNodeIndex) {
                used_bins_[top_bin_index] &= static_cast<std::uint8_t>(~(1u << leaf_bin_index));
                if (used_bins_[top_bin_index] == 0) {
                    used_bins_top_ &= ~(1u << top_bin_index);
                }
            }
        }
        // Return the node to the freelist.
        free_offset_ += 1;
        free_nodes_[free_offset_] = node_index;
        free_storage_ -= node.data_size;
    }

    std::uint32_t size_         = 0;
    std::uint32_t max_allocs_   = 0;
    std::uint32_t free_storage_ = 0;
    std::uint32_t used_bins_top_ = 0;
    std::array<std::uint8_t, kNumTopBins> used_bins_{};
    std::array<std::uint32_t, kNumLeafBins> bin_indices_{};
    std::vector<Node> nodes_;
    std::vector<std::uint32_t> free_nodes_;
    std::uint32_t free_offset_ = 0;
};

}  // namespace epix::render::mesh::offset_allocator
