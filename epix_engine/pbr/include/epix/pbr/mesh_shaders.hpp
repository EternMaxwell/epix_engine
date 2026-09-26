#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/shader.hpp>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief The embedded path of the mesh type declarations (Bevy
 *  `render/mesh_types.wgsl`). */
EPIX_EXPORT inline constexpr std::string_view MESH_TYPES_SHADER_PATH = "pbr/mesh_types.slang";
/** @brief The embedded path of the per-object mesh bindings (Bevy
 *  `render/mesh_bindings.wgsl`). */
EPIX_EXPORT inline constexpr std::string_view MESH_BINDINGS_SHADER_PATH = "pbr/mesh_bindings.slang";
/** @brief The embedded path of the forward pass's vertex input and output
 *  (Bevy `render/forward_io.wgsl`). */
EPIX_EXPORT inline constexpr std::string_view FORWARD_IO_SHADER_PATH = "pbr/forward_io.slang";
/** @brief The embedded path of the group-0 view types (Bevy
 *  `render/mesh_view_types.wgsl`). */
EPIX_EXPORT inline constexpr std::string_view MESH_VIEW_TYPES_SHADER_PATH = "pbr/mesh_view_types.slang";
/** @brief The embedded path of the atmosphere shader types (Bevy
 *  `atmosphere/types.wgsl`), which the view bindings import. */
EPIX_EXPORT inline constexpr std::string_view ATMOSPHERE_TYPES_SHADER_PATH = "pbr/atmosphere_types.slang";
/** @brief The embedded path of the group-0 and group-1 view bindings (Bevy
 *  `render/mesh_view_bindings.wgsl`). */
EPIX_EXPORT inline constexpr std::string_view MESH_VIEW_BINDINGS_SHADER_PATH = "pbr/mesh_view_bindings.slang";
/** @brief The embedded path of the occlusion-culling helpers (Bevy
 *  `render/occlusion_culling.wgsl`). */
EPIX_EXPORT inline constexpr std::string_view OCCLUSION_CULLING_SHADER_PATH = "pbr/occlusion_culling.slang";
/** @brief The embedded path of the morph-target helpers (Bevy
 *  `render/morph.wgsl`). */
EPIX_EXPORT inline constexpr std::string_view MORPH_SHADER_PATH = "pbr/morph.slang";
/** @brief The embedded path of the skinning helpers (Bevy
 *  `render/skinning.wgsl`). */
EPIX_EXPORT inline constexpr std::string_view SKINNING_SHADER_PATH = "pbr/skinning.slang";
/** @brief The embedded path of the previous frame's view matrices (Bevy
 *  `prepass/prepass_bindings.wgsl`). */
EPIX_EXPORT inline constexpr std::string_view PREPASS_BINDINGS_SHADER_PATH = "pbr/prepass_bindings.slang";
/** @brief The embedded path of the view-space conversion helpers (Bevy
 *  `render/view_transformations.wgsl`). */
EPIX_EXPORT inline constexpr std::string_view VIEW_TRANSFORMATIONS_SHADER_PATH = "pbr/view_transformations.slang";
/** @brief The embedded path of the mesh transform and attribute helpers (Bevy
 *  `render/mesh_functions.wgsl`). */
EPIX_EXPORT inline constexpr std::string_view MESH_FUNCTIONS_SHADER_PATH = "pbr/mesh_functions.slang";
/** @brief The embedded path of the forward mesh shader (Bevy
 *  `render/mesh.wgsl`), the vertex and fragment entry points a pipeline
 *  compiles. */
EPIX_EXPORT inline constexpr std::string_view MESH_SHADER_PATH = "pbr/mesh.slang";

/** @brief Handles keeping the mesh shader modules loaded.
 *
 * The embedded registry only publishes bytes; the shader processor resolves
 * `import epix.mesh_types;` through the asset server, so the modules must be
 * *loaded* and the handles held alive. `MeshPipeline::shader` (a later batch)
 * holds the main shader; these are the libraries it imports. */
EPIX_EXPORT struct MeshShaderHandles {
    assets::Handle<shader::Shader> mesh_types;
    assets::Handle<shader::Shader> mesh_bindings;
    assets::Handle<shader::Shader> forward_io;
    assets::Handle<shader::Shader> mesh_view_types;
    assets::Handle<shader::Shader> atmosphere_types;
    assets::Handle<shader::Shader> mesh_view_bindings;
    assets::Handle<shader::Shader> occlusion_culling;
    assets::Handle<shader::Shader> morph;
    assets::Handle<shader::Shader> skinning;
    assets::Handle<shader::Shader> prepass_bindings;
    assets::Handle<shader::Shader> view_transformations;
    assets::Handle<shader::Shader> mesh_functions;
    /** The forward mesh shader's entry points (Bevy's `mesh.wgsl`). */
    assets::Handle<shader::Shader> mesh;
};

/** @brief Publish the mesh shader modules as embedded assets and load them
 *  (Bevy `MeshRenderPlugin::build`'s `load_shader_library!` calls,
 *  `render/mesh.rs:131-150`, together with the modules they import).
 *
 * Registered by tests until a plugin can register Bevy's actual set, the same
 * arrangement the light systems have. */
EPIX_EXPORT EPIX_API void register_mesh_shaders(ecs::World& world);

}  // namespace epix::pbr
