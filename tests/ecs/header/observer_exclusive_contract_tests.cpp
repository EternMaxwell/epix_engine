#include <gtest/gtest.h>

#include <epix/ecs/system/commands.hpp>
#include <stdexcept>

namespace ecs = epix::ecs;
namespace {
struct ExclusiveProbe {};
struct WrappedWorld {
    ecs::World& world;
    static WrappedWorld from_param(ecs::World& world) { return {world}; }
};
}  // namespace

TEST(ObserverExclusiveContract, MutableWorldSystemIsExclusiveBeforeAndAfterInitialization) {
    ecs::World world{2};
    auto system = ecs::make_system_unique([](ecs::World&) {});
    EXPECT_TRUE(system->is_exclusive());
    system->initialize(world);
    EXPECT_TRUE(system->is_exclusive());
}

TEST(ObserverExclusiveContract, ReadOnlyWorldDoesNotBecomeExclusive) {
    ecs::World world{2};
    auto system = ecs::make_system_unique([](const ecs::World&) {});
    EXPECT_FALSE(system->is_exclusive());
    system->initialize(world);
    EXPECT_FALSE(system->is_exclusive());
}

TEST(ObserverExclusiveContract, MutableWorldObserverIsRejectedWithoutCreatingAnEntity) {
    ecs::World world{2};
    const auto before = world.entities().used_count();
    EXPECT_THROW(world.add_observer([](ecs::On<ecs::Add, ExclusiveProbe>, ecs::World&) {}), std::logic_error);
    EXPECT_EQ(world.entities().used_count(), before);
}

TEST(ObserverExclusiveContract, TupleAndFromParamPropagateExclusiveMetadata) {
    ecs::World world{2};
    auto nested = ecs::make_system_unique([](std::tuple<std::tuple<ecs::World&>>) {});
    auto wrapped = ecs::make_system_unique([](WrappedWorld) {});
    auto optional = ecs::make_system_unique([](std::optional<std::reference_wrapper<ecs::World>>) {});
    auto param_set = ecs::make_system_unique([](ecs::ParamSet<ecs::World&>) {});
    for (auto* system : {nested.get(), wrapped.get(), optional.get(), param_set.get()}) {
        EXPECT_TRUE(system->is_exclusive());
        system->initialize(world);
        EXPECT_TRUE(system->is_exclusive());
    }
    const auto before = world.entities().used_count();
    EXPECT_THROW(world.add_observer([](ecs::On<ecs::Add, ExclusiveProbe>, WrappedWorld) {}), std::logic_error);
    EXPECT_THROW(world.add_observer([](ecs::On<ecs::Add, ExclusiveProbe>,
                                      std::tuple<std::tuple<ecs::World&>>) {}), std::logic_error);
    EXPECT_EQ(world.entities().used_count(), before);
}

TEST(ObserverExclusiveContract, ReadOnlyWorldIsAllowedInAnObserver) {
    ecs::World world{2};
    unsigned count = 0;
    world.add_observer([&](ecs::On<ecs::Add, ExclusiveProbe> event, const ecs::World& current) {
        EXPECT_TRUE(current.entity(event->entity).contains<ExclusiveProbe>());
        ++count;
    });
    world.spawn(ExclusiveProbe{});
    EXPECT_EQ(count, 1u);
}

TEST(ObserverExclusiveContract, DeferredRejectionDoesNotReserveAnEntity) {
    ecs::World world{2};
    auto system = ecs::make_system_unique([](ecs::Commands commands) {
        EXPECT_THROW(commands.add_observer([](ecs::On<ecs::Add, ExclusiveProbe>, ecs::World&) {}), std::logic_error);
    });
    system->initialize(world);
    const auto before = world.entities().used_count();
    EXPECT_TRUE(system->run({}, world));
    EXPECT_EQ(world.entities().used_count(), before);
}
