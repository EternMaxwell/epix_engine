#include <epix/pbr/pbr_plugin.hpp>

#include <epix/pbr/cluster.hpp>
#include <epix/pbr/light_plugin.hpp>
#include <epix/pbr/lightmap.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/mesh_shaders.hpp>
#include <epix/render.hpp>

namespace epix::pbr {

void PbrPlugin::attach(epix::app::App& app) {
    // Bevy initialises the resource on the main app before it aggregates the plugin
    // tuple (`bevy_pbr/src/lib.rs:220`), and extracts it with the tuple at `:224`.
    app.world_mut().init_resource<DefaultOpaqueRendererMethod>();

    // Bevy passes its own `use_gpu_instance_buffer_builder` and `debug_flags` through to
    // these two; this port has the first, and no debug flags to pass yet.
    app.add_plugins(MeshRenderPlugin{.use_gpu_instance_buffer_builder = use_gpu_instance_buffer_builder});
    app.add_plugins(epix::render::ExtractResourcePlugin<DefaultOpaqueRendererMethod>{});
    app.add_plugins(LightmapPlugin{});
    app.add_plugins(LightPlugin{});
}

void PbrPlugin::ready(epix::app::App& app) {
    // Bevy builds these settings in `finish` (`bevy_pbr/src/lib.rs:364-371`): it reads
    // the render world's device and adapter, then inserts the result into the **main**
    // app, because `assign_objects_to_clusters` is a main-world system. Without the
    // resource that system returns at its first line and clustering gathers nothing,
    // which is what the cluster probe measured (PBR-890). Doing this in `attach` would
    // find no render app yet and silently do nothing.
    auto render_app = app.get_sub_app(epix::render::Render);
    if (!render_app) {
        return;
    }
    app.world_mut().insert_resource(
        make_global_cluster_settings(supports_storage_buffers, clustered_decals_are_usable));
}

}  // namespace epix::pbr
