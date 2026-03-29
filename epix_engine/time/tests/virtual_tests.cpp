#include <gtest/gtest.h>

import std;
import epix.time;

using namespace epix::time;
using namespace std::chrono_literals;

TEST(TimeVirtual, DefaultState) {
    Time<Virtual> t;
    EXPECT_FALSE(t.is_paused());
    EXPECT_FLOAT_EQ(t.relative_speed(), 1.0f);
    EXPECT_EQ(t.max_delta(), Time<Virtual>::DEFAULT_MAX_DELTA);
    EXPECT_EQ(t.delta(), 0ns);
    EXPECT_EQ(t.elapsed(), 0ns);
}

TEST(TimeVirtual, Advance) {
    Time<Virtual> t;

    t.advance_with_raw_delta(125ms);
    EXPECT_EQ(t.delta(), 125ms);
    EXPECT_EQ(t.elapsed(), 125ms);

    t.advance_with_raw_delta(125ms);
    EXPECT_EQ(t.delta(), 125ms);
    EXPECT_EQ(t.elapsed(), 250ms);

    t.advance_with_raw_delta(125ms);
    EXPECT_EQ(t.delta(), 125ms);
    EXPECT_EQ(t.elapsed(), 375ms);

    t.advance_with_raw_delta(125ms);
    EXPECT_EQ(t.delta(), 125ms);
    EXPECT_EQ(t.elapsed(), 500ms);
}

TEST(TimeVirtual, RelativeSpeed) {
    Time<Virtual> t;

    t.advance_with_raw_delta(250ms);
    EXPECT_FLOAT_EQ(t.relative_speed(), 1.0f);
    EXPECT_FLOAT_EQ(t.effective_speed(), 1.0f);
    EXPECT_EQ(t.delta(), 250ms);
    EXPECT_EQ(t.elapsed(), 250ms);

    t.set_relative_speed_f64(2.0);
    EXPECT_FLOAT_EQ(t.relative_speed(), 2.0f);
    EXPECT_FLOAT_EQ(t.effective_speed(), 1.0f);

    t.advance_with_raw_delta(250ms);
    EXPECT_FLOAT_EQ(t.relative_speed(), 2.0f);
    EXPECT_FLOAT_EQ(t.effective_speed(), 2.0f);
    EXPECT_EQ(t.delta(), 500ms);
    EXPECT_EQ(t.elapsed(), 750ms);

    t.set_relative_speed_f64(0.5);
    EXPECT_FLOAT_EQ(t.relative_speed(), 0.5f);
    EXPECT_FLOAT_EQ(t.effective_speed(), 2.0f);

    t.advance_with_raw_delta(250ms);
    EXPECT_FLOAT_EQ(t.relative_speed(), 0.5f);
    EXPECT_FLOAT_EQ(t.effective_speed(), 0.5f);
    EXPECT_EQ(t.delta(), 125ms);
    EXPECT_EQ(t.elapsed(), 875ms);
}

TEST(TimeVirtual, Pause) {
    Time<Virtual> t;

    t.advance_with_raw_delta(250ms);
    EXPECT_FALSE(t.is_paused());
    EXPECT_FALSE(t.was_paused());
    EXPECT_FLOAT_EQ(t.effective_speed(), 1.0f);
    EXPECT_EQ(t.delta(), 250ms);
    EXPECT_EQ(t.elapsed(), 250ms);

    t.pause();
    EXPECT_TRUE(t.is_paused());
    EXPECT_FALSE(t.was_paused());

    t.advance_with_raw_delta(250ms);
    EXPECT_TRUE(t.is_paused());
    EXPECT_TRUE(t.was_paused());
    EXPECT_FLOAT_EQ(t.effective_speed(), 0.0f);
    EXPECT_EQ(t.delta(), 0ns);
    EXPECT_EQ(t.elapsed(), 250ms);

    t.unpause();
    EXPECT_FALSE(t.is_paused());
    EXPECT_TRUE(t.was_paused());

    t.advance_with_raw_delta(250ms);
    EXPECT_FALSE(t.is_paused());
    EXPECT_FALSE(t.was_paused());
    EXPECT_FLOAT_EQ(t.effective_speed(), 1.0f);
    EXPECT_EQ(t.delta(), 250ms);
    EXPECT_EQ(t.elapsed(), 500ms);
}

TEST(TimeVirtual, MaxDelta) {
    Time<Virtual> t;
    t.set_max_delta(500ms);

    t.advance_with_raw_delta(250ms);
    EXPECT_EQ(t.delta(), 250ms);
    EXPECT_EQ(t.elapsed(), 250ms);

    t.advance_with_raw_delta(500ms);
    EXPECT_EQ(t.delta(), 500ms);
    EXPECT_EQ(t.elapsed(), 750ms);

    t.advance_with_raw_delta(750ms);
    EXPECT_EQ(t.delta(), 500ms);
    EXPECT_EQ(t.elapsed(), 1250ms);

    t.set_max_delta(1s);
    EXPECT_EQ(t.max_delta(), 1s);

    t.advance_with_raw_delta(750ms);
    EXPECT_EQ(t.delta(), 750ms);
    EXPECT_EQ(t.elapsed(), 2s);

    t.advance_with_raw_delta(1250ms);
    EXPECT_EQ(t.delta(), 1s);
    EXPECT_EQ(t.elapsed(), 3s);
}

TEST(TimeVirtual, Toggle) {
    Time<Virtual> t;
    EXPECT_FALSE(t.is_paused());

    t.toggle();
    EXPECT_TRUE(t.is_paused());

    t.toggle();
    EXPECT_FALSE(t.is_paused());
}

TEST(TimeVirtual, AsGeneric) {
    Time<Virtual> t;
    t.advance_with_raw_delta(200ms);

    auto g = t.as_generic();
    EXPECT_EQ(g.delta(), 200ms);
    EXPECT_EQ(g.elapsed(), 200ms);
}
