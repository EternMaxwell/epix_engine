#include <epix/pbr/mesh_batching.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <epix/render/batching.hpp>
#include <epix/pbr/shadow_phase.hpp>
#include <epix/core_graph/core3d.hpp>
#include <gtest/gtest.h>

/** @brief Which clause of the batching concepts the mesh adapter and its input
 *  uniform are missing.
 *
 *  `BinnedRenderPhasePlugin::attach` asserts the whole `GetFullBatchDataImpl`
 *  concept at once, so a failure there names neither the clause nor the member -
 *  and iterating one guess per compiler round is expensive. These asserts ask the
 *  clauses separately, each with its own message, so one build names the answer.
 *  This is the measurement batch 124 showed to be worth taking before any fix. */
TEST(MeshBatchAdapter, ReportsWhichClauseIsMissing) {
    // The two the aggregate assert composes.
    static_assert(epix::render::batching::GetBatchDataImpl<epix::pbr::MeshPipeline>,
                  "MISSING CLAUSE: GetBatchDataImpl<MeshPipeline>");
    static_assert(epix::render::batching::GetFullBatchDataImpl<epix::pbr::MeshPipeline>,
                  "MISSING CLAUSE: GetFullBatchDataImpl<MeshPipeline>");

    // And the input uniform's half of it.
    static_assert(epix::render::render_resource::PodElement<epix::pbr::MeshInputUniform>,
                  "MISSING CLAUSE: PodElement<MeshInputUniform>");
    static_assert(std::default_initializable<epix::pbr::MeshInputUniform>,
                  "MISSING CLAUSE: default_initializable<MeshInputUniform>");

    // The plugin's *second* assert: the phase items' keys must be orderable for
    // Bevy-compatible bin sorting. Each is named so one build identifies all of them.
    static_assert(std::totally_ordered<epix::core_graph::core_3d::Opaque3dBatchSetKey>,
                  "MISSING: Opaque3dBatchSetKey ordering");
    static_assert(std::totally_ordered<epix::pbr::ShadowBatchSetKey>, "MISSING: ShadowBatchSetKey ordering");

    SUCCEED();
}
