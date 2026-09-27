#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/material.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

#include <exception>
#include <utility>

using namespace epix::app;
using namespace epix::assets;
using namespace epix::render;

namespace app = epix::app;
namespace ecs = epix::ecs;
namespace pbr = epix::pbr;

namespace {

struct IoTaskPoolInit {
    IoTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4)});
    }
} g_io_task_pool_init;

void add_render_prerequisites(App& app) {
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{})
        .add_plugins(FrameCountPlugin{})
        .add_plugins(view::ViewPlugin{})
        .add_plugins(GlobalsPlugin{});
}

/** @brief A stand-in material type: the registry is untyped, as Bevy's is, so the
 *  systems can be driven before the 3D `Material` counterpart exists. */
struct StandInMaterial {};

}  // namespace

/** @brief Extraction fills the registry for visible meshes, empties it for
 *  invisible ones, and the sweeps keep the tick story straight (Bevy
 *  `extract_mesh_materials`, `early_sweep_material_instances` and
 *  `late_sweep_material_instances`, `bevy_pbr/src/material.rs:698-800`).
 *
 *  The two behaviours worth pinning are the ones a re-implementation gets wrong:
 *  visibility decides insertion *and* removal in the same pass, and the late sweep
 *  advances the change tick exactly once so that the next frame's early sweeps can
 *  tell "updated this frame" from "left over". */
TEST(MaterialExtract, VisibilityDecidesRecordsAndTheSweepsAdvanceTheTick) {
    App application = App::create();
    application.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(application);
    try {
        application.add_plugins(RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = application.get_sub_app_mut(Render);
    ASSERT_TRUE(render_app.has_value());

    using Material = StandInMaterial;
    const auto handle = Handle<Material>{AssetId<Material>::invalid()};
    epix::camera::ViewVisibility visible;
    visible.set_visible();
    const epix::ecs::Entity lit = application.world_mut().spawn(pbr::MeshMaterial3d<Material>{handle}, visible,
                                                        epix::mesh::Mesh3d{}).id();
    const epix::ecs::Entity dark =
        application.world_mut().spawn(pbr::MeshMaterial3d<Material>{handle}, epix::camera::ViewVisibility::hidden(),
                              epix::mesh::Mesh3d{}).id();

    // The plugin registers the systems and creates the registry, so this covers both
    // its asset registration and the ordering it installs.
    application.add_plugins(pbr::MaterialPlugin<Material>{});

    auto render_sub = application.take_sub_app(Render);
    ASSERT_TRUE(render_sub);
    // The registry is created by the plugin, which is what this checks: nothing here
    // inserts it by hand.
    render_sub->extract(application);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });

    const auto& registry = render_sub->world().resource<pbr::RenderMaterialInstances>();
    const auto lit_record = registry.instances.find(sync_world::MainEntity{lit});
    ASSERT_NE(lit_record, registry.instances.end()) << "a visible mesh records which material it uses";
    EXPECT_EQ(lit_record->second.asset_id, UntypedAssetId{handle.id()});
    EXPECT_FALSE(registry.instances.contains(sync_world::MainEntity{dark}))
        << "and an invisible one never gets a record";
    EXPECT_EQ(registry.current_change_tick.get(), 1u) << "the late sweep advances the tick exactly once";

    // Turning the visible mesh invisible removes its record in the same pass - the
    // query observes the visibility change, so no sweep is needed for it.
    application.world_mut().entity_mut(lit).remove<epix::camera::ViewVisibility>();
    application.world_mut().entity_mut(lit).insert(epix::camera::ViewVisibility::hidden());
    render_sub->extract(application);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });
    const auto& after = render_sub->world().resource<pbr::RenderMaterialInstances>();
    EXPECT_FALSE(after.instances.contains(sync_world::MainEntity{lit}))
        << "an invisible mesh has no record";
    EXPECT_EQ(after.current_change_tick.get(), 2u) << "and the tick advanced once more";
}
