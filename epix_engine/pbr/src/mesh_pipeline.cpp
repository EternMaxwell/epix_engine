#include <epix/pbr/mesh_pipeline.hpp>
#include <epix/pbr/mesh_pipeline_resource.hpp>

#include <epix/core_graph/core3d.hpp>
#include <epix/image.hpp>
#include <epix/render/view.hpp>

#include <array>
#include <string>
#include <vector>

namespace epix::pbr {

namespace {

/** The mesh bind group layout a specialization's morph and skinning key selects
 *  (Bevy indexes `MeshLayouts` with the value its
 *  `setup_morph_and_skinning_defs` returns, `render/mesh.rs:2443-2451`). */
const render::BindGroupLayoutDescriptor& mesh_layout_for(const MeshLayouts& layouts, MeshLayoutKind kind) {
    switch (kind) {
        case MeshLayoutKind::Lightmapped: return layouts.lightmapped;
        case MeshLayoutKind::Morphed: return layouts.morphed;
        case MeshLayoutKind::MorphedMotion: return layouts.morphed_motion;
        case MeshLayoutKind::Skinned: return layouts.skinned;
        case MeshLayoutKind::SkinnedMotion: return layouts.skinned_motion;
        case MeshLayoutKind::MorphedSkinned: return layouts.morphed_skinned;
        case MeshLayoutKind::MorphedSkinnedMotion: return layouts.morphed_skinned_motion;
        case MeshLayoutKind::ModelOnly: break;
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
    shader_defs.insert(shader_defs.end(), morph_and_skinning.shader_defs.begin(),
                       morph_and_skinning.shader_defs.end());

    {
        auto key_defs = specialize_mesh_key_shader_defs(key, pass.is_opaque);
        shader_defs.insert(shader_defs.end(), key_defs.begin(), key_defs.end());
    }
    {
        auto capability_defs = specialize_mesh_capability_shader_defs(
            binding_arrays_are_usable, IRRADIANCE_VOLUMES_ARE_USABLE, clustered_decals_are_usable,
            per_object_buffer_batch_size);
        shader_defs.insert(shader_defs.end(), capability_defs.begin(), capability_defs.end());
    }
    append_names(pass.shader_defs);

    // The vertex buffer layout is resolved from the attributes the mesh has, at
    // their fixed locations (Bevy's `layout.0.get_layout(&vertex_attributes)`).
    const MeshVertexAttributePresence attributes = mesh_vertex_attribute_presence(layout);
    std::vector<mesh::VertexAttributeDescriptor> vertex_attributes;
    if (attributes.position)
        vertex_attributes.push_back(mesh::Mesh::ATTRIBUTE_POSITION.at_shader_location(0));
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
    const wgpu::TextureFormat format = key.contains(MeshPipelineKey::HDR)
                                           ? render::view::ViewTarget::TEXTURE_FORMAT_HDR
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

    const MeshPipelineViewLayout& view_layout =
        get_view_layout(MeshPipelineViewLayoutKey::from_pipeline_key(key));
    return render::RenderPipelineDescriptor{
        .label  = pass.label,
        .layouts = std::vector<render::BindGroupLayoutDescriptor>{view_layout.main_layout,
                                                                 view_layout.binding_array_layout,
                                                                 mesh_layout_for(mesh_layouts,
                                                                                 morph_and_skinning.layout)},
        .vertex  = std::move(vertex_state),
        .primitive = wgpu::PrimitiveState()
                         .setTopology(key.primitive_topology())
                         .setFrontFace(wgpu::FrontFace::eCCW)
                         .setCullMode(wgpu::CullMode::eBack),
        // Core3d clears the reverse-Z depth buffer to 0, so greater-equal.
        .depth_stencil = wgpu::DepthStencilState()
                             .setFormat(core_graph::core_3d::CORE_3D_DEPTH_FORMAT)
                             .setDepthWriteEnabled(pass.depth_write_enabled ? wgpu::OptionalBool::eTrue
                                                                            : wgpu::OptionalBool::eFalse)
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
        .fragment = std::move(fragment_state),
    };
}

}  // namespace epix::pbr
