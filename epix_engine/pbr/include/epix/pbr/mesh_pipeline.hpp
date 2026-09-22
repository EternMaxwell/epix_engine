#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <string>
#include <vector>
#include <epix/common.hpp>
#endif

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

}  // namespace epix::pbr
