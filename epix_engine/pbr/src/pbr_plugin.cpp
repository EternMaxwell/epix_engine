#include <epix/pbr/pbr_plugin.hpp>

#include <epix/pbr/light_plugin.hpp>
#include <epix/pbr/lightmap.hpp>
#include <epix/pbr/mesh_shaders.hpp>

namespace epix::pbr {

void PbrPlugin::attach(epix::app::App& app) {
    // Bevy passes its own `use_gpu_instance_buffer_builder` and `debug_flags` through to
    // these two; this port has the first, and no debug flags to pass yet.
    app.add_plugins(MeshRenderPlugin{.use_gpu_instance_buffer_builder = use_gpu_instance_buffer_builder});
    app.add_plugins(LightmapPlugin{});
    app.add_plugins(LightPlugin{});
}

}  // namespace epix::pbr
