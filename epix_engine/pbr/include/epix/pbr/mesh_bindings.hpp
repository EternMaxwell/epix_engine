#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/mesh/morph.hpp>
#include <epix/render/bind_group_layout.hpp>
#include <epix/render/bind_group_layout_entries.hpp>
#include <epix/render/assets.hpp>
#include <epix/render/mesh/render_asset.hpp>
#include <epix/render/mesh/render_mesh.hpp>
#include <epix/render/pipeline_server.hpp>
#include <epix/render/render_resource.hpp>
#include <glm/glm.hpp>
#include <limits>
#include <optional>
#include <typeindex>
#include <unordered_map>
#include <variant>
#include <webgpu/webgpu.hpp>
#include <epix/assets/id.hpp>
#include <epix/meta.hpp>
#endif

#include <epix/pbr/lightmap.hpp>
#include <epix/pbr/mesh_uniform.hpp>
#include <epix/pbr/morph.hpp>
#include <epix/pbr/skin.hpp>

namespace epix::pbr {

/** @brief The size of one morph weight, in bytes (Bevy `MORPH_WEIGHT_SIZE`,
 *  `render/mesh_bindings.rs:12`). */
inline constexpr std::size_t MORPH_WEIGHT_SIZE = sizeof(float);

/** @brief The size of the morph weight buffer the layout declares (Bevy
 *  `MORPH_BUFFER_SIZE`, `render/mesh_bindings.rs:19`).
 *
 *  Bevy's value is chosen because it is guaranteed to work everywhere: a larger
 *  one would need a runtime check against the device's limits, which would mean
 *  the size could no longer be a constant. */
EPIX_EXPORT inline constexpr std::size_t MORPH_BUFFER_SIZE = mesh::MAX_MORPH_WEIGHTS * MORPH_WEIGHT_SIZE;

/** @brief The size of one joint matrix, in bytes (Bevy `JOINT_SIZE`,
 *  `render/mesh_bindings.rs:21`). */
inline constexpr std::size_t JOINT_SIZE = sizeof(glm::mat4);

/** @brief The size of the joint buffer the layout declares (Bevy
 *  `JOINT_BUFFER_SIZE`, `render/mesh_bindings.rs:22`). */
EPIX_EXPORT inline constexpr std::size_t JOINT_BUFFER_SIZE = MAX_JOINTS * JOINT_SIZE;

/** @brief Whether a mesh's vertex layout carries joints (Bevy `is_skinned`,
 *  `render/mesh.rs:2308-2311`).
 *
 *  It lives beside the bind groups rather than in `mesh_pipeline.hpp` because that header
 *  includes this one, so the predicate could not be reached from here otherwise. */
EPIX_EXPORT inline bool is_skinned(const mesh::MeshVertexBufferLayoutRef& layout) {
    return layout.value->contains(mesh::Mesh::ATTRIBUTE_JOINT_INDEX) &&
           layout.value->contains(mesh::Mesh::ATTRIBUTE_JOINT_WEIGHT);
}


/** @brief Every bind group layout the default mesh shader can be specialized
 *  with (Bevy `MeshLayouts`, `render/mesh_bindings.rs:165-198`).
 *
 *  Eight layouts, because the model uniform is always group 1 binding 0 and the
 *  optional parts - joints, morph weights, morph target textures, lightmaps - are
 *  added at fixed binding numbers, so a shader can name them without knowing which
 *  mesh it is drawing. The two motion-vector layouts add bindings 6 and 7 for the
 *  previous frame's joints and weights. */
EPIX_EXPORT struct EPIX_API MeshLayouts {
    /** @brief The mesh model uniform (transform) and nothing else. */
    render::BindGroupLayoutDescriptor model_only;
    /** @brief Also the lightmap texture and sampler, one or a slab's worth. */
    render::BindGroupLayoutDescriptor lightmapped;
    /** @brief Also the joint matrices. */
    render::BindGroupLayoutDescriptor skinned;
    /** @brief Like `skinned`, plus the previous frame's joint matrices. */
    render::BindGroupLayoutDescriptor skinned_motion;
    /** @brief Also the morph weights and target textures. */
    render::BindGroupLayoutDescriptor morphed;
    /** @brief Like `morphed`, plus the previous frame's morph weights. */
    render::BindGroupLayoutDescriptor morphed_motion;
    /** @brief Both the joints and the morph targets. */
    render::BindGroupLayoutDescriptor morphed_skinned;
    /** @brief Both, plus the previous frame's joints and morph weights. */
    render::BindGroupLayoutDescriptor morphed_skinned_motion;

    /** @brief Build the eight layouts for a device (Bevy `MeshLayouts::new`).
     *
     *  Only the limits and one device capability are needed, not the device
     *  itself: the layouts are descriptors, and the pipeline cache interns the
     *  native layouts from them. `binding_arrays_are_usable` is Bevy's
     *  `binding_arrays_are_usable(render_device, render_adapter)`, which
     *  `MeshPipeline` owns in Bevy; it is a parameter here for the same reason
     *  `specialize_mesh_capability_shader_defs` takes the other capabilities. */
    static MeshLayouts make(const wgpu::Limits& limits, bool binding_arrays_are_usable);

    // ---------- Bind group creation (Bevy `render/mesh_bindings.rs:370-562`) ---
    //
    // One per layout: the model buffer, then whichever of the joint buffers, the
    // morph weight buffers and the morph target texture the layout declares. Bevy
    // reads the device's limits for the joint entries' size; these take that fact
    // as the joint entry does. The methods are named `create_*` because Bevy
    // gives a field and a method the same name (`model_only`) and the fields are
    // public, so C++ cannot have both. The lightmapped layout's method is not
    // here: its bindless branch needs the binding-array entries (`PBR-319`).
    wgpu::BindGroup create_model_only(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                               const render::render_resource::BufferBinding& model) const;
    wgpu::BindGroup create_skinned(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                            bool skins_use_uniform_buffers, const render::render_resource::BufferBinding& model,
                            const wgpu::Buffer& current_skin) const;
    wgpu::BindGroup create_skinned_motion(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                                   bool skins_use_uniform_buffers, const render::render_resource::BufferBinding& model,
                                   const wgpu::Buffer& current_skin, const wgpu::Buffer& prev_skin) const;
    wgpu::BindGroup create_morphed(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                            const render::render_resource::BufferBinding& model, const wgpu::Buffer& current_weights,
                            const wgpu::TextureView& targets) const;
    wgpu::BindGroup create_morphed_motion(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                                   const render::render_resource::BufferBinding& model, const wgpu::Buffer& current_weights,
                                   const wgpu::TextureView& targets, const wgpu::Buffer& prev_weights) const;
    wgpu::BindGroup create_morphed_skinned(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                                    bool skins_use_uniform_buffers, const render::render_resource::BufferBinding& model,
                                    const wgpu::Buffer& current_skin, const wgpu::Buffer& current_weights,
                                    const wgpu::TextureView& targets) const;
    /** @brief The bind group for meshes carrying a lightmap (Bevy
     *  `MeshLayouts::lightmapped`, `render/mesh_bindings.rs:385-416`).
     *
     *  Bevy branches on `bindless_lightmaps`: with binding arrays usable it binds
     *  the slab's whole array, otherwise the first slot's texture view and
     *  sampler. The slab supplies either through `build_binding_arrays` or
     *  `bindings_for_first_lightmap`. */
    wgpu::BindGroup create_lightmapped(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                                       const render::render_resource::BufferBinding& model, const LightmapSlab& lightmap_slab,
                                       bool bindless_lightmaps) const;
    wgpu::BindGroup create_morphed_skinned_motion(const wgpu::Device& device, const render::PipelineServer& pipeline_server,
                                           bool skins_use_uniform_buffers, const render::render_resource::BufferBinding& model,
                                           const wgpu::Buffer& current_skin, const wgpu::Buffer& current_weights,
                                           const wgpu::TextureView& targets, const wgpu::Buffer& prev_skin,
                                           const wgpu::Buffer& prev_weights) const;

   private:
    static render::BindGroupLayoutDescriptor model_only_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor skinned_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor skinned_motion_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor morphed_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor morphed_motion_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor morphed_skinned_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor morphed_skinned_motion_layout(const wgpu::Limits& limits);
    static render::BindGroupLayoutDescriptor lightmapped_layout(const wgpu::Limits& limits,
                                                                bool binding_arrays_are_usable);
};

/** @brief A mesh's bind group in both motion-vector flavours (Bevy `MeshBindGroupPair`,
 *  `render/mesh.rs:2755-2758`).
 *
 *  Two groups rather than one because whether a view needs motion vectors is a property of
 *  the *view*, not of the mesh, and the mesh's groups are built once. */
EPIX_EXPORT struct MeshBindGroupPair {
    wgpu::BindGroup motion_vectors;
    wgpu::BindGroup no_motion_vectors;

    /** @brief The group for a view that does or does not need motion vectors (Bevy
     *  `MeshBindGroupPair::get`). */
    [[nodiscard]] const wgpu::BindGroup& get(bool motion_vectors) const {
        return motion_vectors ? this->motion_vectors : no_motion_vectors;
    }
};

/** @brief Every mesh bind group one phase needs (Bevy `MeshPhaseBindGroups`,
 *  `render/mesh.rs:2745-2753`).
 *
 *  One group serves every unskinned, unmorphed mesh with no lightmap, which is why it is a
 *  single optional field rather than a map; the variants that differ per mesh are keyed. */
EPIX_EXPORT struct MeshPhaseBindGroups {
    /** @brief The group for every mesh with no skin, morph targets or lightmap.
     *
     *  A single group rather than a pair: which motion-vector flavour a mesh needs is a
     *  property of the *view*, but the model binding itself does not change with it, so Bevy
     *  keeps one group here and only the skinned and morphed cases carry both flavours. */
    std::optional<wgpu::BindGroup> model_only;
    /** @brief The group every skinned mesh shares, when the device uses uniform skins. */
    std::optional<MeshBindGroupPair> skinned;
    /** @brief The per-mesh groups for meshes with morph targets. */
    std::unordered_map<assets::AssetId<mesh::Mesh>, MeshBindGroupPair> morph_targets;
    /** @brief The per-lightmap-slab groups, which are what the lightmap binding arrays
     *  are built for. */
    std::unordered_map<LightmapSlabIndex, wgpu::BindGroup> lightmaps;

    /** @brief Forget every group, as Bevy does at the start of a rebuild. */
    void reset() {
        model_only = std::nullopt;
        skinned    = std::nullopt;
        morph_targets.clear();
        lightmaps.clear();
    }

    /** @brief The group for one mesh in one view (Bevy `MeshPhaseBindGroups::get`,
     *  `render/mesh.rs:2780-2800`).
     *
     *  The three tests are ordered and that order is the behaviour: a morphed mesh takes its
     *  own group even when it is also skinned, a skinned mesh takes the shared skinned group
     *  even when it has a lightmap, and only an unskinned, unmorphed mesh distinguishes
     *  between its lightmap's group and the shared model-only one. */
    [[nodiscard]] const wgpu::BindGroup* get(const assets::AssetId<mesh::Mesh>& asset_id,
                                             std::optional<LightmapSlabIndex> lightmap,
                                             bool is_skinned,
                                             bool morph,
                                             bool motion_vectors) const {
        if (morph) {
            const auto found = morph_targets.find(asset_id);
            return found != morph_targets.end() ? &found->second.get(motion_vectors) : nullptr;
        }
        if (is_skinned) {
            return skinned.has_value() ? &skinned->get(motion_vectors) : nullptr;
        }
        if (lightmap.has_value()) {
            const auto found = lightmaps.find(*lightmap);
            return found != lightmaps.end() ? &found->second : nullptr;
        }
        return model_only.has_value() ? &*model_only : nullptr;
    }
};

/** @brief All the mesh bind groups for the meshes currently loaded (Bevy `MeshBindGroups`,
 *  `render/mesh.rs:2760-2769`).
 *
 *  Bevy makes this an enum because GPU mesh preprocessing keeps one set of groups *per
 *  phase* - each phase has its own instance buffers - while the CPU path needs only one set
 *  for the whole scene. The port's `RenderMeshInstances` carries the same split.
 *
 *  The per-phase map is keyed by `std::type_index` rather than this port's `meta::type_index`
 *  because the phase instance buffers it is built from key their own table that way
 *  (`batching.hpp:344`), and there is no conversion between the two. */
EPIX_EXPORT using MeshBindGroups =
    std::variant<MeshPhaseBindGroups, std::unordered_map<std::type_index, MeshPhaseBindGroups>>;

/** @brief The bind groups every mesh of one phase draws with (Bevy
 *  `prepare_mesh_bind_groups_for_phase`, `render/mesh.rs:2889-2989`).
 *
 *  One group serves every unskinned, unmorphed, unlightmapped mesh; skinned meshes share a
 *  pair, because the motion-vector flavour binds last frame's joints as well; meshes with
 *  morph targets get a pair of their own, textured with their deltas; and each lightmap slab
 *  gets a group, which is what the lightmap binding arrays exist for. Rebuilding from
 *  scratch rather than reusing allocations is Bevy's behaviour too (its own TODO). */
EPIX_EXPORT inline MeshPhaseBindGroups prepare_mesh_bind_groups_for_phase(
    const render::render_resource::BufferBinding& model,
    const render::RenderAssets<render::mesh::RenderMesh>& meshes,
    const MeshLayouts& layouts,
    bool skins_use_uniform_buffers,
    const wgpu::Device& render_device,
    const render::PipelineServer& pipeline_server,
    const SkinUniforms& skins_uniform,
    const MorphUniforms& weights_uniform,
    RenderLightmaps& render_lightmaps) {
    MeshPhaseBindGroups groups;
    groups.model_only.emplace(layouts.create_model_only(render_device, pipeline_server, model));

    groups.skinned.emplace(MeshBindGroupPair{
        .motion_vectors =
            layouts.create_skinned_motion(render_device, pipeline_server, skins_use_uniform_buffers, model,
                                          skins_uniform.current_buffer, skins_uniform.prev_buffer),
        .no_motion_vectors = layouts.create_skinned(render_device, pipeline_server, skins_use_uniform_buffers, model,
                                                    skins_uniform.current_buffer)});

    // Morph groups exist only while there are weights to read, as in Bevy: with no morph
    // weights uploaded there is nothing for a morphed group to bind, and the groups would be
    // built against buffers the device never saw.
    if (const wgpu::Buffer* weights = weights_uniform.current_buffer.buffer()) {
        const wgpu::Buffer* previous_weights = weights_uniform.prev_buffer.buffer();
        if (previous_weights == nullptr) previous_weights = weights;

        for (const auto& [id, gpu_mesh] : meshes.iter()) {
            if (!gpu_mesh.morph_targets.has_value()) continue;
            const wgpu::TextureView& targets = *gpu_mesh.morph_targets;

            if (is_skinned(gpu_mesh.layout)) {
                groups.morph_targets.emplace(
                    id, MeshBindGroupPair{
                            .motion_vectors = layouts.create_morphed_skinned_motion(
                                render_device, pipeline_server, skins_use_uniform_buffers, model,
                                skins_uniform.current_buffer, *weights, targets, skins_uniform.prev_buffer,
                                *previous_weights),
                            .no_motion_vectors = layouts.create_morphed_skinned(
                                render_device, pipeline_server, skins_use_uniform_buffers, model,
                                skins_uniform.current_buffer, *weights, targets)});
            } else {
                groups.morph_targets.emplace(
                    id, MeshBindGroupPair{
                            .motion_vectors = layouts.create_morphed_motion(render_device, pipeline_server, model,
                                                                            *weights, targets, *previous_weights),
                            .no_motion_vectors =
                                layouts.create_morphed(render_device, pipeline_server, model, *weights, targets)});
            }
        }
    }

    // One group per slab, which is the whole point of the lightmap binding arrays: a mesh
    // indexes into its slab's array rather than binding its own views.
    for (std::size_t slab_index = 0; slab_index < render_lightmaps.slabs.size(); ++slab_index) {
        groups.lightmaps.emplace(LightmapSlabIndex{static_cast<std::uint32_t>(slab_index)},
                                 layouts.create_lightmapped(render_device, pipeline_server, model,
                                                            render_lightmaps.slabs[slab_index],
                                                            render_lightmaps.bindless_supported));
    }

    return groups;
}

}  // namespace epix::pbr
