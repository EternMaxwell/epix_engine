// A dedicated visual test for the 3D mesh plugin's view path, which until now no
// example exercised: `MeshRenderPlugin` was registered only by tests, so the
// per-view key assembly, its cache and the shadow-filtering extraction ran
// nowhere a person could watch.
//
// There is no concrete 3D material in the engine yet, so nothing draws and the
// screen shows the Core3D cleared output. The evidence is therefore the log, the
// arrangement `core3d_clear.cpp` uses for pipeline parts that are invisible on
// screen: a probe reads the view key cache a few frames in and reports the key
// recorded for the view, whether it carries the shadow filtering method the
// camera asked for, and whether a specialization tick was stamped. Any system the
// render world skipped would say so, because the port logs
// `parameter validation error at system ...` by name.

#include <spdlog/spdlog.h>

#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/pbr.hpp>
#include <epix/pbr/material.hpp>
#include <epix/pbr/mesh_pipeline_resource.hpp>
#include <epix/pbr/mesh_shaders.hpp>
#include <epix/pbr/pbr_plugin.hpp>
#include <epix/pbr/shadow_filtering_method.hpp>
#include <epix/render.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/sdl3/render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

using namespace epix;
using namespace epix::ecs;
using namespace epix::app;

namespace {

/** @brief A stand-in 3D material, exactly as the mesh extraction tests use one: the
 *  registry is untyped and nothing here draws, so `MaterialPlugin` only has to exist
 *  for the material resources the extraction reads to be created. */
struct StandInMaterial : pbr::Material<StandInMaterial> {};

/** @brief Render-world probe state: report the recorded key once, a few frames in. */
struct MeshViewKeyProbe {
    int frames  = 0;
    bool logged = false;
};

/** @brief Report every key the specialization has recorded, with its filtering bits
 *  and whether a tick was stamped - the state `check_views_need_specialization` writes.
 *
 *  It reads the cache rather than querying views on purpose. A view query written the
 *  way `skybox.hpp` writes one matched nothing here while the cache demonstrably held a
 *  key, so the cache is both the working signal and the thing under test. */
void probe_mesh_view_keys(ResMut<MeshViewKeyProbe> probe,
                          Res<pbr::ViewKeyCache> key_cache,
                          Res<pbr::ViewSpecializationTicks> ticks) {
    if (probe->logged) return;
    probe->frames += 1;
    if (probe->frames < 3) return;

    if (key_cache->keys.empty()) {
        spdlog::warn("[mesh] view key cache still empty after {} frames", probe->frames);
        return;
    }

    for (const auto& [retained, value] : key_cache->keys) {
        spdlog::info(
            "[mesh] view key recorded: gaussian={} hardware_2x2={} temporal={} perspective={} ticked={}",
            value.contains(pbr::MeshPipelineKey::SHADOW_FILTER_METHOD_GAUSSIAN),
            value.contains(pbr::MeshPipelineKey::SHADOW_FILTER_METHOD_HARDWARE_2X2),
            value.contains(pbr::MeshPipelineKey::SHADOW_FILTER_METHOD_TEMPORAL),
            value.contains(pbr::MeshPipelineKey::VIEW_PROJECTION_PERSPECTIVE),
            ticks->ticks.contains(retained));
    }
    probe->logged = true;
}

/** @brief Main-world probe state for the light simulation. */
struct LightClusterProbe {
    int frames  = 0;
    bool logged = false;
};

/** @brief Report the light simulation's effect on the scene, a few frames in.
 *
 *  This is what separates "the systems validate" from "the systems work", which batch 203
 *  could only claim the first half of. `Clusters` on a camera is `add_clusters`' doing;
 *  non-zero dimensions and tile size on it are `assign_objects_to_clusters`' doing, since
 *  that system computes them; and the per-cluster occupancy is the only one of the three
 *  that proves something was actually placed rather than merely laid out. A system that
 *  runs and quietly does nothing - the failure that hid in `check_views_need_specialization`
 *  for two batches - would fail all three. */
void probe_light_clusters(ResMut<LightClusterProbe> probe,
                          Query<Item<Entity, const pbr::Clusters&>> clustered_views,
                          Res<pbr::GlobalVisibleClusterableObjects> global_objects) {
    if (probe->logged) return;
    probe->frames += 1;
    if (probe->frames < 3) return;

    int views_with_clusters  = 0;
    std::size_t assigned_objects = 0;
    for (auto&& [entity, clusters] : clustered_views.iter()) {
        (void)entity;
        views_with_clusters += 1;
        for (const pbr::VisibleClusterableObjects& cluster : clusters.clusterable_objects) {
            assigned_objects += cluster.entities.size();
        }
        spdlog::info("[light] clusters: dimensions=({},{},{}) tile_size=({},{}) cluster_slots={}",
                     clusters.dimensions.x, clusters.dimensions.y, clusters.dimensions.z,
                     clusters.tile_size.x, clusters.tile_size.y,
                     clusters.clusterable_objects.size());
    }
    // The per-cluster occupancy is the signal that a light was actually placed: non-zero
    // dimensions only prove the layout was computed, not that anything went into it.
    // `GlobalVisibleClusterableObjects` is the wrong place to look - it holds objects
    // that fall outside every cluster, so a locally assigned light leaves it at zero.
    spdlog::info("[light] views_with_clusters={} assigned_objects={} global_clusterable_objects={}",
                 views_with_clusters, assigned_objects, global_objects->entities.size());
    probe->logged = true;
}

/** @brief Main-world FPS logger so the capture shows a live frame. */
void log_fps(Res<time::Time<>> time) {
    static float last_log = 0.0f;
    const float now       = time->elapsed_secs();
    if (now - last_log >= 1.0f) {
        spdlog::info("[mesh] {:.1f}s elapsed", now);
        last_log = now;
    }
}

/** @brief Spawn the camera the 3D mesh path specializes for, carrying the shadow
 *  filtering method explicitly.
 *
 *  `Gaussian` is Bevy's default, so naming it changes no rendering - which is the
 *  point: it exercises the extraction, the specialization arm and the key assembly
 *  while the frame stays what it would have been without the component. */
void spawn_camera(Commands cmd) {
    // No projection is named here any more: `Camera3d` requires one (`PBR-895`), and the
    // view key reads the required `Projection` rather than asking for a separate
    // component. The earlier hand-named `PerspectiveProjection` was the workaround for
    // that defect, so leaving it out is part of the test.
    cmd.spawn(::epix::camera::Camera3d{}, transform::Transform{}, pbr::ShadowFilteringMethod::Gaussian);

    // A light, so the simulation systems the plugin wires have something to gather,
    // cluster and check visibility for. Without one they validate and then no-op.
    cmd.spawn(pbr::PointLight{}, transform::Transform::from_xyz(2.0f, 2.0f, 2.0f));

    // One of each other light kind, because the requirements this port registers differ
    // per kind and the family audit found two of them missing: a light with no Visibility
    // is never clustered and casts nothing, and only having one kind in the scene hides
    // that for the others.
    cmd.spawn(pbr::SpotLight{}, transform::Transform::from_xyz(-2.0f, 2.0f, 2.0f));
    cmd.spawn(pbr::DirectionalLight{}, transform::Transform::from_xyz(0.0f, 4.0f, 0.0f));
}

}  // namespace

int main() {
    App app = App::create();

    window::Window primary_window;
    primary_window.title = "3D Mesh View Specialization (Camera3d)";
    primary_window.size  = {1280, 720};

    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{
            .primary_window = primary_window,
            .exit_condition = window::ExitCondition::OnPrimaryClosed,
        })
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(sdl3::SDL3Plugin{})
        .add_plugins(sdl3::SDL3RenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(mesh::MeshPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        // Creates the material registries the mesh extraction reads. Without it that
        // system is skipped on validation, which is the loudest thing this example says.
        .add_plugins(pbr::MaterialPlugin<StandInMaterial>{})
        // The subject of this example: until now no other example registered the mesh
        // path at all. It comes through `PbrPlugin` rather than directly, so the light
        // plugin and its four resources are exercised by an app for the first time too.
        .add_plugins(pbr::PbrPlugin{.supports_storage_buffers = true});

    app.add_systems(Startup, into(spawn_camera).set_name("spawn 3d camera"));
    app.add_systems(Update, into(log_fps).set_name("log fps"));
    // Main world: `Clusters` is attached to cameras by `add_clusters`, a PostUpdate
    // system in this world, and `GlobalVisibleClusterableObjects` is a main-world
    // resource the plugin initialises.
    app.world_mut().init_resource<LightClusterProbe>();
    app.add_systems(Update, into(probe_light_clusters).set_name("probe light clusters"));

    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<MeshViewKeyProbe>();
        render_app->get().add_systems(render::Render, into(probe_mesh_view_keys).set_name("probe mesh view keys"));
    }

    app.run();
}
