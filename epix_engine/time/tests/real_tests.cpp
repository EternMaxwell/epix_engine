#include <gtest/gtest.h>

import std;
import epix.time;

using namespace epix::time;
using namespace std::chrono_literals;

TEST(TimeReal, InitialState) {
    Time<Real> t;
    EXPECT_EQ(t.delta(), 0ns);
    EXPECT_EQ(t.elapsed(), 0ns);
    EXPECT_FALSE(t.first_update().has_value());
    EXPECT_FALSE(t.last_update().has_value());
}

TEST(TimeReal, FirstUpdateZeroDelta) {
    Time<Real> t;

    t.update_with_duration(100ms);
    EXPECT_EQ(t.delta(), 0ns);
    EXPECT_EQ(t.elapsed(), 0ns);
    EXPECT_TRUE(t.first_update().has_value());
    EXPECT_TRUE(t.last_update().has_value());
}

TEST(TimeReal, SecondUpdateGivesDelta) {
    Time<Real> t;

    t.update_with_duration(100ms);
    t.update_with_duration(250ms);
    EXPECT_EQ(t.delta(), 250ms);
    EXPECT_EQ(t.elapsed(), 250ms);
}

TEST(TimeReal, MultipleUpdates) {
    Time<Real> t;

    t.update_with_duration(0ms);
    t.update_with_duration(100ms);
    EXPECT_EQ(t.delta(), 100ms);
    EXPECT_EQ(t.elapsed(), 100ms);

    t.update_with_duration(200ms);
    EXPECT_EQ(t.delta(), 200ms);
    EXPECT_EQ(t.elapsed(), 300ms);

    t.update_with_duration(50ms);
    EXPECT_EQ(t.delta(), 50ms);
    EXPECT_EQ(t.elapsed(), 350ms);
}

TEST(TimeReal, UpdateWithInstant) {
    auto start = std::chrono::steady_clock::now();
    Time<Real> t(start);

    t.update_with_instant(start + 0ms);
    EXPECT_EQ(t.delta(), 0ns);
    EXPECT_EQ(t.elapsed(), 0ns);

    t.update_with_instant(start + 500ms);
    EXPECT_EQ(t.delta(), 500ms);
    EXPECT_EQ(t.elapsed(), 500ms);

    t.update_with_instant(start + 800ms);
    EXPECT_EQ(t.delta(), 300ms);
    EXPECT_EQ(t.elapsed(), 800ms);
}

TEST(TimeReal, BackwardInstantClamped) {
    auto start = std::chrono::steady_clock::now();
    Time<Real> t(start);

    t.update_with_instant(start + 100ms);
    t.update_with_instant(start + 500ms);
    EXPECT_EQ(t.delta(), 400ms);

    t.update_with_instant(start + 200ms);
    EXPECT_EQ(t.delta(), 0ns);
    EXPECT_EQ(t.elapsed(), 400ms);
}

TEST(TimeReal, AsGeneric) {
    Time<Real> t;
    t.update_with_duration(0ms);
    t.update_with_duration(500ms);

    auto g = t.as_generic();
    EXPECT_EQ(g.delta(), 500ms);
    EXPECT_EQ(g.elapsed(), 500ms);
}
