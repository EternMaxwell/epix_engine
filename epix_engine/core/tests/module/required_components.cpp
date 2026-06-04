#include <gtest/gtest.h>
#ifndef EPIX_IMPORT_STD
#include <memory>
#include <string>
#include <type_traits>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.core;

using namespace epix::core;

namespace {
struct ManualRoot {};
struct ManualRequired {
    int value;
};

struct StaticRequired {
    int value;
};
struct StaticRoot {
    static void register_required_components(Components& components) {
        components.register_required<StaticRoot>([] { return StaticRequired{17}; });
    }
};

struct IdHookRequired {
    int value;
};
struct IdHookRoot {
    static void register_required_components(Components& components, TypeId self) {
        components.register_required(self, [] { return IdHookRequired{23}; });
    }
};

struct TransitiveSparseRequired {
    std::string value;
};
struct TransitiveMiddle {
    int value;
    static void register_required_components(Components& components) {
        components.register_required<TransitiveMiddle>([] { return TransitiveSparseRequired{"leaf"}; });
    }
};
struct TransitiveRoot {
    static void register_required_components(Components& components) {
        components.register_required<TransitiveRoot>([] { return TransitiveMiddle{42}; });
    }
};
struct ExplicitBundleRequired {
    int value;
};
struct ExplicitBundleRoot {
    inline static int required_constructor_calls = 0;

    static void register_required_components(Components& components) {
        components.register_required<ExplicitBundleRoot>([] {
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
    static void register_required_components(Components& components) {
        components.register_required<RecursiveMiddleRequired>([] { return RecursiveSharedRequired{11}; });
    }
};
struct RecursiveRootRequired {
    static void register_required_components(Components& components) {
        components.register_required<RecursiveRootRequired>([] { return RecursiveMiddleRequired{22}; });
        components.register_required<RecursiveRootRequired>([] { return RecursiveSharedRequired{33}; });
    }
};
}  // namespace

template <>
struct epix::core::sparse_component<TransitiveSparseRequired> : std::true_type {};

TEST(core, required_components_manual_registration) {
    World world(0, std::make_shared<TypeRegistry>());

    world.components_mut().register_required<ManualRoot>([] { return ManualRequired{11}; });

    auto entity = world.spawn(ManualRoot{}).id();
    auto ref    = world.get_entity(entity).value();
    ASSERT_TRUE(ref.contains<ManualRoot>());
    ASSERT_TRUE(ref.contains<ManualRequired>());
    EXPECT_EQ(ref.get<ManualRequired>()->get().value, 11);
}

TEST(core, required_components_static_registration) {
    World world(0, std::make_shared<TypeRegistry>());

    auto entity = world.spawn(StaticRoot{}).id();
    auto ref    = world.get_entity(entity).value();
    ASSERT_TRUE(ref.contains<StaticRequired>());
    EXPECT_EQ(ref.get<StaticRequired>()->get().value, 17);
}

TEST(core, required_components_static_registration_with_type_id) {
    World world(0, std::make_shared<TypeRegistry>());

    auto entity = world.spawn(IdHookRoot{}).id();
    auto ref    = world.get_entity(entity).value();
    ASSERT_TRUE(ref.contains<IdHookRequired>());
    EXPECT_EQ(ref.get<IdHookRequired>()->get().value, 23);
}

TEST(core, required_components_transitive_sparse_registration) {
    World world(0, std::make_shared<TypeRegistry>());

    auto entity = world.spawn(TransitiveRoot{}).id();
    auto ref    = world.get_entity(entity).value();
    ASSERT_TRUE(ref.contains<TransitiveMiddle>());
    ASSERT_TRUE(ref.contains<TransitiveSparseRequired>());
    EXPECT_EQ(ref.get<TransitiveMiddle>()->get().value, 42);
    EXPECT_EQ(ref.get<TransitiveSparseRequired>()->get().value, "leaf");
}

TEST(core, required_components_keep_explicit_bundle_component) {
    World world(0, std::make_shared<TypeRegistry>());
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

TEST(core, required_components_recursive_registration_uses_least_depth_constructor) {
    World world(0, std::make_shared<TypeRegistry>());

    auto entity = world.spawn(RecursiveRootRequired{}).id();
    auto ref    = world.get_entity(entity).value();
    ASSERT_TRUE(ref.contains<RecursiveMiddleRequired>());
    ASSERT_TRUE(ref.contains<RecursiveSharedRequired>());
    EXPECT_EQ(ref.get<RecursiveMiddleRequired>()->get().value, 22);
    EXPECT_EQ(ref.get<RecursiveSharedRequired>()->get().value, 33);
}
