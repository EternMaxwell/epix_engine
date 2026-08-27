#include <gtest/gtest.h>

#include <epix/camera.hpp>

TEST(camera_visibility, inherited_visibility_follows_parent_children_hierarchy) {
    using namespace epix::camera;
    using namespace epix::ecs;

    World world(WorldId(1));
    world.register_required_components<Visibility, InheritedVisibility>();

    const Entity root = world.spawn(Visibility::hidden()).id();
    const Entity inherited_child = world.spawn(Visibility::inherited(), Parent{root}).id();
    const Entity explicitly_visible_child = world.spawn(Visibility::visible(), Parent{root}).id();
    const Entity grandchild = world.spawn(Visibility::inherited(), Parent{inherited_child}).id();

    // Run through the scheduler: propagation keeps ordinary query-level access and change filters.
    Schedule schedule(ScheduleLabel::from_type<VisibilitySystems>());
    schedule.add_systems(into(visibility_propagate_system));
    schedule.execute(world);

    EXPECT_FALSE(world.entity(root).get<InheritedVisibility>()->get().get());
    EXPECT_FALSE(world.entity(inherited_child).get<InheritedVisibility>()->get().get());
    EXPECT_TRUE(world.entity(explicitly_visible_child).get<InheritedVisibility>()->get().get());
    EXPECT_FALSE(world.entity(grandchild).get<InheritedVisibility>()->get().get());

    world.entity_mut(root).insert(Visibility::visible());
    schedule.execute(world);

    EXPECT_TRUE(world.entity(inherited_child).get<InheritedVisibility>()->get().get());
    EXPECT_TRUE(world.entity(grandchild).get<InheritedVisibility>()->get().get());
}
