#include <epix/pbr/cluster.hpp>
#include <epix/pbr/cluster_prepare.hpp>
#include <epix/pbr/fog.hpp>
#include <epix/pbr/light_meta.hpp>
#include <epix/pbr/light_plugin.hpp>
#include <epix/pbr/light_probe.hpp>
#include <epix/pbr/lightmap.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/material_bind_groups.hpp>
#include <epix/pbr/mesh_pipeline_resource.hpp>
#include <epix/pbr/mesh_shaders.hpp>
#include <epix/pbr/pbr_plugin.hpp>
#include <epix/pbr/point_light.hpp>
#include <epix/pbr/shadow_filtering_method.hpp>
#include <epix/pbr/spot_light.hpp>
#include <epix/pbr/ssr.hpp>
#include <epix/render.hpp>

namespace epix::pbr {

namespace {
using ExtractedLights = std::tuple<ExtractedDirectionalLight, ExtractedPointLight>;

void add_light_view_entities(ecs::On<ecs::Add, ExtractedLights> event, ecs::Commands commands) {
    if (auto entity = commands.get_entity(event->entity)) entity->insert(LightViewEntities{});
}

void extracted_light_removed(ecs::On<ecs::Remove, ExtractedLights> event, ecs::Commands commands) {
    // Epix EntityCommands::remove is already fallible for missing entities.
    if (auto entity = commands.get_entity(event->entity)) entity->remove<LightViewEntities>();
}

void remove_light_view_entities(ecs::On<ecs::Remove, LightViewEntities> event,
                                ecs::Query<ecs::Item<const LightViewEntities&>> maps, ecs::Commands commands) {
    const auto map = maps.get(event->entity);
    if (!map) return;
    const auto& [entities] = *map;
    for (const auto& [camera, views] : entities.entities)
        for (auto view : views)
            if (auto entity = commands.get_entity(view)) entity->despawn();
}
}  // namespace

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
        // Bevy render/light.rs:617-649: observers are installed in this render
        // world only, not innate hooks on the extracted component types.
        auto& world = render_app->get().world_mut();
        world.add_observer(add_light_view_entities);
        world.add_observer(extracted_light_removed);
        world.add_observer(remove_light_view_entities);
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
        // The scene-wide ambient light has to exist in the render world before the prepare
        // reads it, and Bevy extracts it in the same schedule (`lib.rs:326`).
        render_app->get().add_systems(app::ScheduleInfo(render::ExtractSchedule),
                                      ecs::into(extract_ambient_light_resource)
                                          .set_name("extract ambient light resource"));
        // One native preparation flow owns the initial writer guard, packing,
        // shadow allocation and view retirement (Bevy lib.rs:334-337).
        render_app->get().add_systems(render::Render,
                                      ecs::into(prepare_lights)
                                          .in_set(render::RenderSystems::ManageViews)
                                          .after(render::camera::sort_cameras)
                                          .set_name("prepare lights"));
        // Bevy's `ScreenSpaceReflectionsPlugin` initialises this buffer and writes one entry per
        // view in `PrepareResources` (`ssr/mod.rs:197,206`). Binding 15 of the view layout is a
        // dynamic uniform, so the mesh view bind group cannot exist without it - the port's SSR
        // is only as far as that, with no tracing pass.
        render_app->get().world_mut().init_resource<ScreenSpaceReflectionsBuffer>();
        render_app->get().add_systems(render::Render,
                                      ecs::into(prepare_ssr_settings)
                                          .in_set(render::RenderSystems::PrepareResources)
                                          .set_name("prepare ssr settings"));
        // Bevy builds the view bind groups in `PrepareBindGroups` (`lib.rs:331`), once the
        // resources they bind have been written.
        render_app->get().add_systems(render::Render, ecs::into(prepare_mesh_view_bind_groups)
                                                          .in_set(render::RenderSystems::PrepareBindGroups)
                                                          .set_name("prepare mesh view bind groups"));
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
