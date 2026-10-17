#include <epix/pbr/pbr_plugin.hpp>

#include <epix/pbr/cluster.hpp>
#include <epix/pbr/cluster_prepare.hpp>
#include <epix/pbr/fog.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/light_plugin.hpp>
#include <epix/pbr/light_probe.hpp>
#include <epix/pbr/lightmap.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/material_bind_groups.hpp>
#include <epix/pbr/mesh_shaders.hpp>
#include <epix/pbr/point_light.hpp>
#include <epix/pbr/shadow_filtering_method.hpp>
#include <epix/pbr/spot_light.hpp>
#include <epix/render.hpp>

namespace epix::pbr {

void PbrPlugin::attach(epix::app::App& app) {
    // Bevy initialises the resource on the main app before it aggregates the plugin
    // tuple (`bevy_pbr/src/lib.rs:220`), and extracts it with the tuple at `:224`.
    app.world_mut().init_resource<DefaultOpaqueRendererMethod>();

    // Bevy passes its own `use_gpu_instance_buffer_builder` and `debug_flags` through to
    // these two; this port has the first, and no debug flags to pass yet. `MaterialsPlugin`
    // follows `MeshRenderPlugin` in Bevy's tuple (`lib.rs:222-228`), and it is the plugin
    // that owns the globals every material type shares.
    app.add_plugins(MeshRenderPlugin{.use_gpu_instance_buffer_builder = use_gpu_instance_buffer_builder});
    app.add_plugins(MaterialsPlugin{});
    app.add_plugins(epix::render::ExtractResourcePlugin<DefaultOpaqueRendererMethod>{});
    app.add_plugins(LightmapPlugin{});
    app.add_plugins(LightPlugin{});
    // Bevy's tuple carries `FogPlugin` too (`lib.rs:222-228`); it extracts the camera's fog
    // and stamps each view with the fog uniform offset the mesh view bind group reads.
    app.add_plugins(FogPlugin{});
    // Bevy's tuple carries `LightProbePlugin` as well (`lib.rs:222-228`), which is what
    // gives a view its environment-map uniform offset.
    app.add_plugins(LightProbePlugin{});

    // Bevy's tuple also syncs the light types and the shadow-filtering method to the render
    // world (`lib.rs:235-250`), and these are what make a light *exist* there: without them a
    // light has no `RenderEntity`, so no extraction can find it and nothing can read its
    // visibility. Their absence is why `extract_lights` reported zero lights for a scene that
    // had one.
    app.add_plugins(render::SyncComponentPlugin<ShadowFilteringMethod>{});
    app.add_plugins(render::SyncComponentPlugin<DirectionalLight>{});
    app.add_plugins(render::SyncComponentPlugin<PointLight>{});
    app.add_plugins(render::SyncComponentPlugin<SpotLight>{});
    app.add_plugins(render::SyncComponentPlugin<AmbientLight>{});

    // Bevy registers the cluster pair straight from `PbrPlugin::build` (`lib.rs:322-341`):
    // extraction in `ExtractSchedule`, the per-view bindings in `PrepareResources`, and the
    // scene-wide resource in render-world startup, before the mesh pipeline that is built from
    // the same binding type. Both systems were written in this port and neither was ever
    // registered, so no view carried its extracted clusters and `prepare_lights` - once it
    // exists - would have had nothing to fill.
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<LightMeta>();
        render_app->get().add_systems(render::RenderStartup,
                                      ecs::into(init_global_clusterable_object_meta)
                                          .set_name("init global clusterable object meta"));
        render_app->get().add_systems(app::ScheduleInfo(render::ExtractSchedule),
                                      ecs::into(extract_clusters).set_name("extract clusters"));
        // Bevy extracts the lights in the same schedule, right beside the clusters
        // (`lib.rs:325`): the two together are what the lighting prepare consumes.
        render_app->get().add_systems(app::ScheduleInfo(render::ExtractSchedule),
                                      ecs::into(extract_lights).set_name("extract lights"));
        render_app->get().add_systems(render::Render,
                                      ecs::into(prepare_clusters)
                                          .in_set(render::RenderSystems::PrepareResources)
                                          .set_name("prepare clusters"));
    }
}

void PbrPlugin::ready(epix::app::App& app) {
    // Bevy builds these settings in `finish` (`bevy_pbr/src/lib.rs:364-371`): it reads
    // the render world's device and adapter, then inserts the result into the **main**
    // app, because `assign_objects_to_clusters` is a main-world system. Without the
    // resource that system returns at its first line and clustering gathers nothing,
    // which is what the cluster probe measured (PBR-890). Doing this in `attach` would
    // find no render app yet and silently do nothing.
    auto render_app = app.get_sub_app_mut(epix::render::Render);
    if (!render_app) {
        return;
    }
    app.world_mut().insert_resource(
        make_global_cluster_settings(supports_storage_buffers, clustered_decals_are_usable));

    // Bevy builds the fallback bindless resources alongside the shadow samplers
    // (`bevy_pbr/src/lib.rs:319`), in the render world's startup, because they are
    // samplers the device creates. A bindless array's unused slots are filled from them,
    // so a shader indexing a slot no material occupies still reads something valid.
    render_app->get().add_systems(render::RenderStartup,
                                  ecs::into(init_fallback_bindless_resources)
                                      .set_name("init fallback bindless resources"));

    // Bevy creates this beside `LightMeta` in `PbrPlugin::finish` (`lib.rs:343`). The mesh
    // extraction, the instance builder and `SetMaterialBindGroup` all read it, and an app
    // that adds `MaterialPlugin` without this plugin has nothing to record bindings in.
    render_app->get().world_mut().init_resource<RenderMaterialBindings>();
}

}  // namespace epix::pbr
