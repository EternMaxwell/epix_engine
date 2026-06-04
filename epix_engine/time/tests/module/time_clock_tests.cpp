#include <gtest/gtest.h>
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.time;

using namespace epix::time;
using namespace std::chrono_literals;

TEST(TimeClock, InitialState) {
    Time<> t;
    EXPECT_EQ(t.wrap_period(), Time<>::DEFAULT_WRAP_PERIOD);
    EXPECT_EQ(t.delta(), 0ns);
    EXPECT_EQ(t.delta_secs(), 0.0f);
    EXPECT_EQ(t.delta_secs_f64(), 0.0);
    EXPECT_EQ(t.elapsed(), 0ns);
    EXPECT_EQ(t.elapsed_secs(), 0.0f);
    EXPECT_EQ(t.elapsed_secs_f64(), 0.0);
    EXPECT_EQ(t.elapsed_wrapped(), 0ns);
    EXPECT_EQ(t.elapsed_secs_wrapped(), 0.0f);
    EXPECT_EQ(t.elapsed_secs_wrapped_f64(), 0.0);
}

TEST(TimeClock, AdvanceBy) {
    Time<> t;

    t.advance_by(250ms);
    EXPECT_EQ(t.delta(), 250ms);
    EXPECT_FLOAT_EQ(t.delta_secs(), 0.25f);
    EXPECT_DOUBLE_EQ(t.delta_secs_f64(), 0.25);
    EXPECT_EQ(t.elapsed(), 250ms);
    EXPECT_FLOAT_EQ(t.elapsed_secs(), 0.25f);
    EXPECT_DOUBLE_EQ(t.elapsed_secs_f64(), 0.25);

    t.advance_by(500ms);
    EXPECT_EQ(t.delta(), 500ms);
    EXPECT_FLOAT_EQ(t.delta_secs(), 0.5f);
    EXPECT_DOUBLE_EQ(t.delta_secs_f64(), 0.5);
    EXPECT_EQ(t.elapsed(), 750ms);
    EXPECT_FLOAT_EQ(t.elapsed_secs(), 0.75f);
    EXPECT_DOUBLE_EQ(t.elapsed_secs_f64(), 0.75);

    t.advance_by(0ns);
    EXPECT_EQ(t.delta(), 0ns);
    EXPECT_FLOAT_EQ(t.delta_secs(), 0.0f);
    EXPECT_DOUBLE_EQ(t.delta_secs_f64(), 0.0);
    EXPECT_EQ(t.elapsed(), 750ms);
    EXPECT_FLOAT_EQ(t.elapsed_secs(), 0.75f);
    EXPECT_DOUBLE_EQ(t.elapsed_secs_f64(), 0.75);
}

TEST(TimeClock, AdvanceTo) {
    Time<> t;

    t.advance_to(250ms);
    EXPECT_EQ(t.delta(), 250ms);
    EXPECT_FLOAT_EQ(t.delta_secs(), 0.25f);
    EXPECT_EQ(t.elapsed(), 250ms);

    t.advance_to(750ms);
    EXPECT_EQ(t.delta(), 500ms);
    EXPECT_FLOAT_EQ(t.delta_secs(), 0.5f);
    EXPECT_EQ(t.elapsed(), 750ms);

    t.advance_to(750ms);
    EXPECT_EQ(t.delta(), 0ns);
    EXPECT_FLOAT_EQ(t.delta_secs(), 0.0f);
    EXPECT_EQ(t.elapsed(), 750ms);
}

TEST(TimeClock, Wrapping) {
    Time<> t;
    t.set_wrap_period(3s);

    t.advance_by(2s);
    EXPECT_EQ(t.elapsed_wrapped(), 2s);
    EXPECT_FLOAT_EQ(t.elapsed_secs_wrapped(), 2.0f);
    EXPECT_DOUBLE_EQ(t.elapsed_secs_wrapped_f64(), 2.0);

    t.advance_by(2s);
    EXPECT_EQ(t.elapsed_wrapped(), 1s);
    EXPECT_FLOAT_EQ(t.elapsed_secs_wrapped(), 1.0f);
    EXPECT_DOUBLE_EQ(t.elapsed_secs_wrapped_f64(), 1.0);

    t.advance_by(2s);
    EXPECT_EQ(t.elapsed_wrapped(), 0ns);
    EXPECT_FLOAT_EQ(t.elapsed_secs_wrapped(), 0.0f);
    EXPECT_DOUBLE_EQ(t.elapsed_secs_wrapped_f64(), 0.0);

    t.advance_by(3250ms);
    EXPECT_EQ(t.elapsed_wrapped(), 250ms);
    EXPECT_FLOAT_EQ(t.elapsed_secs_wrapped(), 0.25f);
    EXPECT_DOUBLE_EQ(t.elapsed_secs_wrapped_f64(), 0.25);
}

TEST(TimeClock, WrappingChange) {
    Time<> t;
    t.set_wrap_period(5s);

    t.advance_by(8s);
    EXPECT_EQ(t.elapsed_wrapped(), 3s);
    EXPECT_FLOAT_EQ(t.elapsed_secs_wrapped(), 3.0f);

    t.set_wrap_period(2s);
    EXPECT_EQ(t.elapsed_wrapped(), 3s);

    t.advance_by(0ns);
    EXPECT_EQ(t.elapsed_wrapped(), 0ns);
    EXPECT_FLOAT_EQ(t.elapsed_secs_wrapped(), 0.0f);
}

TEST(TimeClock, AsGeneric) {
    Time<> t;
    t.advance_by(500ms);

    auto g = t.as_generic();
    EXPECT_EQ(g.delta(), 500ms);
    EXPECT_EQ(g.elapsed(), 500ms);
    EXPECT_EQ(g.wrap_period(), t.wrap_period());
}

