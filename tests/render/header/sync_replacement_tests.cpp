#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <epix/render.hpp>

namespace {
namespace ecs = epix::ecs;
namespace render = epix::render;
namespace sync = render::sync_world;
struct Synced { int value = 0; };
struct DerivedArtifact { int value = 73; };
struct Extra {};

class SyncReplacement : public testing::Test {
 protected:
    epix::app::App app = epix::app::App::create();
    ecs::World render_world{2};
    ecs::Entity main_entity;
    ecs::Entity initial_render;
    void SetUp() override {
        app.add_plugins(render::SyncComponentPlugin<Synced>{});
        main_entity = app.world_mut().spawn(Synced{1}).id();
        sync::entity_sync_system(app.world_mut(), render_world);
        initial_render = render_entity();
        render_world.entity_mut(initial_render).insert(DerivedArtifact{});
    }
    ecs::Entity render_entity() {
        return app.world().entity(main_entity).get<sync::RenderEntity>()->get().id();
    }
    void expect_retained() {
        EXPECT_TRUE(app.world().resource<sync::PendingSyncEntity>().records.empty());
        sync::entity_sync_system(app.world_mut(), render_world);
        EXPECT_EQ(render_entity(), initial_render);
        ASSERT_TRUE(render_world.get_entity(initial_render));
        EXPECT_EQ(render_world.entity(initial_render).get<DerivedArtifact>()->get().value, 73);
    }
};

TEST_F(SyncReplacement, ReplacingASyncedValuePreservesRenderIdentityAndArtifacts) {
    app.world_mut().entity_mut(main_entity).insert(Synced{2});
    expect_retained();
}

TEST_F(SyncReplacement, BundleTransitionPreservesRenderIdentityAndArtifacts) {
    app.world_mut().entity_mut(main_entity).insert(Synced{2}, Extra{});
    expect_retained();
}

TEST_F(SyncReplacement, ReplacingTheSyncMarkerDoesNotDespawnOrResynchronize) {
    app.world_mut().entity_mut(main_entity).insert(sync::SyncToRenderWorld{});
    expect_retained();
}

TEST_F(SyncReplacement, ActualRemovalStillRecreatesAndClearsDerivedRenderState) {
    app.world_mut().entity_mut(main_entity).remove<Synced>();
    const auto& records = app.world().resource<sync::PendingSyncEntity>().records;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<sync::ComponentRemoved>(records[0]));
    sync::entity_sync_system(app.world_mut(), render_world);
    EXPECT_NE(render_entity(), initial_render);
    EXPECT_FALSE(render_world.get_entity(initial_render));
    EXPECT_FALSE(render_world.entity(render_entity()).contains<DerivedArtifact>());
}
}  // namespace
