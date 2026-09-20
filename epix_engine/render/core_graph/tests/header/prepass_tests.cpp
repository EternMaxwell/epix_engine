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

}  // namespace
