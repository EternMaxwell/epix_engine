#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/mesh/mesh.hpp>
#include <epix/render.hpp>
#include <epix/render/mesh/render_asset.hpp>
#include <epix/transform.hpp>
#include <expected>
#include <format>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif
#include <epix/sprite_render/material2d.hpp>

namespace epix::sprite_render {
using mesh::Mesh;
using mesh::Mesh2d;

/** @brief One main-entity keyed instance per visible 2D mesh (Bevy
 *  `extract_mesh2d`, `mesh.rs:249-282`). Defined by `mesh2d.cpp`; the render
 *  plugin registers it from `mesh2d_pipeline.cpp`. */
EPIX_EXPORT EPIX_API void extract_mesh2d(
    ecs::ResMut<RenderMesh2dInstances> render_mesh_instances,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity,
                                      const mesh::Mesh2d&,
                                      const transform::GlobalTransform&,
                                      const camera::ViewVisibility&,
                                      ecs::Opt<const mesh::MeshTag&>,
                                      ecs::Has<render::phase::NoAutomaticBatching>>>> meshes);

/** @brief Plugin that sets up 2D mesh extraction, batching, and rendering. */
EPIX_EXPORT struct EPIX_API Mesh2dRenderPlugin {
    void attach(app::App& app);

   private:
    /** @brief Register the mesh2d shader and add `init_mesh_2d_pipeline` to the
     *  render app's `RenderStartup` (Bevy `Mesh2dRenderPlugin::build`'s
     *  `embedded_asset!` and RenderStartup wiring). Defined by
     *  `mesh2d_pipeline.cpp`, which owns the embedded shader source. */
    void attach_mesh2d_pipeline(app::App& app);
};
}  // namespace epix::sprite_render
