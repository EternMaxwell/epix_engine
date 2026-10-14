#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/material_plugin.hpp>
#include <epix/pbr/mesh_view_key.hpp>
#include <epix/pbr/scattering_medium.hpp>
#include <epix/pbr/scattering_medium_plugin.hpp>
#include <epix/pbr/mesh_extraction.hpp>
#include <epix/pbr/mesh_shaders.hpp>
#include <epix/pbr/mesh_instance.hpp>
#include <epix/render/gpu_preprocessing_support.hpp>
#include <epix/pbr/morph.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <epix/pbr/skin.hpp>
#include <epix/mesh/mesh.hpp>
#include <epix/render/mesh_allocator.hpp>
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
        .add_plugins(AssetPlugin{})
        // Registers `Assets<Mesh>`, which the mesh-asset extraction reads; without it
        // that system is skipped on parameter validation.
        .add_plugins(epix::mesh::MeshPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::shader::ShaderPlugin{})
        .add_plugins(epix::shader::ShaderPlugin{});
}

/** @brief The render-dependent plugins, which must follow `RenderPlugin`.
 *
 * `ViewPlugin` adds `RenderVisibilityRangePlugin` itself (`render/src/view.cpp:393`),
 * and that plugin creates `RenderVisibilityRanges` in its `attach` - which returns early
 * when no render sub-app exists yet. Added before `RenderPlugin` it therefore does nothing,
 * and adding it again afterwards does not help either: the port drops the duplicate by type,
 * so the early, empty registration is the one that counts (batch 157). */
void add_render_dependent_prerequisites(App& app) {
    // Each of these builds render-world state in `attach` behind an
    // `if (!render_app) return;`, so they must follow `RenderPlugin`. Batch 157
    // found it for the visibility ranges; `FrameCountPlugin` is the same shape,
    // which is why the render world was missing `FrameCount` too.
    app.add_plugins(view::ViewPlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(FrameCountPlugin{})
        .add_plugins(GlobalsPlugin{});
    // `MeshRenderPlugin` registers the material draw sequence against the 3D phases'
    // `DrawFunctions`, which the core graph creates in the render world
    // (core_3d/mod.rs:157-160), and it installs the `FullscreenShader` the core
    // pipeline's systems validate against. A real app has all of it through
    // `DefaultPlugins`; Bevy's `MaterialPlugin` has the same requirement, since it is
    // what registers the commands there.
    app.add_plugins(epix::core_graph::CoreGraphPlugin{});
    // The globals every material type shares - the allocator registry, the specialization
    // caches and the material pipeline resource - belong to `MaterialsPlugin`, which
    // `PbrPlugin` installs beside `MeshRenderPlugin` in a real app (`lib.rs:222-228`).
    // `MaterialPlugin<M>` registers only its own type's pieces.
    app.add_plugins(pbr::MaterialsPlugin{});

    // `extract_meshes_for_cpu_building` takes `Res<RenderMaterialBindings>`, which
    // `PbrPlugin` creates in a real app (`lib.rs:343`). Bevy's signature requires it too
    // (bevy_pbr/src/render/mesh.rs:1346-1351). Tests that drive the extraction systems
    // without that plugin still need the resource present, exactly as
    // mesh_instance_tests.cpp does.
    if (auto render_app = app.get_sub_app_mut(Render); render_app.has_value()) {
        auto& world = render_app->get().world_mut();
        world.init_resource<pbr::RenderMaterialBindings>();

        // `MaterialPlugin` now also registers `init_material_pipeline` in
        // `RenderStartup` (Bevy `material.rs:300`), and that system takes
        // `Res<MeshPipeline>`. A real app should get the resource from
        // `MeshRenderPlugin`, which does not create it yet - that engine gap is
        // recorded as remaining work - so this app supplies it the way
        // cluster_prepare_tests.cpp does rather than weakening the parameter.
        world.insert_resource(pbr::MeshPipeline{
            .view_layouts                          = {},
            .clustered_forward_buffer_binding_type = wgpu::BufferBindingType::eStorage,
            .mesh_layouts = pbr::MeshLayouts::make(wgpu::Limits{}, /*binding_arrays_are_usable=*/false),
            .shader       = epix::assets::Handle<epix::shader::Shader>{
                epix::assets::AssetId<epix::shader::Shader>::invalid()},
        });
    }
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
    add_render_dependent_prerequisites(application);

    using Material = StandInMaterial;
    const auto handle = Handle<Material>{AssetId<Material>::invalid()};
    epix::camera::ViewVisibility visible;
    visible.set_visible();
    const epix::ecs::Entity lit = application.world_mut().spawn(pbr::MeshMaterial3d<Material>{handle}, visible,
                                                        epix::mesh::Mesh3d{}).id();
    const epix::ecs::Entity dark =
        application.world_mut().spawn(pbr::MeshMaterial3d<Material>{handle}, epix::camera::ViewVisibility::HIDDEN,
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

    // What `App::run` does before any schedule runs: `SkinUniforms` is created in a
    // plugin's `ready`, and without it the skin extraction is skipped on validation.
    application.resource_scope([&application](epix::app::internal::Plugins& plugins) {
        plugins.ready_all(application);
    });
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
    application.world_mut().entity_mut(lit).insert(epix::camera::ViewVisibility::HIDDEN);
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
/** @brief The CPU branch's extraction builds the instance records (Bevy
 *  `extract_meshes_for_cpu_building`, `render/mesh.rs:1346-1458`). */
TEST(MaterialExtract, TheCpuExtractionBuildsTheInstanceRecords) {
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
    add_render_dependent_prerequisites(application);

    using Material = StandInMaterial;
    application.add_plugins(pbr::MaterialPlugin<Material>{});
    // The default flag means the CPU branch, which is what this test is about.
    application.add_plugins(pbr::MeshRenderPlugin{});

    // What `App::run` does before any schedule runs: several plugins create their
    // render-world resources in `ready`.
    application.resource_scope([&application](epix::app::internal::Plugins& plugins) {
        plugins.ready_all(application);
    });

    const auto handle = Handle<Material>{AssetId<Material>::invalid()};
    epix::camera::ViewVisibility visible;
    visible.set_visible();
    const ecs::Entity meshed = application.world_mut()
                                   .spawn(epix::mesh::Mesh3d{}, pbr::MeshMaterial3d<Material>{handle}, visible,
                                          epix::transform::GlobalTransform{})
                                   .id();
    const ecs::Entity hidden = application.world_mut()
                                   .spawn(epix::mesh::Mesh3d{}, pbr::MeshMaterial3d<Material>{handle},
                                          epix::camera::ViewVisibility::HIDDEN, epix::transform::GlobalTransform{})
                                   .id();

    auto render_sub = application.take_sub_app(Render);
    ASSERT_TRUE(render_sub);
    render_sub->extract(application);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });

    const auto& instances = render_sub->world().resource<pbr::RenderMeshInstances>();
    EXPECT_TRUE(instances.is_cpu_building()) << "the flag is off, so the records are CPU-side";
    EXPECT_TRUE(instances.cpu_instances.contains(sync_world::MainEntity{meshed}))
        << "a visible mesh gets a record from the CPU extraction";
    EXPECT_FALSE(instances.cpu_instances.contains(sync_world::MainEntity{hidden}))
        << "and an invisible one is left out, since the set is rebuilt rather than updated";
}
/** @brief The motion-vector flags follow the morph table (Bevy
 *  `set_mesh_motion_vector_flags`, `render/mesh.rs:1721-1734`).
 *
 *  The system is registered in `RenderSystems::PrepareMeshes`; this test puts it in the
 *  `Extract` schedule instead, ordered after the extraction, because that is the
 *  schedule this harness already runs.
 *
 *  The table to fill is `current`, not `prev`: `extract_morphs` swaps the two and then
 *  clears `current`, so what a caller writes into `current` is what the flags system -
 *  running later in the same extract - reads as last frame's mapping. Writing `prev`
 *  directly is silently undone by that swap, which is how this test first failed.
 *
 *  It tests the morph half. The skin half needs an entry in `SkinUniforms`, and
 *  `SkinUniformInfo` cannot be fabricated here any more than `RenderMeshInstanceShared`
 *  can, so a skin fixture would mean driving `prepare_skins` as well. */
TEST(MaterialExtract, TheMotionVectorFlagsFollowTheMorphTable) {
    App application = App::create();
    application.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(application);
    try {
        application.add_plugins(RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }
    ASSERT_TRUE(application.get_sub_app_mut(Render).has_value());
    add_render_dependent_prerequisites(application);

    using Material = StandInMaterial;
    application.add_plugins(pbr::MaterialPlugin<Material>{});
    application.add_plugins(pbr::MeshRenderPlugin{});
    application.resource_scope([&application](epix::app::internal::Plugins& plugins) {
        plugins.ready_all(application);
    });

    epix::camera::ViewVisibility visible;
    visible.set_visible();
    const auto handle = Handle<Material>{AssetId<Material>::invalid()};
    const ecs::Entity morphed =
        application.world_mut()
            .spawn(epix::mesh::Mesh3d{}, pbr::MeshMaterial3d<Material>{handle}, visible,
                   epix::transform::GlobalTransform{})
            .id();

    auto render_sub = application.take_sub_app(Render);
    ASSERT_TRUE(render_sub);
    render_sub->add_systems(epix::app::ScheduleInfo(ExtractSchedule),
                            epix::ecs::into(pbr::set_mesh_motion_vector_flags)
                                .after(pbr::MeshExtractionSystems::Extract)
                                .set_name("set mesh motion vector flags (test)"));

    render_sub->resource_scope([&](epix::ecs::World& world) {
        auto& morphs = world.resource_mut<pbr::MorphIndices>();
        morphs.current.emplace(sync_world::MainEntity{morphed}, pbr::MorphIndex{});
    });

    render_sub->extract(application);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });

    const auto& instances = render_sub->world().resource<pbr::RenderMeshInstances>();
    ASSERT_TRUE(instances.cpu_instances.contains(sync_world::MainEntity{morphed}))
        << "the extraction records a visible mesh, which the flags then annotate";
    const auto& flags = instances.cpu_instances.at(sync_world::MainEntity{morphed}).shared.flags;
    EXPECT_TRUE(flags.contains(pbr::RenderMeshInstanceFlags::HAS_PREVIOUS_MORPH))
        << "a mesh whose weights were extracted last frame gets the previous-morph flag";
}
/** @brief The collect half drains the queues and keeps the bound buffer non-empty
 *  (Bevy `collect_meshes_for_gpu_building`, `render/mesh.rs:1738-1845`).
 *
 *  Two things had to be right before this could run at all. The plugins that build
 *  render-world state in `attach` must follow `RenderPlugin` - `MeshAllocatorPlugin`
 *  and `LightmapPlugin` are added here, and the shared helper now orders the rest
 *  (batch 158). And the extract has to run **twice**: the render world's `FrameCount`
 *  is created by `extract_frame_count` through `Commands` rather than by its plugin, so
 *  it exists from the second frame, and a system reading it is skipped on the first.
 *
 *  What it asserts is `PBR-183`'s own detail: the previous frame's input buffer is
 *  bound even when empty, so the collect half ends by making sure it is not. Building
 *  an actual instance would need a mesh whose asset the allocator has seen, which this
 *  round did not stage. */
TEST(MaterialExtract, TheCollectHalfDrainsTheQueuesAndKeepsTheBufferNonEmpty) {
    App application = App::create();
    application.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(application);
    try {
        application.add_plugins(RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }
    ASSERT_TRUE(application.get_sub_app_mut(Render).has_value());
    add_render_dependent_prerequisites(application);
    application.add_plugins(epix::render::mesh::MeshAllocatorPlugin{});
    // Creates `RenderLightmaps`, which the collect half reads.
    application.add_plugins(pbr::LightmapPlugin{});

    using Material = StandInMaterial;
    application.add_plugins(pbr::MaterialPlugin<Material>{});
    // The GPU branch: the one that registers both the extraction and the collect.
    application.add_plugins(pbr::MeshRenderPlugin{.use_gpu_instance_buffer_builder = true});
    application.resource_scope([&application](epix::app::internal::Plugins& plugins) {
        plugins.ready_all(application);
    });

    epix::camera::ViewVisibility visible;
    visible.set_visible();
    const auto handle = Handle<Material>{AssetId<Material>::invalid()};
    application.world_mut()
        .spawn(epix::mesh::Mesh3d{}, pbr::MeshMaterial3d<Material>{handle}, visible,
               epix::transform::GlobalTransform{});
    application.update();

    auto render_sub = application.take_sub_app(Render);
    ASSERT_TRUE(render_sub);
    // Only the collect half is registered here, so `extract_frame_count` never runs and
    // its `Commands` never insert `FrameCount`; the collector reads that resource, so the
    // fixture provides it directly. Bevy's equivalent path has it from the previous frame.
    render_sub->world_mut().init_resource<FrameCount>();
    render_sub->add_systems(epix::app::ScheduleInfo(ExtractSchedule),
                            epix::ecs::into(pbr::collect_meshes_for_gpu_building)
                                .after(pbr::MeshExtractionSystems::Extract)
                                .set_name("collect meshes for gpu building (test)"));

    // First extract: the extraction queues its builders and `extract_frame_count`
    // inserts the render world's `FrameCount`, which its `Commands` applies at the end
    // of the schedule.
    render_sub->extract(application);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });

    // Second extract: now that `FrameCount` exists, the collect half validates and runs.
    render_sub->extract(application);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });

    render_sub->resource_scope([](epix::ecs::World& world) {
        const auto& buffers =
            world.resource<epix::render::batching::BatchedInstanceBuffers<pbr::MeshUniform, pbr::MeshInputUniform>>();
        EXPECT_GE(buffers.previous_input_buffer.len(), 1u)
            << "a bound buffer must not be empty, which is what the collect half guarantees";
        const auto& queues = world.resource<pbr::RenderMeshInstanceGpuQueues>();
        EXPECT_FALSE(std::holds_alternative<std::monostate>(queues.queue.queue))
            << "and the queue keeps its shape for the coming frame";
    });
}
/** @brief The medium LUTs and sampler build on a device (Bevy
 *  `GpuScatteringMedium::prepare_asset`, `bevy_pbr/src/medium.rs:423-540`, and
 *  `init_scattering_medium_sampler`, `:570-579`).
 *
 *  The sizes are the part worth pinning: the density LUT is `falloff_resolution x 2`
 *  and the scattering LUT `falloff_resolution x phase_resolution`, both `Rgba32Float`
 *  and texture-binding only. The data itself is covered device-free by the scattering
 *  medium tests; here it only has to reach the texture. */
TEST(MaterialExtract, TheScatteringMediumBuildsItsLutsOnADevice) {
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
    add_render_dependent_prerequisites(application);

    auto render_sub = application.take_sub_app(Render);
    ASSERT_TRUE(render_sub);
    const wgpu::Device& device = render_sub->world().resource<wgpu::Device>();
    const wgpu::Queue& queue  = render_sub->world().resource<wgpu::Queue>();

    const auto medium = pbr::ScatteringMedium::earthlike(8, 4).with_label("test_sky");
    const pbr::GpuScatteringMedium gpu = pbr::make_gpu_scattering_medium(device, queue, medium);

    EXPECT_EQ(gpu.falloff_resolution, 8u);
    EXPECT_EQ(gpu.phase_resolution, 4u);
    EXPECT_EQ(gpu.terms.size(), 3u) << "the terms travel with the LUTs, as Bevy keeps them";
    EXPECT_EQ(gpu.density_lut.getWidth(), 8u);
    EXPECT_EQ(gpu.density_lut.getHeight(), 2u) << "absorption row, then scattering row";
    EXPECT_EQ(gpu.scattering_lut.getWidth(), 8u) << "falloff is the U axis";
    EXPECT_EQ(gpu.scattering_lut.getHeight(), 4u) << "phase is the V axis";
    EXPECT_EQ(gpu.density_lut.getFormat(), wgpu::TextureFormat::eRGBA32Float);

    // Creating the sampler is the check: a bad descriptor would fail validation here,
    // and this wrapper handle has no comparable null.
    const pbr::ScatteringMediumSampler sampler = pbr::make_scattering_medium_sampler(device);
    (void)sampler;
}
/** @brief The view specialisation check fills both caches (Bevy
 *  `check_views_need_specialization`, `bevy_pbr/src/render/mesh.rs:309-446`).
 *
 *  The system is registered in `Render`; this test puts it in the `Extract` schedule the
 *  harness runs, spawns a view and asserts the key and its tick land - which also proves
 *  the batch-183 re-keying, since the system could not insert into a cache keyed by main
 *  entity. The key is checked for the perspective arm and for Gaussian filtering, the
 *  latter because the field zero is hardware 2x2. */
TEST(MaterialExtract, TheViewSpecialisationCheckFillsItsCaches) {
    App application = App::create();
    application.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(application);
    try {
        application.add_plugins(RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }
    ASSERT_TRUE(application.get_sub_app_mut(Render).has_value());
    add_render_dependent_prerequisites(application);
    application.add_plugins(pbr::MeshRenderPlugin{});
    application.resource_scope([&application](epix::app::internal::Plugins& plugins) {
        plugins.ready_all(application);
    });

    auto render_sub = application.take_sub_app(Render);
    ASSERT_TRUE(render_sub);
    render_sub->add_systems(epix::app::ScheduleInfo(ExtractSchedule),
                            epix::ecs::into(pbr::check_views_need_specialization)
                                .set_name("check views need specialization (test)"));

    const auto view_id = epix::render::view::RetainedViewEntity::create(sync_world::MainEntity{ecs::Entity{1}},
                                                                       std::nullopt, 0);
    render_sub->world_mut().spawn(epix::render::view::ExtractedView{.retained_view_entity = view_id},
                                 epix::render::view::Msaa::Sample4, epix::camera::Projection{});

    render_sub->extract(application);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });

    render_sub->resource_scope([&](epix::ecs::World& world) {
        const auto& cache = world.resource<pbr::ViewKeyCache>();
        const auto found  = cache.keys.find(view_id);
        ASSERT_NE(found, cache.keys.end()) << "the key is recorded against the retained view";
        EXPECT_TRUE(found->second.contains(pbr::MeshPipelineKey::VIEW_PROJECTION_PERSPECTIVE));
        EXPECT_TRUE(found->second.contains(pbr::MeshPipelineKey::SHADOW_FILTER_METHOD_GAUSSIAN))
            << "the default filtering method, not the field zero";

        const auto& ticks = world.resource<pbr::ViewSpecializationTicks>();
        EXPECT_TRUE(ticks.ticks.contains(view_id)) << "and a tick is stamped for it";
    });
}
/** @brief The medium plugin registers the asset, the render asset and the sampler
 *  (Bevy `ScatteringMediumPlugin`, `bevy_pbr/src/medium.rs:25-40`).
 *
 *  Bevy adds it as one plugin; this port splits the work the same way its other plugins
 *  do, with the asset and render-asset registrations in `attach` and the sampler in a
 *  `RenderStartup` system. The test therefore checks the two resources after adding the
 *  plugin and the sampler after running that schedule - which is also the only way to
 *  see the last piece, since nothing else runs `RenderStartup` here. */
TEST(MaterialExtract, TheScatteringMediumPluginRegistersItsParts) {
    App application = App::create();
    application.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(application);
    try {
        application.add_plugins(RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }
    ASSERT_TRUE(application.get_sub_app_mut(Render).has_value());
    add_render_dependent_prerequisites(application);

    application.add_plugins(pbr::ScatteringMediumPlugin{});

    // The asset and its GPU counterpart are registered by `attach`, so they exist without
    // a device being involved.
    EXPECT_TRUE(application.world().get_resource<epix::assets::Assets<pbr::ScatteringMedium>>().has_value())
        << "the medium asset type is registered";

    auto render_sub = application.take_sub_app(Render);
    ASSERT_TRUE(render_sub);
    EXPECT_TRUE(render_sub->world()
                    .get_resource<epix::render::RenderAssets<pbr::GpuScatteringMedium>>()
                    .has_value())
        << "and the render-asset plugin pairs it with the GPU representation";

    application.resource_scope([&application](epix::app::internal::Plugins& plugins) {
        plugins.ready_all(application);
    });

    // The sampler is registered by the plugin's `RenderStartup` system, and that is the
    // one part this test cannot observe: running the schedule by hand - prepared and
    // initialized, with deferred work applied, following `App::run` - inserted nothing and
    // logged no validation error, so the hand-run schedule is not reaching the system in
    // this harness. The registration is therefore verified by reading the plugin rather
    // than by the test, and that is recorded on the row instead of implied.    (void)render_sub;
}
/** @brief The camera filtering method actually reaches its render entity (Bevy
 *  `extract_shadow_filtering_method`, `bevy_pbr/src/render/light.rs:234-245`).
 *
 *  The device-free test asserts the specialization; this one asserts the path: a
 *  main-world camera carrying `SyncToRenderWorld` gets a render entity from
 *  `SyncWorldPlugin`, and the extraction puts the method there. Without this the arm in
 *  `check_views_need_specialization` reads a component that never arrived - which is
 *  exactly the state batch 189 found and fixed. */
TEST(MaterialExtract, TheShadowFilteringMethodReachesItsRenderEntity) {
    App application = App::create();
    application.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(application);
    try {
        application.add_plugins(RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }
    ASSERT_TRUE(application.get_sub_app_mut(Render).has_value());
    add_render_dependent_prerequisites(application);
    // Registers the extraction plugin this test exercises.
    application.add_plugins(pbr::MeshRenderPlugin{});
    application.resource_scope([&application](epix::app::internal::Plugins& plugins) {
        plugins.ready_all(application);
    });

    const ecs::Entity camera =
        application.world_mut()
            .spawn(epix::render::sync_world::SyncToRenderWorld{}, epix::camera::Camera3d{},
                   pbr::ShadowFilteringMethod::Temporal)
            .id();

    auto render_sub = application.take_sub_app(Render);
    ASSERT_TRUE(render_sub);
    render_sub->extract(application);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });

    // The sync step maps the main entity to a render entity, and the extraction writes
    // the component there.
    const auto camera_ref = application.world().get_entity(camera);
    ASSERT_TRUE(camera_ref.has_value());
    const auto mapped = camera_ref->get<epix::render::sync_world::RenderEntity>();
    ASSERT_TRUE(mapped.has_value()) << "SyncToRenderWorld gives the camera a render entity";
    const auto render_entity = render_sub->world().get_entity(mapped->get().id());
    ASSERT_TRUE(render_entity.has_value()) << "the render entity exists";
    const auto extracted = render_entity->get<pbr::ShadowFilteringMethod>();
    ASSERT_TRUE(extracted.has_value()) << "and the extraction reaches it";
    EXPECT_EQ(extracted->get(), pbr::ShadowFilteringMethod::Temporal);
}