#include <gtest/gtest.h>

#include <epix/camera.hpp>

TEST(camera_projection, standalone_projection_plugin_updates_frusta) {
    using namespace epix::camera;
    using namespace epix::ecs;

    auto app = epix::app::App::create();
    app.add_plugins(CameraProjectionPlugin{});

    const epix::transform::GlobalTransform transform{.matrix = glm::translate(glm::mat4(1.0f), {2.0f, 0.0f, 0.0f})};
    const Projection projection = Projection::perspective();
    const Entity entity = app.world_mut().spawn(transform, projection, Frustum{}).id();

    app.update();

    const auto& frustum = app.world().entity(entity).get<Frustum>()->get();
    const auto expected = projection.compute_frustum(transform);
    EXPECT_EQ(frustum.planes, expected.planes);
}

TEST(camera_projection, camera_plugin_registers_child_plugins) {
    auto app = epix::app::App::create();
    app.add_plugins(epix::camera::CameraPlugin{});

    EXPECT_TRUE(app.get_plugin<epix::camera::CameraProjectionPlugin>().has_value());
    EXPECT_TRUE(app.get_plugin<epix::camera::VisibilityPlugin>().has_value());
    EXPECT_TRUE(app.get_plugin<epix::camera::VisibilityRangePlugin>().has_value());
}

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

TEST(camera_visibility, visibility_bookkeeping_bypasses_change_detection) {
    using namespace epix::camera;
    using namespace epix::ecs;

    static_assert(SetViewVisibility<Mut<ViewVisibility>>);

    World world(WorldId(2));
    const Entity entity = world.spawn(ViewVisibility::hidden()).id();
    Schedule reset_schedule(ScheduleLabel::from_type<CameraProjectionPlugin>());
    reset_schedule.add_systems(into(reset_view_visibility));

    // Hidden -> visible is an observable transition.
    world.increment_change_tick();
    auto view_visibility = world.entity_mut(entity).get_mut<ViewVisibility>().value();
    set_view_visible(view_visibility);
    const Tick visible_tick = world.entity(entity).get_ticks<ViewVisibility>()->modified;

    // The frame-to-frame scratch update and a continuing visible state must
    // preserve the component's last semantic-change tick.
    reset_schedule.execute(world);
    EXPECT_EQ(world.entity(entity).get_ticks<ViewVisibility>()->modified.get(), visible_tick.get());
    world.increment_change_tick();
    view_visibility = world.entity_mut(entity).get_mut<ViewVisibility>().value();
    set_view_visible(view_visibility);
    EXPECT_TRUE(world.entity(entity).get<ViewVisibility>()->get().get());
    EXPECT_EQ(world.entity(entity).get_ticks<ViewVisibility>()->modified.get(), visible_tick.get());

    struct Bookkeeping {
        int value = 0;
    };
    const Entity bookkeeping_entity = world.spawn(Bookkeeping{}).id();
    const Tick bookkeeping_tick = world.entity(bookkeeping_entity).get_ticks<Bookkeeping>()->modified;
    world.increment_change_tick();
    world.entity_mut(bookkeeping_entity).get_mut<Bookkeeping>()->bypass_change_detection().value = 7;
    EXPECT_EQ(world.entity(bookkeeping_entity).get<Bookkeeping>()->get().value, 7);
    EXPECT_EQ(world.entity(bookkeeping_entity).get_ticks<Bookkeeping>()->modified.get(), bookkeeping_tick.get());
}
