#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/common.hpp>
#include <epix/mesh.hpp>
#include <epix/shader.hpp>
#include <string>
#include <vector>
#endif
#include <epix/pbr/mesh_pipeline_key.hpp>

namespace epix::pbr {

#ifndef NDEBUG
/** @brief The texture count a mesh view layout may reach before the pipeline
 *  warns (Bevy `MESH_PIPELINE_VIEW_LAYOUT_SAFE_MAX_TEXTURES`,
 *  `render/mesh.rs:118-127`).
 *
 *  `MAX_TEXTURE_IMAGE_UNITS` in WebGL and `maxSampledTexturesPerShaderStage` in
 *  WebGPU are both 16; Bevy uses 10 because that still leaves six textures for
 *  the other bind groups in the worst case. Debug-only, like Bevy's
 *  `#[cfg(debug_assertions)]`. */
EPIX_EXPORT inline constexpr std::size_t MESH_PIPELINE_VIEW_LAYOUT_SAFE_MAX_TEXTURES = 10;
#endif

/** @brief Which attributes a mesh's vertex buffer layout provides (Bevy reads these
 *  off the layout it is handed, `render/mesh.rs:2388-2418`). */
EPIX_EXPORT struct MeshVertexAttributePresence {
    bool position = false;
    bool normal   = false;
    bool uv_0     = false;
    bool uv_1     = false;
    bool tangent  = false;
    bool color    = false;
};

/** @brief Read a mesh's attribute presence off its vertex buffer layout (Bevy
 *  `MeshPipeline::specialize`, `render/mesh.rs:2388-2418`).
 *
 *  Bevy asks the layout `contains(..)` once per attribute as it builds the defs;
 *  Epix asks the same six questions here, so the presence the specialization step
 *  reads is derived from the mesh rather than assembled by the caller. Epix's
 *  `contains` takes the attribute id that `MeshVertexAttribute` converts to. */
/** @brief Read a mesh's attribute presence off its vertex buffer layout (Bevy
 *  `MeshPipeline::specialize`, `render/mesh.rs:2388-2418`).
 *
 *  Bevy asks the layout `contains(..)` once per attribute as it builds the defs;
 *  Epix asks the same six questions here, so the presence the specialization step
 *  reads is derived from the mesh rather than assembled by the caller. Epix's
 *  `contains` takes the attribute id that `MeshVertexAttribute` converts to. */
EPIX_EXPORT EPIX_API MeshVertexAttributePresence mesh_vertex_attribute_presence(
    const mesh::MeshVertexBufferLayoutRef& layout);

/** @brief One vertex attribute at its shader location (Bevy's
 *  `Mesh::ATTRIBUTE_*.at_shader_location(n)`). */
EPIX_EXPORT struct MeshVertexAttributeLocation {
    /** @brief The attribute this entry places, as its WGSL type name. */
    std::string name;
    std::uint32_t shader_location = 0;
};

/** @brief What a mesh pipeline's specialization contributes before the material and
 *  mesh-layout steps (Bevy `MeshPipeline::specialize`, `render/mesh.rs:2380-2441`).
 *
 *  Two things always hold: the shader is told it is running in a mesh pipeline, and
 *  that the instance index is an output. Then each attribute the mesh actually has
 *  adds a def and a fixed shader location, so location numbers are the same whatever
 *  the mesh carries - a mesh without normals leaves location 1 unused rather than
 *  shifting the rest up.
 *
 *  The UV case is worth reading twice: both UV sets need the shared `VERTEX_UVS` def,
 *  and each adds its own lettered one, so a shader can require "some UVs" or a
 *  specific set. Multisampling follows the key's sample count rather than the layout.
 *
 *  Bevy also pushes four feature-gated defs here (`PBR_TRANSMISSION_TEXTURES_SUPPORTED`
 *  and friends); they exist only under Bevy's `pbr_*` cargo features, which the pinned
 *  feature set does not enable, so they have no counterpart. */
EPIX_EXPORT struct MeshPipelineVertexSpecialization {
    std::vector<std::string> shader_defs;
    std::vector<MeshVertexAttributeLocation> vertex_attributes;
};

/** @brief The shader defs and vertex attribute locations a mesh's attributes produce
 *  (Bevy `MeshPipeline::specialize`, `render/mesh.rs:2380-2441`). */
/** @brief The shader defs and vertex attribute locations a mesh's attributes produce
 *  (Bevy `MeshPipeline::specialize`, `render/mesh.rs:2380-2441`). */
EPIX_EXPORT EPIX_API MeshPipelineVertexSpecialization specialize_mesh_vertex_layout(
    const mesh::MeshVertexBufferLayoutRef& layout, std::uint32_t msaa_samples);

/** @brief The pass a mesh pipeline's key selects (Bevy `MeshPipeline::specialize`,
 *  `render/mesh.rs:2459-2519`).
 *
 *  The key's blend bits choose one of five passes, and the OIT case takes precedence
 *  over plain alpha blending: an OIT pass blends nothing itself (`BlendState::None`)
 *  because the resolved layers do the blending, which Bevy notes as a TODO about tail
 *  blending needing alpha. Everything but the blending passes writes depth.
 *
 *  `is_opaque` is not "the opaque arm": an alpha-to-coverage or plain pass that reads
 *  the view transmission texture is not opaque, because something downstream still has
 *  to composite it. That flag travels with the pipeline and is easy to set from the
 *  arm alone. */
EPIX_EXPORT struct MeshPassState {
    std::string label;
    /** @brief Whether the pass blends. Bevy passes `None` where replacement suffices. */
    bool blend = false;
    /** @brief Whether the key's blend adds the premultiply, premultiplied or multiply
     *  shader defs. */
    bool premultiply_alpha   = false;
    bool blend_premultiplied = false;
    bool blend_multiply      = false;
    bool alpha_to_coverage   = false;
    bool depth_write_enabled = true;
    bool is_opaque           = false;
    std::vector<std::string> shader_defs;
};

/** @brief The pass a key selects (Bevy `MeshPipeline::specialize`,
 *  `render/mesh.rs:2459-2519`). */
/** @brief The pass a key selects (Bevy `MeshPipeline::specialize`,
 *  `render/mesh.rs:2459-2519`). */
EPIX_EXPORT EPIX_API MeshPassState specialize_mesh_pass(const MeshPipelineKey& key);

/** @brief Whether irradiance volumes can be used on this platform (Bevy
 *  `IRRADIANCE_VOLUMES_ARE_USABLE`, `render/irradiance_volume.rs:161`:
 *  `cfg!(not(target_arch = "wasm32"))`).
 *
 *  Bevy's constant is a compile-time platform test, and every platform Epix builds
 *  for is not wasm32, so the value Bevy's `cfg!` produces here is `true`. Bevy reads
 *  the one constant in both places it gates on it, and the key-gated def below does
 *  the same; the capability helper still takes the value as an argument, because its
 *  callers model a pipeline's device capabilities as a whole. */
EPIX_EXPORT inline constexpr bool IRRADIANCE_VOLUMES_ARE_USABLE = true;

/** @brief The shader defs a key adds besides the pass and vertex ones (Bevy
 *  `MeshPipeline::specialize`, `render/mesh.rs:2453-2618`).
 *
 *  `is_opaque` is the pass's, not the arm's, because one def depends on it:
 *  `LOAD_PREPASS_NORMALS` is only pushed when the mesh reads prepass normals, which
 *  needs a normal prepass, no multisampling and an opaque pass - with anything else
 *  the prepass texture cannot be loaded. The view projection def is one of three, and
 *  `VIEW_PROJECTION_NONSTANDARD` is tested first so a key carrying it wins over the
 *  orthographic default - a key with *no* projection bits is nonstandard as well,
 *  because that constant is zero in Bevy too, though a real key always carries one.
 *  Tonemapping carries its two LUT binding indices as *valued*
 *  defs rather than bare names, and debanding is nested inside it because the shader
 *  cannot deband without tonemapping.
 *
 *  One Bevy condition has no counterpart here and is noted rather than silently
 *  dropped: the WebGL2 and PCSS defs are cargo/feature-gated. The irradiance-volume
 *  def is *not* in that group - it is gated on the key bit and a platform constant,
 *  which this port has as `IRRADIANCE_VOLUMES_ARE_USABLE`. */
/** @brief The shader defs a key adds besides the pass and vertex ones (Bevy
 *  `MeshPipeline::specialize`, `render/mesh.rs:2453-2618`).
 *
 *  `is_opaque` is the pass's, not the arm's, because one def depends on it:
 *  `LOAD_PREPASS_NORMALS` is only pushed when the mesh reads prepass normals, which
 *  needs a normal prepass, no multisampling and an opaque pass - with anything else
 *  the prepass texture cannot be loaded. The view projection def is one of three, and
 *  `VIEW_PROJECTION_NONSTANDARD` is tested first so a key carrying it wins over the
 *  orthographic default - a key with *no* projection bits is nonstandard as well,
 *  because that constant is zero in Bevy too, though a real key always carries one.
 *  Tonemapping carries its two LUT binding indices as *valued*
 *  defs rather than bare names, and debanding is nested inside it because the shader
 *  cannot deband without tonemapping.
 *
 *  One Bevy condition has no counterpart here and is noted rather than silently
 *  dropped: the WebGL2 and PCSS defs are cargo/feature-gated. The irradiance-volume
 *  def is *not* in that group - it is gated on the key bit and a platform constant,
 *  which this port has as `IRRADIANCE_VOLUMES_ARE_USABLE`. */
EPIX_EXPORT EPIX_API std::vector<::epix::shader::ShaderDefVal> specialize_mesh_key_shader_defs(const MeshPipelineKey& key,
                                                                                             bool is_opaque);

/** @brief The defs a mesh pipeline adds from what the device and the platform allow
 *  (Bevy `MeshPipeline::specialize`, `render/mesh.rs:2660-2690`).
 *
 *  These depend on the pipeline's own capabilities rather than on the key: binding
 *  arrays decide whether light probes and lightmaps can live in arrays, the
 *  irradiance-volume constant whether volumes exist at all, and clustered decals
 *  whether decals do. The per-object batch size is a *valued* def, and its absence is
 *  meaningful - no def means storage buffers are in use, so the shader declares an
 *  unbounded array.
 *
 *  Bevy's `pbr_light_textures` feature defs inside the clustered-decal branch are
 *  feature-gated and so have no counterpart here. */
/** @brief The defs a mesh pipeline adds from what the device and the platform allow
 *  (Bevy `MeshPipeline::specialize`, `render/mesh.rs:2660-2690`).
 *
 *  These depend on the pipeline's own capabilities rather than on the key: binding
 *  arrays decide whether light probes and lightmaps can live in arrays, the
 *  irradiance-volume constant whether volumes exist at all, and clustered decals
 *  whether decals do. The per-object batch size is a *valued* def, and its absence is
 *  meaningful - no def means storage buffers are in use, so the shader declares an
 *  unbounded array.
 *
 *  Bevy's `pbr_light_textures` feature defs inside the clustered-decal branch are
 *  feature-gated and so have no counterpart here. */
EPIX_EXPORT EPIX_API std::vector<::epix::shader::ShaderDefVal> specialize_mesh_capability_shader_defs(
    bool binding_arrays_are_usable,
    bool irradiance_volumes_are_usable,
    bool clustered_decals_are_usable,
    std::optional<std::uint32_t> per_object_buffer_batch_size);

/** @brief The mesh pipeline's primitive state (Bevy `MeshPipeline::specialize`,
 *  `render/mesh.rs:2710-2715`): back-face culling and no depth clipping, with the
 *  topology from the key so a mesh's primitive topology reaches the pipeline. */
/** @brief The mesh pipeline's primitive state (Bevy `MeshPipeline::specialize`,
 *  `render/mesh.rs:2710-2715`): back-face culling and no depth clipping, with the
 *  topology from the key so a mesh's primitive topology reaches the pipeline. */
EPIX_EXPORT EPIX_API wgpu::PrimitiveState mesh_pipeline_primitive_state(wgpu::PrimitiveTopology topology);

/** @brief The mesh pipeline's depth state (Bevy `MeshPipeline::specialize`,
 *  `render/mesh.rs:2716-2731`).
 *
 *  The compare is `GreaterEqual` because the engine renders with an infinite
 *  reverse-Z projection: near is depth 1 and far is 0, so a fragment passes when it
 *  is nearer than what is stored, which is the *opposite* of a conventional
 *  `Less`-compare pipeline. Stencil is ignored with zero masks, and the depth bias is
 *  zero - per-light bias is applied in the vertex transform, not here. */
/** @brief The mesh pipeline's depth state (Bevy `MeshPipeline::specialize`,
 *  `render/mesh.rs:2716-2731`).
 *
 *  The compare is `GreaterEqual` because the engine renders with an infinite
 *  reverse-Z projection: near is depth 1 and far is 0, so a fragment passes when it
 *  is nearer than what is stored, which is the *opposite* of a conventional
 *  `Less`-compare pipeline. Stencil is ignored with zero masks, and the depth bias is
 *  zero - per-light bias is applied in the vertex transform, not here. */
EPIX_EXPORT EPIX_API wgpu::DepthStencilState mesh_pipeline_depth_state(wgpu::TextureFormat format,
                                                                     bool depth_write_enabled);

/** @brief The mesh pipeline's multisample state (Bevy `MeshPipeline::specialize`,
 *  `render/mesh.rs:2732-2736`): the sample count from the key, the full sample mask,
 *  and alpha-to-coverage only for the pass that asked for it. */
/** @brief The mesh pipeline's multisample state (Bevy `MeshPipeline::specialize`,
 *  `render/mesh.rs:2732-2736`): the sample count from the key, the full sample mask,
 *  and alpha-to-coverage only for the pass that asked for it. */
EPIX_EXPORT EPIX_API wgpu::MultisampleState mesh_pipeline_multisample_state(std::uint32_t msaa_samples,
                                                                          bool alpha_to_coverage_enabled);

/** @brief Which mesh bind-group layout a key's mesh needs (Bevy's `MeshLayouts`
 *  fields, `render/mesh.rs:2312-2370`).
 *
 *  Named rather than handed back as a layout because a layout is a device object;
 *  the assembly that owns the device maps this to the real one. */
EPIX_EXPORT enum class MeshLayoutKind {
    ModelOnly,
    Lightmapped,
    Morphed,
    MorphedMotion,
    Skinned,
    SkinnedMotion,
    MorphedSkinned,
    MorphedSkinnedMotion,
};

/** @brief What the morph and skinning step contributes (Bevy
 *  `setup_morph_and_skinning_defs`, `render/mesh.rs:2312-2370`). */
EPIX_EXPORT struct MeshMorphSkinningSetup {
    std::vector<::epix::shader::ShaderDefVal> shader_defs;
    std::vector<MeshVertexAttributeLocation> vertex_attributes;
    MeshLayoutKind layout = MeshLayoutKind::ModelOnly;
    /** @brief Whether the skinned attributes were added, which is what the joint
     *  locations in `vertex_attributes` mean. */
    bool skinned = false;
};

/** @brief Whether a mesh's vertex layout provides both joint attributes (Bevy
 *  `is_skinned`, `bevy_pbr/src/render/mesh.rs:2308-2311`).
 *
 *  This is the only thing that decides whether a mesh is skinned, which is why
 *  `setup_morph_and_skinning_defs` takes the layout rather than a pre-computed
 *  flag. Bevy reads `layout.0.contains(..)`; the reference's `value` is where
 *  Epix's interned layout lives. */
/** @brief Whether a mesh's vertex layout provides both joint attributes (Bevy
 *  `is_skinned`, `bevy_pbr/src/render/mesh.rs:2308-2311`).
 *
 *  This is the only thing that decides whether a mesh is skinned, which is why
 *  `setup_morph_and_skinning_defs` takes the layout rather than a pre-computed
 *  flag. Bevy reads `layout.0.contains(..)`; the reference's `value` is where
 *  Epix's interned layout lives. */
EPIX_EXPORT EPIX_API bool is_skinned(const mesh::MeshVertexBufferLayoutRef& layout);

/** @brief The defs and vertex attributes a mesh's morph and skinning state adds
 *  (Bevy `setup_morph_and_skinning_defs`, `render/mesh.rs:2312-2370`).
 *
 *  `offset` is the shader location the mesh's own attributes ended at, so the joint
 *  index and weight land immediately after them. Bevy's one call site passes the
 *  literal `6` (`render/mesh.rs:2443-2451`) and so do Epix's callers; the parameter
 *  exists because the offset has to follow whatever attributes the caller placed.
 *
 *  The eight cases are a precedence, not a cross product. Two rules matter: the
 *  **lightmapped layout is only used when the mesh is neither skinned nor morphed**,
 *  so a skinned lightmapped mesh takes the skinned layout - there is no skinned
 *  lightmapped layout; and among the skinned and morphed families **motion vectors
 *  choose which variant**, so a morphed motion-vector mesh takes the morphed motion
 *  layout. Lightmapping is checked *after* motion only because it is the last family,
 *  not because motion outranks it. A version that treated the four flags as
 *  independent would produce a layout that does not exist. */
/** @brief The defs and vertex attributes a mesh's morph and skinning state adds
 *  (Bevy `setup_morph_and_skinning_defs`, `render/mesh.rs:2312-2370`).
 *
 *  `offset` is the shader location the mesh's own attributes ended at, so the joint
 *  index and weight land immediately after them. Bevy's one call site passes the
 *  literal `6` (`render/mesh.rs:2443-2451`) and so do Epix's callers; the parameter
 *  exists because the offset has to follow whatever attributes the caller placed.
 *
 *  The eight cases are a precedence, not a cross product. Two rules matter: the
 *  **lightmapped layout is only used when the mesh is neither skinned nor morphed**,
 *  so a skinned lightmapped mesh takes the skinned layout - there is no skinned
 *  lightmapped layout; and among the skinned and morphed families **motion vectors
 *  choose which variant**, so a morphed motion-vector mesh takes the morphed motion
 *  layout. Lightmapping is checked *after* motion only because it is the last family,
 *  not because motion outranks it. A version that treated the four flags as
 *  independent would produce a layout that does not exist. */
EPIX_EXPORT EPIX_API MeshMorphSkinningSetup setup_morph_and_skinning_defs(const MeshPipelineKey& key,
                                                                        const mesh::MeshVertexBufferLayoutRef& layout,
                                                                        std::uint32_t offset,
                                                                        bool skins_use_uniform_buffers);

}  // namespace epix::pbr
