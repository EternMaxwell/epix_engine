#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/image/detail/rectangle_pack.hpp>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rp = epix::image::detail;

namespace {

constexpr std::uint32_t kBiggest  = 50;
constexpr std::uint32_t kMiddle   = 25;
constexpr std::uint32_t kSmallest = 10;
constexpr std::uint32_t kFull     = 100;

/** @brief A `BinSection` in the crate's `BinSection::new_spread` argument order. */
rp::BinSection section(
    std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t width, std::uint32_t height, std::uint32_t depth) {
    return rp::BinSection{x, y, z, rp::WidthHeightDepth{width, height, depth}};
}

/** @brief The fixture's `ungrouped` helper: `(width, height, depth)` triples with no group. */
rp::GroupedRectsToPlace<std::size_t> ungrouped(const std::vector<std::array<std::uint32_t, 3>>& sizes) {
    rp::GroupedRectsToPlace<std::size_t> rects;
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        rects.push_rect(index, std::nullopt, rp::RectToInsert{sizes[index][0], sizes[index][1], sizes[index][2]});
    }
    return rects;
}

/** @brief The fixture's `bins` helper: `(id, max_width, max_height, max_depth)` entries. */
std::map<std::size_t, rp::TargetBin> bins(const std::vector<std::array<std::uint32_t, 4>>& entries) {
    std::map<std::size_t, rp::TargetBin> targets;
    for (const auto& entry : entries) {
        targets.emplace(entry[0], rp::TargetBin{entry[1], entry[2], entry[3]});
    }
    return targets;
}

/** @brief The fixture's `report`, so the ported packer is compared against the crate's own output
 * rather than against invariants. */
template <typename GroupId>
std::string report(const std::string& name,
                   const rp::GroupedRectsToPlace<std::size_t, GroupId>& rects,
                   std::map<std::size_t, rp::TargetBin>& targets) {
    std::string output = "scenario " + name + "\n";

    const auto packed = rp::pack_rects(rects, targets, rp::volume_heuristic, rp::contains_smallest_box);
    if (!packed) return output + "  error " + rp::to_string(packed.error()) + "\n";

    std::vector<std::string> lines;
    lines.reserve(packed->packed_locations().size());
    for (const auto& entry : packed->packed_locations()) {
        const auto& location = entry.second.second;
        lines.push_back(std::format("  {} bin {} at {} {} {} size {} {} {}", entry.first, entry.second.first,
                                    location.x(), location.y(), location.z(), location.width(), location.height(),
                                    location.depth()));
    }

    std::sort(lines.begin(), lines.end());
    for (const auto& line : lines) {
        output += line + "\n";
    }

    return output;
}

}  // namespace

TEST(WidthHeightDepth, DefaultsOrderingAndHeuristicsMatchTheCrate) {
    EXPECT_EQ(rp::WidthHeightDepth{}, (rp::WidthHeightDepth{0, 0, 0}));
    EXPECT_EQ((rp::WidthHeightDepth{2, 3, 4}.volume()), 24u);
    EXPECT_EQ(rp::volume_heuristic(rp::WidthHeightDepth{2, 3, 4}), 24u);
    EXPECT_EQ(rp::WidthHeightDepth::checked(2, 3, 4), (rp::WidthHeightDepth{2, 3, 4}));

    // Rust's derived `Ord` compares the fields in declaration order.
    EXPECT_LT((rp::WidthHeightDepth{1, 1, 1}), (rp::WidthHeightDepth{1, 1, 2}));
    EXPECT_LT((rp::WidthHeightDepth{1, 1, 9}), (rp::WidthHeightDepth{1, 2, 0}));
    EXPECT_LT((rp::WidthHeightDepth{1, 9, 9}), (rp::WidthHeightDepth{2, 0, 0}));
}

// The crate's own `bin_section/overlaps.rs` test table.
TEST(BinSection, OverlapsMatchesTheCratesTests) {
    const rp::BinSection other = section(2, 3, 4, 2, 3, 4);

    EXPECT_TRUE(section(3, 4, 5, 1, 1, 1).overlaps(other));
    EXPECT_FALSE(section(3, 40, 50, 1, 1, 1).overlaps(other));
    EXPECT_FALSE(section(30, 4, 50, 1, 1, 1).overlaps(other));
    EXPECT_FALSE(section(30, 40, 5, 1, 1, 1).overlaps(other));
}

// The crate's own `bin_section.rs` tests: `incoming_can_fit` reports the first size that is too
// large, in width, height, depth order.
TEST(BinSection, TryPlaceRejectsPlacementsThatDoNotFit) {
    const rp::BinSection bin_section = section(0, 0, 0, 100, 100, 100);

    const auto too_wide =
        bin_section.try_place(rp::RectToInsert{200, 100, 100}, rp::contains_smallest_box, rp::volume_heuristic);
    ASSERT_FALSE(too_wide.has_value());
    EXPECT_EQ(too_wide.error(), rp::BinSectionError::PlacementWiderThanBinSection);

    const auto too_tall =
        bin_section.try_place(rp::RectToInsert{100, 200, 100}, rp::contains_smallest_box, rp::volume_heuristic);
    ASSERT_FALSE(too_tall.has_value());
    EXPECT_EQ(too_tall.error(), rp::BinSectionError::PlacementTallerThanBinSection);

    const auto too_deep =
        bin_section.try_place(rp::RectToInsert{100, 100, 200}, rp::contains_smallest_box, rp::volume_heuristic);
    ASSERT_FALSE(too_deep.has_value());
    EXPECT_EQ(too_deep.error(), rp::BinSectionError::PlacementDeeperThanBinSection);

    const auto exact =
        bin_section.try_place(rp::RectToInsert{100, 100, 100}, rp::contains_smallest_box, rp::volume_heuristic);
    ASSERT_TRUE(exact.has_value());
    EXPECT_EQ(std::get<0>(*exact).x(), 0u);
    EXPECT_EQ(std::get<0>(*exact), (rp::PackedLocation{.x_              = 0,
                                                       .y_              = 0,
                                                       .z_              = 0,
                                                       .whd             = {100, 100, 100},
                                                       .x_axis_rotation = rp::RotatedBy::ZeroDegrees,
                                                       .y_axis_rotation = rp::RotatedBy::ZeroDegrees,
                                                       .z_axis_rotation = rp::RotatedBy::ZeroDegrees}));
}

// The crate's six `bin_section.rs` split tests: with `contains_smallest_box` the winning division of
// the remaining space is the one whose smallest box is largest, tie-broken on the second smallest.
TEST(BinSection, TryPlaceChoosesTheSplitThatContainsTheSmallestBox) {
    struct Case {
        rp::WidthHeightDepth rect;
        std::array<rp::BinSection, 3> expected;
    };

    const std::array<Case, 6> cases{
        // width > height > depth
        Case{{kBiggest, kMiddle, kSmallest},
             {section(kBiggest, 0, 0, kFull - kBiggest, kMiddle, kSmallest),
              section(0, kMiddle, 0, kFull, kFull - kMiddle, kSmallest),
              section(0, 0, kSmallest, kFull, kFull, kFull - kSmallest)}},
        // width > depth > height
        Case{{kBiggest, kSmallest, kMiddle},
             {section(kBiggest, 0, 0, kFull - kBiggest, kSmallest, kMiddle),
              section(0, kSmallest, 0, kFull, kFull - kSmallest, kFull),
              section(0, 0, kMiddle, kFull, kSmallest, kFull - kMiddle)}},
        // height > width > depth
        Case{{kMiddle, kBiggest, kSmallest},
             {section(kMiddle, 0, 0, kFull - kMiddle, kFull, kSmallest),
              section(0, kBiggest, 0, kMiddle, kFull - kBiggest, kSmallest),
              section(0, 0, kSmallest, kFull, kFull, kFull - kSmallest)}},
        // height > depth > width
        Case{{kSmallest, kBiggest, kMiddle},
             {section(kSmallest, 0, 0, kFull - kSmallest, kFull, kFull),
              section(0, kBiggest, 0, kSmallest, kFull - kBiggest, kMiddle),
              section(0, 0, kMiddle, kSmallest, kFull, kFull - kMiddle)}},
        // depth > width > height
        Case{{kMiddle, kSmallest, kBiggest},
             {section(kMiddle, 0, 0, kFull - kMiddle, kSmallest, kFull),
              section(0, kSmallest, 0, kFull, kFull - kSmallest, kFull),
              section(0, 0, kBiggest, kMiddle, kSmallest, kFull - kBiggest)}},
        // depth > height > width
        Case{{kSmallest, kMiddle, kBiggest},
             {section(kSmallest, 0, 0, kFull - kSmallest, kFull, kFull),
              section(0, kMiddle, 0, kSmallest, kFull - kMiddle, kFull),
              section(0, 0, kBiggest, kSmallest, kMiddle, kFull - kBiggest)}},
    };

    for (const auto& test_case : cases) {
        const rp::BinSection bin_section{0, 0, 0, rp::WidthHeightDepth{kFull, kFull, kFull}};
        const auto placed =
            bin_section.try_place(rp::RectToInsert{test_case.rect.width, test_case.rect.height, test_case.rect.depth},
                                  rp::contains_smallest_box, rp::volume_heuristic);
        ASSERT_TRUE(placed.has_value());

        auto actual   = std::get<1>(*placed);
        auto expected = test_case.expected;
        std::sort(actual.begin(), actual.end());
        std::sort(expected.begin(), expected.end());
        EXPECT_EQ(actual, expected);
    }
}

// The crate's `target_bin/push_available_bin_section.rs` tests.
TEST(TargetBin, PushAvailableBinSectionValidatesBoundsAndOverlap) {
    rp::TargetBin out_of_bounds_bin{100, 100, 1};
    const rp::BinSection out_of_bounds = section(101, 0, 0, 1, 1, 1);
    const auto bounds_error            = out_of_bounds_bin.push_available_bin_section(out_of_bounds);
    ASSERT_FALSE(bounds_error.has_value());
    EXPECT_EQ(bounds_error.error(), rp::PushBinSectionError{rp::push_bin_section_error::OutOfBounds{out_of_bounds}});

    rp::TargetBin overlapping_bin{100, 100, 1};
    const rp::BinSection overlapping = section(0, 0, 0, 1, 1, 1);
    const auto overlap_error         = overlapping_bin.push_available_bin_section(overlapping);
    ASSERT_FALSE(overlap_error.has_value());
    EXPECT_EQ(
        overlap_error.error(),
        (rp::PushBinSectionError{rp::push_bin_section_error::Overlaps{section(0, 0, 0, 100, 100, 1), overlapping}}));

    // The crate clears the free sections to reach the success path; `remove_filled_section` is the
    // ported equivalent of Rust's `Vec::remove`.
    rp::TargetBin empty_bin{100, 100, 1};
    empty_bin.remove_filled_section(0);
    ASSERT_TRUE(empty_bin.available_bin_sections().empty());

    const rp::BinSection valid = section(1, 2, 0, 1, 1, 1);
    EXPECT_TRUE(empty_bin.push_available_bin_section(valid).has_value());
    ASSERT_EQ(empty_bin.available_bin_sections().size(), 1u);
    EXPECT_EQ(empty_bin.available_bin_sections()[0], valid);

    empty_bin.push_available_bin_section_unchecked(valid);
    EXPECT_EQ(empty_bin.available_bin_sections().size(), 2u);
}

TEST(TargetBin, AddNewSectionsDropsZeroVolumeSections) {
    rp::TargetBin bin{4, 4, 1};
    bin.remove_filled_section(0);

    bin.add_new_sections({section(0, 0, 0, 2, 2, 1), section(2, 0, 0, 0, 2, 1), section(0, 2, 0, 2, 0, 1)});

    ASSERT_EQ(bin.available_bin_sections().size(), 1u);
    EXPECT_EQ(bin.available_bin_sections()[0], section(0, 0, 0, 2, 2, 1));
}

// The crate's `grouped_rects_to_place.rs` tests.
TEST(GroupedRectsToPlace, RecordsGroupIdsLikeTheCrate) {
    rp::GroupedRectsToPlace<std::size_t, std::uint32_t> rects;

    rects.push_rect(1, std::vector<std::uint32_t>{0, 1}, rp::RectToInsert{10, 10, 1});
    EXPECT_EQ(rects.rects.at(1), (rp::RectToInsert{10, 10, 1}));
    EXPECT_EQ(rects.inbound_id_to_group_ids.at(1), (std::vector<rp::Group<std::uint32_t, std::size_t>>{
                                                       rp::Grouped<std::uint32_t>{0}, rp::Grouped<std::uint32_t>{1}}));

    rects.push_rect(2, std::nullopt, rp::RectToInsert{10, 10, 1});
    EXPECT_EQ(rects.group_id_to_inbound_ids.at(rp::Group<std::uint32_t, std::size_t>{rp::Ungrouped<std::size_t>{2}}),
              (std::vector<std::size_t>{2}));
    EXPECT_EQ(rects.inbound_id_to_group_ids.at(2),
              (std::vector<rp::Group<std::uint32_t, std::size_t>>{rp::Ungrouped<std::size_t>{2}}));

    rects.push_rect(3, std::vector<std::uint32_t>{0}, rp::RectToInsert{10, 10, 1});
    EXPECT_EQ(rects.group_id_to_inbound_ids.at(rp::Group<std::uint32_t, std::size_t>{rp::Grouped<std::uint32_t>{0}}),
              (std::vector<std::size_t>{1, 3}));
}

TEST(RectanglePackError, DisplayMatchesTheCrate) {
    EXPECT_EQ(rp::to_string(rp::BinSectionError::PlacementWiderThanBinSection),
              "Can not place a rectangle inside of a bin that is wider than that rectangle.");
    EXPECT_EQ(rp::to_string(rp::BinSectionError::PlacementTallerThanBinSection),
              "Can not place a rectangle inside of a bin that is taller than that rectangle.");
    EXPECT_EQ(rp::to_string(rp::BinSectionError::PlacementDeeperThanBinSection),
              "Can not place a rectangle inside of a bin that is deeper than that rectangle.");
    EXPECT_EQ(rp::to_string(rp::RectanglePackError::NotEnoughBinSpace),
              "Not enough space to place all of the rectangles.");

    // `PushBinSectionError`'s `Display` formats the variant the way Rust's `Debug` would.
    rp::TargetBin bin{100, 100, 1};
    const auto overlap_error = bin.push_available_bin_section(bin.available_bin_sections()[0]);
    ASSERT_FALSE(overlap_error.has_value());
    EXPECT_EQ(overlap_error.error().to_string(),
              "Overlaps { remaining_section: BinSection { x: 0, y: 0, z: 0, whd: WidthHeightDepth { width: 100, "
              "height: 100, depth: 1 } }, new_section: BinSection { x: 0, y: 0, z: 0, whd: WidthHeightDepth { width: "
              "100, height: 100, depth: 1 } } }");

    // Rust cannot name `BinSection`, so the crate can never construct this variant; the text is the
    // `debug_tuple("BinSection")` its `Display` asks for.
    EXPECT_EQ(
        (rp::PushBinSectionError{rp::push_bin_section_error::OutOfBounds{section(101, 0, 0, 1, 1, 1)}}).to_string(),
        "BinSection(BinSection { x: 101, y: 0, z: 0, whd: WidthHeightDepth { width: 1, height: 1, depth: 1 } "
        "})");
}

// Every placement below is a recorded dump of the real `rectangle-pack` 0.4.0 crate (the
// `tmp/rectangle-pack-fixture` Rust project), so the ported packer's placements, its `NotEnoughBinSpace`
// verdicts, and the order in which bins and groups are chosen all have to match the crate exactly.
TEST(RectanglePack, MatchesRectanglePack040Placements) {
    std::string dump;

    auto square      = bins({{0, 64, 64, 1}});
    auto exact_fit   = bins({{0, 8, 8, 1}});
    auto too_small   = bins({{0, 8, 8, 1}});
    auto two_bins    = bins({{0, 16, 16, 1}, {1, 64, 64, 1}});
    auto grouped_bin = bins({{0, 16, 16, 1}, {1, 64, 64, 1}});
    auto wide        = bins({{0, 32, 8, 1}});
    auto boxes       = bins({{0, 4, 4, 3}});

    dump += report(
        "square-64",
        ungrouped({{10, 10, 1}, {5, 5, 1}, {20, 4, 1}, {3, 3, 1}, {12, 12, 1}, {9, 9, 1}, {1, 1, 1}, {33, 7, 1}}),
        square);
    dump += report("exact-fit-8", ungrouped({{4, 4, 1}, {4, 4, 1}, {4, 4, 1}, {4, 4, 1}}), exact_fit);
    dump += report("too-small", ungrouped({{5, 5, 1}, {5, 5, 1}}), too_small);
    dump += report("two-bins", ungrouped({{10, 10, 1}, {10, 10, 1}, {10, 10, 1}, {2, 2, 1}, {31, 31, 1}}), two_bins);

    rp::GroupedRectsToPlace<std::size_t, std::uint32_t> grouped;
    grouped.push_rect(0, std::vector<std::uint32_t>{7}, rp::RectToInsert{12, 12, 1});
    grouped.push_rect(1, std::vector<std::uint32_t>{7}, rp::RectToInsert{12, 12, 1});
    grouped.push_rect(2, std::vector<std::uint32_t>{8}, rp::RectToInsert{3, 3, 1});
    grouped.push_rect(3, std::nullopt, rp::RectToInsert{20, 20, 1});
    dump += report("grouped", grouped, grouped_bin);

    // Once the four 8x8 rectangles fill the bin the remaining sections have a volume of zero, so
    // they are dropped and the 3x4 rectangle has nowhere left to go.
    dump += report("wide-32-8", ungrouped({{8, 8, 1}, {8, 8, 1}, {8, 8, 1}, {8, 8, 1}, {3, 4, 1}}), wide);
    dump += report("boxes-4-4-3", ungrouped({{2, 2, 1}, {2, 2, 1}, {2, 2, 2}, {1, 1, 3}}), boxes);

    const std::string expected =
        "scenario square-64\n"
        "  0 bin 0 at 0 19 0 size 10 10 1\n"
        "  1 bin 0 at 12 11 0 size 5 5 1\n"
        "  2 bin 0 at 12 7 0 size 20 4 1\n"
        "  3 bin 0 at 17 11 0 size 3 3 1\n"
        "  4 bin 0 at 0 7 0 size 12 12 1\n"
        "  5 bin 0 at 0 29 0 size 9 9 1\n"
        "  6 bin 0 at 17 14 0 size 1 1 1\n"
        "  7 bin 0 at 0 0 0 size 33 7 1\n"
        "scenario exact-fit-8\n"
        "  0 bin 0 at 0 0 0 size 4 4 1\n"
        "  1 bin 0 at 4 0 0 size 4 4 1\n"
        "  2 bin 0 at 0 4 0 size 4 4 1\n"
        "  3 bin 0 at 4 4 0 size 4 4 1\n"
        "scenario too-small\n"
        "  error Not enough space to place all of the rectangles.\n"
        "scenario two-bins\n"
        "  0 bin 0 at 0 0 0 size 10 10 1\n"
        "  1 bin 1 at 31 0 0 size 10 10 1\n"
        "  2 bin 1 at 31 10 0 size 10 10 1\n"
        "  3 bin 0 at 10 0 0 size 2 2 1\n"
        "  4 bin 1 at 0 0 0 size 31 31 1\n"
        "scenario grouped\n"
        "  0 bin 1 at 20 0 0 size 12 12 1\n"
        "  1 bin 1 at 32 0 0 size 12 12 1\n"
        "  2 bin 0 at 0 0 0 size 3 3 1\n"
        "  3 bin 1 at 0 0 0 size 20 20 1\n"
        "scenario wide-32-8\n"
        "  error Not enough space to place all of the rectangles.\n"
        "scenario boxes-4-4-3\n"
        "  0 bin 0 at 0 0 2 size 2 2 1\n"
        "  1 bin 0 at 2 0 0 size 2 2 1\n"
        "  2 bin 0 at 0 0 0 size 2 2 2\n"
        "  3 bin 0 at 0 2 0 size 1 1 3\n";

    EXPECT_EQ(dump, expected);
}
