#include <gtest/gtest.h>
#ifndef EPIX_IMPORT_STD
#include <memory>
#include <string>
#include <type_traits>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.ecs;

using namespace epix::ecs;

namespace {
struct ManualRoot {};
struct ManualRequired {
    int value;
};

struct StaticRequired {
    int value;
};
struct StaticRoot {
    static void register_required_components(RequiredComponentsRegistrator& components) {
        components.register_required<StaticRequired>([] { return StaticRequired{17}; });
    }
};

struct IdHookRequired {
    int value;
};
struct IdHookRoot {
    static void register_required_components(TypeId self, RequiredComponentsRegistrator& components) {
        components.register_required<IdHookRequired>([self] { return IdHookRequired{23}; });
    }
};

struct TransitiveSparseRequired {
    std::string value;
};
struct TransitiveMiddle {
    int value;
    static void register_required_components(RequiredComponentsRegistrator& components) {
        components.register_required<TransitiveSparseRequired>([] { return TransitiveSparseRequired{"leaf"}; });
    }
};
struct TransitiveRoot {
    static void register_required_components(RequiredComponentsRegistrator& components) {
        components.register_required<TransitiveMiddle>([] { return TransitiveMiddle{42}; });
    }
};
struct ExplicitBundleRequired {
    int value;
};
struct ExplicitBundleRoot {
    inline static int required_constructor_calls = 0;

    static void register_required_components(RequiredComponentsRegistrator& components) {
        components.register_required<ExplicitBundleRequired>([] {
            ++ExplicitBundleRoot::required_constructor_calls;
            return ExplicitBundleRequired{-1};
        });
    }
};
struct RecursiveSharedRequired {
    int value;
};
struct RecursiveMiddleRequired {
    int value;
    static void register_required_components(RequiredComponentsRegistrator& components) {
        components.register_required<RecursiveSharedRequired>([] { return RecursiveSharedRequired{11}; });
    }
};
struct RecursiveRootRequired {
    static void register_required_components(RequiredComponentsRegistrator& components) {
        components.register_required<RecursiveMiddleRequired>([] { return RecursiveMiddleRequired{22}; });
        components.register_required<RecursiveSharedRequired>([] { return RecursiveSharedRequired{33}; });
    }
};
struct QueuedFlushRequired {
    int value;
};
struct QueuedFlushRoot {
    static void register_required_components(RequiredComponentsRegistrator& components) {
        components.register_required<QueuedFlushRequired>([] { return QueuedFlushRequired{19}; });
    }
};
}  // namespace

template <>
struct epix::ecs::sparse_component<TransitiveSparseRequired> : std::true_type {};

TEST(ecs, required_components_manual_registration) {
    World world(WorldId(0));

    world.register_required_components_with<ManualRoot>([] { return ManualRequired{11}; });

    auto entity = world.spawn(ManualRoot{}).id();
    auto ref    = world.get_entity(entity).value();
    ASSERT_TRUE(ref.contains<ManualRoot>());
    ASSERT_TRUE(ref.contains<ManualRequired>());
    EXPECT_EQ(ref.get<ManualRequired>()->get().value, 11);
}

TEST(ecs, required_components_static_registration) {
    World world(WorldId(0));

    auto entity = world.spawn(StaticRoot{}).id();
    auto ref    = world.get_entity(entity).value();
    ASSERT_TRUE(ref.contains<StaticRequired>());
    EXPECT_EQ(ref.get<StaticRequired>()->get().value, 17);
}

TEST(ecs, required_components_static_registration_with_type_id) {
    World world(WorldId(0));

    auto entity = world.spawn(IdHookRoot{}).id();
    auto ref    = world.get_entity(entity).value();
    ASSERT_TRUE(ref.contains<IdHookRequired>());
    EXPECT_EQ(ref.get<IdHookRequired>()->get().value, 23);
}

TEST(ecs, required_components_transitive_sparse_registration) {
    World world(WorldId(0));

    auto entity = world.spawn(TransitiveRoot{}).id();
    auto ref    = world.get_entity(entity).value();
    ASSERT_TRUE(ref.contains<TransitiveMiddle>());
    ASSERT_TRUE(ref.contains<TransitiveSparseRequired>());
    EXPECT_EQ(ref.get<TransitiveMiddle>()->get().value, 42);
    EXPECT_EQ(ref.get<TransitiveSparseRequired>()->get().value, "leaf");
}

TEST(ecs, required_components_keep_explicit_bundle_component) {
    World world(WorldId(0));
    ExplicitBundleRoot::required_constructor_calls = 0;

    auto entity = world
                      .spawn(make_bundle<ExplicitBundleRoot, ExplicitBundleRequired>(std::tuple{ExplicitBundleRoot{}},
                                                                                     std::tuple{99}))
                      .id();
    auto ref    = world.get_entity(entity).value();
    ASSERT_TRUE(ref.contains<ExplicitBundleRoot>());
    ASSERT_TRUE(ref.contains<ExplicitBundleRequired>());
    EXPECT_EQ(ref.get<ExplicitBundleRequired>()->get().value, 99);
    EXPECT_EQ(ExplicitBundleRoot::required_constructor_calls, 0);
}

TEST(ecs, required_components_direct_registration_overrides_inherited_constructor) {
    World world(WorldId(0));

    auto entity = world.spawn(RecursiveRootRequired{}).id();
    auto ref    = world.get_entity(entity).value();
    ASSERT_TRUE(ref.contains<RecursiveMiddleRequired>());
    ASSERT_TRUE(ref.contains<RecursiveSharedRequired>());
    EXPECT_EQ(ref.get<RecursiveMiddleRequired>()->get().value, 22);
    EXPECT_EQ(ref.get<RecursiveSharedRequired>()->get().value, 33);
}

TEST(ecs, components_register_required_components_typed_api) {
    struct Root {};
    struct Required {
        int value;
    };

    World world(WorldId(0));
    auto registrator = world.registrator();
    TypeId root      = registrator.register_component<Root>();
    TypeId required  = registrator.register_component<Required>();
    ASSERT_TRUE(
        world.components_mut().register_required_components<Required>(root, required, [] { return Required{13}; }));

    auto entity = world.spawn(Root{}).id();
    EXPECT_EQ(world.entity(entity).get<Required>()->get().value, 13);
}

TEST(ecs, runtime_required_components_propagate_to_existing_requirees) {
    struct Root {};
    struct Middle {};
    struct Leaf {};
    struct Marker {
        int value;
    };

    World world(WorldId(0));
    world.register_required_components_with<Root>([] { return Middle{}; });
    world.register_required_components_with<Middle>([] { return Leaf{}; });
    world.register_required_components_with<Leaf>([] { return Marker{7}; });

    auto entity = world.spawn(Root{}).id();
    auto ref    = world.entity(entity);
    EXPECT_EQ(ref.get<Marker>()->get().value, 7);
}

TEST(ecs, required_components_use_depth_first_precedence) {
    struct Root {};
    struct Left {};
    struct LeftLeaf {};
    struct Right {};
    struct Counter {
        int value;
    };

    World world(WorldId(0));
    world.register_required_components_with<Root>([] { return Left{}; });
    world.register_required_components_with<Root>([] { return Right{}; });
    world.register_required_components_with<Left>([] { return LeftLeaf{}; });
    world.register_required_components_with<LeftLeaf>([] { return Counter{0}; });
    world.register_required_components_with<Right>([] { return Counter{1}; });

    auto entity = world.spawn(Root{}).id();
    EXPECT_EQ(world.entity(entity).get<Counter>()->get().value, 0);
}

TEST(ecs, runtime_required_components_reject_duplicate_cycle_and_existing_archetype) {
    struct A {};
    struct B {};
    struct C {};

    World world(WorldId(0));
    ASSERT_TRUE(world.try_register_required_components_with<A>([] { return B{}; }));
    EXPECT_EQ(world.try_register_required_components_with<A>([] { return B{}; }).error().kind,
              RequiredComponentsErrorKind::DuplicateRegistration);
    EXPECT_EQ(world.try_register_required_components_with<B>([] { return A{}; }).error().kind,
              RequiredComponentsErrorKind::CyclicRequirement);

    world.spawn(C{});
    EXPECT_EQ(world.try_register_required_components_with<C>([] { return B{}; }).error().kind,
              RequiredComponentsErrorKind::ArchetypeExists);
}

TEST(ecs, flush_applies_queued_component_registrations) {
    World world(WorldId(0));

    TypeId root = world.queued_registrator().queue_register_component<QueuedFlushRoot>();
    ASSERT_EQ(world.components().num_queued(), 1);
    EXPECT_FALSE(world.components().get_info(root).has_value());

    world.flush();

    EXPECT_EQ(world.components().num_queued(), 0);
    ASSERT_TRUE(world.components().get_info(root).has_value());
    auto required = world.components().get_id<QueuedFlushRequired>();
    ASSERT_TRUE(required.has_value());
    ASSERT_TRUE(world.components().get_required_components(root).has_value());
    EXPECT_TRUE(world.components().get_required_components(root)->get().contains(*required));
}
