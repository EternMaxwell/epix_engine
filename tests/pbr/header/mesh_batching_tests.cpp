#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/pbr/mesh_batching.hpp>
#include <epix/render.hpp>
#include <epix/render/mesh_allocator.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <optional>
#include <utility>

using namespace epix::app;
using namespace epix::assets;

namespace pbr    = epix::pbr;
namespace render = epix::render;

namespace {

// Attaching RenderPlugin loads an embedded shader, which spawns an async load
// task that terminates the process when the IO task pool has not been
// initialized.
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
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::view::ViewPlugin{})
        .add_plugins(render::GlobalsPlugin{});
}

/** @brief The entity pair a batch item carries: the render-world entity and the
 *  main-world one the resources are keyed by. */
struct BatchEntity {
    epix::ecs::Entity entity = epix::ecs::Entity{70};
    render::sync_world::MainEntity main_entity{epix::ecs::Entity{7}};
};

/** @brief The lightmap rectangle the instance's uniform must pack. */
epix::image::Rect lightmap_rect() {
    return epix::image::Rect{.min = glm::vec2{0.25f, 0.5f}, .max = glm::vec2{0.5f, 0.75f}};
}

/** @brief Fill the five resources the adapter's `Param` names, with one instance
 *  whose record carries the ids the compare data is built from. */
void prepare_resources(epix::ecs::World& world, const BatchEntity& ids, bool gpu_building) {
    pbr::RenderMeshInstances instances = pbr::RenderMeshInstances::make(gpu_building);
    pbr::RenderMeshInstanceShared shared{
        .mesh_asset_id = epix::assets::AssetId<epix::mesh::Mesh>::invalid(),
        .material_bindings_index =
            pbr::MaterialBindingId{.group = pbr::MaterialBindGroupIndex{.index = 3},
                                   .slot  = pbr::MaterialBindGroupSlot{.slot = 1}},
        .flags               = pbr::RenderMeshInstanceFlags{},
        .lightmap_slab_index = std::nullopt,
        .tag                 = 11,
        .render_layers       = std::nullopt,
        .center              = glm::vec3{0.0f},
    };
    shared.flags.insert(pbr::RenderMeshInstanceFlags::AUTOMATIC_BATCHING);
    pbr::RenderMeshInstanceCpu instance{.shared = shared, .transforms = {}};
    instances.cpu_instances.emplace(ids.main_entity, instance);
    world.insert_resource(std::move(instances));

    pbr::RenderLightmaps lightmaps;
    lightmaps.render_lightmaps.emplace(ids.main_entity,
                                       pbr::RenderLightmap{.uv_rect        = lightmap_rect(),
                                                           .slab_index    = pbr::LightmapSlabIndex{.index = 2},
                                                           .slot_index    = pbr::LightmapSlotIndex{.index = 1},
                                                           .bicubic_sampling = false});
    world.insert_resource(std::move(lightmaps));

    world.insert_resource(render::RenderAssets<render::mesh::RenderMesh>{});
    auto allocator = render::mesh::MeshAllocator::from_world(world);
    world.insert_resource(std::move(allocator));

    // A skin allocated at unit 2, so the uniform's skin index is 2 units of
    // joints rather than the "no skin" sentinel.
    pbr::SkinUniforms skins{.allocator = render::mesh::offset_allocator::Allocator(pbr::MAX_TOTAL_JOINTS)};
    pbr::SkinUniformInfo skin_info{};
    skin_info.allocation = render::mesh::offset_allocator::Allocation{.offset = 2};
    skins.skin_uniform_info.emplace(ids.main_entity, skin_info);
    world.insert_resource(std::move(skins));
}

}  // namespace

/** @brief The adapter every automatic batch of 3D meshes goes through produces
 *  the instance's `MeshUniform` and the comparand that decides what may share a
 *  batch (Bevy `GetBatchData for MeshPipeline`, `render/mesh.rs:1980-2038`).
 *
 *  The resources are the real ones and the call is made the way the batching
 *  system makes it, through the adapter's `Param`, so the tuple's shape and the
 *  lookup are exercised together. It needs the mesh allocator, which is created
 *  from a device, and skips without one like the other GPU tests. */
TEST(MeshBatching, AdapterProducesTheUniformAndTheCompareData) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(app);
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    const BatchEntity ids{};
    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    prepare_resources(render_app->get().world_mut(), ids, /*gpu_building=*/false);

    render_app->get().add_systems(
        render::RenderStartup,
        epix::ecs::into([ids](render::batching::GetBatchData<pbr::MeshPipeline>::Param param) {
            const render::batching::GetBatchData<pbr::MeshPipeline> adapter;

            const auto result = adapter.get_batch_data(param, {ids.entity, ids.main_entity});
            ASSERT_TRUE(result.has_value());
            const auto& [uniform, compare_data] = *result;

            // The comparand decides what may share a batch: this instance's
            // material group, its mesh and its lightmap slab.
            ASSERT_TRUE(compare_data.has_value());
            EXPECT_EQ(std::get<0>(*compare_data).index, 3u);
            EXPECT_EQ(std::get<1>(*compare_data), epix::assets::AssetId<epix::mesh::Mesh>::invalid());
            ASSERT_TRUE(std::get<2>(*compare_data).has_value());
            EXPECT_EQ(std::get<2>(*compare_data)->index, 2u);

            // The uniform carries the instance's identifications: its tag, the
            // material slot and lightmap slot packed together, the lightmap's
            // packed UV rectangle, and the skin's offset in joints.
            EXPECT_EQ(uniform.tag, 11u);
            EXPECT_EQ(uniform.material_and_lightmap_bind_group_slot, (1u << 16) | 1u);
            EXPECT_EQ(uniform.lightmap_uv_rect, pbr::pack_lightmap_uv_rect(lightmap_rect()));
            EXPECT_EQ(uniform.current_skin_index, 2u * pbr::JOINTS_PER_ALLOCATION_UNIT);
            // No vertex slice was allocated for that mesh, which Bevy treats as
            // an instance whose indices start at zero.
            EXPECT_EQ(uniform.first_vertex_index, 0u);

            // An entity with no instance record is not a batch item at all.
            const auto missing =
                adapter.get_batch_data(param, {epix::ecs::Entity{71}, render::sync_world::MainEntity{epix::ecs::Entity{8}}});
            EXPECT_FALSE(missing.has_value());
        }));
    render_app->get().run_schedule(render::RenderStartup);
}

/** @brief The adapter refuses to run when the GPU instance builder owns the
 *  instances, rather than reading the empty CPU side and reporting nothing
 *  (Bevy's `error!` in the same place). */
TEST(MeshBatching, AdapterRefusesWhenTheGpuBuilderOwnsTheInstances) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(app);
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    const BatchEntity ids{};
    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    prepare_resources(render_app->get().world_mut(), ids, /*gpu_building=*/true);

    render_app->get().add_systems(
        render::RenderStartup,
        epix::ecs::into([ids](render::batching::GetBatchData<pbr::MeshPipeline>::Param param) {
            const render::batching::GetBatchData<pbr::MeshPipeline> adapter;
            EXPECT_FALSE(adapter.get_batch_data(param, {ids.entity, ids.main_entity}).has_value());
        }));
    render_app->get().run_schedule(render::RenderStartup);
}
