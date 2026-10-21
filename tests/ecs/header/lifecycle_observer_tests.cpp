#include <gtest/gtest.h>
#include <epix/ecs.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace ecs = epix::ecs;
namespace {
struct Trace {
    std::vector<std::string> events;
    std::vector<std::pair<int, int>> old_values;
};
struct A {
    int value = 0;
    static void on_add(ecs::World& world, ecs::HookContext) {
        world.resource_mut<Trace>().events.push_back("a.add");
        world.command_queue().push([](ecs::World& w) { w.resource_mut<Trace>().events.push_back("a.add.command"); });
    }
    static void on_insert(ecs::World& w, ecs::HookContext) { w.resource_mut<Trace>().events.push_back("a.insert"); }
    static void on_replace(ecs::World& w, ecs::HookContext) { w.resource_mut<Trace>().events.push_back("a.replace"); }
    static void on_remove(ecs::World& w, ecs::HookContext) { w.resource_mut<Trace>().events.push_back("a.remove"); }
};
struct B {
    int value = 0;
    static void on_add(ecs::World& world, ecs::HookContext) {
        world.resource_mut<Trace>().events.push_back("b.add");
        world.command_queue().push([](ecs::World& w) { w.resource_mut<Trace>().events.push_back("b.add.command"); });
    }
    static void on_insert(ecs::World& w, ecs::HookContext) { w.resource_mut<Trace>().events.push_back("b.insert"); }
    static void on_replace(ecs::World& w, ecs::HookContext) { w.resource_mut<Trace>().events.push_back("b.replace"); }
    static void on_remove(ecs::World& w, ecs::HookContext) { w.resource_mut<Trace>().events.push_back("b.remove"); }
};
using AB = std::tuple<A, B>;
struct Plain { int value = 0; };
struct Other {};
struct FilterBundle { Plain plain; Other other; };
}  // namespace

template<> struct epix::ecs::sparse_component<Other> : std::true_type {};

template<> struct epix::ecs::Bundle<FilterBundle> {
    static void get_components(FilterBundle& bundle,
                               epix::utils::function_ref<void(epix::utils::function_ref<void(void*)>)> write) {
        auto nested = ecs::make_bundle(std::move(bundle.plain), std::move(bundle.other));
        ecs::Bundle<decltype(nested)>::get_components(nested, write);
    }
    static auto type_ids(const ecs::Components& components) {
        return std::vector{components.get_id<Plain>(), components.get_id<Other>()};
    }
    static auto register_components(ecs::ComponentsRegistrator& registrator) {
        return std::vector{registrator.register_component<Plain>(), registrator.register_component<Other>()};
    }
};

namespace {

class LifecycleObservers : public testing::Test {
   protected:
    ecs::World world{2};
    void SetUp() override {
        world.init_resource<Trace>();
        world.registrator().register_component<A>();
        world.registrator().register_component<B>();
    }
    void install() {
        world.add_observer([](ecs::On<ecs::Add, AB>, ecs::ResMut<Trace> trace, ecs::Commands commands) {
            trace->events.push_back("observe.add");
            commands.queue([](ecs::World& w) { w.resource_mut<Trace>().events.push_back("observe.add.command"); });
        });
        world.add_observer([](ecs::On<ecs::Insert, AB>, ecs::ResMut<Trace> trace) { trace->events.push_back("observe.insert"); });
        world.add_observer([](ecs::On<ecs::Replace, AB> event,
                              ecs::Query<ecs::Item<const A&, const B&>> values, ecs::ResMut<Trace> trace) {
            const auto value = values.get(event->entity);
            if (!value) throw std::runtime_error("old replacement components unavailable");
            const auto& [a, b] = *value;
            trace->events.push_back("observe.replace");
            trace->old_values.emplace_back(a.value, b.value);
        });
        world.add_observer([](ecs::On<ecs::Remove, AB> event,
                              ecs::Query<ecs::Item<const A&, const B&>> values, ecs::ResMut<Trace> trace) {
            const auto value = values.get(event->entity);
            if (!value) throw std::runtime_error("old removed components unavailable");
            const auto& [a, b] = *value;
            trace->events.push_back("observe.remove");
            trace->old_values.emplace_back(a.value, b.value);
        });
    }
};

// Expected traces are from the actual pinned native ECS probe, not inferred
// from this implementation (tmp/ecs-observer-reference-run.log).
TEST_F(LifecycleObservers, AddRunsAfterAllAddHooksAndDefersCommandsPastInsertion) {
    install();
    world.spawn(A{1}, B{2});
    world.flush();
    EXPECT_EQ(world.resource<Trace>().events, (std::vector<std::string>{
        "a.add", "b.add", "observe.add", "a.insert", "b.insert", "observe.insert",
        "a.add.command", "b.add.command", "observe.add.command"}));
}
TEST_F(LifecycleObservers, ReplacementReadsOldValuesBeforeHooksWithoutRemove) {
    install();
    const auto entity = world.spawn(A{1}, B{2}).id();
    world.resource_mut<Trace>().events.clear();
    world.entity_mut(entity).insert(A{10}, B{20});
    EXPECT_EQ(world.resource<Trace>().events, (std::vector<std::string>{
        "observe.replace", "a.replace", "b.replace", "a.insert", "b.insert", "observe.insert"}));
    EXPECT_EQ(world.resource<Trace>().old_values, (std::vector<std::pair<int,int>>{{1,2}}));
}
TEST_F(LifecycleObservers, RemovalReadsOldValuesBeforeBothHookBatches) {
    install();
    const auto entity = world.spawn(A{10}, B{20}).id();
    world.resource_mut<Trace>().events.clear();
    world.entity_mut(entity).remove<A, B>();
    EXPECT_EQ(world.resource<Trace>().events, (std::vector<std::string>{
        "observe.replace", "a.replace", "b.replace", "observe.remove", "a.remove", "b.remove"}));
    EXPECT_EQ(world.resource<Trace>().old_values, (std::vector<std::pair<int,int>>{{10,20},{10,20}}));
}
TEST_F(LifecycleObservers, RepeatedComponentFiltersInvokeOneSystemOncePerEvent) {
    int count = 0;
    world.add_observer([&](ecs::On<ecs::Add, std::tuple<Plain, Plain, Other>>) { ++count; });
    world.spawn(Plain{}, Other{});
    EXPECT_EQ(count, 1);
}
TEST_F(LifecycleObservers, GlobalAndComponentFiltersDoNotMatchUnrelatedChanges) {
    int filtered = 0, global = 0;
    world.add_observer([&](ecs::On<ecs::Add, Plain>) { ++filtered; });
    world.add_observer([&](ecs::On<ecs::Add>) { ++global; });
    world.spawn(Other{});
    world.spawn(Plain{});
    EXPECT_EQ(filtered, 1);
    // Pinned lifecycle archetype flags are enabled by component listeners, not
    // by a global listener alone (actual ecs-observer-reference-global.log).
    EXPECT_EQ(global, 1);
}
TEST_F(LifecycleObservers, MultipleObserversAndOwnerDespawn) {
    int first = 0, second = 0;
    const auto owner = world.add_observer([&](ecs::On<ecs::Add, Plain>) { ++first; }).id();
    world.add_observer([&](ecs::On<ecs::Add, Plain>) { ++second; });
    world.spawn(Plain{});
    world.entity_mut(owner).despawn();
    world.spawn(Plain{});
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 2);
}
TEST_F(LifecycleObservers, RemovingObserverComponentUnregistersAndDropsCaptures) {
    auto lifetime = std::make_shared<int>(0);
    const auto owner = world.add_observer([lifetime](ecs::On<ecs::Add, Plain>) {}).id();
    EXPECT_EQ(lifetime.use_count(), 2);
    world.entity_mut(owner).remove<ecs::Observer>();
    EXPECT_EQ(lifetime.use_count(), 1);
    EXPECT_TRUE(world.get_entity(owner));
    EXPECT_NO_THROW(world.spawn(Plain{}));
}
TEST_F(LifecycleObservers, PersistentSystemStateAndObserverIdentity) {
    std::vector<int> counts;
    std::vector<ecs::Entity> owners;
    const auto owner = world.add_observer([&](ecs::On<ecs::Add, Plain> event, ecs::Local<int> local) {
        counts.push_back(++*local);
        owners.push_back(event.observer());
        EXPECT_EQ(event.components().size(), 1u);
    }).id();
    world.spawn(Plain{}); world.spawn(Plain{});
    EXPECT_EQ(counts, (std::vector{1, 2}));
    EXPECT_EQ(owners, (std::vector{owner, owner}));
}
TEST_F(LifecycleObservers, RegistrationsAreScopedToTheirWorld) {
    ecs::World other{3};
    other.registrator().register_component<Other>();
    int count = 0;
    world.add_observer([&](ecs::On<ecs::Add, Plain>) { ++count; });
    other.spawn(Plain{});
    world.spawn(Other{});
    EXPECT_EQ(count, 0);
    world.spawn(Plain{});
    EXPECT_EQ(count, 1);
}
TEST_F(LifecycleObservers, DespawnObserverSeesComponentsBeforeAllRemovalPhases) {
    std::vector<std::string> events;
    world.add_observer([&](ecs::On<ecs::Despawn, Plain> event, ecs::Query<ecs::Item<const Plain&>> values) {
        EXPECT_TRUE(values.get(event->entity)); events.push_back("despawn");
    });
    world.add_observer([&](ecs::On<ecs::Replace, Plain>) { events.push_back("replace"); });
    world.add_observer([&](ecs::On<ecs::Remove, Plain>) { events.push_back("remove"); });
    const auto entity = world.spawn(Plain{}).id();
    world.entity_mut(entity).despawn();
    EXPECT_EQ(events, (std::vector<std::string>{"despawn", "replace", "remove"}));
}
TEST_F(LifecycleObservers, ThrowingCallbackDoesNotLeaveSystemRunning) {
    int count = 0;
    world.add_observer([&](ecs::On<ecs::Insert, Plain>) {
        if (++count == 1) throw std::runtime_error("observer failure");
    });
    const auto entity = world.spawn().id();
    EXPECT_THROW(world.entity_mut(entity).insert(Plain{}), std::runtime_error);
    EXPECT_NO_THROW(world.entity_mut(entity).insert(Plain{1}));
    EXPECT_EQ(count, 2);
}

TEST_F(LifecycleObservers, NestedTupleAndEngineBundleFiltersUseComponentMetadata) {
    using EngineBundle = decltype(ecs::make_bundle(Plain{}, Other{}));
    int nested_count = 0, engine_count = 0, custom_count = 0;
    world.add_observer([&](ecs::On<ecs::Add, std::tuple<std::tuple<Plain>, std::tuple<Other, Plain>>>) {
        ++nested_count;
    });
    world.add_observer([&](ecs::On<ecs::Add, EngineBundle>) { ++engine_count; });
    world.add_observer([&](ecs::On<ecs::Add, FilterBundle>) { ++custom_count; });
    world.spawn(Plain{});
    world.spawn(Other{});
    world.spawn(Plain{}, Other{});
    EXPECT_EQ(nested_count, 3);
    EXPECT_EQ(engine_count, 3);
    EXPECT_EQ(custom_count, 3);
    EXPECT_FALSE(world.components().get_id<FilterBundle>());
}

TEST_F(LifecycleObservers, DeferredRegistrationReservesIdentityAndPrecedesTheNextCommand) {
    int count = 0;
    ecs::Entity observer = ecs::Entity::PLACEHOLDER;
    auto system = ecs::make_system_unique([&](ecs::Commands commands) {
        observer = commands.add_observer([&](ecs::On<ecs::Add, Plain>) { ++count; }).id();
        EXPECT_TRUE(commands.get_entity(observer));
        EXPECT_FALSE(world.get_entity(observer));
        commands.spawn(Plain{});
    });
    system->initialize(world);
    EXPECT_TRUE(system->run_no_apply({}, world));
    EXPECT_EQ(count, 0);
    system->apply_deferred(world);
    EXPECT_EQ(count, 1);
    EXPECT_TRUE(world.entity(observer).contains<ecs::Observer>());
    world.spawn(Plain{});
    EXPECT_EQ(count, 2);
}

TEST_F(LifecycleObservers, ObserverCanRegisterAnotherObserverThroughCommands) {
    int count = 0;
    world.add_observer([&](ecs::On<ecs::Add, Plain>, ecs::Commands commands) {
        commands.add_observer([&](ecs::On<ecs::Add, Other>) { ++count; });
        commands.spawn(Other{});
    });
    world.spawn(Plain{});
    EXPECT_EQ(count, 1);
}

TEST_F(LifecycleObservers, CommandsResolveLivingReservedButNotStaleEntities) {
    const auto living = world.spawn().id();
    const auto stale = world.spawn().id();
    world.entity_mut(stale).despawn();
    auto system = ecs::make_system_unique([&](ecs::Commands commands) {
        const auto reserved = commands.spawn_empty().id();
        EXPECT_TRUE(commands.get_entity(living));
        EXPECT_TRUE(commands.get_entity(reserved));
        EXPECT_FALSE(commands.get_entity(stale));
        EXPECT_FALSE(commands.get_entity(ecs::Entity::PLACEHOLDER));
    });
    system->initialize(world);
    EXPECT_TRUE(system->run({}, world));
}

TEST_F(LifecycleObservers, MoveOnlyHandlerRetainsStateAndDropsOnceWhenRemoved) {
    struct Owned {
        int count = 0;
        int& drops;
        explicit Owned(int& drops) : drops(drops) {}
        ~Owned() { ++drops; }
    };
    int drops = 0;
    std::vector<int> counts;
    const auto owner = world.spawn(ecs::Observer::create(
        [owned = std::make_unique<Owned>(drops), &counts](ecs::On<ecs::Add, Plain>) {
            counts.push_back(++owned->count);
        })).id();
    world.spawn(Plain{}); world.spawn(Plain{});
    EXPECT_EQ(counts, (std::vector{1, 2}));
    world.entity_mut(owner).remove<ecs::Observer>();
    EXPECT_EQ(drops, 1);
    world.spawn(Plain{});
    EXPECT_EQ(counts, (std::vector{1, 2}));
}

TEST_F(LifecycleObservers, RegisteredSystemsContinueAfterTheirWorldMoves) {
    int count = 0;
    world.add_observer([&](ecs::On<ecs::Add, Plain> event, ecs::Query<ecs::Item<const Plain&>> values,
                          ecs::Commands commands) {
        EXPECT_TRUE(values.get(event->entity));
        ++count;
        commands.spawn(Other{});
    });
    ecs::World moved{std::move(world)};
    moved.spawn(Plain{});
    EXPECT_EQ(count, 1);
    auto query = moved.query<ecs::Item<const Other&>>();
    EXPECT_EQ(std::ranges::distance(query.iter(moved)), 1);
}

TEST_F(LifecycleObservers, FailedCallbackKeepsCommandsPendingUntilItsNextSuccessfulRun) {
    int applied = 0;
    world.add_observer([&](ecs::On<ecs::Insert, Plain>, ecs::Local<bool> first, ecs::Commands commands) {
        commands.queue([&](ecs::World&) { ++applied; });
        if (!*first) { *first = true; throw std::runtime_error("expected observer failure"); }
    });
    const auto entity = world.spawn().id();
    EXPECT_THROW(world.entity_mut(entity).insert(Plain{}), std::runtime_error);
    world.flush();
    EXPECT_EQ(applied, 0);
    world.entity_mut(entity).insert(Plain{});
    EXPECT_EQ(applied, 2);
}

TEST_F(LifecycleObservers, GlobalListenersAloneDoNotEnableLifecycleDispatch) {
    int count = 0;
    world.add_observer([&](ecs::On<ecs::Add>) { ++count; });
    world.spawn();
    world.spawn(Plain{});
    EXPECT_EQ(count, 0);
}

TEST_F(LifecycleObservers, FallibleCallbackPreservesItsErrorAndPendingCommands) {
    int applied = 0;
    world.add_observer([&](ecs::On<ecs::Insert, Plain>, ecs::Local<bool> first,
                          ecs::Commands commands) -> std::expected<void, std::string> {
        commands.queue([&](ecs::World&) { ++applied; });
        if (!*first) { *first = true; return std::unexpected("expected fallible observer error"); }
        return {};
    });
    const auto entity = world.spawn().id();
    try {
        world.entity_mut(entity).insert(Plain{});
        FAIL() << "expected a failed observer result";
    } catch (const ecs::SystemResultError& error) {
        EXPECT_EQ(error.message, "expected fallible observer error");
    }
    world.flush();
    EXPECT_EQ(applied, 0);
    world.entity_mut(entity).insert(Plain{});
    EXPECT_EQ(applied, 2);
}

TEST_F(LifecycleObservers, GlobalListenersFollowArchetypeFlagsEvenForEmptyAffectedComponents) {
    using Seen = std::pair<std::string, std::size_t>;
    std::vector<Seen> events;
    world.add_observer([](ecs::On<ecs::Add, Plain>) {});
    world.add_observer([](ecs::On<ecs::Insert, Plain>) {});
    world.add_observer([&](ecs::On<ecs::Add> event) { events.emplace_back("add", event.components().size()); });
    world.add_observer([&](ecs::On<ecs::Insert> event) { events.emplace_back("insert", event.components().size()); });
    const auto entity = world.spawn(Plain{}).id();
    EXPECT_EQ(events, (std::vector<Seen>{{"add", 1}, {"insert", 1}}));
    events.clear();
    world.entity_mut(entity).insert(Other{});
    EXPECT_EQ(events, (std::vector<Seen>{{"add", 1}, {"insert", 1}}));
    events.clear();
    world.entity_mut(entity).insert_bundle(ecs::internal::InitializeBundle<std::tuple<>, std::tuple<>>{});
    EXPECT_EQ(events, (std::vector<Seen>{{"add", 0}, {"insert", 0}}));
}
}  // namespace
