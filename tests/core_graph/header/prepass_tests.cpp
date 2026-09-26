#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph/prepass.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/window.hpp>
#include <gtest/gtest.h>

using namespace epix::core_graph;

namespace {

/** @brief Asset loading spawns tasks on the IO pool, so a test that attaches
 *  `RenderPlugin` (whose screenshot plugin loads an asset) needs it
 *  initialized. */
struct PrepassTaskPoolInit {
    PrepassTaskPoolInit() {
        epix::task::IoTaskPool::get_or_init(epix::task::TaskPool{epix::task::TaskPoolBuilder{}.num_threads(4).build()});
    }
} g_prepass_task_pool_init;

/** @brief The previous-view data is Bevy's five matrices in Bevy's order, and
 *  its layout is what the shader's constant buffer expects
 *  (`prepass/mod.rs:96-102`). */
TEST(Prepass, PreviousViewDataMatchesBevyLayout) {
    EXPECT_EQ(sizeof(PreviousViewData), 5u * sizeof(glm::mat4));
    EXPECT_EQ(offsetof(PreviousViewData, view_from_world), 0u);
    EXPECT_EQ(offsetof(PreviousViewData, clip_from_world), sizeof(glm::mat4));
    EXPECT_EQ(offsetof(PreviousViewData, clip_from_view), 2u * sizeof(glm::mat4));
    EXPECT_EQ(offsetof(PreviousViewData, world_from_clip), 3u * sizeof(glm::mat4));
    EXPECT_EQ(offsetof(PreviousViewData, view_from_clip), 4u * sizeof(glm::mat4));

    static_assert(epix::render::render_resource::ShaderType<PreviousViewData>);
    static_assert(epix::render::render_resource::ShaderWritable<PreviousViewData>);
    EXPECT_EQ(epix::render::render_resource::ShaderTypeInfo<PreviousViewData>::shader_size,
              sizeof(PreviousViewData));
}

/** @brief The offset component defaults to zero, so a view that never had a
 *  previous frame reads the buffer's first entry. */
TEST(Prepass, PreviousViewUniformOffsetDefaultsToZero) {
    const PreviousViewUniformOffset offset;
    EXPECT_EQ(offset.offset, 0u);
}

/** @brief The target set is Bevy's `prepass_target_descriptors`: four slots in
 *  the order normals, motion vectors, deferred G-buffer, deferred lighting id,
 *  each present only when its prepass is (`prepass/mod.rs:383-405`). */
TEST(Prepass, TargetDescriptorsFollowTheRequestedPrepasses) {
    const auto verify = [](const std::array<std::optional<wgpu::ColorTargetState>, 4>& targets, bool normal,
                           bool motion_vectors, bool deferred) {
        EXPECT_EQ(targets[0].has_value(), normal);
        EXPECT_EQ(targets[1].has_value(), motion_vectors);
        EXPECT_EQ(targets[2].has_value(), deferred);
        EXPECT_EQ(targets[3].has_value(), deferred);
        if (targets[0]) {
            EXPECT_EQ(targets[0]->format, epix::core_graph::core_3d::NORMAL_PREPASS_FORMAT);
            EXPECT_EQ(targets[0]->writeMask, wgpu::ColorWriteMask::eAll);
        }
        if (targets[1]) {
            EXPECT_EQ(targets[1]->format, epix::core_graph::core_3d::MOTION_VECTOR_PREPASS_FORMAT);
            EXPECT_EQ(targets[1]->writeMask, wgpu::ColorWriteMask::eAll);
        }
        if (targets[2]) EXPECT_EQ(targets[2]->format, epix::core_graph::core_3d::DEFERRED_PREPASS_FORMAT);
        if (targets[3]) EXPECT_EQ(targets[3]->format, epix::core_graph::core_3d::DEFERRED_LIGHTING_PASS_ID_FORMAT);
    };

    verify(prepass_target_descriptors(false, false, false), false, false, false);
    verify(prepass_target_descriptors(true, false, false), true, false, false);
    verify(prepass_target_descriptors(false, true, false), false, true, false);
    verify(prepass_target_descriptors(false, false, true), false, false, true);
    verify(prepass_target_descriptors(true, true, true), true, true, true);
}

/** @brief Bevy uses exactly this set for the skybox prepass - normals plus
 *  motion vectors, never deferred (`skybox/prepass.rs:105`). */
TEST(Prepass, SkyboxPrepassTargetsAreNormalsAndMotionVectors) {
    const auto targets = prepass_target_descriptors(true, true, false);
    EXPECT_TRUE(targets[0].has_value());
    EXPECT_TRUE(targets[1].has_value());
    EXPECT_FALSE(targets[2].has_value());
    EXPECT_FALSE(targets[3].has_value());
}

/** @brief The pipeline-ready form of that set keeps all four locations and
 *  spells the unwritten ones as `Undefined`-format targets, which is the only
 *  encoding Epix's fragment target list can carry. */
TEST(Prepass, PipelineTargetsSpellUnwrittenLocationsAsUndefined) {
    const auto targets = prepass_pipeline_targets(true, true, false);
    ASSERT_EQ(targets.size(), 4u);
    EXPECT_EQ(targets[0].format, epix::core_graph::core_3d::NORMAL_PREPASS_FORMAT);
    EXPECT_EQ(targets[1].format, epix::core_graph::core_3d::MOTION_VECTOR_PREPASS_FORMAT);
    EXPECT_EQ(targets[2].format, wgpu::TextureFormat::eUndefined)
        << "the deferred G-buffer location is unwritten here";
    EXPECT_EQ(targets[3].format, wgpu::TextureFormat::eUndefined)
        << "the deferred lighting id location is unwritten here";
    for (const auto& target : targets) {
        EXPECT_EQ(target.writeMask, wgpu::ColorWriteMask::eAll);
    }

    // The deferred set is the mirror image: its first two locations are holes.
    const auto deferred = prepass_pipeline_targets(false, false, true);
    ASSERT_EQ(deferred.size(), 4u);
    EXPECT_EQ(deferred[0].format, wgpu::TextureFormat::eUndefined);
    EXPECT_EQ(deferred[1].format, wgpu::TextureFormat::eUndefined);
    EXPECT_EQ(deferred[2].format, epix::core_graph::core_3d::DEFERRED_PREPASS_FORMAT);
    EXPECT_EQ(deferred[3].format, epix::core_graph::core_3d::DEFERRED_LIGHTING_PASS_ID_FORMAT);
}

/** @brief `prepare_previous_view_uniforms` gives every view an entry in the
 *  buffer and stamps it with that entry's byte offset, which is what a prepass
 *  bind group passes as the dynamic offset (Bevy
 *  `prepare_previous_view_uniforms`). */
TEST(Prepass, PreparePreviousViewUniformsStampsEachView) {
    auto app = epix::app::App::create();
    app.add_events<epix::window::WindowClosed>();
    app.add_plugins(epix::time::TimePlugin{})
        .add_plugins(epix::camera::CameraPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::image::ImagePlugin{})
        .add_plugins(epix::render::FrameCountPlugin{});
    try {
        app.add_plugins(epix::render::RenderPlugin{.synchronous_pipeline_compilation = true});
    } catch (const std::exception& error) {
        GTEST_SKIP() << "GPU/Vulkan not available, skipping GPU test: " << error.what();
    }

    auto& world = app.world_mut();
    const auto first  = world.spawn(PreviousViewData{}).id();
    const auto second = world.spawn(PreviousViewData{}).id();

    auto& render_app   = app.get_sub_app_mut(epix::render::Render).value().get();
    auto& render_world = render_app.world_mut();
    // The render-world entities carry the extracted components; mirror them. The
    // buffer needs the device's uniform alignment, which is what the plugin that
    // registers this resource has to do too (`ViewUniforms::from_world`'s
    // counterpart for this buffer).
    const auto render_first  = render_world.spawn(PreviousViewData{}).id();
    const auto render_second = render_world.spawn(PreviousViewData{}).id();
    wgpu::Limits limits{};
    render_world.resource<wgpu::Device>().getLimits(&limits);
    render_world.insert_resource(
        PreviousViewUniforms{.uniforms =
                                 epix::render::render_resource::DynamicUniformBuffer<PreviousViewData>::
                                     new_with_alignment(limits.minUniformBufferOffsetAlignment)});

    render_app.add_systems(epix::render::RenderStartup,
                           epix::ecs::into(prepare_previous_view_uniforms)
                               .set_name("prepare previous view uniforms"));
    render_app.run_schedule(epix::render::RenderStartup);

    const auto first_offset  = render_world.entity(render_first).get<PreviousViewUniformOffset>();
    const auto second_offset = render_world.entity(render_second).get<PreviousViewUniformOffset>();
    ASSERT_TRUE(first_offset.has_value());
    ASSERT_TRUE(second_offset.has_value());
    EXPECT_EQ(first_offset->get().offset, 0u) << "the first view starts the buffer";
    EXPECT_EQ(second_offset->get().offset, sizeof(PreviousViewData))
        << "the second entry follows the first, aligned to the uniform stride";
    static_cast<void>(first);
    static_cast<void>(second);
}

/** @brief `update_previous_view_data` derives Bevy's five matrices from the
 *  camera's global transform and its projection
 *  (`bevy_pbr/src/prepass/mod.rs:189-206`), storing the *current* frame's so the
 *  render world reads them one frame late. */
TEST(Prepass, UpdatePreviousViewDataDerivesTheMatrixSet) {
    auto app = epix::app::App::create();
    app.add_plugins(epix::transform::TransformPlugin{});

    const glm::mat4 world_from_view =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 5.0f)) * glm::rotate(glm::mat4(1.0f), 0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
    const auto camera = app.world_mut().spawn(epix::camera::Camera3d{}, epix::camera::Camera{},
                                              epix::transform::GlobalTransform{.matrix = world_from_view});
    static_cast<void>(camera);

    app.add_systems(epix::app::Update, epix::ecs::into(update_previous_view_data).set_name("probe previous view data"));
    app.run_schedule(epix::app::Update);

    const auto close = [](const glm::mat4& left, const glm::mat4& right, float epsilon = 1e-3f) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                const float difference = left[column][row] - right[column][row];
                if (difference > epsilon || difference < -epsilon) return false;
            }
        }
        return true;
    };

    auto query = app.world_mut().query<const PreviousViewData&>();
    auto found = false;
    for (auto&& data : query.iter(app.world())) {
        found = true;
        // The global transform is the world-from-view matrix, and every other
        // matrix follows from it and the projection.
        EXPECT_TRUE(close(data.view_from_world, glm::inverse(world_from_view)));
        EXPECT_TRUE(close(data.clip_from_world, data.clip_from_view * data.view_from_world));
        EXPECT_TRUE(close(data.world_from_clip, glm::inverse(data.clip_from_world)));
        EXPECT_TRUE(close(data.view_from_clip, glm::inverse(data.clip_from_view)));
    }
    EXPECT_TRUE(found) << "no PreviousViewData was inserted for the Camera3d";
}

}  // namespace
