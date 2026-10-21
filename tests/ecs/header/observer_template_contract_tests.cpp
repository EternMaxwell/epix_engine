#include <gtest/gtest.h>

#include <epix/ecs/observer.hpp>

// The shared declaration constraints work before World/System are complete.
using HeaderCallback = void (*)(epix::ecs::On<epix::ecs::Add>);
static_assert(epix::ecs::valid_observer_system<HeaderCallback>);
static_assert(!epix::ecs::valid_observer_system<int>);

#include <epix/ecs/system/commands.hpp>
#include <epix/ecs/system/local.hpp>
#include <memory>
#include <string>

namespace ecs = epix::ecs;
namespace {
struct Watched {};
struct Incomplete;
struct Immovable {
    Immovable() = delete;
    Immovable(const Immovable&) = delete;
    Immovable& operator=(const Immovable&) = delete;
};
using Event = ecs::On<ecs::Add, Watched>;
struct PlainCallback { void operator()(Event) {} };
struct FallibleCallback { std::expected<void, std::string> operator()(Event) { return {}; } };
struct NoInput { void operator()() {} };
struct WrongInput { void operator()(ecs::In<int>) {} };
struct WrongResult { int operator()(Event) { return 1; } };
struct WrongFallibleResult { std::expected<int, std::string> operator()(Event) { return 1; } };
struct WrongParam { void operator()(Event, int) {} };
struct MoveOnlyCallback {
    std::unique_ptr<int> owned = std::make_unique<int>(0);
    void operator()(Event) { ++*owned; }
};
struct MemberCallback { void run(Event) {} };
struct GenericCallback { template<class T> void operator()(T) {} };
struct AmbiguousCallback { void operator()(Event) {} void operator()() {} };
struct PrivateCallback { private: void operator()(Event) {} };
struct FakeInput { using Event = ecs::Add; using Components = Watched; };
struct FakeInputCallback { void operator()(FakeInput) {} };
struct MismatchedInput {};
struct MismatchedInputCallback { void operator()(MismatchedInput) {} };
}  // namespace

template<> struct epix::ecs::internal::SystemInput<FakeInput> {
    using Param = FakeInput;
    using Input = FakeInput;
    static Param wrap_input(Input input) { return input; }
};
template<> struct epix::ecs::internal::SystemInput<MismatchedInput> {
    using Param = int;
    using Input = int;
    static Param wrap_input(Input input) { return input; }
};

namespace {
template<class F>
concept CanCreate = requires(F&& callback) {
    { ecs::Observer::create(std::forward<F>(callback)) } -> std::same_as<ecs::Observer>;
};
template<class F>
concept CanAddToWorld = requires(ecs::World& world, F&& callback) {
    { world.add_observer(std::forward<F>(callback)) } -> std::same_as<ecs::EntityWorldMut>;
};
template<class F>
concept CanAddThroughCommands = requires(ecs::Commands commands, F&& callback) {
    { commands.add_observer(std::forward<F>(callback)) } -> std::same_as<ecs::EntityCommands>;
};
template<class F>
constexpr bool accepted_by_all = CanCreate<F> && CanAddToWorld<F> && CanAddThroughCommands<F>;
template<class F>
constexpr bool rejected_by_all = !CanCreate<F> && !CanAddToWorld<F> && !CanAddThroughCommands<F>;
template<class E, class B>
concept CanFormOn = requires { typename ecs::On<E, B>; };
template<class F>
concept CanMakeSystem = requires(F&& callback) { ecs::make_system_unique(std::forward<F>(callback)); };
template<class R>
concept CanMatch = requires(ecs::internal::LifecycleObserverCache& cache, R&& components) {
    { cache.matches(ecs::LifecycleEventKind::Add, std::forward<R>(components)) } -> std::same_as<bool>;
};

static_assert(accepted_by_all<PlainCallback>);
static_assert(accepted_by_all<PlainCallback&>);
static_assert(accepted_by_all<FallibleCallback>);
static_assert(accepted_by_all<MoveOnlyCallback>);
static_assert(accepted_by_all<void (*)(Event)>);
static_assert(accepted_by_all<HeaderCallback>);
static_assert(rejected_by_all<MoveOnlyCallback&>);
static_assert(rejected_by_all<const MoveOnlyCallback&>);
static_assert(rejected_by_all<int>);
static_assert(rejected_by_all<NoInput>);
static_assert(rejected_by_all<WrongInput>);
static_assert(rejected_by_all<WrongResult>);
static_assert(rejected_by_all<WrongFallibleResult>);
static_assert(rejected_by_all<WrongParam>);
static_assert(rejected_by_all<FakeInputCallback>);
static_assert(rejected_by_all<decltype(&MemberCallback::run)>);
static_assert(rejected_by_all<GenericCallback>);
static_assert(rejected_by_all<AmbiguousCallback>);
static_assert(rejected_by_all<PrivateCallback>);
static_assert(accepted_by_all<decltype([](Event, ecs::World&) {})>); // Native runtime rejection.
static_assert(ecs::valid_observer_system<PlainCallback>);
static_assert(!ecs::valid_observer_system<FakeInputCallback>);
static_assert(CanMakeSystem<MoveOnlyCallback>);
static_assert(!CanMakeSystem<MoveOnlyCallback&>);
static_assert(!CanMakeSystem<decltype(&MemberCallback::run)>);
static_assert(!CanMakeSystem<MismatchedInputCallback>);
static_assert(CanFormOn<ecs::Add, Watched>);
static_assert(CanFormOn<ecs::Remove, std::tuple<Watched, std::tuple<Watched>>>);
static_assert(CanFormOn<ecs::Add, Immovable>);  // Filters only register metadata.
static_assert(!CanFormOn<int, Watched>);
static_assert(!CanFormOn<ecs::Add, void>);
static_assert(!CanFormOn<ecs::Add, Incomplete>);
static_assert(!CanFormOn<ecs::Add, Watched&>);
static_assert(!CanFormOn<ecs::Add, const Watched>);
static_assert(!CanFormOn<ecs::Add, int[2]>);
static_assert(!CanFormOn<ecs::Add, std::tuple<Watched, void>>);
static_assert(CanMatch<std::vector<ecs::TypeId>&>);
static_assert(CanMatch<const std::vector<ecs::TypeId>&>);
static_assert(CanMatch<std::span<const ecs::TypeId>>);
static_assert(!CanMatch<std::vector<int>&>);
static_assert(!CanMatch<int>);

TEST(ObserverTemplateContract, AcceptedCallbacksRemainUsableAtRuntime) {
    ecs::World world{2};
    int count = 0;
    PlainCallback lvalue;
    world.add_observer(lvalue);
    world.add_observer(FallibleCallback{});
    world.add_observer([](ecs::On<ecs::Add, Immovable>) {});
    world.add_observer([state = std::make_unique<int>(0), &count](Event) { count = ++*state; });
    world.spawn(Watched{});
    world.spawn(Watched{});
    EXPECT_EQ(count, 2);
}
}  // namespace
