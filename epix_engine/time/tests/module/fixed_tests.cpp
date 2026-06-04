#include <gtest/gtest.h>
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.time;

using namespace epix::time;
using namespace std::chrono_literals;

TEST(TimeFixed, DefaultTimestep) {
    Time<Fixed> t;
    EXPECT_EQ(t.timestep(), Time<Fixed>::DEFAULT_TIMESTEP);
}

TEST(TimeFixed, SetTimestep) {
    Time<Fixed> t;

    t.set_timestep(500ms);
    EXPECT_EQ(t.timestep(), 500ms);

    t.set_timestep_seconds(0.25);
    EXPECT_EQ(t.timestep(), 250ms);

    t.set_timestep_hz(8.0);
    EXPECT_EQ(t.timestep(), 125ms);
}

TEST(TimeFixed, FromDuration) {
    auto t = Time<Fixed>::from_duration(100ms);
    EXPECT_EQ(t.timestep(), 100ms);
}

TEST(TimeFixed, FromSeconds) {
    auto t = Time<Fixed>::from_seconds(2.0);
    EXPECT_EQ(t.timestep(), 2s);
}

TEST(TimeFixed, FromHz) {
    auto t = Time<Fixed>::from_hz(4.0);
    EXPECT_EQ(t.timestep(), 250ms);
}

TEST(TimeFixed, Expend) {
    auto t = Time<Fixed>::from_seconds(2.0);

    EXPECT_EQ(t.delta(), 0ns);
    EXPECT_EQ(t.elapsed(), 0ns);

    t.accumulate_overstep(1s);
    EXPECT_EQ(t.overstep(), 1s);
    EXPECT_NEAR(t.overstep_fraction(), 0.5f, 1e-5f);
    EXPECT_NEAR(t.overstep_fraction_f64(), 0.5, 1e-12);

    EXPECT_FALSE(t.expend());
    EXPECT_EQ(t.delta(), 0ns);
    EXPECT_EQ(t.elapsed(), 0ns);
    EXPECT_EQ(t.overstep(), 1s);

    t.accumulate_overstep(1s);
    EXPECT_EQ(t.overstep(), 2s);
    EXPECT_NEAR(t.overstep_fraction(), 1.0f, 1e-5f);

    EXPECT_TRUE(t.expend());
    EXPECT_EQ(t.delta(), 2s);
    EXPECT_EQ(t.elapsed(), 2s);
    EXPECT_EQ(t.overstep(), 0ns);

    EXPECT_FALSE(t.expend());
    EXPECT_EQ(t.delta(), 2s);
    EXPECT_EQ(t.elapsed(), 2s);

    t.accumulate_overstep(1s);
    EXPECT_EQ(t.overstep(), 1s);
    EXPECT_FALSE(t.expend());
}

TEST(TimeFixed, ExpendMultiple) {
    auto t = Time<Fixed>::from_seconds(2.0);

    t.accumulate_overstep(7s);
    EXPECT_EQ(t.overstep(), 7s);

    EXPECT_TRUE(t.expend());
    EXPECT_EQ(t.elapsed(), 2s);
    EXPECT_EQ(t.overstep(), 5s);

    EXPECT_TRUE(t.expend());
    EXPECT_EQ(t.elapsed(), 4s);
    EXPECT_EQ(t.overstep(), 3s);

    EXPECT_TRUE(t.expend());
    EXPECT_EQ(t.elapsed(), 6s);
    EXPECT_EQ(t.overstep(), 1s);

    EXPECT_FALSE(t.expend());
    EXPECT_EQ(t.elapsed(), 6s);
    EXPECT_EQ(t.overstep(), 1s);
}

TEST(TimeFixed, DiscardOverstep) {
    auto t = Time<Fixed>::from_seconds(1.0);

    t.accumulate_overstep(3s);
    EXPECT_EQ(t.overstep(), 3s);

    t.discard_overstep(1s);
    EXPECT_EQ(t.overstep(), 2s);

    t.discard_overstep(5s);
    EXPECT_EQ(t.overstep(), 0ns);
}

TEST(TimeFixed, AsGeneric) {
    auto t = Time<Fixed>::from_seconds(1.0);
    t.accumulate_overstep(2s);
    t.expend();

    auto g = t.as_generic();
    EXPECT_EQ(g.delta(), 1s);
    EXPECT_EQ(g.elapsed(), 1s);
}

