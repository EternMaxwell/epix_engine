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

/** @brief Report the key, its filtering bits and its tick for each extracted view -
 *  the state `check_views_need_specialization` writes. */
void probe_mesh_view_keys(ResMut<MeshViewKeyProbe> probe,
                          Res<pbr::ViewKeyCache> key_cache,
                          Res<pbr::ViewSpecializationTicks> ticks,
                          Query<Item<const render::view::ExtractedView&>> views) {
    if (probe->logged) return;
    probe->frames += 1;
    if (probe->frames < 3) return;

    bool reported = false;
    for (auto&& [extracted_view] : views.iter()) {
        const auto& retained = extracted_view.retained_view_entity;
        const auto key       = key_cache->keys.find(retained);
        if (key == key_cache->keys.end()) continue;

        const auto& value = key->second;
        spdlog::info(
            "[mesh] view key recorded: gaussian={} hardware_2x2={} temporal={} perspective={} ticked={}",
            value.contains(pbr::MeshPipelineKey::SHADOW_FILTER_METHOD_GAUSSIAN),
            value.contains(pbr::MeshPipelineKey::SHADOW_FILTER_METHOD_HARDWARE_2X2),
            value.contains(pbr::MeshPipelineKey::SHADOW_FILTER_METHOD_TEMPORAL),
            value.contains(pbr::MeshPipelineKey::VIEW_PROJECTION_PERSPECTIVE),
            ticks->ticks.contains(retained));
        reported = true;
    }

    if (reported) {
        probe->logged = true;
    } else {
        spdlog::warn("[mesh] no view key recorded after {} frames", probe->frames);
    }
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
    cmd.spawn(::epix::camera::Camera3d{}, transform::Transform{},
              pbr::ShadowFilteringMethod::Gaussian);
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
        // The subject of this example: registered by no other example until now.
        .add_plugins(pbr::MeshRenderPlugin{});

    app.add_systems(Startup, into(spawn_camera).set_name("spawn 3d camera"));
    app.add_systems(Update, into(log_fps).set_name("log fps"));

    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<MeshViewKeyProbe>();
        render_app->get().add_systems(render::Render, into(probe_mesh_view_keys).set_name("probe mesh view keys"));
    }

    app.run();
}
