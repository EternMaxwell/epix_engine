#include <epix/core_graph/prepass.hpp>
#include <gtest/gtest.h>

using namespace epix::core_graph;

namespace {

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

/** @brief Bevy exposes six views on `ViewPrepassTextures`, and the two that
 *  read the *previous* frame report a view exactly when the attachment is
 *  double-buffered (`prepass/mod.rs:138-168`). */
TEST(Prepass, ViewPrepassTexturesPreviousFrameViewsFollowTheAttachments) {
    using epix::render::render_resource::CachedTexture;
    using epix::render::render_resource::ColorAttachment;
    using epix::core_graph::core_3d::ViewPrepassTextures;

    // Nothing attached: every accessor reports nothing.
    const ViewPrepassTextures empty;
    EXPECT_FALSE(empty.depth_view().has_value());
    EXPECT_FALSE(empty.previous_depth_view().has_value());
    EXPECT_FALSE(empty.normal_view().has_value());
    EXPECT_FALSE(empty.motion_vectors_view().has_value());
    EXPECT_FALSE(empty.deferred_view().has_value());
    EXPECT_FALSE(empty.previous_deferred_view().has_value());

    // Single-buffered: the current view exists and the previous one does not,
    // which is what a view with a prepass but no double-buffer marker sees.
    ViewPrepassTextures single;
    single.depth    = ColorAttachment{CachedTexture{}};
    single.deferred = ColorAttachment{CachedTexture{}};
    EXPECT_TRUE(single.depth_view().has_value());
    EXPECT_FALSE(single.previous_depth_view().has_value());
    EXPECT_TRUE(single.deferred_view().has_value());
    EXPECT_FALSE(single.previous_deferred_view().has_value());

    // Double-buffered: `package_double_buffered_texture` fills the other
    // buffer into `previous_frame_texture`, and both views are then readable.
    ViewPrepassTextures doubled;
    doubled.depth    = ColorAttachment{CachedTexture{}, std::nullopt, CachedTexture{}};
    doubled.deferred = ColorAttachment{CachedTexture{}, std::nullopt, CachedTexture{}};
    EXPECT_TRUE(doubled.depth_view().has_value());
    EXPECT_TRUE(doubled.previous_depth_view().has_value());
    EXPECT_TRUE(doubled.deferred_view().has_value());
    EXPECT_TRUE(doubled.previous_deferred_view().has_value());

    // The other two accessors stay independent of the double buffering.
    EXPECT_FALSE(doubled.normal_view().has_value());
    EXPECT_FALSE(doubled.motion_vectors_view().has_value());
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


}  // namespace
