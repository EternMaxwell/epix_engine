#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/mesh_shaders.hpp>
#include <epix/pbr/mesh_instance.hpp>
#include <epix/render/gpu_preprocessing_support.hpp>
#include <epix/pbr/morph.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <epix/pbr/skin.hpp>
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
struct StandInMaterial : pbr::Material<StandInMaterial> {};

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

    // `MeshRenderPlugin` is what creates the resources the mesh path needs; the
    // material plugin creates the registry, and `init_resource` in both is safe
    // because one untyped registry serves every material type.
    // With `use_gpu_instance_buffer_builder` set, the plugin takes Bevy's GPU branch -
    // the resources and systems it creates differ by that flag, so the test states
    // which branch it means.
    application.add_plugins(pbr::MeshRenderPlugin{.use_gpu_instance_buffer_builder = true});
    {
        auto& render_world = render_app->get().world();
        EXPECT_TRUE(render_world.get_resource<pbr::MorphUniforms>().has_value())
            << "the morph uniforms the extraction reads";
        EXPECT_TRUE(render_world.get_resource<pbr::MorphIndices>().has_value());
        EXPECT_TRUE(render_world.get_resource<pbr::RenderMaterialInstances>().has_value());
        EXPECT_TRUE(render_world.get_resource<epix::render::batching::GpuPreprocessingSupport>().has_value())
            << "the adapter capability summary the instance builder branch reads";
        EXPECT_TRUE(render_world.get_resource<pbr::ViewKeyCache>().has_value())
            << "and the per-view key cache the specialization check fills";
        EXPECT_TRUE(render_world.get_resource<pbr::ViewSpecializationTicks>().has_value());

        // The alias is needed because the macro would otherwise see the comma in the
        // template argument list and take it for a second argument.
        using InstanceBuffers = epix::render::batching::BatchedInstanceBuffers<pbr::MeshUniform, pbr::MeshInputUniform>;
        EXPECT_TRUE(render_world.get_resource<InstanceBuffers>().has_value())
            << "the GPU branch's instance buffers";
        EXPECT_TRUE(render_world.get_resource<pbr::RenderMeshInstanceGpuQueues>().has_value());
        EXPECT_TRUE(render_world.get_resource<pbr::MeshesToReextractNextFrame>().has_value())
            << "and the re-extraction set, which the GPU branch creates";
        EXPECT_TRUE(render_world.get_resource<pbr::RenderMeshInstances>().has_value())
            << "the instance record set the mesh path is built on";
        // `SkinUniforms` is built in the plugin's `ready` hook, which the port invokes
        // only from `App::run()` - so a test that drives schedules directly cannot
        // observe it. Asserting it here would assert the harness, not the plugin.
        EXPECT_TRUE(render_world.get_resource<pbr::MeshCullingDataBuffer>().has_value())
            << "the culling buffer, whose type the port already defined";
    }

    // The plugin also creates the specialization-tick resource its extraction
    // system writes, which is what makes the two halves one path.
    EXPECT_TRUE(render_app->get().world().get_resource<pbr::EntitySpecializationTicks>().has_value());

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

    // The sweep drops the specialization bookkeeping of an entity that lost its
    // material, unless its tick was updated this frame - which is what distinguishes
    // "replaced by another material type" from "lost its material"
    // (Bevy `sweep_entities_needing_specialization<M>`, `material.rs:820-880`).

    // The sweep drops the specialization bookkeeping of an entity that lost its
    // material (Bevy `sweep_entities_needing_specialization<M>`, `material.rs:820-880`).
    // Staging it is subtle, and the measurement in batch 131 is what makes it possible:
    // the sweep keeps an entity whose tick was recorded *this run*, so the entity has to
    // settle a frame first (out of the queue), and the removal then has to happen in the
    // same frame as the extraction, since a frame in between is long enough for the
    // removal event to be gone by the time the sweep reads it.
    const ecs::Entity lost =
        application.world_mut().spawn(pbr::MeshMaterial3d<Material>{handle}, visible, epix::mesh::Mesh3d{}).id();
    application.update();
    render_sub->extract(application);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });
    ASSERT_TRUE(render_sub->world().resource<pbr::EntitySpecializationTicks>().entities.contains(
        sync_world::MainEntity{lost}))
        << "the extraction records a tick for the new mesh";

    application.update();  // settle: the entity is no longer queued
    application.world_mut().entity_mut(lost).remove<pbr::MeshMaterial3d<Material>>();
    render_sub->extract(application);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });

    EXPECT_FALSE(render_sub->world().resource<pbr::EntitySpecializationTicks>().entities.contains(
        sync_world::MainEntity{lost}))
        << "and the sweep clears it once the material is gone";
}
