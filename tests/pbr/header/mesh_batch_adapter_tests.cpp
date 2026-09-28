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

/** @brief Which phase item's key types are not hashable.
 *
 *  The phase plugin registers resources keyed by the item's batch-set and bin keys,
 *  so those types need `std::hash` as well as ordering. `std::hash<T>` is disabled
 *  for unsupported `T`, so its default-constructibility is the test. Each is named,
 *  so one build identifies every missing one. */
template <typename T>
constexpr bool hashable() {
    return std::is_default_constructible_v<std::hash<T>>;
}

TEST(MeshBatchAdapter, ReportsWhichPhaseKeyIsNotHashable) {
    using namespace epix::core_graph::core_3d;

    static_assert(hashable<Opaque3d::BatchSetKey>(), "MISSING HASH: Opaque3d::BatchSetKey");
    static_assert(hashable<Opaque3d::BinKey>(), "MISSING HASH: Opaque3d::BinKey");
    static_assert(hashable<AlphaMask3d::BatchSetKey>(), "MISSING HASH: AlphaMask3d::BatchSetKey");
    static_assert(hashable<AlphaMask3d::BinKey>(), "MISSING HASH: AlphaMask3d::BinKey");
    static_assert(hashable<Opaque3dDeferred::BatchSetKey>(), "MISSING HASH: Opaque3dDeferred::BatchSetKey");
    static_assert(hashable<AlphaMask3dDeferred::BatchSetKey>(), "MISSING HASH: AlphaMask3dDeferred::BatchSetKey");
    static_assert(hashable<epix::pbr::Shadow::BatchSetKey>(), "MISSING HASH: Shadow::BatchSetKey");
    static_assert(hashable<epix::pbr::Shadow::BinKey>(), "MISSING HASH: Shadow::BinKey");

    SUCCEED();
}

/** @brief Which of the two instance-buffer resource types is a valid system
 *  parameter.
 *
 *  `write_batched_instance_buffers<MeshPipeline>` cannot be bound by `ecs::into`
 *  while `clear_batched_cpu_instance_buffers<MeshPipeline>` can, and the two differ
 *  in the buffer type they take: `BatchedInstanceBuffers<..>` against
 *  `BatchedInstanceBuffer<..>`. Asking the concept directly distinguishes "this
 *  resource type is not a system parameter" from "the template arguments are not
 *  deducible", which are very different problems with very different fixes. */
TEST(MeshBatchAdapter, ReportsWhichInstanceBufferIsASystemParam) {
    using PbrBuffers = epix::render::batching::BatchedInstanceBuffers<epix::pbr::MeshUniform, epix::pbr::MeshInputUniform>;
    using CpuBuffer  = epix::render::batching::BatchedInstanceBuffer<epix::pbr::MeshUniform>;

    static_assert(epix::ecs::system_param<epix::ecs::ResMut<CpuBuffer>>,
                  "MISSING SYSTEM PARAM: ResMut<BatchedInstanceBuffer<MeshUniform>> - the one that works");
    static_assert(epix::ecs::system_param<epix::ecs::ResMut<PbrBuffers>>,
                  "MISSING SYSTEM PARAM: ResMut<BatchedInstanceBuffers<MeshUniform, MeshInputUniform>>");

    SUCCEED();
}