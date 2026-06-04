#include <gtest/gtest.h>
#ifndef EPIX_IMPORT_STD
#include <cstdint>
#include <limits>
#include <chrono>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.time;

using namespace epix::time;
using namespace std::chrono_literals;

TEST(Timer, NonRepeating) {
    auto t = Timer::from_seconds(10.0f, TimerMode::Once);

    t.tick(250ms);
    EXPECT_FLOAT_EQ(t.elapsed_secs(), 0.25f);
    EXPECT_EQ(t.duration(), std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<float>(10.0f)));
    EXPECT_FALSE(t.is_finished());
    EXPECT_FALSE(t.just_finished());
    EXPECT_EQ(t.times_finished_this_tick(), 0u);
    EXPECT_EQ(t.mode(), TimerMode::Once);
    EXPECT_NEAR(t.fraction(), 0.025f, 1e-5f);
    EXPECT_NEAR(t.fraction_remaining(), 0.975f, 1e-5f);

    t.pause();
    t.tick(500s);
    EXPECT_FLOAT_EQ(t.elapsed_secs(), 0.25f);
    EXPECT_FALSE(t.is_finished());
    EXPECT_FALSE(t.just_finished());
    EXPECT_EQ(t.times_finished_this_tick(), 0u);

    t.unpause();
    t.tick(500s);
    EXPECT_FLOAT_EQ(t.elapsed_secs(), 10.0f);
    EXPECT_TRUE(t.is_finished());
    EXPECT_TRUE(t.just_finished());
    EXPECT_EQ(t.times_finished_this_tick(), 1u);
    EXPECT_FLOAT_EQ(t.fraction(), 1.0f);
    EXPECT_FLOAT_EQ(t.fraction_remaining(), 0.0f);

    t.tick(1s);
    EXPECT_FLOAT_EQ(t.elapsed_secs(), 10.0f);
    EXPECT_TRUE(t.is_finished());
    EXPECT_FALSE(t.just_finished());
    EXPECT_EQ(t.times_finished_this_tick(), 0u);
    EXPECT_FLOAT_EQ(t.fraction(), 1.0f);
}

TEST(Timer, Repeating) {
    auto t = Timer::from_seconds(2.0f, TimerMode::Repeating);

    t.tick(750ms);
    EXPECT_FLOAT_EQ(t.elapsed_secs(), 0.75f);
    EXPECT_FALSE(t.is_finished());
    EXPECT_FALSE(t.just_finished());
    EXPECT_EQ(t.times_finished_this_tick(), 0u);
    EXPECT_EQ(t.mode(), TimerMode::Repeating);

    t.tick(1500ms);
    EXPECT_NEAR(t.elapsed_secs(), 0.25f, 1e-5f);
    EXPECT_TRUE(t.is_finished());
    EXPECT_TRUE(t.just_finished());
    EXPECT_EQ(t.times_finished_this_tick(), 1u);

    t.tick(1s);
    EXPECT_NEAR(t.elapsed_secs(), 1.25f, 1e-5f);
    EXPECT_FALSE(t.is_finished());
    EXPECT_FALSE(t.just_finished());
    EXPECT_EQ(t.times_finished_this_tick(), 0u);
}

TEST(Timer, TimesFinishedRepeating) {
    auto t = Timer::from_seconds(1.0f, TimerMode::Repeating);
    EXPECT_EQ(t.times_finished_this_tick(), 0u);

    t.tick(3500ms);
    EXPECT_EQ(t.times_finished_this_tick(), 3u);
    EXPECT_NEAR(t.elapsed_secs(), 0.5f, 1e-5f);
    EXPECT_TRUE(t.is_finished());
    EXPECT_TRUE(t.just_finished());

    t.tick(200ms);
    EXPECT_EQ(t.times_finished_this_tick(), 0u);
}

TEST(Timer, TimesFinishedOnce) {
    auto t = Timer::from_seconds(1.0f, TimerMode::Once);
    EXPECT_EQ(t.times_finished_this_tick(), 0u);

    t.tick(1500ms);
    EXPECT_EQ(t.times_finished_this_tick(), 1u);

    t.tick(500ms);
    EXPECT_EQ(t.times_finished_this_tick(), 0u);
}

TEST(Timer, RepeatingZeroDuration) {
    Timer t(0ns, TimerMode::Repeating);
    EXPECT_EQ(t.times_finished_this_tick(), 0u);
    EXPECT_EQ(t.elapsed(), 0ns);
    EXPECT_FLOAT_EQ(t.fraction(), 1.0f);

    t.tick(1s);
    EXPECT_EQ(t.times_finished_this_tick(), std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(t.elapsed(), 0ns);
    EXPECT_FLOAT_EQ(t.fraction(), 1.0f);

    t.tick(2s);
    EXPECT_EQ(t.times_finished_this_tick(), std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(t.elapsed(), 0ns);

    t.reset();
    EXPECT_EQ(t.times_finished_this_tick(), 0u);
    EXPECT_EQ(t.elapsed(), 0ns);
    EXPECT_FLOAT_EQ(t.fraction(), 1.0f);
}

TEST(Timer, AlmostFinishRepeating) {
    auto t = Timer::from_seconds(10.0f, TimerMode::Repeating);

    t.almost_finish();
    EXPECT_FALSE(t.is_finished());
    EXPECT_EQ(t.times_finished_this_tick(), 0u);
    EXPECT_EQ(t.remaining(), 1ns);

    t.tick(1ns);
    EXPECT_TRUE(t.is_finished());
    EXPECT_EQ(t.times_finished_this_tick(), 1u);
}

TEST(Timer, PausedOnce) {
    auto t = Timer::from_seconds(10.0f, TimerMode::Once);

    t.tick(10s);
    EXPECT_TRUE(t.just_finished());
    EXPECT_TRUE(t.is_finished());

    t.pause();
    t.tick(5s);
    EXPECT_FALSE(t.just_finished());
    EXPECT_TRUE(t.is_finished());
}

TEST(Timer, PausedRepeating) {
    auto t = Timer::from_seconds(10.0f, TimerMode::Repeating);

    t.tick(10s);
    EXPECT_TRUE(t.just_finished());
    EXPECT_TRUE(t.is_finished());

    t.pause();
    t.tick(5s);
    EXPECT_FALSE(t.just_finished());
    EXPECT_FALSE(t.is_finished());
}

TEST(Timer, Reset) {
    auto t = Timer::from_seconds(5.0f, TimerMode::Once);
    t.tick(6s);
    EXPECT_TRUE(t.is_finished());

    t.reset();
    EXPECT_FALSE(t.is_finished());
    EXPECT_FALSE(t.just_finished());
    EXPECT_EQ(t.elapsed(), 0ns);
    EXPECT_EQ(t.times_finished_this_tick(), 0u);
}

