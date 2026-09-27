#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/pbr/skin.hpp>
#include <epix/render.hpp>
#include <epix/shader.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <utility>

using namespace epix::app;
using namespace epix::assets;
using namespace epix::render;

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

}  // namespace

/** @brief This frame's joint matrices reach the GPU, and the buffers grow to hold
 *  them (Bevy `prepare_skins`, `render/skin.rs:199-268`).
 *
 *  What is observable from outside is the growth: the buffers start at exactly one
 *  `MAX_JOINTS`-sized binding and must end large enough for the staging data plus
 *  that same margin, since a full uniform buffer's worth has to stay bindable
 *  where skins use uniform buffers. */
TEST(SkinPrepare, UploadsTheJointMatrices) {
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

    const auto& device = world.resource<wgpu::Device>();
    wgpu::Limits limits{};
    ASSERT_EQ(device.getLimits(&limits), wgpu::Status::eSuccess);

    pbr::SkinUniforms uniforms = pbr::SkinUniforms::make(device, limits);
    const std::uint64_t initial_size = uniforms.current_buffer.getSize();
    // Eight joints is nothing, but it is two more than the margin can absorb:
    // `(8 + MAX_JOINTS)` `mat4`s do not fit in `MAX_JOINTS` of them.
    uniforms.current_staging_buffer.assign(8, glm::mat4{1.0f});
    world.insert_resource(std::move(uniforms));

    render_app->get().add_systems(render::RenderStartup,
                                  epix::ecs::into(epix::pbr::prepare_skins).set_name("prepare skins"));
    render_app->get().run_schedule(render::RenderStartup);

    const std::uint64_t uploaded_size = world.resource<pbr::SkinUniforms>().current_buffer.getSize();
    EXPECT_GE(uploaded_size, static_cast<std::uint64_t>(8u + pbr::MAX_JOINTS) * sizeof(glm::mat4))
        << "the buffer grows to hold the staging data plus the MAX_JOINTS margin";
    EXPECT_GT(uploaded_size, initial_size) << "which is larger than what it started at";
}

/** @brief Removing a skin frees its allocation, prunes the reverse joint mapping
 *  and lowers the joint count (Bevy `remove_skin`, `render/skin.rs:583-607`).
 *
 *  No device is involved: the allocator and both maps are plain values, so this
 *  half of the skin pipeline is checkable directly rather than through a frame. */
TEST(SkinPrepare, RemoveSkinFreesAndForgets) {
    pbr::SkinUniforms uniforms{
        .current_staging_buffer = {},
        .current_buffer         = {},
        .prev_buffer            = {},
        .allocator              = render::mesh::offset_allocator::Allocator(pbr::MAX_TOTAL_JOINTS),
        .skin_uniform_info      = {},
        .joint_to_skins         = {},
        .total_joints           = 0,
    };

    const auto skin    = render::sync_world::MainEntity{epix::ecs::Entity{11}};
    const auto joint_a = render::sync_world::MainEntity{epix::ecs::Entity{21}};
    const auto joint_b = render::sync_world::MainEntity{epix::ecs::Entity{22}};

    const auto allocation = uniforms.allocator.allocate(1);
    ASSERT_TRUE(allocation.has_value());
    uniforms.skin_uniform_info.emplace(
        skin, pbr::SkinUniformInfo{.allocation = *allocation, .joints = {joint_a, joint_b}});
    uniforms.joint_to_skins[joint_a].push_back(skin);
    uniforms.joint_to_skins[joint_b].push_back(skin);
    uniforms.total_joints = 2;

    pbr::remove_skin(uniforms, skin);

    EXPECT_TRUE(uniforms.skin_uniform_info.empty()) << "the skin is forgotten";
    EXPECT_TRUE(uniforms.joint_to_skins.empty()) << "and so is every joint that referred only to it";
    EXPECT_EQ(uniforms.total_joints, 0u) << "the joint count falls by the skin's joint count";

    // A skin that was never registered is a no-op rather than an error.
    pbr::remove_skin(uniforms, skin);
    EXPECT_EQ(uniforms.total_joints, 0u);
}

/** @brief Extraction fills the joint staging buffer for a visible skinned mesh
 *  (Bevy `extract_skins`, `render/skin.rs:296-345`).
 *
 *  This is the driving test for the whole write side: the mesh and its joints live
 *  in the main world, `extract_skins` runs on the extraction schedule, and what is
 *  checked is the render world's `SkinUniforms` - the skin is registered and the
 *  staging buffer holds the joint matrix at its allocation's offset. Identities
 *  everywhere make the expected matrix the identity, so the arithmetic that
 *  matters (transform times inverse bind pose) is what is exercised rather than
 *  the numbers. */
TEST(SkinPrepare, ExtractSkinsFillsTheStagingBuffer) {
    App app = App::create();
    app.add_events<epix::window::WindowClosed>();
    add_render_prerequisites(app);
    try {
        app.add_plugins(render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    // The joints and the mesh that skins them, all in the main world.
    const epix::ecs::Entity joint_a = app.world_mut().spawn(epix::transform::GlobalTransform{}).id();
    const epix::ecs::Entity joint_b = app.world_mut().spawn(epix::transform::GlobalTransform{}).id();

    // The asset store for this type does not exist until it is asked for, so the
    // test creates it: nothing else in the app registers a store for skins.
    app.world_mut().insert_resource(Assets<epix::mesh::SkinnedMeshInverseBindposes>{});
    auto& inverse_bindposes = app.world_mut().resource_mut<Assets<epix::mesh::SkinnedMeshInverseBindposes>>();
    const auto handle       = inverse_bindposes.add(
        epix::mesh::SkinnedMeshInverseBindposes(std::vector<glm::mat4>{glm::mat4{1.0f}, glm::mat4{1.0f}}));

    epix::camera::ViewVisibility visible;
    visible.set_visible();
    const epix::ecs::Entity skinned_mesh_entity =
        app.world_mut()
            .spawn(epix::mesh::SkinnedMesh{.inverse_bindposes = handle, .joints = {joint_a, joint_b}}, visible)
            .id();

    // The render world's side of the pipeline.
    auto render_app = app.get_sub_app_mut(render::Render);
    ASSERT_TRUE(render_app.has_value());
    render_app->get().add_systems(ExtractSchedule, epix::ecs::into(epix::pbr::extract_skins).set_name("extract skins"));

    const auto& device = render_app->get().world().resource<wgpu::Device>();
    wgpu::Limits limits{};
    ASSERT_EQ(device.getLimits(&limits), wgpu::Status::eSuccess);
    render_app->get().world_mut().insert_resource(pbr::SkinUniforms::make(device, limits));

    auto render_sub = app.take_sub_app(render::Render);
    ASSERT_TRUE(render_sub);
    render_sub->extract(app);
    render_sub->resource_scope([](epix::ecs::Schedules& schedules, epix::ecs::World& world) {
        schedules.schedule_mut(ExtractSchedule).apply_deferred(world);
    });

    const auto& uniforms = render_sub->world().resource<pbr::SkinUniforms>();
    const auto skin      = render::sync_world::MainEntity{skinned_mesh_entity};
    const auto index     = uniforms.skin_index(skin);
    ASSERT_TRUE(index.has_value()) << "the visible skinned mesh is allocated";
    EXPECT_EQ(uniforms.total_joints, 2u) << "both joints are counted";

    ASSERT_GE(uniforms.current_staging_buffer.size(), static_cast<std::size_t>(*index) + 2u);
    const glm::mat4 expected{1.0f};
    EXPECT_EQ(uniforms.current_staging_buffer[*index], expected) << "identity transform times identity inverse bind pose";
}

/** @brief Skinned meshes are tagged un-batchable exactly where skins use uniform
 *  buffers (Bevy `no_automatic_skin_batching`, `render/skin.rs:611-622`).
 *
 *  The expectation is derived from the same predicate the system uses rather than
 *  hard-coded, because which branch runs is a property of the platform: with
 *  storage buffers available the question does not arise and nothing is tagged.
 *  `skins_use_uniform_buffers` memoises once per process, as Bevy's `OnceLock`
 *  does, so the answer is whatever the first device in this binary reported. */
TEST(SkinPrepare, NoAutomaticSkinBatchingFollowsThePlatform) {
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

    const auto& device = render_app->get().world().resource<wgpu::Device>();
    wgpu::Limits limits{};
    ASSERT_EQ(device.getLimits(&limits), wgpu::Status::eSuccess);
    const bool uniform_buffers = pbr::skins_use_uniform_buffers(limits);

    // One skinned mesh and one mesh that is not skinned: only the first is a
    // candidate, and only where the platform needs it.
    const epix::ecs::Entity skinned = render_app->get().world_mut().spawn(epix::mesh::SkinnedMesh{}).id();
    const epix::ecs::Entity plain   = render_app->get().world_mut().spawn(epix::transform::GlobalTransform{}).id();

    render_app->get().add_systems(render::Render,
                                  epix::ecs::into(epix::pbr::no_automatic_skin_batching).set_name("no automatic skin batching"));
    render_app->get().run_schedule(render::Render);

    const auto tagged = [&](epix::ecs::Entity entity) {
        return render_app->get().world().entity(entity).get<render::phase::NoAutomaticBatching>().has_value();
    };
    EXPECT_EQ(tagged(skinned), uniform_buffers) << "tagged exactly where skins bind at dynamic offsets";
    EXPECT_FALSE(tagged(plain)) << "a mesh that is not skinned is never tagged";
}
