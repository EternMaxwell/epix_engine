#pragma once

#ifndef EPIX_CXX_MODULE
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/shader.hpp>
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
/** @brief The lightmap sampling library (Bevy `lightmap.wgsl`). */
EPIX_EXPORT inline constexpr std::string_view LIGHTMAP_SHADER_PATH = "pbr/lightmap.slang";
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
    /** @brief The lightmap sampling library (Bevy `lightmap.wgsl`). */
    assets::Handle<shader::Shader> lightmap;
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


/** @brief Registers the 3D mesh render pipeline (Bevy `MeshRenderPlugin`,
 *  `bevy_pbr/src/render/mesh.rs:96-183`).
 *
 *  This is the first slice of it, and it is deliberately the part that needs no
 *  device: the embedded shader libraries and the forward mesh shader that
 *  `register_mesh_shaders` publishes (Bevy''s `load_shader_library!` calls and
 *  `embedded_asset!(app, "mesh.wgsl")`), the two `PostUpdate` batching systems, and
 *  the registration of `MeshExtractionSystems` - which until now had no owner,
 *  because Bevy attaches it here.
 *
 *  What is still missing, each with its own row: the render-app resources
 *  (`MorphUniforms`, `MorphIndices` and the two the port has not ported), the seven
 *  render-phase plugins, the `ExtractSchedule` systems, the `Render` schedule
 *  systems, and the whole `finish` half with the pipeline caches. */
EPIX_EXPORT struct EPIX_API MeshRenderPlugin {

    /** @brief Whether mesh uniforms are built on the GPU (Bevy
     *  `use_gpu_instance_buffer_builder`). Bevy''s `MeshRenderPlugin::new` starts it
     *  `false` and its plugin group passes its own value through; the port starts at
     *  `false` for the same reason, and a GPU-capable app sets it. It is additionally
     *  gated on the device actually supporting GPU preprocessing. */
    bool use_gpu_instance_buffer_builder = false;
    /** @brief Debugging flags, as Bevy carries them. Unused so far, because nothing
     *  here compiles a pipeline. */
    bool debug_flags = false;

    /** @brief Bevy's build-time hook, spelled `attach` in this port. */
    void attach(app::App& app);
    /** @brief Bevy's finish-time hook, spelled `ready` in this port. The split matters:
     *  `attach` runs before the render device exists and `ready` after it, which is
     *  why anything built from device facts belongs here (Bevy does the same work in
     *  `finish`). */
    void ready(app::App& app);
};

}  // namespace epix::pbr
