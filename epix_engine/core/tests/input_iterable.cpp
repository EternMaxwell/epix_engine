#include <gtest/gtest.h>

import std;
import epix.utils;
import epix.core;

TEST(core, input_iterable) {
    using namespace epix::utils;

    std::vector<int> v{1, 2, 3};
    input_iterable<int> it(v);

    int sum1 = 0;
    for (auto x : it) sum1 += x;
    EXPECT_EQ(sum1, 6);

    // reuse same iterable
    int sum2 = 0;
    for (auto x : it) sum2 += x;
    EXPECT_EQ(sum2, 6);

    // copy the iterable and iterate both
    auto it_copy = it;
    int s1 = 0, s2 = 0;
    for (auto x : it) s1 += x;
    for (auto x : it_copy) s2 += x;
    EXPECT_EQ(s1, 6);
    EXPECT_EQ(s2, 6);
}

TEST(core, input_iterable_views_transform_filter) {
    using namespace epix::utils;

    std::vector<int> v{1, 2, 3, 4, 5};

    // transform view (temporary) - owned by iterable
    auto tv = std::views::transform(v, [](int a) { return a * 2; });
    input_iterable<int> tit(tv);
    int tsum = 0;
    for (auto x : tit) tsum += x;
    EXPECT_EQ(tsum, (1 + 2 + 3 + 4 + 5) * 2);

    // filter view (borrowed) - lvalue view
    auto fv = std::views::filter(v, [](int a) { return (a % 2) == 1; });
    input_iterable<int> fit(fv);
    int fsum = 0;
    for (auto x : fit) fsum += x;
    EXPECT_EQ(fsum, 1 + 3 + 5);

    // temporary owning range (rvalue vector)
    input_iterable<int> oit(std::vector<int>{7, 8, 9});
    int osum = 0;
    for (auto x : oit) osum += x;
    EXPECT_EQ(osum, 7 + 8 + 9);
}

TEST(core, input_iterable_istream_view_single_pass) {
    using namespace epix::utils;

    std::istringstream iss("10 20 30");
    std::ranges::istream_view<int> iv(iss);

    // borrowed iterator over istream_view (single-pass behaviour)
    input_iterable<int> iit(iv);

    int s = 0;
    for (auto x : iit) s += x;
    EXPECT_EQ(s, 60);

    // reusing the iterable should still work because factory owns the view or iterators
    int s2 = 0;
    for (auto x : iit) s2 += x;
    // istream_view is single-pass; second iteration over same view yields 0
    // but input_iterable should support reuse when possible; we accept either 0 or 60 depending on underlying range
    EXPECT_TRUE(s2 == 0 || s2 == 60);
}
