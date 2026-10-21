#include <gtest/gtest.h>

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/pbr_plugin.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

#include <vector>

namespace {
namespace ecs = epix::ecs;
namespace pbr = epix::pbr;
namespace render = epix::render;

struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPoolBuilder{}.num_threads(4).build());
    }
} io_task_pool_init;

// Use the real plugin/render world, not hand-registered replacement callbacks.
// No windows/cameras/render passes are needed for these ECS lifecycle events.
class LightViewLifecycle : public testing::Test {
   protected:
    epix::app::App app = epix::app::App::create();
    ecs::World* world = nullptr;

    void SetUp() override {
        app.add_events<epix::window::WindowClosed>();
        app.add_plugins(epix::time::TimePlugin{}, epix::camera::CameraPlugin{}, epix::assets::AssetPlugin{},
                        epix::image::ImagePlugin{}, epix::mesh::MeshPlugin{}, epix::transform::TransformPlugin{},
                        epix::shader::ShaderPlugin{}, render::FrameCountPlugin{});
        ASSERT_NO_THROW(app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true}));
        app.add_plugins(epix::core_graph::CoreGraphPlugin{}, pbr::PbrPlugin{.supports_storage_buffers = true});
        app.resource_scope([&](epix::app::internal::Plugins& plugins) { plugins.ready_all(app); });
        auto& render_app = app.get_sub_app_mut(render::Render)->get();
        world = &render_app.world_mut();
        render_app.run_schedule(render::RenderStartup);
    }

    void seed(ecs::Entity light, ecs::Entity camera, std::vector<ecs::Entity> views) {
        pbr::LightViewEntities map;
        map.entities.emplace(camera, std::move(views));
        world->entity_mut(light).insert(std::move(map));
        world->flush();
    }
};

TEST_F(LightViewLifecycle, DirectionalAdditionAttachesAnEmptyMap) {
    const auto light = world->spawn(pbr::ExtractedDirectionalLight{}).id();
    world->flush();
    const auto map = world->entity(light).get<pbr::LightViewEntities>();
    ASSERT_TRUE(map);
    EXPECT_TRUE(map->get().entities.empty());
}

TEST_F(LightViewLifecycle, PointAdditionAttachesAnEmptyMap) {
    const auto light = world->spawn(pbr::ExtractedPointLight{}).id();
    world->flush();
    const auto map = world->entity(light).get<pbr::LightViewEntities>();
    ASSERT_TRUE(map);
    EXPECT_TRUE(map->get().entities.empty());
}

TEST_F(LightViewLifecycle, AddingBothExtractedLightTypesAttachesOneMap) {
    const auto light = world->spawn(pbr::ExtractedPointLight{}, pbr::ExtractedDirectionalLight{}).id();
    world->flush();
    EXPECT_TRUE(world->entity(light).contains<pbr::LightViewEntities>());
}

TEST_F(LightViewLifecycle, ReplacingAnExistingLightRetainsTheMapAndViews) {
    const auto camera = world->spawn().id();
    const auto view = world->spawn().id();
    const auto light = world->spawn(pbr::ExtractedDirectionalLight{}).id();
    // Seed independently so this control is not hidden by a missing Add path.
    seed(light, camera, {view});
    world->entity_mut(light).insert(pbr::ExtractedDirectionalLight{.illuminance = 123.0f});
    world->flush();
    const auto map = world->entity(light).get<pbr::LightViewEntities>();
    ASSERT_TRUE(map);
    ASSERT_TRUE(map->get().entities.contains(camera));
    EXPECT_EQ(map->get().entities.at(camera), (std::vector{view}));
    EXPECT_TRUE(world->get_entity(view));
}

TEST_F(LightViewLifecycle, AddingAnotherLightTypeResetsTheMapWithoutRemovingOldViews) {
    const auto camera = world->spawn().id();
    const auto view = world->spawn().id();
    const auto light = world->spawn(pbr::ExtractedDirectionalLight{}).id();
    seed(light, camera, {view});
    world->entity_mut(light).insert(pbr::ExtractedPointLight{});
    world->flush();
    const auto map = world->entity(light).get<pbr::LightViewEntities>();
    ASSERT_TRUE(map);
    // Pinned Add unconditionally inserts an empty map, not insert_if_new.
    EXPECT_TRUE(map->get().entities.empty());
    // Replacement is not Remove: native map-replacement does not retire views.
    EXPECT_TRUE(world->get_entity(view));
}

TEST_F(LightViewLifecycle, RemovingEitherTypeRemovesMapEvenWhenAnotherTypeRemains) {
    const auto camera = world->spawn().id();
    const auto view = world->spawn().id();
    const auto light = world->spawn(pbr::ExtractedDirectionalLight{}, pbr::ExtractedPointLight{}).id();
    seed(light, camera, {view});
    world->entity_mut(light).remove<pbr::ExtractedPointLight>();
    world->flush();
    EXPECT_TRUE(world->entity(light).contains<pbr::ExtractedDirectionalLight>());
    EXPECT_FALSE(world->entity(light).contains<pbr::LightViewEntities>());
    EXPECT_FALSE(world->get_entity(view));
}

TEST_F(LightViewLifecycle, RemovingMapRetiresViewsAndToleratesAlreadyGoneViews) {
    const auto camera = world->spawn().id();
    const auto view = world->spawn().id();
    const auto gone = world->spawn().id();
    const auto light = world->spawn(pbr::ExtractedPointLight{}).id();
    seed(light, camera, {view, gone});
    world->entity_mut(gone).despawn();
    EXPECT_NO_THROW(world->entity_mut(light).remove<pbr::LightViewEntities>());
    world->flush();
    EXPECT_TRUE(world->get_entity(light));
    EXPECT_FALSE(world->get_entity(view));
}

TEST_F(LightViewLifecycle, DespawningLightRetiresItsMappedViews) {
    const auto camera = world->spawn().id();
    const auto view = world->spawn().id();
    const auto light = world->spawn(pbr::ExtractedPointLight{}).id();
    seed(light, camera, {view});
    world->entity_mut(light).despawn();
    world->flush();
    EXPECT_FALSE(world->get_entity(light));
    EXPECT_FALSE(world->get_entity(view));
}

TEST(LightViewLifecycleControl, BareWorldDoesNotAttachMapsWithoutPluginRegistration) {
    ecs::World world{2};
    const auto light = world.spawn(pbr::ExtractedDirectionalLight{}, pbr::ExtractedPointLight{}).id();
    world.flush();
    EXPECT_FALSE(world.entity(light).contains<pbr::LightViewEntities>());
}
}  // namespace
