#include <gtest/gtest.h>

#include <epix/ecs.hpp>
#include <memory>
#include <stdexcept>

namespace ecs = epix::ecs;

namespace {
struct ExplicitCopy {
    int value = 0;
    ExplicitCopy() = default;
    explicit ExplicitCopy(const ExplicitCopy& other) : value(other.value) {}
    ExplicitCopy(ExplicitCopy&&) = default;
    int operator()() { return ++value; }
};
struct ThrowingCopy {
    ThrowingCopy() = default;
    ThrowingCopy(const ThrowingCopy&) { throw std::runtime_error("copy failed"); }
    ThrowingCopy(ThrowingCopy&&) = default;
    void operator()() {}
};
}  // namespace

TEST(SystemOwnedCallable, NoncopyableCallableRunsWithoutRequiringClone) {
    ecs::World world{2};
    auto system = ecs::make_system_unique([owned = std::make_unique<int>(40)]() mutable { return ++*owned; });
    system->initialize(world);
    const auto first = system->run({}, world);
    const auto second = system->run({}, world);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(*first, 41);
    EXPECT_EQ(*second, 42);
    EXPECT_THROW((void)system->clone(), std::logic_error);
}

TEST(SystemOwnedCallable, CopyableCallableStillClonesIntoAnUninitializedSystem) {
    ecs::World world{2};
    auto system = ecs::make_system_unique([](ecs::Local<int> value) { return ++*value; });
    system->initialize(world);
    EXPECT_EQ(system->run({}, world).value(), 1);
    EXPECT_EQ(system->run({}, world).value(), 2);
    std::unique_ptr<ecs::System<std::tuple<>, int>> copied{system->clone()};
    ASSERT_NE(copied, nullptr);
    EXPECT_FALSE(copied->initialized());
    copied->initialize(world);
    EXPECT_EQ(copied->run({}, world).value(), 1);
}

TEST(SystemOwnedCallable, ExplicitCopyConstructorIsSupported) {
    static_assert(std::is_copy_constructible_v<ExplicitCopy>);
    static_assert(!std::copy_constructible<ExplicitCopy>);
    ecs::World world{2};
    auto system = ecs::make_system_unique(ExplicitCopy{});
    system->initialize(world);
    EXPECT_EQ(system->run({}, world).value(), 1);
    std::unique_ptr<ecs::System<std::tuple<>, int>> copied{system->clone()};
    copied->initialize(world);
    EXPECT_EQ(copied->run({}, world).value(), 2);
}

TEST(SystemOwnedCallable, CopyConstructorExceptionsAreNotReclassified) {
    auto system = ecs::make_system_unique(ThrowingCopy{});
    EXPECT_THROW((void)system->clone(), std::runtime_error);
}
