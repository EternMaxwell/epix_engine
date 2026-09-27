#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/mesh.hpp>
#include <epix/render/assets.hpp>
#include <epix/render/batching.hpp>
#include <epix/render/mesh/render_mesh.hpp>
#include <epix/render/mesh_allocator.hpp>
#include <epix/render/sync_world.hpp>
#include <optional>
#include <spdlog/spdlog.h>
#include <tuple>
#include <utility>
#include <epix/common.hpp>
#endif

#include <epix/pbr/lightmap.hpp>
#include <epix/pbr/material_bindings.hpp>
#include <epix/pbr/mesh_instance.hpp>
#include <epix/pbr/mesh_pipeline_resource.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <epix/pbr/skin.hpp>

namespace epix::render::batching {

/** @brief How the 3D mesh pipeline batches: one `MeshUniform` per instance,
 *  compared by `(material bind group, mesh, lightmap slab)` (Bevy
 *  `GetBatchData for MeshPipeline`, `bevy_pbr/src/render/mesh.rs:1980-2038`).
 *
 *  The comparand is what makes automatic batching safe: two instances may share
 *  a batch only when they bind the same material group, draw the same mesh, and
 *  use the same lightmap slab. A `None` comparand - an instance with automatic
 *  batching off - makes the item unbatchable, the same way the 2D adapter does.
 *
 *  Bevy's `Param` is the same five resources; the set of instances is the one
 *  thing the adapter refuses to work without, and it refuses loudly rather than
 *  silently producing zeros when the GPU builder owns the instances. */
template <>
struct GetBatchData<epix::pbr::MeshPipeline> {
    using Param = std::tuple<ecs::Res<epix::pbr::RenderMeshInstances>,
                             ecs::Res<epix::pbr::RenderLightmaps>,
                             ecs::Res<epix::render::RenderAssets<epix::render::mesh::RenderMesh>>,
                             ecs::Res<epix::render::mesh::MeshAllocator>,
                             ecs::Res<epix::pbr::SkinUniforms>>;
    /** The material bind group, the mesh id and the lightmap slab, respectively
     *  (Bevy's `CompareData`). */
    using CompareData = std::tuple<epix::pbr::MaterialBindGroupIndex, epix::assets::AssetId<epix::mesh::Mesh>,
                                   std::optional<epix::pbr::LightmapSlabIndex>>;
    using BufferData = epix::pbr::MeshUniform;

    std::optional<std::pair<BufferData, std::optional<CompareData>>> get_batch_data(
        Param& param, std::pair<ecs::Entity, epix::render::sync_world::MainEntity> ids) const {
        const auto& [mesh_instances, lightmaps, render_meshes, mesh_allocator, skin_uniforms] = param;
        // Bevy matches the `CpuBuilding` variant here and derefs to its map;
        // Epix's resource carries the kind flag and both maps, so the check and
        // the lookup are the same two steps.
        if (!mesh_instances->is_cpu_building()) {
            spdlog::error(
                "[pbr] get_batch_data should never be called in GPU mesh uniform building mode; the GPU instance "
                "builder owns the instance data.");
            return std::nullopt;
        }
        const auto found = mesh_instances->cpu_instances.find(ids.second);
        if (found == mesh_instances->cpu_instances.end()) return std::nullopt;
        const epix::pbr::RenderMeshInstanceCpu& mesh_instance = found->second;

        // Bevy reads the mesh asset's id and leaves `render_meshes` unused: the
        // vertex slice is what places the instance's indices in the buffer, and
        // a mesh with no allocated slice starts at zero.
        (void)render_meshes;
        const auto vertex_slice          = mesh_allocator->mesh_vertex_slice(mesh_instance.shared.mesh_asset_id);
        const std::uint32_t first_vertex = vertex_slice ? vertex_slice->range.first : 0u;

        const auto lightmap_found = lightmaps->render_lightmaps.find(ids.second);
        const epix::pbr::RenderLightmap* maybe_lightmap =
            lightmap_found == lightmaps->render_lightmaps.end() ? nullptr : &lightmap_found->second;
        const std::optional<std::pair<epix::pbr::LightmapSlotIndex, ::epix::image::Rect>> lightmap_for_uniform =
            maybe_lightmap ? std::optional{std::pair{maybe_lightmap->slot_index, maybe_lightmap->uv_rect}}
                           : std::nullopt;

        const auto material_bind_group = mesh_instance.shared.material_bindings_index;
        const auto current_skin_index  = skin_uniforms->skin_index(ids.second);

        std::optional<CompareData> compare_data;
        if (mesh_instance.shared.should_batch()) {
            compare_data = CompareData{material_bind_group.group, mesh_instance.shared.mesh_asset_id,
                                       maybe_lightmap ? std::optional{maybe_lightmap->slab_index} : std::nullopt};
        }
        return std::make_pair(epix::pbr::MeshUniform::make(mesh_instance.transforms, first_vertex, material_bind_group.slot,
                                                           lightmap_for_uniform, current_skin_index,
                                                           std::optional{mesh_instance.shared.tag}),
                              compare_data);
    }
};

/** @brief Everything the binned, GPU-preprocessed path needs from the 3D mesh
 *  pipeline (Bevy `GetFullBatchData for MeshPipeline`,
 *  `bevy_pbr/src/render/mesh.rs:2040-2144`).
 *
 *  The four methods are the two halves of a mode: `get_index_and_compare_data` and
 *  `get_binned_index` are the **GPU-building** side, where the instance record
 *  already holds the index the GPU instance builder assigned, and
 *  `get_binned_batch_data` is the **CPU-building** side, where this code assembles
 *  the uniform itself. Both refuse loudly when called in the other mode rather than
 *  reading a map that is empty by construction - which is Bevy's behaviour, and the
 *  reason each is a separate method rather than one function with a branch.
 *
 *  `write_batch_indirect_parameters_metadata` is the only one that runs in either
 *  mode: it records, per batch, where the batch's output starts and which batch set
 *  it belongs to, with `!0` standing in for "no batch set" exactly as Bevy writes
 *  it. */
template <>
struct GetFullBatchData<epix::pbr::MeshPipeline> {
    using BufferInputData = epix::pbr::MeshInputUniform;
    using BufferData      = GetBatchData<epix::pbr::MeshPipeline>::BufferData;
    using CompareData     = GetBatchData<epix::pbr::MeshPipeline>::CompareData;
    using Param           = GetBatchData<epix::pbr::MeshPipeline>::Param;

    std::optional<std::pair<std::uint32_t, std::optional<CompareData>>> get_index_and_compare_data(
        Param& param, epix::render::sync_world::MainEntity main_entity) const {
        const auto& mesh_instances = std::get<0>(param);
        // This should only be called during GPU building.
        if (mesh_instances->is_cpu_building()) {
            spdlog::error(
                "[pbr] get_index_and_compare_data should never be called in CPU mesh uniform building mode; "
                "the CPU builder owns the instance data.");
            return std::nullopt;
        }
        const auto found = mesh_instances->gpu_instances.find(main_entity);
        if (found == mesh_instances->gpu_instances.end()) return std::nullopt;
        const epix::pbr::RenderMeshInstanceGpu& mesh_instance = found->second;

        const auto& lightmaps       = std::get<1>(param);
        const auto lightmap_found   = lightmaps->render_lightmaps.find(main_entity);
        const epix::pbr::RenderLightmap* maybe_lightmap =
            lightmap_found == lightmaps->render_lightmaps.end() ? nullptr : &lightmap_found->second;

        return std::make_pair(
            mesh_instance.current_uniform_index,
            mesh_instance.shared.should_batch()
                ? std::optional<CompareData>{CompareData{
                      mesh_instance.shared.material_bindings_index.group, mesh_instance.shared.mesh_asset_id,
                      maybe_lightmap ? std::optional{maybe_lightmap->slab_index} : std::nullopt}}
                : std::nullopt);
    }

    std::optional<BufferData> get_binned_batch_data(Param& param,
                                                    epix::render::sync_world::MainEntity main_entity) const {
        const auto& [mesh_instances, lightmaps, render_meshes, mesh_allocator, skin_uniforms] = param;
        // This should only be called during CPU building.
        if (!mesh_instances->is_cpu_building()) {
            spdlog::error(
                "[pbr] get_binned_batch_data should never be called in GPU mesh uniform building mode; the GPU "
                "instance builder owns the instance data.");
            return std::nullopt;
        }
        const auto found = mesh_instances->cpu_instances.find(main_entity);
        if (found == mesh_instances->cpu_instances.end()) return std::nullopt;
        const epix::pbr::RenderMeshInstanceCpu& mesh_instance = found->second;

        (void)render_meshes;
        const auto vertex_slice          = mesh_allocator->mesh_vertex_slice(mesh_instance.shared.mesh_asset_id);
        const std::uint32_t first_vertex = vertex_slice ? vertex_slice->range.first : 0u;

        const auto lightmap_found = lightmaps->render_lightmaps.find(main_entity);
        const epix::pbr::RenderLightmap* maybe_lightmap =
            lightmap_found == lightmaps->render_lightmaps.end() ? nullptr : &lightmap_found->second;
        const std::optional<std::pair<epix::pbr::LightmapSlotIndex, ::epix::image::Rect>> lightmap_for_uniform =
            maybe_lightmap ? std::optional{std::pair{maybe_lightmap->slot_index, maybe_lightmap->uv_rect}}
                           : std::nullopt;

        const auto material_bind_group = mesh_instance.shared.material_bindings_index;
        const auto current_skin_index  = skin_uniforms->skin_index(main_entity);

        return epix::pbr::MeshUniform::make(mesh_instance.transforms, first_vertex, material_bind_group.slot,
                                           lightmap_for_uniform, current_skin_index,
                                           std::optional{mesh_instance.shared.tag});
    }

    std::optional<std::uint32_t> get_binned_index(Param& param,
                                                 epix::render::sync_world::MainEntity main_entity) const {
        const auto& mesh_instances = std::get<0>(param);
        // This should only be called during GPU building.
        if (mesh_instances->is_cpu_building()) {
            spdlog::error(
                "[pbr] get_binned_index should never be called in CPU mesh uniform building mode; the CPU builder "
                "owns the instance data.");
            return std::nullopt;
        }
        const auto found = mesh_instances->gpu_instances.find(main_entity);
        if (found == mesh_instances->gpu_instances.end()) return std::nullopt;
        return found->second.current_uniform_index;
    }

    void write_batch_indirect_parameters_metadata(bool indexed, std::uint32_t base_output_index,
                                                  std::optional<std::uint32_t> batch_set_index,
                                                  UntypedPhaseIndirectParametersBuffers& phase_indirect_parameters_buffers,
                                                  std::uint32_t indirect_parameters_offset) const {
        const IndirectParametersCpuMetadata indirect_parameters{
            .base_output_index = base_output_index,
            .batch_set_index   = batch_set_index ? *batch_set_index : ~0u,
        };

        if (indexed) {
            phase_indirect_parameters_buffers.indexed_cpu_metadata.set(indirect_parameters_offset, indirect_parameters);
        } else {
            phase_indirect_parameters_buffers.non_indexed_cpu_metadata.set(indirect_parameters_offset,
                                                                           indirect_parameters);
        }
    }
};

}  // namespace epix::render::batching
