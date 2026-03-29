#include <gtest/gtest.h>

import std;
import epix.time;

using namespace epix::time;
using namespace std::chrono_literals;

TEST(Stopwatch, InitialState) {
    Stopwatch sw;
    EXPECT_EQ(sw.elapsed(), 0ns);
    EXPECT_FLOAT_EQ(sw.elapsed_secs(), 0.0f);
    EXPECT_FALSE(sw.is_paused());
}

TEST(Stopwatch, Tick) {
    Stopwatch sw;
    sw.tick(1s);
    EXPECT_EQ(sw.elapsed(), 1s);
    EXPECT_FLOAT_EQ(sw.elapsed_secs(), 1.0f);
}

TEST(Stopwatch, PausePreventsTick) {
    Stopwatch sw;
    sw.tick(1s);
    EXPECT_FLOAT_EQ(sw.elapsed_secs(), 1.0f);

    sw.pause();
    EXPECT_TRUE(sw.is_paused());
    sw.tick(1s);
    EXPECT_FLOAT_EQ(sw.elapsed_secs(), 1.0f);

    sw.unpause();
    EXPECT_FALSE(sw.is_paused());
    sw.tick(1s);
    EXPECT_FLOAT_EQ(sw.elapsed_secs(), 2.0f);
}

TEST(Stopwatch, Reset) {
    Stopwatch sw;
    sw.tick(5s);
    EXPECT_EQ(sw.elapsed(), 5s);

    sw.reset();
    EXPECT_EQ(sw.elapsed(), 0ns);
    EXPECT_FALSE(sw.is_paused());
}

TEST(Stopwatch, ResetKeepsPauseState) {
    Stopwatch sw;
    sw.pause();
    sw.tick(1s);
    sw.reset();
    EXPECT_TRUE(sw.is_paused());
    EXPECT_EQ(sw.elapsed(), 0ns);
}

TEST(Stopwatch, SetElapsed) {
    Stopwatch sw;
    sw.set_elapsed(3s);
    EXPECT_EQ(sw.elapsed(), 3s);
    EXPECT_FLOAT_EQ(sw.elapsed_secs(), 3.0f);
}
