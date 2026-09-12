#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#endif

namespace epix::image::detail {

/**
 * @brief A port of the `rectangle-pack` crate version 0.4.0, which is the bin packer
 * `bevy_image`'s `TextureAtlasBuilder` packs textures through.
 *
 * `TextureAtlasBuilder::build` only needs `GroupedRectsToPlace`, `RectToInsert`, `TargetBin`,
 * `volume_heuristic`, `contains_smallest_box` and `pack_rects`, but the whole crate is ported so
 * that the packing decisions stay identical to Bevy's: the placement order depends on the
 * group/bin/section ordering rules *and* on the stable sorts that reorder them, so a partial port
 * would silently diverge as soon as more than one group or bin is involved.
 *
 * ## Adaptations
 *
 * - The crate's `u128` heuristic values become `std::uint64_t`, because MSVC has no 128-bit
 *   integer type. A volume only exceeds 64 bits when `width * height * depth > 2^64 - 1` (sides
 *   above ~2^21 on all three axes); Bevy packs 2D rectangles whose sides are at most 8192.
 * - `BoxSizeHeuristicFn`/`ComparePotentialContainersFn` are `std::function`s, the C++ counterpart
 *   of the crate's `&dyn Fn`.
 * - The sorts are `std::stable_sort`, matching `slice::sort_by`/`Vec::sort_by` (not the
 *   `*_unstable_*` variants).
 * - `TargetBin::coalesce_available_sections` is not ported: it is `unimplemented!()` in the crate.
 * - `WidthHeightDepth::new` (which panics on a zero side) is `checked`; Rust's struct literals,
 *   which do not check, map to plain aggregate initialisation.
 * - C++ cannot declare a data member and a member function with the same name, so the fields that
 *   the crate exposes through a same-named accessor carry a trailing underscore.
 */

/** @brief A volume, or an area when `depth` is 1 (`rectangle_pack::WidthHeightDepth`). */
struct WidthHeightDepth {
    std::uint32_t width  = 0;
    std::uint32_t height = 0;
    std::uint32_t depth  = 0;

    /** @brief `WidthHeightDepth::new`, which rejects a zero side (the crate panics). */
    static WidthHeightDepth checked(std::uint32_t width, std::uint32_t height, std::uint32_t depth);

    /** @brief `WidthHeightDepth::volume`. */
    std::uint64_t volume() const noexcept {
        return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) *
               static_cast<std::uint64_t>(depth);
    }

    bool operator==(const WidthHeightDepth&) const  = default;
    auto operator<=>(const WidthHeightDepth&) const = default;
};

/** @brief A rectangle that we want to insert into a target bin (`rectangle_pack::RectToInsert`). */
struct RectToInsert {
    /** @brief `RectToInsert::new`; rotation is not yet supported, so all three flags stay false. */
    RectToInsert(std::uint32_t width, std::uint32_t height, std::uint32_t depth) : whd{width, height, depth} {}

    std::uint32_t width() const noexcept { return whd.width; }
    std::uint32_t height() const noexcept { return whd.height; }
    std::uint32_t depth() const noexcept { return whd.depth; }

    bool operator==(const RectToInsert&) const = default;

    /** @brief `pub(crate)` in the crate. */
    WidthHeightDepth whd;

   private:
    bool allow_global_x_axis_rotation = false;
    bool allow_global_y_axis_rotation = false;
    bool allow_global_z_axis_rotation = false;
};

/** @brief How a packed rectangle was rotated (`rectangle_pack::RotatedBy`).
 *
 * The crate marks the enum `#[allow(unused)]` because rotations are not implemented; every
 * placement it makes is `ZeroDegrees`. */
enum class RotatedBy {
    ZeroDegrees,
    NinetyDegrees,
};

/** @brief Describes how and where an incoming rectangle was packed into the target bins
 * (`rectangle_pack::PackedLocation`).
 *
 * The crate has both `pub(crate)` fields and same-named public accessors for `x`/`y`/`z`; C++
 * cannot, so those three fields carry a trailing underscore. */
struct PackedLocation {
    std::uint32_t x_ = 0;
    std::uint32_t y_ = 0;
    std::uint32_t z_ = 0;
    WidthHeightDepth whd{};
    RotatedBy x_axis_rotation = RotatedBy::ZeroDegrees;
    RotatedBy y_axis_rotation = RotatedBy::ZeroDegrees;
    RotatedBy z_axis_rotation = RotatedBy::ZeroDegrees;

    std::uint32_t x() const noexcept { return x_; }
    std::uint32_t y() const noexcept { return y_; }
    std::uint32_t z() const noexcept { return z_; }
    std::uint32_t width() const noexcept { return whd.width; }
    std::uint32_t height() const noexcept { return whd.height; }
    std::uint32_t depth() const noexcept { return whd.depth; }

    bool operator==(const PackedLocation&) const = default;
};

/** @brief How large a box is, in the units the packer sorts by
 * (`rectangle_pack::BoxSizeHeuristicFn`). */
using BoxSizeHeuristicFn = std::function<std::uint64_t(const WidthHeightDepth&)>;

/** @brief `rectangle_pack::volume_heuristic`: the volume of the box. */
std::uint64_t volume_heuristic(const WidthHeightDepth& whd);

/** @brief Decides which of two ways of splitting the remaining space is better
 * (`rectangle_pack::ComparePotentialContainersFn`).
 *
 * `std::strong_ordering::less` means the first set of containers is better, matching the crate's
 * `Ordering::Less`. */
using ComparePotentialContainersFn = std::function<std::strong_ordering(
    std::array<WidthHeightDepth, 3>, std::array<WidthHeightDepth, 3>, const BoxSizeHeuristicFn&)>;

/** @brief `rectangle_pack::contains_smallest_box`: prefer the container with the smallest box,
 * tie-broken on the second smallest box. */
std::strong_ordering contains_smallest_box(std::array<WidthHeightDepth, 3> container1,
                                           std::array<WidthHeightDepth, 3> container2,
                                           const BoxSizeHeuristicFn& heuristic);

/** @brief An error while attempting to place a rectangle within a bin section
 * (`rectangle_pack::BinSectionError`). */
enum class BinSectionError {
    PlacementWiderThanBinSection,
    PlacementTallerThanBinSection,
    PlacementDeeperThanBinSection,
};

/** @brief `BinSectionError`'s `Display` text. */
std::string to_string(BinSectionError error);

/** @brief A rectangular section within a target bin that takes up one or more layers
 * (`rectangle_pack::BinSection`).
 *
 * The crate's `BinSection::new` is plain aggregate initialisation here, because it does not check
 * its arguments. */
struct BinSection {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t z = 0;
    WidthHeightDepth whd{};

    /** @brief `rectangle_pack::BinSection::try_place`: place `incoming` at this section's origin
     * when it fits, returning where it landed and the three sections the remaining space splits
     * into. */
    std::expected<std::pair<PackedLocation, std::array<BinSection, 3>>, BinSectionError> try_place(
        const RectToInsert& incoming,
        const ComparePotentialContainersFn& container_comparison_fn,
        const BoxSizeHeuristicFn& heuristic_fn) const;

    /** @brief `rectangle_pack::BinSection::overlaps`: whether two bin sections overlap. */
    bool overlaps(const BinSection& other) const noexcept {
        return (x >= other.x && x <= other.right()) && (y >= other.y && y <= other.top()) &&
               (z >= other.z && z <= other.back());
    }

    bool operator==(const BinSection&) const  = default;
    auto operator<=>(const BinSection&) const = default;

   private:
    /** @brief The crate's `BinSection::new_spread`, kept for the empty-space helpers. */
    static BinSection new_spread(std::uint32_t x,
                                 std::uint32_t y,
                                 std::uint32_t z,
                                 std::uint32_t width,
                                 std::uint32_t height,
                                 std::uint32_t depth) noexcept {
        return BinSection{x, y, z, WidthHeightDepth{width, height, depth}};
    }

    std::uint32_t right() const noexcept { return x + (whd.width - 1); }
    std::uint32_t top() const noexcept { return y + (whd.height - 1); }
    std::uint32_t back() const noexcept { return z + (whd.depth - 1); }

    std::expected<void, BinSectionError> incoming_can_fit(const RectToInsert& incoming) const;

    std::array<BinSection, 3> width_largest_height_second_largest_depth_smallest(const RectToInsert& incoming) const;
    std::array<BinSection, 3> width_largest_depth_second_largest_height_smallest(const RectToInsert& incoming) const;
    std::array<BinSection, 3> height_largest_width_second_largest_depth_smallest(const RectToInsert& incoming) const;
    std::array<BinSection, 3> height_largest_depth_second_largest_width_smallest(const RectToInsert& incoming) const;
    std::array<BinSection, 3> depth_largest_width_second_largest_height_smallest(const RectToInsert& incoming) const;
    std::array<BinSection, 3> depth_largest_height_second_largest_width_smallest(const RectToInsert& incoming) const;

    BinSection all_empty_space_above(const RectToInsert& incoming) const;
    BinSection all_empty_space_right(const RectToInsert& incoming) const;
    BinSection all_empty_space_behind(const RectToInsert& incoming) const;
    BinSection empty_space_directly_above(const RectToInsert& incoming) const;
    BinSection empty_space_directly_right(const RectToInsert& incoming) const;
    BinSection empty_space_directly_behind(const RectToInsert& incoming) const;
    BinSection all_empty_space_above_excluding_right(const RectToInsert& incoming) const;
    BinSection all_empty_space_above_excluding_behind(const RectToInsert& incoming) const;
    BinSection all_empty_space_right_excluding_above(const RectToInsert& incoming) const;
    BinSection all_empty_space_right_excluding_behind(const RectToInsert& incoming) const;
    BinSection all_empty_space_behind_excluding_above(const RectToInsert& incoming) const;
    BinSection all_empty_space_behind_excluding_right(const RectToInsert& incoming) const;
};

/** @brief The bounds `rectangle-pack` puts on its rectangle, bin and group identifiers
 * (`Debug + Hash + PartialEq + Eq + Clone + Ord + PartialOrd`). */
template <typename T>
concept PackKey = std::copyable<T> && std::totally_ordered<T> && requires(const T& value) {
    { std::hash<T>{}(value) } -> std::convertible_to<std::size_t>;
};

/** @brief `Group`'s `Ungrouped(RectToPlaceId)` alternative: a lone rectangle is its own group. */
template <typename RectToPlaceId>
struct Ungrouped {
    RectToPlaceId value;

    bool operator==(const Ungrouped&) const  = default;
    auto operator<=>(const Ungrouped&) const = default;
};

/** @brief `Group`'s `Grouped(GroupId)` alternative: wraps a user provided group identifier. */
template <typename GroupId>
struct Grouped {
    GroupId value;

    bool operator==(const Grouped&) const  = default;
    auto operator<=>(const Grouped&) const = default;
};

/** @brief A group of rectangles that need to be placed together (`rectangle_pack::Group`).
 *
 * The variant's ordering is index first and payload second, which is exactly how Rust orders the
 * derived `Ord` of the crate's two-variant enum. */
template <typename GroupId, typename RectToPlaceId>
using Group = std::variant<Ungrouped<RectToPlaceId>, Grouped<GroupId>>;

/** @brief Groups of rectangles that need to be placed into bins
 * (`rectangle_pack::GroupedRectsToPlace`).
 *
 * A group's heuristic is the sum of the heuristics of the rectangles inside it, and larger groups
 * are placed first. */
template <PackKey RectToPlaceId, PackKey GroupId = std::monostate>
struct GroupedRectsToPlace {
    /** @brief `GroupedRectsToPlace::new`. */
    GroupedRectsToPlace() = default;

    /** @brief `GroupedRectsToPlace::push_rect`.
     *
     * The crate documents a panic when `group_ids` is `Some` but empty; the implementation does not
     * check it, so neither does this port. */
    void push_rect(RectToPlaceId inbound_id, std::optional<std::vector<GroupId>> group_ids, RectToInsert inbound) {
        rects.insert_or_assign(inbound_id, inbound);

        if (!group_ids) {
            group_id_to_inbound_ids.insert_or_assign(
                Group<GroupId, RectToPlaceId>{Ungrouped<RectToPlaceId>{inbound_id}},
                std::vector<RectToPlaceId>{inbound_id});
            inbound_id_to_group_ids.insert_or_assign(
                inbound_id, std::vector<Group<GroupId, RectToPlaceId>>{Ungrouped<RectToPlaceId>{inbound_id}});
        } else {
            std::vector<Group<GroupId, RectToPlaceId>> inbound_groups;
            inbound_groups.reserve(group_ids->size());
            for (const auto& group_id : *group_ids) {
                inbound_groups.push_back(Group<GroupId, RectToPlaceId>{Grouped<GroupId>{group_id}});
            }
            inbound_id_to_group_ids.insert_or_assign(inbound_id, std::move(inbound_groups));

            for (const auto& group_id : *group_ids) {
                group_id_to_inbound_ids[Group<GroupId, RectToPlaceId>{Grouped<GroupId>{group_id}}].push_back(
                    inbound_id);
            }
        }
    }

    /** @brief `pub(crate)` in the crate; the crate's own `FIXME` notes that it appears to be
     * unused. */
    std::unordered_map<RectToPlaceId, std::vector<Group<GroupId, RectToPlaceId>>> inbound_id_to_group_ids;
    /** @brief `pub(crate)` in the crate. */
    std::map<Group<GroupId, RectToPlaceId>, std::vector<RectToPlaceId>> group_id_to_inbound_ids;
    /** @brief `pub(crate)` in the crate. */
    std::unordered_map<RectToPlaceId, RectToInsert> rects;
};

/** @brief `BinSection`'s derived `Debug` output (`WidthHeightDepth { width: 1, height: 1,
 * depth: 1 }`), which `PushBinSectionError`'s `Display` embeds. */
std::string to_debug_string(const WidthHeightDepth& whd);

/** @brief `BinSection`'s derived `Debug` output
 * (`BinSection { x: 0, y: 0, z: 0, whd: WidthHeightDepth { .. } }`). */
std::string to_debug_string(const BinSection& section);

/** @brief C++ counterparts to `PushBinSectionError`'s alternatives
 * (`target_bin/push_available_bin_section.rs:67-77`). */
namespace push_bin_section_error {
/** @brief Attempted to push a `BinSection` that is not fully contained by the bin. */
struct OutOfBounds {
    BinSection section;

    bool operator==(const OutOfBounds&) const = default;
};
/** @brief Attempted to push a `BinSection` that overlaps another empty bin section. */
struct Overlaps {
    /** @brief The section that is already stored as empty within the `TargetBin`. */
    BinSection remaining_section;
    /** @brief The section that you were trying to add to the `TargetBin`. */
    BinSection new_section;

    bool operator==(const Overlaps&) const = default;
};
}  // namespace push_bin_section_error

/** @brief An error while attempting to push a `BinSection` into the remaining bin sections of a
 * `TargetBin` (`rectangle_pack::PushBinSectionError`). */
struct PushBinSectionError : std::variant<push_bin_section_error::OutOfBounds, push_bin_section_error::Overlaps> {
    using Base = std::variant<push_bin_section_error::OutOfBounds, push_bin_section_error::Overlaps>;
    using Base::Base;

    /** @brief The crate's `Display`, which formats the variant the way Rust's `Debug` would. */
    std::string to_string() const;
};

/** @brief A bin that we'd like to pack our incoming rectangles into
 * (`rectangle_pack::TargetBin`). */
class TargetBin {
   public:
    /** @brief `TargetBin::new`: a bin whose whole volume is one available section. */
    TargetBin(std::uint32_t max_width, std::uint32_t max_height, std::uint32_t max_depth);

    /** @brief The free `BinSection`s within the `TargetBin` that rectangles can still be placed
     * into. */
    const std::vector<BinSection>& available_bin_sections() const noexcept { return available_bin_sections_; }

    /** @brief Remove the section that was just split by a placed rectangle. */
    void remove_filled_section(std::size_t idx);

    /** @brief When a section is filled it gets split into three new sections; here we add those.
     *
     * TODO: Ignore sections with a volume of 0 */
    void add_new_sections(const std::array<BinSection, 3>& new_sections);

    /** @brief Push a `BinSection` to the list of remaining `BinSection`s that rectangles can be
     * placed in, after checking that it is in bounds and does not overlap. */
    std::expected<void, PushBinSectionError> push_available_bin_section(const BinSection& bin_section);

    /** @brief `TargetBin::push_available_bin_section_unchecked`. */
    void push_available_bin_section_unchecked(const BinSection& bin_section) {
        available_bin_sections_.push_back(bin_section);
    }

    /** @brief `pub(crate)` in the crate; `sort_bins_smallest_to_largest` reads the three sizes. */
    std::uint32_t max_width  = 0;
    std::uint32_t max_height = 0;
    std::uint32_t max_depth  = 0;

   private:
    std::vector<BinSection> available_bin_sections_;
};

/** @brief An error while attempting to pack rectangles into bins
 * (`rectangle_pack::RectanglePackError`). */
enum class RectanglePackError {
    /** @brief The rectangles can't be placed into the bins. More bin space needs to be provided. */
    NotEnoughBinSpace,
};

/** @brief `RectanglePackError`'s `Display` text. */
std::string to_string(RectanglePackError error);

/** @brief Information about successfully packed rectangles
 * (`rectangle_pack::RectanglePackOk`). */
template <PackKey RectToPlaceId, PackKey BinId>
struct RectanglePackOk {
    /** @brief Indicates where every incoming rectangle was placed. */
    const std::unordered_map<RectToPlaceId, std::pair<BinId, PackedLocation>>& packed_locations() const noexcept {
        return packed_locations_;
    }

    bool operator==(const RectanglePackOk&) const = default;

    std::unordered_map<RectToPlaceId, std::pair<BinId, PackedLocation>> packed_locations_;
};

/** @brief `sort_bins_smallest_to_largest`: the crate sorts the bins gathered from a `BTreeMap`,
 * i.e. in ascending id order, with a stable sort. */
template <PackKey BinId>
void sort_bins_smallest_to_largest(std::vector<std::pair<const BinId*, TargetBin*>>& bins,
                                   const BoxSizeHeuristicFn& box_size_heuristic) {
    std::stable_sort(bins.begin(), bins.end(), [&box_size_heuristic](const auto& a, const auto& b) {
        return box_size_heuristic(WidthHeightDepth{a.second->max_width, a.second->max_height, a.second->max_depth}) <
               box_size_heuristic(WidthHeightDepth{b.second->max_width, b.second->max_height, b.second->max_depth});
    });
}

/** @brief `sort_by_size_largest_to_smallest`. */
void sort_by_size_largest_to_smallest(std::array<BinSection, 3>& items, const BoxSizeHeuristicFn& box_size_heuristic);

/** @brief `sort_groups_largest_to_smallest`: groups arrive in ascending group-key order from the
 * crate's `BTreeMap`, and are stably sorted by descending summed rectangle heuristic. */
template <PackKey RectToPlaceId, PackKey GroupId>
void sort_groups_largest_to_smallest(
    std::vector<std::pair<const Group<GroupId, RectToPlaceId>*, const std::vector<RectToPlaceId>*>>&
        group_id_to_inbound_ids,
    const GroupedRectsToPlace<RectToPlaceId, GroupId>& incoming_groups,
    const BoxSizeHeuristicFn& box_size_heuristic) {
    std::stable_sort(group_id_to_inbound_ids.begin(), group_id_to_inbound_ids.end(),
                     [&incoming_groups, &box_size_heuristic](const auto& a, const auto& b) {
                         std::uint64_t a_heuristic = 0;
                         for (const auto& inbound : *a.second) {
                             a_heuristic += box_size_heuristic(incoming_groups.rects.at(inbound).whd);
                         }

                         std::uint64_t b_heuristic = 0;
                         for (const auto& inbound : *b.second) {
                             b_heuristic += box_size_heuristic(incoming_groups.rects.at(inbound).whd);
                         }

                         return b_heuristic < a_heuristic;
                     });
}

/** @brief `can_fit_entire_group_into_bin`: takes the bin by value because the crate clones it and
 * spends the clone's free sections. */
template <PackKey RectToPlaceId, PackKey GroupId>
bool can_fit_entire_group_into_bin(TargetBin bin,
                                   const std::vector<RectToPlaceId>& group,
                                   const GroupedRectsToPlace<RectToPlaceId, GroupId>& rects_to_place,
                                   const BoxSizeHeuristicFn& box_size_heuristic,
                                   const ComparePotentialContainersFn& more_suitable_containers_fn) {
    for (const auto& rect_to_place_id : group) {
        if (bin.available_bin_sections().empty()) return false;

        auto bin_sections                  = bin.available_bin_sections();
        const std::size_t last_section_idx = bin_sections.size() - 1;
        std::size_t sections_tried         = 0;
        bool placed                        = false;

        while (!bin_sections.empty()) {
            const BinSection remaining_section = bin_sections.back();
            bin_sections.pop_back();

            const RectToInsert& rect_to_place = rects_to_place.rects.at(rect_to_place_id);

            auto placement =
                remaining_section.try_place(rect_to_place, more_suitable_containers_fn, box_size_heuristic);
            if (!placement) {
                ++sections_tried;
                continue;
            }

            // Rust: `let (_placement, mut new_sections) = placement.unwrap();`
            auto new_sections = std::get<1>(*placement);
            sort_by_size_largest_to_smallest(new_sections, box_size_heuristic);

            bin.remove_filled_section(last_section_idx - sections_tried);
            bin.add_new_sections(new_sections);

            placed = true;
            break;
        }

        if (!placed) return false;
    }

    return true;
}

/** @brief Determine how to fit a set of incoming rectangles (2d or 3d) into a set of target bins
 * (`rectangle_pack::pack_rects`).
 *
 * Groups are placed largest first, into the smallest bin they fit in, and the first bin that can
 * take a whole group gets it. The algorithm was originally inspired by `rectpack2D` and then
 * modified to work in 3D. */
template <PackKey RectToPlaceId, PackKey BinId, PackKey GroupId>
std::expected<RectanglePackOk<RectToPlaceId, BinId>, RectanglePackError> pack_rects(
    const GroupedRectsToPlace<RectToPlaceId, GroupId>& rects_to_place,
    std::map<BinId, TargetBin>& target_bins,
    const BoxSizeHeuristicFn& box_size_heuristic,
    const ComparePotentialContainersFn& more_suitable_containers_fn) {
    std::unordered_map<RectToPlaceId, std::pair<BinId, PackedLocation>> packed_locations;

    std::vector<std::pair<const BinId*, TargetBin*>> bins;
    bins.reserve(target_bins.size());
    for (auto& [bin_id, bin] : target_bins) {
        bins.emplace_back(&bin_id, &bin);
    }
    sort_bins_smallest_to_largest(bins, box_size_heuristic);

    std::vector<std::pair<const Group<GroupId, RectToPlaceId>*, const std::vector<RectToPlaceId>*>> groups;
    groups.reserve(rects_to_place.group_id_to_inbound_ids.size());
    for (const auto& [group_id, inbound_ids] : rects_to_place.group_id_to_inbound_ids) {
        groups.emplace_back(&group_id, &inbound_ids);
    }
    sort_groups_largest_to_smallest(groups, rects_to_place, box_size_heuristic);

    for (const auto& group_entry : groups) {
        const std::vector<RectToPlaceId>& rects_to_place_ids = *group_entry.second;
        bool group_placed                                    = false;

        for (auto& [bin_id, bin] : bins) {
            if (!can_fit_entire_group_into_bin(TargetBin(*bin), rects_to_place_ids, rects_to_place, box_size_heuristic,
                                               more_suitable_containers_fn)) {
                continue;
            }

            for (const auto& rect_to_place_id : rects_to_place_ids) {
                if (bin->available_bin_sections().empty()) {
                    continue;
                }

                auto bin_sections                  = bin->available_bin_sections();
                const std::size_t last_section_idx = bin_sections.size() - 1;
                std::size_t sections_tried         = 0;

                while (!bin_sections.empty()) {
                    const BinSection remaining_section = bin_sections.back();
                    bin_sections.pop_back();

                    const RectToInsert& rect_to_place = rects_to_place.rects.at(rect_to_place_id);

                    auto placement =
                        remaining_section.try_place(rect_to_place, more_suitable_containers_fn, box_size_heuristic);
                    if (!placement) {
                        ++sections_tried;
                        continue;
                    }

                    auto [placement_value, new_sections] = *placement;
                    sort_by_size_largest_to_smallest(new_sections, box_size_heuristic);

                    bin->remove_filled_section(last_section_idx - sections_tried);
                    bin->add_new_sections(new_sections);

                    packed_locations.insert_or_assign(rect_to_place_id, std::pair{*bin_id, placement_value});

                    break;
                }
            }

            group_placed = true;
            break;
        }

        if (!group_placed) return std::unexpected(RectanglePackError::NotEnoughBinSpace);
    }

    RectanglePackOk<RectToPlaceId, BinId> ok;
    ok.packed_locations_ = std::move(packed_locations);
    return ok;
}

}  // namespace epix::image::detail
