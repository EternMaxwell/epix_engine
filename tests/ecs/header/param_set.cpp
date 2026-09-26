#include <gtest/gtest.h>

#include <epix/ecs.hpp>
#include <type_traits>

using namespace epix::ecs;

namespace {
struct ParamSetResourceA {
    int value = 11;
};
struct ParamSetResourceB {
    int value = 22;
};
}  // namespace

// Bevy's `ParamSet` exists for parameters that conflict with each other, so it
// exposes exactly one member per accessor: `p0()..p7()` each take `&mut self`
// and the documentation states "No other parameters may be accessed while this
// one is active" (system_param.rs:728-744). Epix enforces the same one-at-a-time
// discipline by taking an invocable, and deliberately provides no accessor that
// returns several members simultaneously - the previous tuple-returning `get()`
// allowed a read and a write of the same component type to be live at once.
TEST(ParamSet, ExposesOneMemberAtATime) {
    using Params = ParamSet<Res<ParamSetResourceA>, ResMut<ParamSetResourceB>>;
    static_assert(system_param<Params>);

    // Each member is fetched inside its own callback, which is how Bevy's
    // one-at-a-time rule is expressed in C++. There is deliberately no
    // accessor that returns several members at once: every `get` overload
    // requires an explicit index, so `params.get()` has no candidate.
    static_assert(requires(Params& params) {
        params.get<0>([](Res<ParamSetResourceA>) {});
        params.get<1>([](ResMut<ParamSetResourceB>) {});
    });
    static_assert(requires(Params& params) {
        params.get<0>([](auto& a) { (void)a; });
    });
    // Read-only members are also reachable through a const ParamSet.
    static_assert(requires(const ParamSet<Res<ParamSetResourceA>>& params) {
        params.get<0>([](Res<ParamSetResourceA>) {});
    });
}

TEST(ParamSet, AppliesMutationsThroughIndividualMembers) {
    World world(2);
    world.insert_resource(ParamSetResourceA{1});
    world.insert_resource(ParamSetResourceB{2});

    int observed_a = 0;
    auto system =
        make_system_unique([&observed_a](ParamSet<Res<ParamSetResourceA>, ResMut<ParamSetResourceB>> params) {
            params.get<0>([&observed_a](Res<ParamSetResourceA> a) { observed_a = a->value; });
            params.get<1>([](ResMut<ParamSetResourceB> b) { b->value = 7; });
        });
    system->initialize(world);
    ASSERT_TRUE(system->run({}, world).has_value());
    EXPECT_EQ(observed_a, 1);
    EXPECT_EQ(world.resource<ParamSetResourceB>().value, 7);
}
