#include <gtest/gtest.h>

#include <epix/utils/index_map.hpp>

#include <vector>

TEST(IndexMap, SwapRemovalMovesLastEntryAndRepairsLookupIndex) {
    epix::utils::IndexMap<int, int> entries;
    entries[1] = 10;
    entries[2] = 20;
    entries[3] = 30;

    EXPECT_TRUE(entries.swap_remove(2));
    ASSERT_EQ(entries.size(), 2u);
    std::vector<int> keys;
    for (const auto& [key, value] : entries.iter()) {
        (void)value;
        keys.push_back(key);
    }
    EXPECT_EQ(keys, (std::vector<int>{1, 3}));
    EXPECT_EQ(entries.index_of(3), std::optional<std::size_t>{1});
    ASSERT_NE(entries.get(3), nullptr);
    EXPECT_EQ(*entries.get(3), 30);

    auto removed = entries.swap_remove_index(0);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(removed->first, 1);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.index_of(3), std::optional<std::size_t>{0});
    EXPECT_FALSE(entries.swap_remove(1));
}
