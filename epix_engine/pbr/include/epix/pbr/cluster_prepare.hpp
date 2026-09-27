#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/common.hpp>
#include <epix/pbr/cluster.hpp>
#include <epix/pbr/mesh_pipeline_resource.hpp>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::pbr {

/** @brief Build every view's cluster bindings from its extracted records (Bevy
 *  `prepare_clusters`, `bevy_pbr/src/cluster.rs:289-336`).
 *
 *  The bindings are built and uploaded per view and then inserted as a component,
 *  one view at a time: the record walk itself is `prepare_view_cluster_bindings`,
 *  which this calls with the pipeline's binding type and whether that type allows
 *  the index lists to grow past `MAX_INDICES`.
 *
 *  Whether storage buffers are usable is read from `MeshPipeline`'s
 *  `clustered_forward_buffer_binding_type`, as Bevy reads it - not from the global
 *  cluster settings, which carry a related but different fact.
 *
 *  Bevy keeps this system beside the cluster types in `cluster.rs`; the
 *  declaration lives here because it is the one place where the cluster types and
 *  `MeshPipeline` meet - `mesh_view_bindings.hpp` already includes the cluster
 *  header, so `cluster.hpp` cannot include the pipeline resource back. */
EPIX_EXPORT EPIX_API void prepare_clusters(
    ecs::Commands commands, ecs::Res<wgpu::Device> render_device, ecs::Res<wgpu::Queue> render_queue,
    ecs::Res<MeshPipeline> mesh_pipeline, ecs::Res<GlobalClusterableObjectMeta> global_clusterable_object_meta,
    ecs::Query<ecs::Item<ecs::Entity, const ExtractedClusterableObjects&>> views);

}  // namespace epix::pbr
