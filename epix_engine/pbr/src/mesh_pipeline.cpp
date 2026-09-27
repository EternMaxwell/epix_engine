#include <array>
#include <epix/core_graph/core3d.hpp>
#include <epix/image.hpp>
#include <epix/pbr/mesh_batching.hpp>
#include <epix/pbr/mesh_pipeline.hpp>
#include <epix/pbr/mesh_pipeline_resource.hpp>
#include <epix/render/view.hpp>
#include <string>
#include <vector>

// The adapter must satisfy the customization point's own concept, as the 2D
// pipeline's does.
static_assert(epix::render::batching::GetBatchDataImpl<epix::pbr::MeshPipeline>,
              "GetBatchData<MeshPipeline> must satisfy its own concept");

namespace epix::pbr {

MeshVertexAttributePresence mesh_vertex_attribute_presence(
    const mesh::MeshVertexBufferLayoutRef& layout) {
    return MeshVertexAttributePresence{
        .position = layout.value->contains(mesh::Mesh::ATTRIBUTE_POSITION),
        .normal   = layout.value->contains(mesh::Mesh::ATTRIBUTE_NORMAL),
        .uv_0     = layout.value->contains(mesh::Mesh::ATTRIBUTE_UV_0),
        .uv_1     = layout.value->contains(mesh::Mesh::ATTRIBUTE_UV_1),
        .tangent  = layout.value->contains(mesh::Mesh::ATTRIBUTE_TANGENT),
        .color    = layout.value->contains(mesh::Mesh::ATTRIBUTE_COLOR),
    };
}

MeshPipelineVertexSpecialization specialize_mesh_vertex_layout(
    const mesh::MeshVertexBufferLayoutRef& layout, std::uint32_t msaa_samples) {
    const MeshVertexAttributePresence attributes = mesh_vertex_attribute_presence(layout);
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

MeshPassState specialize_mesh_pass(const MeshPipelineKey& key) {
    MeshPassState state;
    const MeshPipelineKey pass = key.intersection(MeshPipelineKey::BLEND_RESERVED_BITS);

    if (key.contains(MeshPipelineKey::OIT_ENABLED) && pass == MeshPipelineKey::BLEND_ALPHA) {
        state.label = "oit_mesh_pipeline";
        state.blend = false;
        state.shader_defs.emplace_back("OIT_ENABLED");
        state.depth_write_enabled = false;
    } else if (pass == MeshPipelineKey::BLEND_ALPHA) {
        state.label = "alpha_blend_mesh_pipeline";
        state.blend = true;
        // A transparent fragment blends but must not write depth.
        state.depth_write_enabled = false;
    } else if (pass == MeshPipelineKey::BLEND_PREMULTIPLIED_ALPHA) {
        state.label               = "premultiplied_alpha_mesh_pipeline";
        state.blend               = true;
        state.premultiply_alpha   = true;
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

std::vector<::epix::shader::ShaderDefVal> specialize_mesh_key_shader_defs(const MeshPipelineKey& key,
                                                                                             bool is_opaque) {
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
    // The def needs the key bit *and* the platform capability, exactly as Bevy gates
    // it (`render/mesh.rs:2609-2611`).
    if (key.contains(MeshPipelineKey::IRRADIANCE_VOLUME) && IRRADIANCE_VOLUMES_ARE_USABLE) {
        defs.emplace_back("IRRADIANCE_VOLUME");
    }
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
    const MeshPipelineKey blur = key.intersection(MeshPipelineKey::SCREEN_SPACE_SPECULAR_TRANSMISSION_RESERVED_BITS);
    std::uint32_t blur_taps    = 4;
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

std::vector<::epix::shader::ShaderDefVal> specialize_mesh_capability_shader_defs(
    bool binding_arrays_are_usable,
    bool irradiance_volumes_are_usable,
    bool clustered_decals_are_usable,
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

wgpu::PrimitiveState mesh_pipeline_primitive_state(wgpu::PrimitiveTopology topology) {
    wgpu::PrimitiveState state;
    state.setTopology(topology).setCullMode(wgpu::CullMode::eBack).setUnclippedDepth(false);
    return state;
}

wgpu::DepthStencilState mesh_pipeline_depth_state(wgpu::TextureFormat format,
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

wgpu::MultisampleState mesh_pipeline_multisample_state(std::uint32_t msaa_samples,
                                                                          bool alpha_to_coverage_enabled) {
    wgpu::MultisampleState state;
    state.setCount(msaa_samples).setMask(0xFFFFFFFFu).setAlphaToCoverageEnabled(alpha_to_coverage_enabled);
    return state;
}

MeshMorphSkinningSetup setup_morph_and_skinning_defs(const MeshPipelineKey& key,
                                                                        const mesh::MeshVertexBufferLayoutRef& layout,
                                                                        std::uint32_t offset,
                                                                        bool skins_use_uniform_buffers) {
    using Def = ::epix::shader::ShaderDefVal;

    const bool is_morphed            = key.intersects(MeshPipelineKey::MORPH_TARGETS);
    const bool is_lightmapped        = key.intersects(MeshPipelineKey::LIGHTMAPPED);
    const bool motion_vector_prepass = key.intersects(MeshPipelineKey::MOTION_VECTOR_PREPASS);
    const bool skinned               = is_skinned(layout);

    MeshMorphSkinningSetup setup;
    if (skins_use_uniform_buffers) setup.shader_defs.emplace_back("SKINS_USE_UNIFORM_BUFFERS");

    const auto add_skin_data = [&] {
        setup.shader_defs.emplace_back("SKINNED");
        setup.vertex_attributes.push_back({.name = "joint_index", .shader_location = offset});
        setup.vertex_attributes.push_back({.name = "joint_weight", .shader_location = offset + 1});
        setup.skinned = true;
    };
    const auto add_morph = [&] { setup.shader_defs.emplace_back("MORPH_TARGETS"); };

    if (skinned && !is_morphed && motion_vector_prepass) {
        add_skin_data();
        setup.layout = MeshLayoutKind::SkinnedMotion;
    } else if (skinned && !is_morphed) {
        add_skin_data();
        setup.layout = MeshLayoutKind::Skinned;
    } else if (skinned && is_morphed && motion_vector_prepass) {
        add_skin_data();
        add_morph();
        setup.layout = MeshLayoutKind::MorphedSkinnedMotion;
    } else if (skinned && is_morphed) {
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

namespace {

/** The mesh bind group layout a specialization's morph and skinning key selects
 *  (Bevy indexes `MeshLayouts` with the value its
 *  `setup_morph_and_skinning_defs` returns, `render/mesh.rs:2443-2451`). */
const render::BindGroupLayoutDescriptor& mesh_layout_for(const MeshLayouts& layouts, MeshLayoutKind kind) {
    switch (kind) {
        case MeshLayoutKind::Lightmapped:
            return layouts.lightmapped;
        case MeshLayoutKind::Morphed:
            return layouts.morphed;
        case MeshLayoutKind::MorphedMotion:
            return layouts.morphed_motion;
        case MeshLayoutKind::Skinned:
            return layouts.skinned;
        case MeshLayoutKind::SkinnedMotion:
            return layouts.skinned_motion;
        case MeshLayoutKind::MorphedSkinned:
            return layouts.morphed_skinned;
        case MeshLayoutKind::MorphedSkinnedMotion:
            return layouts.morphed_skinned_motion;
        case MeshLayoutKind::ModelOnly:
            break;
    }
    return layouts.model_only;
}

}  // namespace

std::expected<render::RenderPipelineDescriptor, render::SpecializedMeshPipelineError> MeshPipeline::specialize(
    const MeshPipelineKey& key, const mesh::MeshVertexBufferLayoutRef& layout) const {
    // Bevy `SpecializedMeshPipeline for MeshPipeline` (`render/mesh.rs:2372-2740`).
    // The definitions and attributes come from the helpers this module already
    // had, in Bevy's order: the mesh pipeline marker and the vertex attributes
    // first, then the build-time features, then the morph and skinning step,
    // whose layout the key selects, then the pass, projection, prepass and
    // tonemapping definitions, and finally the device capabilities.
    using Def = shader::ShaderDefVal;

    const MeshPipelineVertexSpecialization vertex = specialize_mesh_vertex_layout(layout, key.msaa_samples());
    const MeshPassState pass                      = specialize_mesh_pass(key);

    std::vector<Def> shader_defs;
    const auto append_names = [&shader_defs](const std::vector<std::string>& names) {
        for (const auto& name : names) shader_defs.push_back(Def::from_bool(name));
    };
    append_names(vertex.shader_defs);

    // Bevy pushes four texture-feature definitions here, each behind a `bevy_pbr`
    // cargo feature that its default feature set does not enable
    // (`bevy_pbr/Cargo.toml:14-24`, reached from `default = ["2d", "3d", "ui"]`),
    // so a default build emits none of them and neither does this port
    // (`PBR-244`).

    const MeshMorphSkinningSetup morph_and_skinning =
        setup_morph_and_skinning_defs(key, layout, 6, skins_use_uniform_buffers);
    shader_defs.insert(shader_defs.end(), morph_and_skinning.shader_defs.begin(), morph_and_skinning.shader_defs.end());

    {
        auto key_defs = specialize_mesh_key_shader_defs(key, pass.is_opaque);
        shader_defs.insert(shader_defs.end(), key_defs.begin(), key_defs.end());
    }
    {
        auto capability_defs =
            specialize_mesh_capability_shader_defs(binding_arrays_are_usable, IRRADIANCE_VOLUMES_ARE_USABLE,
                                                   clustered_decals_are_usable, per_object_buffer_batch_size);
        shader_defs.insert(shader_defs.end(), capability_defs.begin(), capability_defs.end());
    }
    append_names(pass.shader_defs);

    // The vertex buffer layout is resolved from the attributes the mesh has, at
    // their fixed locations (Bevy's `layout.0.get_layout(&vertex_attributes)`).
    const MeshVertexAttributePresence attributes = mesh_vertex_attribute_presence(layout);
    std::vector<mesh::VertexAttributeDescriptor> vertex_attributes;
    if (attributes.position) vertex_attributes.push_back(mesh::Mesh::ATTRIBUTE_POSITION.at_shader_location(0));
    if (attributes.normal) vertex_attributes.push_back(mesh::Mesh::ATTRIBUTE_NORMAL.at_shader_location(1));
    if (attributes.uv_0) vertex_attributes.push_back(mesh::Mesh::ATTRIBUTE_UV_0.at_shader_location(2));
    if (attributes.uv_1) vertex_attributes.push_back(mesh::Mesh::ATTRIBUTE_UV_1.at_shader_location(3));
    if (attributes.tangent) vertex_attributes.push_back(mesh::Mesh::ATTRIBUTE_TANGENT.at_shader_location(4));
    if (attributes.color) vertex_attributes.push_back(mesh::Mesh::ATTRIBUTE_COLOR.at_shader_location(5));
    if (morph_and_skinning.skinned) {
        vertex_attributes.push_back(mesh::Mesh::ATTRIBUTE_JOINT_INDEX.at_shader_location(6));
        vertex_attributes.push_back(mesh::Mesh::ATTRIBUTE_JOINT_WEIGHT.at_shader_location(7));
    }

    auto vertex_buffer_layout = layout.value->get_layout(vertex_attributes);
    if (!vertex_buffer_layout) return std::unexpected(vertex_buffer_layout.error());

    std::vector<mesh::VertexBufferLayout> vertex_buffers;
    if (!vertex_buffer_layout->attributes.empty()) vertex_buffers.push_back(*vertex_buffer_layout);

    // HDR views render to the HDR format, the rest to the buffer's default
    // (Bevy `render/mesh.rs:2676-2680`).
    const wgpu::TextureFormat format = key.contains(MeshPipelineKey::HDR) ? render::view::ViewTarget::TEXTURE_FORMAT_HDR
                                                                          : image::bevy_default<wgpu::TextureFormat>();

    render::VertexState vertex_state{.shader = shader};
    vertex_state.set_buffers(vertex_buffers);
    vertex_state.set_shader_defs(shader_defs);
    // The embedded mesh shader names its entry points `vertex` and `fragment`,
    // and wgpu does not infer them.
    vertex_state.set_entry_point("vertex");

    auto color_target = wgpu::ColorTargetState().setFormat(format).setWriteMask(wgpu::ColorWriteMask::eAll);
    if (pass.blend) {
        auto color_blend = wgpu::BlendComponent()
                               .setOperation(wgpu::BlendOperation::eAdd)
                               .setSrcFactor(wgpu::BlendFactor::eSrcAlpha)
                               .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha);
        auto alpha_blend = wgpu::BlendComponent()
                               .setOperation(wgpu::BlendOperation::eAdd)
                               .setSrcFactor(wgpu::BlendFactor::eOne)
                               .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha);
        color_target.setBlend(wgpu::BlendState().setColor(color_blend).setAlpha(alpha_blend));
    }

    render::FragmentState fragment_state{.shader = shader};
    fragment_state.add_target(color_target);
    fragment_state.set_shader_defs(shader_defs);
    fragment_state.set_entry_point("fragment");

    const MeshPipelineViewLayout& view_layout = get_view_layout(MeshPipelineViewLayoutKey::from_pipeline_key(key));
    return render::RenderPipelineDescriptor{
        .label = pass.label,
        .layouts =
            std::vector<render::BindGroupLayoutDescriptor>{view_layout.main_layout, view_layout.binding_array_layout,
                                                           mesh_layout_for(mesh_layouts, morph_and_skinning.layout)},
        .vertex    = std::move(vertex_state),
        .primitive = wgpu::PrimitiveState()
                         .setTopology(key.primitive_topology())
                         .setFrontFace(wgpu::FrontFace::eCCW)
                         .setCullMode(wgpu::CullMode::eBack),
        // Core3d clears the reverse-Z depth buffer to 0, so greater-equal.
        .depth_stencil =
            wgpu::DepthStencilState()
                .setFormat(core_graph::core_3d::CORE_3D_DEPTH_FORMAT)
                .setDepthWriteEnabled(pass.depth_write_enabled ? wgpu::OptionalBool::eTrue : wgpu::OptionalBool::eFalse)
                .setDepthCompare(wgpu::CompareFunction::eGreaterEqual)
                .setStencilFront(wgpu::StencilFaceState()
                                     .setCompare(wgpu::CompareFunction::eAlways)
                                     .setFailOp(wgpu::StencilOperation::eKeep)
                                     .setDepthFailOp(wgpu::StencilOperation::eKeep)
                                     .setPassOp(wgpu::StencilOperation::eKeep))
                .setStencilBack(wgpu::StencilFaceState()
                                    .setCompare(wgpu::CompareFunction::eAlways)
                                    .setFailOp(wgpu::StencilOperation::eKeep)
                                    .setDepthFailOp(wgpu::StencilOperation::eKeep)
                                    .setPassOp(wgpu::StencilOperation::eKeep))
                .setStencilReadMask(0)
                .setStencilWriteMask(0),
        .multisample = wgpu::MultisampleState()
                           .setCount(key.msaa_samples())
                           .setMask(~0u)
                           .setAlphaToCoverageEnabled(pass.alpha_to_coverage),
        .fragment    = std::move(fragment_state),
    };
}

}  // namespace epix::pbr
