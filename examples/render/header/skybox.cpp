// A dedicated visual test for the skybox (C20). Epix still has no 3D mesh draw
// pipeline, so this is the first thing the Core3D main opaque pass ever draws: a
// six-face cubemap behind the camera. Every face has its own colour, so a capture
// shows which one is visible, and the camera orbits the origin while always
// looking at it, so all six come into view and the environment visibly rotates
// with the camera.

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
#include <vector>

using namespace epix;
using namespace epix::ecs;
using namespace epix::app;

namespace {

/** @brief Marks the camera the orbit system drives. */
struct OrbitCamera {};

/** @brief Distance from the origin the camera orbits at. */
constexpr float kOrbitRadius = 4.0f;

/** @brief The skybox shader multiplies the cubemap by the view's exposure, and
 *  the engine's default exposure is about 0.001 (the same very low value Bevy
 *  uses, which is why its skybox example scales by a thousand too). The authored
 *  face colours therefore need this scale to read as themselves. */
constexpr float kSkyboxBrightness = 1000.0f;

/** @brief The six faces' colours, in the cubemap order +X, -X, +Y, -Y, +Z, -Z. */
constexpr std::array<std::array<std::uint8_t, 4>, 6> kFaceColors{{
    {220, 60, 60, 255},
    {60, 200, 90, 255},
    {80, 110, 230, 255},
    {230, 200, 70, 255},
    {200, 90, 210, 255},
    {70, 200, 210, 255},
}};

/** @brief Build the six-face cubemap. A cubemap is an ordinary layered image
 *  whose view descriptor asks for a cube view; the render path takes the view
 *  dimension from that descriptor. */
std::optional<image::Image> make_cubemap() {
    constexpr std::uint32_t kFaceSize = 8;
    constexpr std::uint32_t kChannels = 4;
    constexpr std::uint32_t kFaces    = 6;

    std::vector<std::uint8_t> pixels(kFaceSize * kFaceSize * kFaces * kChannels);
    for (std::size_t face = 0; face < kFaces; ++face) {
        for (std::size_t texel = 0; texel < kFaceSize * kFaceSize; ++texel) {
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                pixels[(face * kFaceSize * kFaceSize + texel) * kChannels + channel] = kFaceColors[face][channel];
            }
        }
    }

    auto cubemap = image::Image::create2d_array(kFaceSize, kFaceSize, kFaces,
                                                wgpu::TextureFormat::eRGBA8UnormSrgb, pixels);
    if (!cubemap) return std::nullopt;

    cubemap->texture_view_descriptor = wgpu::TextureViewDescriptor()
                                           .setLabel("skybox cubemap view")
                                           .setDimension(wgpu::TextureViewDimension::eCube)
                                           .setBaseMipLevel(0)
                                           .setMipLevelCount(1)
                                           .setBaseArrayLayer(0)
                                           .setArrayLayerCount(kFaces)
                                           .setFormat(wgpu::TextureFormat::eRGBA8UnormSrgb);
    return cubemap;
}

/** @brief Spawn the orbiting camera and give it the cubemap as its skybox. */
void spawn_scene(Commands commands, ResMut<assets::Assets<image::Image>> images) {
    auto cubemap = make_cubemap();
    if (!cubemap) {
        spdlog::error("[skybox] failed to build the cubemap");
        return;
    }

    const auto handle = images->emplace(std::move(*cubemap));
    // The prepass markers make this example exercise the skybox's motion-vector
    // prepass too: with a `MotionVectorPrepass` the view gets the prepass
    // pipeline, its bind group and the previous-frame offsets, and the prepass
    // node draws the skybox's velocity after its phase items - the only way that
    // path runs in a real frame.
    commands.spawn(::epix::camera::Camera3d{}, transform::Transform::from_xyz(0.0f, 0.0f, kOrbitRadius),
                   core_graph::Skybox{.image = handle, .brightness = kSkyboxBrightness},
                   core_graph::core_3d::DepthPrepass{}, core_graph::core_3d::NormalPrepass{},
                   core_graph::core_3d::MotionVectorPrepass{}, OrbitCamera{});
    spdlog::info("[skybox] spawned a cubemap camera orbiting the origin at radius {}", kOrbitRadius);
}

/** @brief Orbit the camera around the origin while it keeps looking at it.
 *
 *  The yaw drives both the position and the rotation, and the pitch keeps the
 *  origin centred as the elevation changes, so the motion is a purposeful path
 *  with a fixed up vector rather than aimless translation, and it never passes
 *  through a pole where the orientation would snap. The angle is a function of
 *  elapsed time, so the motion is frame-rate independent. */
void orbit_camera(Res<time::Time<>> time, Query<Mut<transform::Transform>, With<OrbitCamera>> cameras) {
    const float seconds   = time->elapsed_secs();
    const float yaw       = seconds * 0.45f;
    const float elevation = 0.5f * std::sin(seconds * 0.23f);

    const glm::vec3 position{kOrbitRadius * std::cos(elevation) * std::sin(yaw),
                             kOrbitRadius * std::sin(elevation),
                             kOrbitRadius * std::cos(elevation) * std::cos(yaw)};
    const glm::quat rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                               glm::angleAxis(-elevation, glm::vec3(1.0f, 0.0f, 0.0f));

    for (auto&& transform : cameras.iter()) {
        auto& value       = transform.get_mut();
        value.translation = position;
        value.rotation    = rotation;
    }
}

/** @brief Report the view's exposure, its skybox components and the GPU image
 *  the cubemap handle resolves to, once, so a capture can be traced back to what
 *  the node actually bound. */
void probe_skybox(Query<Item<const render::camera::ExtractedCamera&,
                             const core_graph::Skybox&,
                             Opt<const core_graph::SkyboxPipelineId&>,
                             Opt<const core_graph::SkyboxBindGroup&>,
                             Opt<const core_graph::RenderSkyboxPrepassPipeline&>,
                             Opt<const core_graph::SkyboxPrepassBindGroup&>,
                             Opt<const core_graph::PreviousViewUniformOffset&>,
                             Opt<const render::view::ViewUniformOffset&>>,
                  With<core_graph::Skybox>>
                      views,
                  Res<render::RenderAssets<render::texture::GpuImage>> images,
                  Local<int> frames) {
    // One-shot, but only after the prepare systems have run for several frames: a
    // probe that fires at an arbitrary point in the schedule reads whatever
    // happens to exist at that instant, which is what made the first version of
    // this probe report different values on different runs.
    *frames += 1;
    if (*frames != 30) return;
    for (auto&& [camera, skybox, pipeline_id, bind_group, prepass_pipeline, prepass_bind_group, previous_offset,
                 view_offset] : views.iter()) {
        const auto image = images->get(skybox.image);
        spdlog::info("[skybox] probe: exposure={} pipeline_id={} bind_group={} gpu_image={} size={}x{}x{}",
                     camera.exposure, pipeline_id.has_value(), bind_group.has_value(), static_cast<bool>(image),
                     image ? image->size.width : 0u, image ? image->size.height : 0u,
                     image ? image->size.depthOrArrayLayers : 0u);
        // The prepass draw's four preconditions: all present means the prepass
        // node's skybox branch runs for this view.
        spdlog::info("[skybox] prepass probe: pipeline={} bind_group={} view_offset={} previous_offset={}",
                     prepass_pipeline.has_value(), prepass_bind_group.has_value(), view_offset.has_value(),
                     previous_offset.has_value());
        return;
    }
}

/** @brief Live progress in the log, so a capture is provably from a running,
 *  animating frame. */
void log_progress(Res<time::Time<>> time) {
    static float last_log = 0.0f;
    const float now       = time->elapsed_secs();
    if (now - last_log >= 1.0f) {
        last_log = now;
        spdlog::info("[skybox] {:.1f}s elapsed, {:.1f} fps", now, 1.0f / std::max(time->delta_secs(), 0.0001f));
    }
}

/** @brief One-shot main-world probe. `PreviousViewData` present here but absent
 *  in the render world means the extraction drops it; absent here means the
 *  producer never matched its query. */
void probe_main_world(Query<Item<Opt<const transform::GlobalTransform&>,
                                  Opt<const ::epix::camera::Camera&>,
                                  Opt<const core_graph::PreviousViewData&>>,
                              With<core_graph::Skybox>>
                          cameras,
                      Local<int> frames) {
    if (*frames > 60) return;
    *frames += 1;
    if (*frames != 60) return;
    for (auto&& [global_transform, camera, previous_view_data] : cameras.iter()) {
        spdlog::info("[skybox] main-world probe: global_transform={} camera={} previous_view_data={}",
                     global_transform.has_value(), camera.has_value(), previous_view_data.has_value());
        return;
    }
    spdlog::warn("[skybox] main-world probe: no entity with a Skybox was found");
}

}  // namespace

int main() {
    App app = App::create();

    window::Window primary_window;
    primary_window.title = "Skybox | six-face cubemap behind an orbiting camera";
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

    app.add_systems(Startup, into(spawn_scene).set_name("spawn skybox scene"));
    app.add_systems(Update, into(orbit_camera).set_name("orbit camera"));
    app.add_systems(Update, into(log_progress).set_name("log progress"));
    app.add_systems(Update, into(probe_main_world).set_name("probe main world"));

    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().add_systems(render::Render,
                                      into(probe_skybox)
                                          .after(epix::core_graph::prepare_skybox_prepass_bind_groups)
                                          .set_name("probe skybox"));
    }

    app.run();
}
