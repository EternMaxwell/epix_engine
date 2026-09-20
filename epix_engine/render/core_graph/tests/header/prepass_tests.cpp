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

}  // namespace
