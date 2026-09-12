#include <algorithm>
#include <cassert>
#include <epix/image/detail/rectangle_pack.hpp>
#include <format>
#include <type_traits>

namespace epix::image::detail {

WidthHeightDepth WidthHeightDepth::checked(std::uint32_t width, std::uint32_t height, std::uint32_t depth) {
    assert(width != 0);
    assert(height != 0);
    assert(depth != 0);

    return WidthHeightDepth{width, height, depth};
}

std::uint64_t volume_heuristic(const WidthHeightDepth& whd) {
    return static_cast<std::uint64_t>(whd.width) * static_cast<std::uint64_t>(whd.height) *
           static_cast<std::uint64_t>(whd.depth);
}

std::strong_ordering contains_smallest_box(std::array<WidthHeightDepth, 3> container1,
                                           std::array<WidthHeightDepth, 3> container2,
                                           const BoxSizeHeuristicFn& heuristic) {
    const auto by_heuristic = [&heuristic](const WidthHeightDepth& a, const WidthHeightDepth& b) {
        return heuristic(a) < heuristic(b);
    };

    std::stable_sort(container1.begin(), container1.end(), by_heuristic);
    std::stable_sort(container2.begin(), container2.end(), by_heuristic);

    const std::strong_ordering smallest = heuristic(container2[0]) <=> heuristic(container1[0]);
    if (smallest != 0) return smallest;
    return heuristic(container2[1]) <=> heuristic(container1[1]);
}

std::string to_string(BinSectionError error) {
    switch (error) {
        case BinSectionError::PlacementWiderThanBinSection:
            return "Can not place a rectangle inside of a bin that is wider than that rectangle.";
        case BinSectionError::PlacementTallerThanBinSection:
            return "Can not place a rectangle inside of a bin that is taller than that rectangle.";
        case BinSectionError::PlacementDeeperThanBinSection:
            return "Can not place a rectangle inside of a bin that is deeper than that rectangle.";
    }

    return {};
}

std::expected<std::pair<PackedLocation, std::array<BinSection, 3>>, BinSectionError> BinSection::try_place(
    const RectToInsert& incoming,
    const ComparePotentialContainersFn& container_comparison_fn,
    const BoxSizeHeuristicFn& heuristic_fn) const {
    auto can_fit = incoming_can_fit(incoming);
    if (!can_fit) return std::unexpected(can_fit.error());

    std::array<std::array<BinSection, 3>, 6> all_combinations{
        depth_largest_height_second_largest_width_smallest(incoming),
        depth_largest_width_second_largest_height_smallest(incoming),
        height_largest_depth_second_largest_width_smallest(incoming),
        height_largest_width_second_largest_depth_smallest(incoming),
        width_largest_depth_second_largest_height_smallest(incoming),
        width_largest_height_second_largest_depth_smallest(incoming),
    };

    std::stable_sort(all_combinations.begin(), all_combinations.end(),
                     [&container_comparison_fn, &heuristic_fn](const std::array<BinSection, 3>& a,
                                                               const std::array<BinSection, 3>& b) {
                         return container_comparison_fn(std::array<WidthHeightDepth, 3>{a[0].whd, a[1].whd, a[2].whd},
                                                        std::array<WidthHeightDepth, 3>{b[0].whd, b[1].whd, b[2].whd},
                                                        heuristic_fn) < 0;
                     });

    const PackedLocation packed_location{
        .x_              = x,
        .y_              = y,
        .z_              = z,
        .whd             = WidthHeightDepth{incoming.width(), incoming.height(), incoming.depth()},
        .x_axis_rotation = RotatedBy::ZeroDegrees,
        .y_axis_rotation = RotatedBy::ZeroDegrees,
        .z_axis_rotation = RotatedBy::ZeroDegrees,
    };

    return std::pair<PackedLocation, std::array<BinSection, 3>>{packed_location, all_combinations[5]};
}

std::expected<void, BinSectionError> BinSection::incoming_can_fit(const RectToInsert& incoming) const {
    if (incoming.width() > whd.width) {
        return std::unexpected(BinSectionError::PlacementWiderThanBinSection);
    }
    if (incoming.height() > whd.height) {
        return std::unexpected(BinSectionError::PlacementTallerThanBinSection);
    }
    if (incoming.depth() > whd.depth) {
        return std::unexpected(BinSectionError::PlacementDeeperThanBinSection);
    }

    return {};
}

std::array<BinSection, 3> BinSection::width_largest_height_second_largest_depth_smallest(
    const RectToInsert& incoming) const {
    return {
        empty_space_directly_right(incoming),
        all_empty_space_above_excluding_behind(incoming),
        all_empty_space_behind(incoming),
    };
}

std::array<BinSection, 3> BinSection::width_largest_depth_second_largest_height_smallest(
    const RectToInsert& incoming) const {
    return {
        empty_space_directly_right(incoming),
        all_empty_space_above(incoming),
        all_empty_space_behind_excluding_above(incoming),
    };
}

std::array<BinSection, 3> BinSection::height_largest_width_second_largest_depth_smallest(
    const RectToInsert& incoming) const {
    return {
        all_empty_space_right_excluding_behind(incoming),
        empty_space_directly_above(incoming),
        all_empty_space_behind(incoming),
    };
}

std::array<BinSection, 3> BinSection::height_largest_depth_second_largest_width_smallest(
    const RectToInsert& incoming) const {
    return {
        all_empty_space_right(incoming),
        empty_space_directly_above(incoming),
        all_empty_space_behind_excluding_right(incoming),
    };
}

std::array<BinSection, 3> BinSection::depth_largest_width_second_largest_height_smallest(
    const RectToInsert& incoming) const {
    return {
        all_empty_space_right_excluding_above(incoming),
        all_empty_space_above(incoming),
        empty_space_directly_behind(incoming),
    };
}

std::array<BinSection, 3> BinSection::depth_largest_height_second_largest_width_smallest(
    const RectToInsert& incoming) const {
    return {
        all_empty_space_right(incoming),
        all_empty_space_above_excluding_right(incoming),
        empty_space_directly_behind(incoming),
    };
}

BinSection BinSection::all_empty_space_above(const RectToInsert& incoming) const {
    return new_spread(x, y + incoming.height(), z, whd.width, whd.height - incoming.height(), whd.depth);
}

BinSection BinSection::all_empty_space_right(const RectToInsert& incoming) const {
    return new_spread(x + incoming.width(), y, z, whd.width - incoming.width(), whd.height, whd.depth);
}

BinSection BinSection::all_empty_space_behind(const RectToInsert& incoming) const {
    return new_spread(x, y, z + incoming.depth(), whd.width, whd.height, whd.depth - incoming.depth());
}

BinSection BinSection::empty_space_directly_above(const RectToInsert& incoming) const {
    return new_spread(x, y + incoming.height(), z, incoming.width(), whd.height - incoming.height(), incoming.depth());
}

BinSection BinSection::empty_space_directly_right(const RectToInsert& incoming) const {
    return new_spread(x + incoming.width(), y, z, whd.width - incoming.width(), incoming.height(), incoming.depth());
}

BinSection BinSection::empty_space_directly_behind(const RectToInsert& incoming) const {
    return BinSection{
        x,
        y,
        z + incoming.depth(),
        WidthHeightDepth{incoming.width(), incoming.height(), whd.depth - incoming.depth()},
    };
}

BinSection BinSection::all_empty_space_above_excluding_right(const RectToInsert& incoming) const {
    return BinSection{
        x,
        y + incoming.height(),
        z,
        WidthHeightDepth{incoming.width(), whd.height - incoming.height(), whd.depth},
    };
}

BinSection BinSection::all_empty_space_above_excluding_behind(const RectToInsert& incoming) const {
    return BinSection{
        x,
        y + incoming.height(),
        z,
        WidthHeightDepth{whd.width, whd.height - incoming.height(), incoming.depth()},
    };
}

BinSection BinSection::all_empty_space_right_excluding_above(const RectToInsert& incoming) const {
    return BinSection{
        x + incoming.width(),
        y,
        z,
        WidthHeightDepth{whd.width - incoming.width(), incoming.height(), whd.depth},
    };
}

BinSection BinSection::all_empty_space_right_excluding_behind(const RectToInsert& incoming) const {
    return BinSection{
        x + incoming.width(),
        y,
        z,
        WidthHeightDepth{whd.width - incoming.width(), whd.height, incoming.depth()},
    };
}

BinSection BinSection::all_empty_space_behind_excluding_above(const RectToInsert& incoming) const {
    return BinSection{
        x,
        y,
        z + incoming.depth(),
        WidthHeightDepth{whd.width, incoming.height(), whd.depth - incoming.depth()},
    };
}

BinSection BinSection::all_empty_space_behind_excluding_right(const RectToInsert& incoming) const {
    return BinSection{
        x,
        y,
        z + incoming.depth(),
        WidthHeightDepth{incoming.width(), whd.height, whd.depth - incoming.depth()},
    };
}

std::string to_debug_string(const WidthHeightDepth& whd) {
    return std::format("WidthHeightDepth {{ width: {}, height: {}, depth: {} }}", whd.width, whd.height, whd.depth);
}

std::string to_debug_string(const BinSection& section) {
    return std::format("BinSection {{ x: {}, y: {}, z: {}, whd: {} }}", section.x, section.y, section.z,
                       to_debug_string(section.whd));
}

std::string PushBinSectionError::to_string() const {
    return std::visit(
        [](const auto& value) -> std::string {
            using Alternative = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Alternative, push_bin_section_error::OutOfBounds>) {
                return "BinSection(" + to_debug_string(value.section) + ")";
            } else {
                return "Overlaps { remaining_section: " + to_debug_string(value.remaining_section) +
                       ", new_section: " + to_debug_string(value.new_section) + " }";
            }
        },
        static_cast<const Base&>(*this));
}

TargetBin::TargetBin(std::uint32_t max_width, std::uint32_t max_height, std::uint32_t max_depth) {
    this->max_width  = max_width;
    this->max_height = max_height;
    this->max_depth  = max_depth;

    available_bin_sections_.push_back(BinSection{0, 0, 0, WidthHeightDepth{max_width, max_height, max_depth}});
}

void TargetBin::remove_filled_section(std::size_t idx) {
    available_bin_sections_.erase(available_bin_sections_.begin() + static_cast<std::ptrdiff_t>(idx));
}

void TargetBin::add_new_sections(const std::array<BinSection, 3>& new_sections) {
    for (const auto& new_section : new_sections) {
        if (new_section.whd.volume() > 0) {
            available_bin_sections_.push_back(new_section);
        }
    }
}

std::expected<void, PushBinSectionError> TargetBin::push_available_bin_section(const BinSection& bin_section) {
    if (bin_section.x >= max_width || bin_section.y >= max_height || bin_section.z >= max_depth) {
        return std::unexpected(PushBinSectionError{push_bin_section_error::OutOfBounds{bin_section}});
    }

    for (const auto& available : available_bin_sections_) {
        if (available.overlaps(bin_section)) {
            return std::unexpected(PushBinSectionError{push_bin_section_error::Overlaps{available, bin_section}});
        }
    }

    push_available_bin_section_unchecked(bin_section);

    return {};
}

void sort_by_size_largest_to_smallest(std::array<BinSection, 3>& items, const BoxSizeHeuristicFn& box_size_heuristic) {
    std::stable_sort(items.begin(), items.end(), [&box_size_heuristic](const BinSection& a, const BinSection& b) {
        return box_size_heuristic(b.whd) < box_size_heuristic(a.whd);
    });
}

std::string to_string(RectanglePackError error) {
    switch (error) {
        case RectanglePackError::NotEnoughBinSpace:
            return "Not enough space to place all of the rectangles.";
    }

    return {};
}

}  // namespace epix::image::detail
