#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/shader.hpp>
#include <string>
#include <vector>
#include <epix/common.hpp>
#endif
#include <epix/pbr/mesh_pipeline_key.hpp>

namespace epix::pbr {

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
EPIX_EXPORT inline MeshPipelineVertexSpecialization specialize_mesh_vertex_layout(
    const MeshVertexAttributePresence& attributes, std::uint32_t msaa_samples) {
    MeshPipelineVertexSpecialization result;
    result.shader_defs.emplace_back("MESH_PIPELINE");
    result.shader_defs.emplace_back("VERTEX_OUTPUT_INSTANCE_INDEX");

    if (attributes.position) {
        result.shader_defs.emplace_back("VERTEX_POSITIONS");
        result.vertex_attributes.push_back({.name = "position", .shader_location = 0});
    }
    if (attributes.normal) {
        result.shader_defs.emplace_back("VERTEX_NORMALS");
        result.vertex_attributes.push_back({.name = "normal", .shader_location = 1});
    }
    if (attributes.uv_0) {
        result.shader_defs.emplace_back("VERTEX_UVS");
        result.shader_defs.emplace_back("VERTEX_UVS_A");
        result.vertex_attributes.push_back({.name = "uv_0", .shader_location = 2});
    }
    if (attributes.uv_1) {
        result.shader_defs.emplace_back("VERTEX_UVS");
        result.shader_defs.emplace_back("VERTEX_UVS_B");
        result.vertex_attributes.push_back({.name = "uv_1", .shader_location = 3});
    }
    if (attributes.tangent) {
        result.shader_defs.emplace_back("VERTEX_TANGENTS");
        result.vertex_attributes.push_back({.name = "tangent", .shader_location = 4});
    }
    if (attributes.color) {
        result.shader_defs.emplace_back("VERTEX_COLORS");
        result.vertex_attributes.push_back({.name = "color", .shader_location = 5});
    }

    if (msaa_samples > 1) {
        result.shader_defs.emplace_back("MULTISAMPLED");
    }
    return result;
}

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
    bool premultiply_alpha = false;
    bool blend_premultiplied = false;
    bool blend_multiply = false;
    bool alpha_to_coverage = false;
    bool depth_write_enabled = true;
    bool is_opaque = false;
    std::vector<std::string> shader_defs;
};

/** @brief The pass a key selects (Bevy `MeshPipeline::specialize`,
 *  `render/mesh.rs:2459-2519`). */
EPIX_EXPORT inline MeshPassState specialize_mesh_pass(const MeshPipelineKey& key) {
    MeshPassState state;
    const MeshPipelineKey pass = key.intersection(MeshPipelineKey::BLEND_RESERVED_BITS);

    if (key.contains(MeshPipelineKey::OIT_ENABLED) && pass == MeshPipelineKey::BLEND_ALPHA) {
        state.label       = "oit_mesh_pipeline";
        state.blend       = false;
        state.shader_defs.emplace_back("OIT_ENABLED");
        state.depth_write_enabled = false;
    } else if (pass == MeshPipelineKey::BLEND_ALPHA) {
        state.label = "alpha_blend_mesh_pipeline";
        state.blend = true;
        // A transparent fragment blends but must not write depth.
        state.depth_write_enabled = false;
    } else if (pass == MeshPipelineKey::BLEND_PREMULTIPLIED_ALPHA) {
        state.label              = "premultiplied_alpha_mesh_pipeline";
        state.blend              = true;
        state.premultiply_alpha  = true;
        state.blend_premultiplied = true;
        state.depth_write_enabled = false;
    } else if (pass == MeshPipelineKey::BLEND_MULTIPLY) {
        state.label               = "multiply_mesh_pipeline";
        state.blend               = true;
        state.premultiply_alpha   = true;
        state.blend_multiply      = true;
        state.depth_write_enabled = false;
    } else if (pass == MeshPipelineKey::BLEND_ALPHA_TO_COVERAGE) {
        state.label               = "alpha_to_coverage_mesh_pipeline";
        state.blend               = false;
        state.depth_write_enabled = true;
        state.is_opaque           = !key.contains(MeshPipelineKey::READS_VIEW_TRANSMISSION_TEXTURE);
        state.alpha_to_coverage   = true;
        state.shader_defs.emplace_back("ALPHA_TO_COVERAGE");
    } else {
        state.label               = "opaque_mesh_pipeline";
        state.blend               = false;
        state.depth_write_enabled = true;
        state.is_opaque           = !key.contains(MeshPipelineKey::READS_VIEW_TRANSMISSION_TEXTURE);
    }
    return state;
}

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
 *  Two Bevy conditions have no counterpart here and are noted rather than silently
 *  dropped: the WebGL2 and PCSS defs are cargo/feature-gated, and the irradiance
 *  volume def is behind a compile-time capability constant. */
EPIX_EXPORT inline std::vector<::epix::shader::ShaderDefVal> specialize_mesh_key_shader_defs(
    const MeshPipelineKey& key, bool is_opaque) {
    using Def = ::epix::shader::ShaderDefVal;

    std::vector<Def> defs;
    if (key.contains(MeshPipelineKey::SCREEN_SPACE_AMBIENT_OCCLUSION)) {
        defs.emplace_back("SCREEN_SPACE_AMBIENT_OCCLUSION");
    }
    if (key.contains(MeshPipelineKey::NORMAL_PREPASS)) defs.emplace_back("NORMAL_PREPASS");
    if (key.contains(MeshPipelineKey::DEPTH_PREPASS)) defs.emplace_back("DEPTH_PREPASS");
    if (key.contains(MeshPipelineKey::MOTION_VECTOR_PREPASS)) defs.emplace_back("MOTION_VECTOR_PREPASS");
    if (key.contains(MeshPipelineKey::HAS_PREVIOUS_SKIN)) defs.emplace_back("HAS_PREVIOUS_SKIN");
    if (key.contains(MeshPipelineKey::HAS_PREVIOUS_MORPH)) defs.emplace_back("HAS_PREVIOUS_MORPH");
    if (key.contains(MeshPipelineKey::DEFERRED_PREPASS)) defs.emplace_back("DEFERRED_PREPASS");

    // Reads prepass normals only when there is a normal prepass, no multisampling and
    // an opaque pass.
    if (key.contains(MeshPipelineKey::NORMAL_PREPASS) && key.msaa_samples() == 1 && is_opaque) {
        defs.emplace_back("LOAD_PREPASS_NORMALS");
    }

    const MeshPipelineKey view_projection = key.intersection(MeshPipelineKey::VIEW_PROJECTION_RESERVED_BITS);
    if (view_projection == MeshPipelineKey::VIEW_PROJECTION_NONSTANDARD) {
        defs.emplace_back("VIEW_PROJECTION_NONSTANDARD");
    } else if (view_projection == MeshPipelineKey::VIEW_PROJECTION_PERSPECTIVE) {
        defs.emplace_back("VIEW_PROJECTION_PERSPECTIVE");
    } else if (view_projection == MeshPipelineKey::VIEW_PROJECTION_ORTHOGRAPHIC) {
        defs.emplace_back("VIEW_PROJECTION_ORTHOGRAPHIC");
    }

    if (key.contains(MeshPipelineKey::TONEMAP_IN_SHADER)) {
        defs.emplace_back("TONEMAP_IN_SHADER");
        defs.push_back(Def::from_uint("TONEMAPPING_LUT_TEXTURE_BINDING_INDEX", 18u));
        defs.push_back(Def::from_uint("TONEMAPPING_LUT_SAMPLER_BINDING_INDEX", 19u));

        const MeshPipelineKey method = key.intersection(MeshPipelineKey::TONEMAP_METHOD_RESERVED_BITS);
        if (method == MeshPipelineKey::TONEMAP_METHOD_NONE) {
            defs.emplace_back("TONEMAP_METHOD_NONE");
        } else if (method == MeshPipelineKey::TONEMAP_METHOD_REINHARD) {
            defs.emplace_back("TONEMAP_METHOD_REINHARD");
        } else if (method == MeshPipelineKey::TONEMAP_METHOD_REINHARD_LUMINANCE) {
            defs.emplace_back("TONEMAP_METHOD_REINHARD_LUMINANCE");
        } else if (method == MeshPipelineKey::TONEMAP_METHOD_ACES_FITTED) {
            defs.emplace_back("TONEMAP_METHOD_ACES_FITTED");
        } else if (method == MeshPipelineKey::TONEMAP_METHOD_AGX) {
            defs.emplace_back("TONEMAP_METHOD_AGX");
        } else if (method == MeshPipelineKey::TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM) {
            defs.emplace_back("TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM");
        } else if (method == MeshPipelineKey::TONEMAP_METHOD_BLENDER_FILMIC) {
            defs.emplace_back("TONEMAP_METHOD_BLENDER_FILMIC");
        } else if (method == MeshPipelineKey::TONEMAP_METHOD_TONY_MC_MAPFACE) {
            defs.emplace_back("TONEMAP_METHOD_TONY_MC_MAPFACE");
        }

        // Debanding is tied to tonemapping and cannot run without it.
        if (key.contains(MeshPipelineKey::DEBAND_DITHER)) defs.emplace_back("DEBAND_DITHER");
    }

    if (key.contains(MeshPipelineKey::MAY_DISCARD)) defs.emplace_back("MAY_DISCARD");
    if (key.contains(MeshPipelineKey::ENVIRONMENT_MAP)) defs.emplace_back("ENVIRONMENT_MAP");
    // The bit is LIGHTMAPPED but the def is LIGHTMAP: Bevy's own naming, kept as is.
    if (key.contains(MeshPipelineKey::LIGHTMAPPED)) defs.emplace_back("LIGHTMAP");
    if (key.contains(MeshPipelineKey::LIGHTMAP_BICUBIC_SAMPLING)) defs.emplace_back("LIGHTMAP_BICUBIC_SAMPLING");
    if (key.contains(MeshPipelineKey::TEMPORAL_JITTER)) defs.emplace_back("TEMPORAL_JITTER");
    if (key.contains(MeshPipelineKey::VISIBILITY_RANGE_DITHER)) defs.emplace_back("VISIBILITY_RANGE_DITHER");
    if (key.contains(MeshPipelineKey::DISTANCE_FOG)) defs.emplace_back("DISTANCE_FOG");
    if (key.contains(MeshPipelineKey::ATMOSPHERE)) defs.emplace_back("ATMOSPHERE");

    // The shadow filter method is one of three, and its zero value is hardware 2x2 - so
    // a key with no filter bits selects that, as it does in Bevy.
    const MeshPipelineKey shadow_filter = key.intersection(MeshPipelineKey::SHADOW_FILTER_METHOD_RESERVED_BITS);
    if (shadow_filter == MeshPipelineKey::SHADOW_FILTER_METHOD_HARDWARE_2X2) {
        defs.emplace_back("SHADOW_FILTER_METHOD_HARDWARE_2X2");
    } else if (shadow_filter == MeshPipelineKey::SHADOW_FILTER_METHOD_GAUSSIAN) {
        defs.emplace_back("SHADOW_FILTER_METHOD_GAUSSIAN");
    } else if (shadow_filter == MeshPipelineKey::SHADOW_FILTER_METHOD_TEMPORAL) {
        defs.emplace_back("SHADOW_FILTER_METHOD_TEMPORAL");
    }

    // Screen-space specular transmission carries its blur tap count as a valued def,
    // and the mask is two bits so the four cases are exhaustive - Bevy marks the
    // default unreachable rather than choosing one.
    const MeshPipelineKey blur =
        key.intersection(MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_RESERVED_BITS);
    std::uint32_t blur_taps = 4;
    if (blur == MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_MEDIUM) {
        blur_taps = 8;
    } else if (blur == MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_HIGH) {
        blur_taps = 16;
    } else if (blur == MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_ULTRA) {
        blur_taps = 32;
    }
    defs.push_back(Def::from_uint("SCREEN_SPACE_SPECULAR_TRANSMISSION_BLUR_TAPS", blur_taps));
    return defs;
}

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
EPIX_EXPORT inline std::vector<::epix::shader::ShaderDefVal> specialize_mesh_capability_shader_defs(
    bool binding_arrays_are_usable, bool irradiance_volumes_are_usable, bool clustered_decals_are_usable,
    std::optional<std::uint32_t> per_object_buffer_batch_size) {
    using Def = ::epix::shader::ShaderDefVal;

    std::vector<Def> defs;
    if (binding_arrays_are_usable) {
        defs.emplace_back("MULTIPLE_LIGHT_PROBES_IN_ARRAY");
        defs.emplace_back("MULTIPLE_LIGHTMAPS_IN_ARRAY");
    }
    if (irradiance_volumes_are_usable) defs.emplace_back("IRRADIANCE_VOLUMES_ARE_USABLE");
    if (clustered_decals_are_usable) defs.emplace_back("CLUSTERED_DECALS_ARE_USABLE");
    if (per_object_buffer_batch_size.has_value()) {
        defs.push_back(Def::from_uint("PER_OBJECT_BUFFER_BATCH_SIZE", *per_object_buffer_batch_size));
    }
    return defs;
}

/** @brief The mesh pipeline's primitive state (Bevy `MeshPipeline::specialize`,
 *  `render/mesh.rs:2710-2715`): back-face culling and no depth clipping, with the
 *  topology from the key so a mesh's primitive topology reaches the pipeline. */
EPIX_EXPORT inline wgpu::PrimitiveState mesh_pipeline_primitive_state(
    wgpu::PrimitiveTopology topology) {
    wgpu::PrimitiveState state;
    state.setTopology(topology).setCullMode(wgpu::CullMode::eBack).setUnclippedDepth(false);
    return state;
}

/** @brief The mesh pipeline's depth state (Bevy `MeshPipeline::specialize`,
 *  `render/mesh.rs:2716-2731`).
 *
 *  The compare is `GreaterEqual` because the engine renders with an infinite
 *  reverse-Z projection: near is depth 1 and far is 0, so a fragment passes when it
 *  is nearer than what is stored, which is the *opposite* of a conventional
 *  `Less`-compare pipeline. Stencil is ignored with zero masks, and the depth bias is
 *  zero - per-light bias is applied in the vertex transform, not here. */
EPIX_EXPORT inline wgpu::DepthStencilState mesh_pipeline_depth_state(wgpu::TextureFormat format,
                                                                    bool depth_write_enabled) {
    wgpu::DepthStencilState state;
    state.setFormat(format)
        .setDepthWriteEnabled(depth_write_enabled ? wgpu::OptionalBool::eTrue : wgpu::OptionalBool::eFalse)
        .setDepthCompare(wgpu::CompareFunction::eGreaterEqual)
        .setStencilFront(wgpu::StencilFaceState{}.setCompare(wgpu::CompareFunction::eAlways))
        .setStencilBack(wgpu::StencilFaceState{}.setCompare(wgpu::CompareFunction::eAlways))
        .setStencilReadMask(0)
        .setStencilWriteMask(0)
        .setDepthBias(0)
        .setDepthBiasSlopeScale(0.0f)
        .setDepthBiasClamp(0.0f);
    return state;
}

/** @brief The mesh pipeline's multisample state (Bevy `MeshPipeline::specialize`,
 *  `render/mesh.rs:2732-2736`): the sample count from the key, the full sample mask,
 *  and alpha-to-coverage only for the pass that asked for it. */
EPIX_EXPORT inline wgpu::MultisampleState mesh_pipeline_multisample_state(std::uint32_t msaa_samples,
                                                                         bool alpha_to_coverage_enabled) {
    wgpu::MultisampleState state;
    state.setCount(msaa_samples).setMask(0xFFFFFFFFu).setAlphaToCoverageEnabled(alpha_to_coverage_enabled);
    return state;
}

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

/** @brief The defs, skinned vertex attributes and layout a mesh needs (Bevy
 *  `setup_morph_and_skinning_defs`, `render/mesh.rs:2312-2370`).
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
EPIX_EXPORT inline MeshMorphSkinningSetup setup_morph_and_skinning_defs(const MeshPipelineKey& key, bool is_skinned,
                                                                       std::uint32_t offset,
                                                                       bool skins_use_uniform_buffers) {
    using Def = ::epix::shader::ShaderDefVal;

    const bool is_morphed            = key.intersects(MeshPipelineKey::MORPH_TARGETS);
    const bool is_lightmapped        = key.intersects(MeshPipelineKey::LIGHTMAPPED);
    const bool motion_vector_prepass = key.intersects(MeshPipelineKey::MOTION_VECTOR_PREPASS);

    MeshMorphSkinningSetup setup;
    if (skins_use_uniform_buffers) setup.shader_defs.emplace_back("SKINS_USE_UNIFORM_BUFFERS");

    const auto add_skin_data = [&] {
        setup.shader_defs.emplace_back("SKINNED");
        setup.vertex_attributes.push_back({.name = "joint_index", .shader_location = offset});
        setup.vertex_attributes.push_back({.name = "joint_weight", .shader_location = offset + 1});
        setup.skinned = true;
    };
    const auto add_morph = [&] { setup.shader_defs.emplace_back("MORPH_TARGETS"); };

    if (is_skinned && !is_morphed && motion_vector_prepass) {
        add_skin_data();
        setup.layout = MeshLayoutKind::SkinnedMotion;
    } else if (is_skinned && !is_morphed) {
        add_skin_data();
        setup.layout = MeshLayoutKind::Skinned;
    } else if (is_skinned && is_morphed && motion_vector_prepass) {
        add_skin_data();
        add_morph();
        setup.layout = MeshLayoutKind::MorphedSkinnedMotion;
    } else if (is_skinned && is_morphed) {
        add_skin_data();
        add_morph();
        setup.layout = MeshLayoutKind::MorphedSkinned;
    } else if (is_morphed && motion_vector_prepass) {
        add_morph();
        setup.layout = MeshLayoutKind::MorphedMotion;
    } else if (is_morphed) {
        add_morph();
        setup.layout = MeshLayoutKind::Morphed;
    } else if (is_lightmapped) {
        setup.layout = MeshLayoutKind::Lightmapped;
    } else {
        setup.layout = MeshLayoutKind::ModelOnly;
    }
    return setup;
}

}  // namespace epix::pbr
