#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/pbr/mesh_instance.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <epix/render.hpp>
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
namespace assets = epix::assets;

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

}  // namespace

/** @brief Removing a mesh's input uniform frees its slot and reports which one
 *  (Bevy `remove_mesh_input_uniform`, `render/mesh.rs:1267-1278`).
 *
 *  No device is involved: the input buffer's `remove` only records the slot as
 *  free for the next `add` to reuse, so the whole operation is checkable as plain
 *  data - which is also why the row could be closed without a GPU test. */
TEST(MeshInstance, RemoveMeshInputUniformFreesTheSlot) {
    render::batching::InstanceInputUniformBuffer<pbr::MeshInputUniform> buffer;
    render::sync_world::MainEntityHashMap<pbr::RenderMeshInstanceGpu> instances;

    // Two instances sharing the buffer, so the freed slot can be seen to be
    // reusable rather than merely gone.
    const render::sync_world::MainEntity first{epix::ecs::Entity{71}};
    const render::sync_world::MainEntity second{epix::ecs::Entity{72}};
    const std::uint32_t first_index  = buffer.add(pbr::MeshInputUniform{});
    const std::uint32_t second_index = buffer.add(pbr::MeshInputUniform{});
    // The shared record has no default constructor - the mesh id is what it is
    // about - so every member is spelled out, as the batching tests do.
    const pbr::RenderMeshInstanceShared shared{
        .mesh_asset_id           = epix::assets::AssetId<epix::mesh::Mesh>::invalid(),
        .material_bindings_index = pbr::MaterialBindingId{},
        .flags                   = pbr::RenderMeshInstanceFlags{},
        .lightmap_slab_index     = std::nullopt,
        .tag                     = 0,
        .render_layers           = std::nullopt,
        .center                  = glm::vec3{0.0f},
    };
    instances.emplace(first, pbr::RenderMeshInstanceGpu{
                                 .shared = shared, .center = glm::vec3{0.0f}, .current_uniform_index = first_index});
    instances.emplace(second, pbr::RenderMeshInstanceGpu{
                                  .shared = shared, .center = glm::vec3{0.0f}, .current_uniform_index = second_index});

    const std::optional<std::uint32_t> removed = pbr::remove_mesh_input_uniform(first, instances, buffer);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(*removed, first_index) << "the slot index is handed back for the buffers keyed by it";
    EXPECT_EQ(instances.size(), 1u) << "the instance is forgotten";
    EXPECT_FALSE(instances.contains(first));
    EXPECT_FALSE(buffer.get(first_index).has_value()) << "a freed slot no longer yields data";
    EXPECT_TRUE(buffer.get(second_index).has_value()) << "the other instance is untouched";

    // And the freed slot is what the next addition reuses.
    const std::uint32_t reused = buffer.add(pbr::MeshInputUniform{});
    EXPECT_EQ(reused, first_index) << "the buffer reuses the freed slot";

    // An entity with no instance is a no-op.
    EXPECT_FALSE(pbr::remove_mesh_input_uniform(render::sync_world::MainEntity{epix::ecs::Entity{99}}, instances, buffer)
                     .has_value());
}

/** @brief The GPU instance builder's `update` resolves an instance's data, writes
 *  its uniform, and keeps its slot on the second frame (Bevy
 *  `RenderMeshInstanceGpuBuilder::update`, `render/mesh.rs:1128-1262`).
 *
 *  The one input that needs a device is `MeshAllocator`, so this is the GPU test
 *  the rows waited for. A mesh id with no allocation is deliberate: the slices are
 *  then absent and every index falls back to zero, which keeps the assertions about
 *  the *uniform's* rules - the sentinels, the packing, the transpose - rather than
 *  about the allocator. */
TEST(MeshInstance, BuilderUpdateWritesTheUniformAndKeepsTheSlot) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(app);
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    auto& world = render_app->get().world_mut();

    const render::sync_world::MainEntity entity{epix::ecs::Entity{51}};
    const render::sync_world::MainEntity other{epix::ecs::Entity{52}};

    // A material the mesh has, bound at a group and slot that are not both zero,
    // so the packing can be told from a default.
    const assets::UntypedAssetId material = assets::AssetId<epix::image::Image>::invalid();
    pbr::RenderMaterialInstances material_ids;
    material_ids.instances.emplace(
        entity, pbr::RenderMaterialInstance{.asset_id = material, .last_change_tick = {}});
    pbr::RenderMaterialBindings material_bindings;
    material_bindings.bindings.emplace(material, pbr::MaterialBindingId{.group = pbr::MaterialBindGroupIndex{.index = 1},
                                                                        .slot  = pbr::MaterialBindGroupSlot{.slot = 7}});

    pbr::RenderLightmaps lightmaps;
    lightmaps.render_lightmaps.emplace(entity, pbr::RenderLightmap{.uv_rect = {},
                                                                   .slab_index = pbr::LightmapSlabIndex{.index = 3},
                                                                   .slot_index = pbr::LightmapSlotIndex{.index = 2},
                                                                   .bicubic_sampling = false});

    pbr::SkinUniforms skins{.current_staging_buffer = {},
                            .current_buffer = {},
                            .prev_buffer = {},
                            .allocator = render::mesh::offset_allocator::Allocator(pbr::MAX_TOTAL_JOINTS),
                            .skin_uniform_info = {},
                            .joint_to_skins = {},
                            .total_joints = 0};

    auto mesh_allocator = render::mesh::MeshAllocator::from_world(world);

    const glm::mat4 world_from_local = glm::translate(glm::mat4{1.0f}, glm::vec3{4.0f, 0.0f, 0.0f});
    const auto shared = pbr::RenderMeshInstanceShared{
        .mesh_asset_id           = assets::AssetId<epix::mesh::Mesh>::invalid(),
        .material_bindings_index = pbr::MaterialBindingId{},
        .flags                   = pbr::RenderMeshInstanceFlags{},
        .lightmap_slab_index     = std::nullopt,
        .tag                     = 9,
        .render_layers           = std::nullopt,
        .center                  = glm::vec3{1.0f, 0.0f, 0.0f},
    };

    render::batching::InstanceInputUniformBuffer<pbr::MeshInputUniform> current;
    render::batching::InstanceInputUniformBuffer<pbr::MeshInputUniform> previous;
    render::sync_world::MainEntityHashMap<pbr::RenderMeshInstanceGpu> instances;
    pbr::MeshesToReextractNextFrame reextract;
    const render::FrameCount frame{.count = 12u};

    pbr::RenderMeshInstanceGpuBuilder builder{.shared           = shared,
                                                   .world_from_local = world_from_local,
                                                   .lightmap_uv_rect = glm::uvec2{5u, 6u},
                                                   .previous_input_index = std::nullopt,
                                                   .mesh_flags       = pbr::MeshFlags{}};
    const std::optional<std::uint32_t> first = builder.update(entity, instances, current, previous, mesh_allocator,
                                                               material_ids, material_bindings, lightmaps, skins, frame,
                                                               reextract);
    ASSERT_TRUE(first.has_value()) << "a prepared material writes the uniform";

    const auto written = current.get(*first);
    ASSERT_TRUE(written.has_value());
    EXPECT_EQ(written->previous_input_index, std::numeric_limits<std::uint32_t>::max())
        << "no previous uniform on the first frame";
    EXPECT_EQ(written->current_skin_index, std::numeric_limits<std::uint32_t>::max()) << "and no skin";
    EXPECT_EQ(written->first_vertex_index, 0u) << "a mesh with no allocation starts at zero";
    EXPECT_EQ(written->index_count, 0u) << "and has nothing to draw";
    EXPECT_EQ(written->tag, 9u);
    EXPECT_EQ(written->timestamp, 12u) << "the frame is what the injection pass validates against";
    EXPECT_EQ(written->material_and_lightmap_bind_group_slot, 7u | (2u << 16))
        << "material slot in the low half, lightmap slot in the high half";
    const glm::uvec2 expected_uv_rect{5u, 6u};
    EXPECT_EQ(written->lightmap_uv_rect, expected_uv_rect);
    EXPECT_EQ(written->world_from_local, pbr::to_transpose(world_from_local)) << "stored transposed, as the shader reads it";

    // The instance record is there too, with the world-space centre: the local
    // centre through the model matrix, whose translation is 4 along x.
    const auto recorded = instances.find(entity);
    ASSERT_NE(recorded, instances.end());
    EXPECT_EQ(recorded->second.current_uniform_index, *first);
    const glm::vec3 expected_center{5.0f, 0.0f, 0.0f};
    EXPECT_EQ(recorded->second.center, expected_center);
    ASSERT_TRUE(recorded->second.shared.lightmap_slab_index.has_value());
    EXPECT_EQ(recorded->second.shared.lightmap_slab_index->index, 3u);
    EXPECT_EQ(recorded->second.shared.material_bindings_index.slot.slot, 7u);

    // The occupied arm: the same entity again keeps its slot, and its previous
    // uniform moves to the previous-frame buffer for motion vectors.
    pbr::RenderMeshInstanceGpuBuilder second_builder{
        .shared = shared, .world_from_local = world_from_local, .lightmap_uv_rect = glm::uvec2{5u, 6u},
        .previous_input_index = std::nullopt, .mesh_flags = pbr::MeshFlags{}};
    const std::optional<std::uint32_t> second = second_builder.update(entity, instances, current, previous,
                                                                      mesh_allocator, material_ids, material_bindings,
                                                                      lightmaps, skins, frame, reextract);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, *first) << "a present entity keeps the slot its previous uniform lives at";
    const auto rewritten = current.get(*second);
    ASSERT_TRUE(rewritten.has_value());
    EXPECT_NE(rewritten->previous_input_index, std::numeric_limits<std::uint32_t>::max())
        << "and now points at the previous frame's copy of its own uniform";
    const bool previous_copy_exists = previous.get(rewritten->previous_input_index).has_value();
    EXPECT_TRUE(previous_copy_exists) << "which really is in the previous-frame buffer";
    EXPECT_EQ(instances.size(), 1u) << "and the map still holds one instance";

    // The bail-out: a mesh whose material is not in the bindings is not drawn with
    // a stale binding - it is queued for re-extraction and nothing is written.
    pbr::RenderMaterialInstances unbound_ids;
    unbound_ids.instances.emplace(
        other, pbr::RenderMaterialInstance{.asset_id = material, .last_change_tick = {}});
    pbr::RenderMeshInstanceGpuBuilder third_builder{
        .shared = shared, .world_from_local = world_from_local, .lightmap_uv_rect = glm::uvec2{5u, 6u},
        .previous_input_index = std::nullopt, .mesh_flags = pbr::MeshFlags{}};
    pbr::RenderMaterialBindings empty_bindings;
    EXPECT_FALSE(third_builder.update(other, instances, current, previous, mesh_allocator, unbound_ids, empty_bindings,
                                      lightmaps, skins, frame, reextract)
                     .has_value());
    EXPECT_TRUE(reextract.entities.contains(other)) << "the unprepared material queues the mesh for next frame";
    EXPECT_FALSE(instances.contains(other)) << "and nothing is written for it";
}