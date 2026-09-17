#include <algorithm>
#include <cassert>
#include <epix/image/detail/atlas_allocator.hpp>
#include <limits>

namespace epix::image::detail {

namespace {
/** @brief `guillotiere`'s `AllocIndex::NONE`. */
constexpr std::uint32_t kNoIndex = 0xFFFFFFFFu;

/** @brief `euclid`'s `Point2D + Vector2D`. */
AtlasPoint point_plus(AtlasPoint point, std::int32_t dx, std::int32_t dy) noexcept {
    return AtlasPoint{point.x + dx, point.y + dy};
}
}  // namespace

std::size_t AtlasAllocator::free_list_for_size(std::int32_t small_threshold,
                                               std::int32_t large_threshold,
                                               const AtlasSize& size) noexcept {
    if (size.width >= large_threshold || size.height >= large_threshold) return kBucketLarge;
    if (size.width >= small_threshold || size.height >= small_threshold) return kBucketMedium;
    return kBucketSmall;
}

AtlasAllocator::AtlasAllocator(AtlasSize size) : AtlasAllocator(size, AtlasAllocatorOptions{}) {}

AtlasAllocator::AtlasAllocator(AtlasSize size, const AtlasAllocatorOptions& options) {
    assert(options.alignment.width > 0);
    assert(options.alignment.height > 0);
    assert(size.width > 0);
    assert(size.height > 0);
    assert(options.large_size_threshold >= options.small_size_threshold);

    alignment_            = options.alignment;
    small_size_threshold_ = options.small_size_threshold;
    large_size_threshold_ = options.large_size_threshold;
    size_                 = size;

    const std::size_t bucket = free_list_for_size(options.small_size_threshold, options.large_size_threshold, size);
    free_lists_[bucket].push_back(0);

    nodes_.push_back(Node{
        .parent       = kNoIndex,
        .next_sibling = kNoIndex,
        .prev_sibling = kNoIndex,
        .kind         = NodeKind::Free,
        .orientation  = Orientation::Vertical,
        .rect         = AtlasRectangle{AtlasPoint{0, 0}, AtlasPoint{size.width, size.height}},
    });
    generations_.push_back(0);
    unused_nodes_ = kNoIndex;
}

void AtlasAllocator::adjust_size(std::int32_t alignment, std::int32_t& size) noexcept {
    const std::int32_t remainder = size % alignment;
    if (remainder > 0) size += alignment - remainder;
}

void AtlasAllocator::guillotine_rect(const AtlasRectangle& chosen_rect,
                                     AtlasSize requested_size,
                                     Orientation default_orientation,
                                     AtlasRectangle& split_rect,
                                     AtlasRectangle& leftover_rect,
                                     Orientation& orientation) noexcept {
    const AtlasRectangle candidate_leftover_rect_to_right{
        point_plus(chosen_rect.min, requested_size.width, 0),
        AtlasPoint{chosen_rect.max.x, chosen_rect.min.y + requested_size.height},
    };
    const AtlasRectangle candidate_leftover_rect_to_bottom{
        point_plus(chosen_rect.min, 0, requested_size.height),
        AtlasPoint{chosen_rect.min.x + requested_size.width, chosen_rect.max.y},
    };

    if (requested_size == chosen_rect.size()) {
        // Perfect fit.
        orientation   = default_orientation;
        split_rect    = AtlasRectangle::zero();
        leftover_rect = AtlasRectangle::zero();
    } else if (candidate_leftover_rect_to_right.area() > candidate_leftover_rect_to_bottom.area()) {
        leftover_rect = candidate_leftover_rect_to_bottom;
        split_rect    = AtlasRectangle{
            candidate_leftover_rect_to_right.min,
            AtlasPoint{candidate_leftover_rect_to_right.max.x, chosen_rect.max.y},
        };
        orientation = Orientation::Horizontal;
    } else {
        leftover_rect = candidate_leftover_rect_to_right;
        split_rect    = AtlasRectangle{
            candidate_leftover_rect_to_bottom.min,
            AtlasPoint{chosen_rect.max.x, candidate_leftover_rect_to_bottom.max.y},
        };
        orientation = Orientation::Vertical;
    }
}

std::uint32_t AtlasAllocator::new_node() {
    const std::uint32_t index = unused_nodes_;
    if (index < nodes_.size()) {
        unused_nodes_ = nodes_[index].next_sibling;
        generations_[index] += 1;
        return index;
    }

    nodes_.push_back(Node{
        .parent       = kNoIndex,
        .next_sibling = kNoIndex,
        .prev_sibling = kNoIndex,
        .kind         = NodeKind::Unused,
        .orientation  = Orientation::Horizontal,
        .rect         = AtlasRectangle::zero(),
    });
    generations_.push_back(0);
    return static_cast<std::uint32_t>(nodes_.size() - 1);
}

void AtlasAllocator::add_free_rect(std::uint32_t id, const AtlasSize& size) {
    assert(nodes_[id].kind == NodeKind::Free);
    const std::size_t bucket = free_list_for_size(small_size_threshold_, large_size_threshold_, size);
    free_lists_[bucket].push_back(id);
}

std::uint32_t AtlasAllocator::alloc_id(std::uint32_t index) const noexcept {
    const std::uint32_t generation = generations_[index];
    return index + (generation << 24);
}

std::uint32_t AtlasAllocator::find_suitable_rect(const AtlasSize& requested_size) {
    const std::size_t ideal_bucket = free_list_for_size(small_size_threshold_, large_size_threshold_, requested_size);

    const bool use_worst_fit = ideal_bucket != kBucketSmall;
    for (std::size_t bucket = ideal_bucket; bucket < kBucketCount; ++bucket) {
        std::int32_t candidate_score = use_worst_fit ? 0 : std::numeric_limits<std::int32_t>::max();
        bool has_candidate           = false;
        std::uint32_t candidate      = kNoIndex;
        std::size_t candidate_slot   = 0;

        std::size_t slot = 0;
        while (slot < free_lists_[bucket].size()) {
            const std::uint32_t id = free_lists_[bucket][slot];

            // Merged nodes are not removed from the free list (there is no doubly linked list for it),
            // so stale entries are dropped here.
            if (nodes_[id].kind != NodeKind::Free) {
                free_lists_[bucket][slot] = free_lists_[bucket].back();
                free_lists_[bucket].pop_back();
                continue;
            }

            const AtlasSize size  = nodes_[id].rect.size();
            const std::int32_t dx = size.width - requested_size.width;
            const std::int32_t dy = size.height - requested_size.height;

            if (dx >= 0 && dy >= 0) {
                if (dx == 0 || dy == 0) {
                    // Perfect fit.
                    candidate      = id;
                    candidate_slot = slot;
                    has_candidate  = true;
                    break;
                }

                // Favour the largest minimum dimension, except for small allocations.
                const std::int32_t score = std::min(dx, dy);
                if ((use_worst_fit && score > candidate_score) || (!use_worst_fit && score < candidate_score)) {
                    candidate_score = score;
                    candidate       = id;
                    candidate_slot  = slot;
                    has_candidate   = true;
                }
            }

            ++slot;
        }

        if (has_candidate) {
            free_lists_[bucket][candidate_slot] = free_lists_[bucket].back();
            free_lists_[bucket].pop_back();
            return candidate;
        }
    }

    return kNoIndex;
}

std::optional<AtlasAllocation> AtlasAllocator::allocate(AtlasSize requested_size) {
    if (requested_size.is_empty()) return std::nullopt;

    adjust_size(alignment_.width, requested_size.width);
    adjust_size(alignment_.height, requested_size.height);

    const std::uint32_t chosen_id = find_suitable_rect(requested_size);
    if (chosen_id == kNoIndex) return std::nullopt;

    const Node chosen_node           = nodes_[chosen_id];
    const AtlasRectangle chosen_rect = chosen_node.rect;
    const AtlasRectangle allocated_rect{
        chosen_rect.min,
        point_plus(chosen_rect.min, requested_size.width, requested_size.height),
    };
    const Orientation current_orientation = chosen_node.orientation;
    assert(chosen_node.kind == NodeKind::Free);

    AtlasRectangle split_rect;
    AtlasRectangle leftover_rect;
    Orientation orientation = Orientation::Vertical;
    guillotine_rect(chosen_rect, requested_size, current_orientation, split_rect, leftover_rect, orientation);

    std::uint32_t allocated_id = kNoIndex;
    std::uint32_t split_id     = kNoIndex;
    std::uint32_t leftover_id  = kNoIndex;
    if (orientation == current_orientation) {
        if (split_rect.area() > 0) {
            const std::uint32_t next_sibling = chosen_node.next_sibling;

            split_id         = new_node();
            nodes_[split_id] = Node{
                .parent       = chosen_node.parent,
                .next_sibling = next_sibling,
                .prev_sibling = chosen_id,
                .kind         = NodeKind::Free,
                .orientation  = current_orientation,
                .rect         = split_rect,
            };
            nodes_[chosen_id].next_sibling = split_id;
            if (next_sibling != kNoIndex) nodes_[next_sibling].prev_sibling = split_id;
        }

        if (leftover_rect.area() > 0) {
            nodes_[chosen_id].kind = NodeKind::Container;
            allocated_id           = new_node();
            leftover_id            = new_node();

            nodes_[allocated_id] = Node{
                .parent       = chosen_id,
                .next_sibling = leftover_id,
                .prev_sibling = kNoIndex,
                .kind         = NodeKind::Alloc,
                .orientation  = flipped(current_orientation),
                .rect         = allocated_rect,
            };
            nodes_[leftover_id] = Node{
                .parent       = chosen_id,
                .next_sibling = kNoIndex,
                .prev_sibling = allocated_id,
                .kind         = NodeKind::Free,
                .orientation  = flipped(current_orientation),
                .rect         = leftover_rect,
            };
        } else {
            // No leftover area: allocate directly in the chosen node.
            allocated_id           = chosen_id;
            nodes_[chosen_id].kind = NodeKind::Alloc;
            nodes_[chosen_id].rect = allocated_rect;
            leftover_id            = kNoIndex;
        }
    } else {
        nodes_[chosen_id].kind = NodeKind::Container;

        if (split_rect.area() > 0) {
            split_id         = new_node();
            nodes_[split_id] = Node{
                .parent       = chosen_id,
                .next_sibling = kNoIndex,
                .prev_sibling = kNoIndex,
                .kind         = NodeKind::Free,
                .orientation  = flipped(current_orientation),
                .rect         = split_rect,
            };
        }

        if (leftover_rect.area() > 0) {
            const std::uint32_t container_id = new_node();
            nodes_[container_id]             = Node{
                .parent       = chosen_id,
                .next_sibling = split_id,
                .prev_sibling = kNoIndex,
                .kind         = NodeKind::Container,
                .orientation  = flipped(current_orientation),
                .rect         = AtlasRectangle::zero(),
            };
            if (split_id != kNoIndex) nodes_[split_id].prev_sibling = container_id;

            allocated_id = new_node();
            leftover_id  = new_node();

            nodes_[allocated_id] = Node{
                .parent       = container_id,
                .next_sibling = leftover_id,
                .prev_sibling = kNoIndex,
                .kind         = NodeKind::Alloc,
                .orientation  = current_orientation,
                .rect         = allocated_rect,
            };
            nodes_[leftover_id] = Node{
                .parent       = container_id,
                .next_sibling = kNoIndex,
                .prev_sibling = allocated_id,
                .kind         = NodeKind::Free,
                .orientation  = current_orientation,
                .rect         = leftover_rect,
            };
        } else {
            allocated_id         = new_node();
            nodes_[allocated_id] = Node{
                .parent       = chosen_id,
                .next_sibling = split_id,
                .prev_sibling = kNoIndex,
                .kind         = NodeKind::Alloc,
                .orientation  = flipped(current_orientation),
                .rect         = allocated_rect,
            };
            if (split_id != kNoIndex) nodes_[split_id].prev_sibling = allocated_id;
            leftover_id = kNoIndex;
        }
    }

    if (split_id != kNoIndex) add_free_rect(split_id, split_rect.size());
    if (leftover_id != kNoIndex) add_free_rect(leftover_id, leftover_rect.size());

    return AtlasAllocation{
        .id        = alloc_id(allocated_id),
        .rectangle = allocated_rect,
    };
}

}  // namespace epix::image::detail
