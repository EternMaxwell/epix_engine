// A dedicated visual test for the Core3D base pipeline introduced by C13a-C13c.
// It has no pass/fail surface of its own: a `Camera3d` renders through the
// Bevy-ordered Core3D node chain (prepass endpoints -> main opaque ->
// transmissive -> transparent -> tonemapping -> upscaling) instead of the 2D
// graph, so the only thing on screen is the camera's cleared output. The
// animated clear color proves the main opaque pass owns the target and that
// upscaling presents it.
//
// A one-shot probe additionally logs the Core3D depth attachment and the four
// phase collections once they exist, so the process log is itself evidence for
// the parts of the pipeline that are invisible on screen. No 3D draw pipeline
// exists yet, so the opaque/alpha-mask/sorted phases are intentionally empty
// and nothing is queued into them here.

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/sdl3/render.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <optional>

using namespace epix;
using namespace epix::ecs;
using namespace epix::app;

namespace {

/** @brief Render-world probe state: log the Core3D pipeline once, after the
 * swapchain surfaces and the first extraction have settled. */
struct Core3dProbe {
    int frames  = 0;
    bool logged = false;
};

/** @brief Animate the render-world ClearColor, which is what the Core3D main
 * opaque pass uses for its color attachment. */
void animate_clear_color(std::optional<ResMut<::epix::camera::ClearColor>> color, Res<time::Time<>> time) {
    if (!color) return;
    const float t = time->elapsed_secs();
    // Linear components, as the pass writes them, so the animated frames keep
    // exactly the values this example has always produced.
    *color.value() = ::epix::camera::ClearColor{::epix::color::Color::linear_rgba(
        0.5f + 0.5f * std::sin(t * 0.8f), 0.5f + 0.5f * std::sin(t * 1.3f + 2.0f),
        0.5f + 0.5f * std::sin(t * 1.9f + 4.0f), 1.0f)};
}

/** @brief Log the Core3D depth attachment and phase membership for the
 * extracted Camera3d view, once, a few frames in. */
void probe_core3d_pipeline(
    ResMut<Core3dProbe> probe,
    Query<Item<const render::view::ExtractedView&, const render::view::ViewDepthTexture&>> views,
    Res<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::Opaque3d>> opaque_phases,
    Res<render::phase::ViewBinnedRenderPhases<core_graph::core_3d::AlphaMask3d>> alpha_mask_phases,
    Res<render::phase::ViewSortedRenderPhases<core_graph::core_3d::Transmissive3d>> transmissive_phases,
    Res<render::phase::ViewSortedRenderPhases<core_graph::core_3d::Transparent3d>> transparent_phases) {
    if (probe->logged) return;
    probe->frames += 1;
    if (probe->frames < 3) return;

    for (auto&& [extracted_view, depth] : views.iter()) {
        probe->logged        = true;
        const auto& retained = extracted_view.retained_view_entity;
        const auto& texture  = depth.texture;
        spdlog::info("[core3d] Camera3d depth attachment: {}x{} format={} core3d_depth_format={} samples={} usage={}",
                     texture.getWidth(), texture.getHeight(), static_cast<int>(texture.getFormat()),
                     texture.getFormat() == core_graph::core_3d::CORE_3D_DEPTH_FORMAT, texture.getSampleCount(),
                     static_cast<int>(texture.getUsage()));
        spdlog::info("[core3d] phases for this view: opaque={} alpha_mask={} transmissive={} transparent={}",
                     opaque_phases->phases.contains(retained), alpha_mask_phases->phases.contains(retained),
                     transmissive_phases->contains(retained), transparent_phases->contains(retained));
        return;
    }
    spdlog::warn("[core3d] No extracted Camera3d view with a depth attachment after {} frames.", probe->frames);
}

/** @brief Log the graph the spawned Camera3d will actually be driven with. */
void log_camera_graph(Query<Item<const render::camera::CameraRenderGraph&>> graphs, Local<bool> logged) {
    if (*logged) return;
    for (auto&& [graph] : graphs.iter()) {
        spdlog::info("[core3d] Camera3d camera render graph = {}", graph.to_string());
        *logged = true;
        return;
    }
}

/** @brief Main-world FPS logger so the capture shows a live, animating frame. */
void log_fps(Res<time::Time<>> time) {
    static float last_log = 0.0f;
    const float now       = time->elapsed_secs();
    if (now - last_log >= 1.0f) {
        spdlog::info("[core3d] {:.1f}s elapsed, {:.1f} fps", now, 1.0f / std::max(time->delta_secs(), 0.0001f));
        last_log = now;
    }
}

/** @brief Spawn the 3D camera the Core3D graph renders. */
void spawn_camera(Commands cmd) { cmd.spawn(::epix::camera::Camera3d{}, transform::Transform{}); }

}  // namespace

int main() {
    App app = App::create();

    window::Window primary_window;
    primary_window.title = "Core3D Base Pipeline (Camera3d)";
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
        .add_plugins(core_graph::CoreGraphPlugin{});

    app.add_systems(Startup, into(spawn_camera).set_name("spawn 3d camera"));
    app.add_systems(Update, into(log_camera_graph).set_name("log camera graph"));
    app.add_systems(Update, into(log_fps).set_name("log fps"));

    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<Core3dProbe>();
        render_app->get().add_systems(render::Render, into(animate_clear_color).set_name("animate clear color"));
        render_app->get().add_systems(render::Render, into(probe_core3d_pipeline).set_name("probe core3d pipeline"));
    }

    app.run();
}
