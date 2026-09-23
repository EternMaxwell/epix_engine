
#include <spdlog/spdlog.h>

#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/sprite_render/mesh2d.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>
using namespace epix;
using namespace epix::ecs;
using namespace epix::app;
using namespace epix::mesh;
using namespace epix::sprite_render;

static_assert(render::HasTakeGpuData<render::mesh::RenderMesh>,
              "Mesh uses RENDER_WORLD-only extraction and must transfer its GPU payload.");

void epix::sprite_render::extract_mesh2d(ResMut<RenderMesh2dInstances> render_mesh_instances,
                    Extract<Query<Item<Entity,
                                      const Mesh2d&,
                                      const transform::GlobalTransform&,
                                      const camera::ViewVisibility&,
                                      Opt<const MeshTag&>,
                                      Has<render::phase::NoAutomaticBatching>>>> meshes) {
    // Bevy `extract_mesh2d` (mesh.rs:249-282): one main-entity keyed instance
    // per visible 2D mesh. Epix keeps these in a main-entity keyed resource
    // because a batchable binned phase deliberately has no render entity to
    // query at draw time. The material bind group id is default here and is
    // filled in when the material is prepared.
    render_mesh_instances->clear();
    for (auto&& [entity, mesh_handle, transform, view_visibility, tag, no_automatic_batching] : meshes.iter()) {
        // Bevy extracts only visible meshes (visibility/mod.rs:448-458).
        if (!view_visibility.get()) continue;
        render_mesh_instances->instances.emplace(
            render::sync_world::MainEntity{entity},
            RenderMesh2dInstance{
                .transforms = Mesh2dTransforms{.world_from_local = transform.matrix, .flags = MeshFlags::NONE},
                .mesh_asset_id           = mesh_handle.handle.id(),
                .material_bind_group_id  = Material2dBindGroupId{},
                .automatic_batching      = !no_automatic_batching,
                .tag                     = tag ? tag->get().value : 0u,
            });
    }
}

void Mesh2dRenderPlugin::attach(app::App& app) {
    spdlog::debug("[sprite] Attaching Mesh2dRenderPlugin.");

    // Bevy's `Mesh2dRenderPlugin::build` sets up the generic mesh2d pipeline;
    // its embedded shader and render-world systems are owned by
    // mesh2d_pipeline.cpp.
    attach_mesh2d_pipeline(app);
}
