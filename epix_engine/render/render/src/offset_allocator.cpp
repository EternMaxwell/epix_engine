
#include <cstdlib>
#include <epix/render/mesh/offset_allocator.hpp>

namespace epix::render::mesh::offset_allocator {

std::uint32_t uint_to_float_round_up(std::uint32_t size) {
    std::uint32_t exp      = 0;
    std::uint32_t mantissa = 0;
    if (size < kMantissaValue) {
        // Denormal: 0..(kMantissaValue-1)
        mantissa = size;
    } else {
        const std::uint32_t highest_set_bit    = 31u - std::countl_zero(size);
        const std::uint32_t mantissa_start_bit = highest_set_bit - kMantissaBits;
        exp                                    = mantissa_start_bit + 1;
        mantissa                               = (size >> mantissa_start_bit) & kMantissaMask;
        const std::uint32_t low_bits_mask      = (1u << mantissa_start_bit) - 1;
        // Round up!
        if ((size & low_bits_mask) != 0) {
            mantissa += 1;
        }
    }
    // + allows mantissa -> exp overflow for round up.
    return (exp << kMantissaBits) + mantissa;
}

std::uint32_t uint_to_float_round_down(std::uint32_t size) {
    std::uint32_t exp      = 0;
    std::uint32_t mantissa = 0;
    if (size < kMantissaValue) {
        mantissa = size;
    } else {
        const std::uint32_t highest_set_bit    = 31u - std::countl_zero(size);
        const std::uint32_t mantissa_start_bit = highest_set_bit - kMantissaBits;
        exp                                    = mantissa_start_bit + 1;
        mantissa                               = (size >> mantissa_start_bit) & kMantissaMask;
    }
    return (exp << kMantissaBits) | mantissa;
}

std::optional<Allocation> Allocator::allocate(std::uint32_t size) {
    if (free_offset_ == 0) {
        return std::nullopt;
    }
    // Round up to bin index so alloc >= bin: the min bin index that fits.
    const std::uint32_t min_bin_index      = uint_to_float_round_up(size);
    const std::uint32_t min_top_bin_index  = min_bin_index >> kTopBinsShift;
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
    const std::uint32_t node_index      = bin_indices_[bin_index];
    auto& node                          = nodes_[node_index];
    const std::uint32_t node_total_size = node.data_size;
    node.data_size                      = size;
    node.used                           = true;
    bin_indices_[bin_index]             = node.bin_list_next;
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
        const std::uint32_t data_offset    = nodes_[node_index].data_offset;
        const std::uint32_t neighbor_next  = nodes_[node_index].neighbor_next;
        const std::uint32_t new_node_index = insert_node_into_bin(remainder_size, data_offset + size);
        if (neighbor_next != kNoNodeIndex) {
            nodes_[neighbor_next].neighbor_prev = new_node_index;
        }
        nodes_[new_node_index].neighbor_prev = node_index;
        nodes_[new_node_index].neighbor_next = neighbor_next;
        nodes_[node_index].neighbor_next     = new_node_index;
    }
    return Allocation{nodes_[node_index].data_offset, node_index};
}

void Allocator::free(Allocation allocation) {
    const std::uint32_t node_index = allocation.metadata;
    std::uint32_t offset           = nodes_[node_index].data_offset;
    std::uint32_t size             = nodes_[node_index].data_size;
    // Bevy offset-allocator `free`: `assert!(used)` is an unconditional panic
    // on double-free, so this check must not be compiled out by NDEBUG.
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

StorageReport Allocator::storage_report() const {
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

void Allocator::reset() {
    free_storage_  = 0;
    used_bins_top_ = 0;
    free_offset_   = max_allocs_ - 1;
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

std::uint32_t Allocator::insert_node_into_bin(std::uint32_t size, std::uint32_t data_offset) {
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

void Allocator::remove_node_from_bin(std::uint32_t node_index) {
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

}  // namespace epix::render::mesh::offset_allocator
