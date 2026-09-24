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
};

/** @brief Publish the mesh shader modules as embedded assets and load them
 *  (Bevy `MeshRenderPlugin::build`'s `load_shader_library!` calls,
 *  `render/mesh.rs:131-150`, together with the modules they import).
 *
 * Registered by tests until a plugin can register Bevy's actual set, the same
 * arrangement the light systems have. */
EPIX_EXPORT void register_mesh_shaders(ecs::World& world);

}  // namespace epix::pbr
