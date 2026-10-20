#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <epix/ecs.hpp>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
namespace ecs = epix::ecs;

TEST(CommandQueueLifecycle, ReentrantFlushConsumesTheCurrentCommandOnlyOnce) {
    ecs::World world{2};
    int runs = 0;
    world.command_queue().push([&](ecs::World& w) {
        // Bound the broken baseline's recursion so the regression can report
        // duplicated execution rather than exhausting the process stack.
        if (++runs == 1) w.flush_commands();
    });
    world.flush_commands();
    EXPECT_EQ(runs, 1);
}

TEST(CommandQueueLifecycle, DetachedCommandsFlushNewWorldCommandsBeforeTheirOldTail) {
    ecs::World world{2};
    ecs::internal::CommandQueue detached;
    std::vector<int> order;
    detached.push([&](ecs::World& w) {
        order.push_back(1);
        w.command_queue().push([&](ecs::World&) { order.push_back(2); });
    });
    detached.push([&](ecs::World&) { order.push_back(3); });
    detached.apply(world);
    EXPECT_EQ(order, (std::vector{1, 2, 3}));
}

TEST(CommandQueueLifecycle, WorldCommandsRunBeforeDetachedCommands) {
    ecs::World world{2};
    ecs::internal::CommandQueue detached;
    std::vector<int> order;
    world.command_queue().push([&](ecs::World&) { order.push_back(1); });
    detached.push([&](ecs::World&) { order.push_back(2); });
    detached.apply(world);
    EXPECT_EQ(order, (std::vector{1, 2}));
}

TEST(CommandQueueLifecycle, AQueuedSpawnDoesNotReapplyItsOwningCommand) {
    ecs::World world{2};
    int runs = 0;
    ecs::Entity spawned;
    world.command_queue().push([&](ecs::World& w) {
        if (++runs == 1) spawned = w.spawn(42).id();
    });
    world.flush();
    EXPECT_EQ(runs, 1);
    ASSERT_TRUE(world.get_entity(spawned));
    EXPECT_EQ(world.entity(spawned).get<int>()->get(), 42);
}

TEST(CommandQueueLifecycle, SameQueueNewCommandsRunBeforeTheOldTail) {
    ecs::World world{2};
    std::vector<int> order;
    world.command_queue().push([&](ecs::World& w) {
        order.push_back(1);
        w.command_queue().push([&](ecs::World&) { order.push_back(2); });
    });
    world.command_queue().push([&](ecs::World&) { order.push_back(3); });
    world.flush();
    EXPECT_EQ(order, (std::vector{1, 2, 3}));
}

TEST(CommandQueueLifecycle, FailureConsumesTheFailedCommandAndRetainsOldTailBeforeNewCommands) {
    ecs::World world{2};
    std::vector<int> order;
    world.command_queue().push([&](ecs::World& w) {
        order.push_back(1);
        w.command_queue().push([&](ecs::World&) { order.push_back(3); });
        throw std::runtime_error("expected command failure");
    });
    world.command_queue().push([&](ecs::World&) { order.push_back(2); });
    EXPECT_THROW(world.flush(), std::runtime_error);
    EXPECT_EQ(order, (std::vector{1}));
    world.flush();
    EXPECT_EQ(order, (std::vector{1, 2, 3}));
}

TEST(CommandQueueLifecycle, NestedFailureRecoversTheInnerTailBeforeTheOuterTail) {
    ecs::World world{2};
    std::vector<int> order;
    world.command_queue().push([&](ecs::World& w) {
        order.push_back(1);
        w.command_queue().push([&](ecs::World&) {
            order.push_back(2);
            throw std::runtime_error("expected nested failure");
        });
        w.command_queue().push([&](ecs::World&) { order.push_back(3); });
        w.flush();
    });
    world.command_queue().push([&](ecs::World&) { order.push_back(4); });
    EXPECT_THROW(world.flush(), std::runtime_error);
    EXPECT_EQ(order, (std::vector{1, 2}));
    world.flush();
    EXPECT_EQ(order, (std::vector{1, 2, 3, 4}));
}

TEST(CommandQueueLifecycle, CaughtInnerFailureKeepsRecoveryBeforeOuterTailAndLaterCommands) {
    ecs::World world{2};
    std::vector<int> order;
    world.command_queue().push([&](ecs::World& w) {
        order.push_back(1);
        w.command_queue().push([&](ecs::World&) {
            order.push_back(2);
            throw std::runtime_error("expected caught inner failure");
        });
        w.command_queue().push([&](ecs::World&) { order.push_back(3); });
        EXPECT_THROW(w.flush(), std::runtime_error);
        w.command_queue().push([&](ecs::World&) { order.push_back(5); });
        throw std::runtime_error("expected outer failure");
    });
    world.command_queue().push([&](ecs::World&) { order.push_back(4); });
    EXPECT_THROW(world.flush(), std::runtime_error);
    EXPECT_EQ(order, (std::vector{1, 2}));
    world.flush();
    // Actual pinned Bevy reference: ecs-life-reference-run.log.
    EXPECT_EQ(order, (std::vector{1, 2, 3, 4, 5}));
}

TEST(CommandQueueLifecycle, CaughtInnerFailureRemainsSeparateUntilALaterRootUnwind) {
    ecs::World world{2};
    std::vector<int> order;
    world.command_queue().push([&](ecs::World& w) {
        order.push_back(1);
        w.command_queue().push([&](ecs::World&) {
            order.push_back(2);
            throw std::runtime_error("expected caught inner failure");
        });
        w.command_queue().push([&](ecs::World&) { order.push_back(3); });
        EXPECT_THROW(w.flush(), std::runtime_error);
        w.command_queue().push([&](ecs::World&) { order.push_back(5); });
    });
    world.command_queue().push([&](ecs::World&) { order.push_back(4); });
    EXPECT_NO_THROW(world.flush());
    EXPECT_EQ(order, (std::vector{1, 2, 5, 4}));
    world.command_queue().push([](ecs::World&) { throw std::runtime_error("expected later root failure"); });
    world.command_queue().push([&](ecs::World&) { order.push_back(7); });
    EXPECT_THROW(world.flush(), std::runtime_error);
    world.flush();
    EXPECT_EQ(order, (std::vector{1, 2, 5, 4, 3, 7}));
}

struct Counted {
    int* destroyed;
    ~Counted() { ++*destroyed; }
};

TEST(CommandQueueLifecycle, MoveOnlyCommandsAreDestroyedOnceAcrossFailureAndAppend) {
    ecs::World world{2};
    int destroyed = 0;
    std::vector<int> order;
    auto owner = [&] { return std::unique_ptr<Counted>(new Counted{&destroyed}); };
    world.command_queue().push([payload = owner()](ecs::World&) {
        throw std::runtime_error("expected owned failure");
    });
    world.command_queue().push([&, payload = owner()](ecs::World&) { order.push_back(2); });
    EXPECT_THROW(world.flush(), std::runtime_error);
    EXPECT_EQ(destroyed, 1);
    ecs::internal::CommandQueue extra;
    extra.push([&, payload = owner()](ecs::World&) { order.push_back(3); });
    world.command_queue().append(extra);
    world.flush();
    EXPECT_EQ(order, (std::vector{2, 3}));
    EXPECT_EQ(destroyed, 3);
}

TEST(CommandQueueLifecycle, ConsumedPrefixCompactsCorrectlyAcrossPushAndAppendGrowth) {
    ecs::World world{2};
    int destroyed = 0;
    std::vector<int> order;
    auto owner = [&] { return std::unique_ptr<Counted>(new Counted{&destroyed}); };
    world.command_queue().push([payload = owner()](ecs::World&) {
        throw std::runtime_error("expected prefix failure");
    });
    std::array<int, 64> small{};
    small.front() = 11;
    small.back() = 22;
    world.command_queue().push([&, small, payload = owner()](ecs::World&) {
        EXPECT_EQ(small.front(), 11);
        EXPECT_EQ(small.back(), 22);
        order.push_back(2);
    });
    EXPECT_THROW(world.flush(), std::runtime_error);
    EXPECT_EQ(destroyed, 1);

    std::array<int, 4096> large{};
    large.front() = 33;
    large.back() = 44;
    world.command_queue().push([&, large, payload = owner()](ecs::World&) {
        EXPECT_EQ(large.front(), 33);
        EXPECT_EQ(large.back(), 44);
        order.push_back(3);
    });
    std::array<int, 8192> larger{};
    larger.front() = 55;
    larger.back() = 66;
    ecs::internal::CommandQueue extra;
    extra.push([&, larger, payload = owner()](ecs::World&) {
        EXPECT_EQ(larger.front(), 55);
        EXPECT_EQ(larger.back(), 66);
        order.push_back(4);
    });
    world.command_queue().append(extra);
    EXPECT_EQ(destroyed, 1);
    world.flush();
    EXPECT_EQ(order, (std::vector{2, 3, 4}));
    EXPECT_EQ(destroyed, 4);
}

TEST(CommandQueueLifecycle, DroppingAnUnappliedRecoveredTailDoesNotDestroyTheFailedCommandTwice) {
    int destroyed = 0;
    bool tail_applied = false;
    {
        ecs::World world{2};
        auto owner = [&] { return std::unique_ptr<Counted>(new Counted{&destroyed}); };
        world.command_queue().push([payload = owner()](ecs::World&) {
            throw std::runtime_error("expected abandoned failure");
        });
        world.command_queue().push([&, payload = owner()](ecs::World&) { tail_applied = true; });
        EXPECT_THROW(world.flush(), std::runtime_error);
        EXPECT_EQ(destroyed, 1);
    }
    EXPECT_FALSE(tail_applied);
    EXPECT_EQ(destroyed, 2);
}

struct ThrowingMove {
    static inline bool fail = false;
    std::unique_ptr<Counted> owner;
    explicit ThrowingMove(int& destroyed) : owner(new Counted{&destroyed}) {}
    ThrowingMove(ThrowingMove&& other) {
        if (fail) throw std::runtime_error("expected command move failure");
        owner = std::move(other.owner);
    }
};

TEST(CommandQueueLifecycle, FailedPushConstructionDoesNotLeaveAHoleBeforeTheNextCommand) {
    int destroyed = 0;
    ecs::World world{2};
    std::vector<int> order;
    world.command_queue().push([&](ecs::World&) { order.push_back(1); });
    ThrowingMove::fail = true;
    EXPECT_THROW(world.command_queue().push([payload = ThrowingMove{destroyed}](ecs::World&) {}),
                 std::runtime_error);
    ThrowingMove::fail = false;
    EXPECT_EQ(destroyed, 1);
    world.command_queue().push([&](ecs::World&) { order.push_back(2); });
    world.flush();
    EXPECT_EQ(order, (std::vector{1, 2}));
}

TEST(CommandQueueLifecycle, FailedGrowthDestroysPartialDestinationsWithoutDoubleDroppingSources) {
    int destroyed = 0;
    {
        ecs::World world{2};
        world.command_queue().push([payload = std::unique_ptr<Counted>(new Counted{&destroyed})](ecs::World&) {});
        world.command_queue().push([payload = ThrowingMove{destroyed}](ecs::World&) {});
        const std::array<int, 4096> large{};
        ThrowingMove::fail = true;
        EXPECT_THROW(world.command_queue().push([large](ecs::World&) {}), std::runtime_error);
        ThrowingMove::fail = false;
        EXPECT_EQ(destroyed, 1);
        // A throwing C++ move provides the basic, not strong, guarantee: a
        // successfully moved source may be moved-from, but still drops safely.
    }
    EXPECT_EQ(destroyed, 2);
}

TEST(CommandQueueLifecycle, FailedAppendRetainsDestinationAndCleansPartialMovedObjects) {
    int destroyed = 0;
    int applied = 0;
    {
        ecs::World world{2};
        ecs::internal::CommandQueue other;
        world.command_queue().push([&](ecs::World&) { ++applied; });
        other.push([payload = std::unique_ptr<Counted>(new Counted{&destroyed})](ecs::World&) {});
        other.push([payload = ThrowingMove{destroyed}](ecs::World&) {});
        ThrowingMove::fail = true;
        EXPECT_THROW(world.command_queue().append(other), std::runtime_error);
        ThrowingMove::fail = false;
        EXPECT_EQ(destroyed, 1);
        world.flush();
        EXPECT_EQ(applied, 1);
    }
    EXPECT_EQ(destroyed, 2);
}

TEST(CommandQueueLifecycle, WorldDestructionDropsOwnersStillInSeparateRecovery) {
    // Intentional C++ RAII difference: pinned Bevy leaves separate recovery
    // owners undropped here; Epix destroys them without applying the commands.
    int destroyed = 0;
    {
        ecs::World world{2};
        world.command_queue().push([&](ecs::World& w) {
            w.command_queue().push([](ecs::World&) { throw std::runtime_error("expected caught failure"); });
            w.command_queue().push([owner = std::unique_ptr<Counted>(new Counted{&destroyed})](ecs::World&) {});
            EXPECT_THROW(w.flush(), std::runtime_error);
        });
        world.flush();
        EXPECT_EQ(destroyed, 0);
    }
    EXPECT_EQ(destroyed, 1);
}

template<std::size_t Alignment> struct alignas(Alignment) AlignedCommand {
    // Scalar volatile access avoids an aligned SIMD copy in this diagnostic
    // command's move constructor on the broken baseline. The observed address
    // is checked outside the callback, across the opaque World::flush call.
    std::uintptr_t* volatile observed;
    explicit AlignedCommand(std::uintptr_t& output) : observed(&output) {}
    AlignedCommand(AlignedCommand&& other) noexcept : observed(other.observed) {}
    void operator()(ecs::World&) const { *observed = reinterpret_cast<std::uintptr_t>(this); }
};

template<class T> class PackedCommandAlignment : public testing::Test {};
using CommandAlignments = testing::Types<std::integral_constant<std::size_t, 8>,
                                        std::integral_constant<std::size_t, 32>,
                                        std::integral_constant<std::size_t, 64>>;
TYPED_TEST_SUITE(PackedCommandAlignment, CommandAlignments);

TYPED_TEST(PackedCommandAlignment, SmallPrecedingCommandDoesNotMisalignExecution) {
    ecs::World world{2};
    std::uintptr_t observed = 0;
    world.command_queue().push([](ecs::World&) {});
    world.command_queue().push(AlignedCommand<TypeParam::value>{observed});
    world.flush();
    ASSERT_NE(observed, 0u);
    EXPECT_EQ(observed % TypeParam::value, 0u);
}

TYPED_TEST(PackedCommandAlignment, GrowthAndAppendPreserveExecutionAlignment) {
    ecs::World world{2};
    std::uintptr_t observed = 0;
    world.command_queue().push([](ecs::World&) {});
    const std::array<std::uint8_t, 257> ballast{};
    world.command_queue().push([ballast](ecs::World&) { EXPECT_EQ(ballast.back(), 0); });
    ecs::internal::CommandQueue extra;
    extra.push([](ecs::World&) {});
    extra.push(AlignedCommand<TypeParam::value>{observed});
    world.command_queue().append(extra);
    world.flush();
    ASSERT_NE(observed, 0u);
    EXPECT_EQ(observed % TypeParam::value, 0u);
}

TYPED_TEST(PackedCommandAlignment, ConsumedPrefixRecoveryPreservesExecutionAlignment) {
    ecs::World world{2};
    std::uintptr_t observed = 0;
    world.command_queue().push([](ecs::World&) { throw std::runtime_error("expected prefix failure"); });
    world.command_queue().push(AlignedCommand<TypeParam::value>{observed});
    EXPECT_THROW(world.flush(), std::runtime_error);
    world.command_queue().push([](ecs::World&) {});
    world.flush();
    ASSERT_NE(observed, 0u);
    EXPECT_EQ(observed % TypeParam::value, 0u);
}
}  // namespace
