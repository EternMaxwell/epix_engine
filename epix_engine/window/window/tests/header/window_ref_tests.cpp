#include <gtest/gtest.h>
#include <epix/window.hpp>

TEST(WindowRef, NormalizesPrimaryAndExplicitEntity) {
    const epix::ecs::Entity primary_entity{.uid = 7};
    const epix::ecs::Entity direct_entity{.uid = 42};
    const ::epix::window::WindowRef primary;
    EXPECT_TRUE(std::holds_alternative<::epix::window::WindowRef::Primary>(primary));
    EXPECT_FALSE(primary.normalize(std::nullopt).has_value());
    const auto normalized_primary = primary.normalize(primary_entity);
    ASSERT_TRUE(normalized_primary.has_value());
    EXPECT_EQ(normalized_primary->entity(), primary_entity);

    const ::epix::window::WindowRef direct{::epix::window::WindowRef::Entity{direct_entity}};
    const auto normalized_direct = direct.normalize(primary_entity);
    ASSERT_TRUE(normalized_direct.has_value());
    EXPECT_EQ(normalized_direct->entity(), direct_entity);
}
