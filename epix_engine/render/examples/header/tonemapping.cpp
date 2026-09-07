// Tonemapping visual comparison: eight HDR Core2D cameras render the same
// continuously changing linear-HDR clear color into a 4x2 viewport grid.
// Each tile applies a different tonemapping method, making both accidental
// bypasses and method-specific regressions visible in one rendered frame.

#include <spdlog/spdlog.h>

#include <array>
#include <cmath>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/task.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <string_view>

using namespace epix;
using namespace epix::app;
using namespace epix::ecs;

namespace {

constexpr glm::uvec2 kWindowSize{1280, 720};
constexpr glm::uvec2 kTileSize{kWindowSize.x / 4, kWindowSize.y / 2};

constexpr std::string_view kGridDescription =
    "top left->right: None, Reinhard, ReinhardLuminance, AcesFitted; "
    "bottom left->right: AgX, SomewhatBoringDisplayTransform, TonyMcMapface, BlenderFilmic";

constexpr std::string_view kWindowTitle =
    "Tonemapping | top: None | Reinhard | ReinhardLuminance | AcesFitted || "
    "bottom: AgX | SomewhatBoringDisplayTransform | TonyMcMapface | BlenderFilmic";

struct TonemappingComparisonCamera {};

struct Tile {
    core_graph::Tonemapping method;
    glm::uvec2 position;
};

constexpr std::array kTiles{
    Tile{core_graph::Tonemapping::None, {0 * kTileSize.x, 0 * kTileSize.y}},
    Tile{core_graph::Tonemapping::Reinhard, {1 * kTileSize.x, 0 * kTileSize.y}},
    Tile{core_graph::Tonemapping::ReinhardLuminance, {2 * kTileSize.x, 0 * kTileSize.y}},
    Tile{core_graph::Tonemapping::AcesFitted, {3 * kTileSize.x, 0 * kTileSize.y}},
    Tile{core_graph::Tonemapping::AgX, {0 * kTileSize.x, 1 * kTileSize.y}},
    Tile{core_graph::Tonemapping::SomewhatBoringDisplayTransform, {1 * kTileSize.x, 1 * kTileSize.y}},
    Tile{core_graph::Tonemapping::TonyMcMapface, {2 * kTileSize.x, 1 * kTileSize.y}},
    Tile{core_graph::Tonemapping::BlenderFilmic, {3 * kTileSize.x, 1 * kTileSize.y}},
};

camera::ClearColor animated_hdr_color(float seconds) {
    // Deliberately exceed display-referred 1.0 so each operator has meaningful
    // highlight information to compress. The incommensurate rates continually
    // change both hue and luminance while every tile receives the exact same
    // source value.
    return camera::ClearColor{
        0.15f + 5.85f * (0.5f + 0.5f * std::sin(seconds * 0.43f)),
        0.10f + 3.90f * (0.5f + 0.5f * std::sin(seconds * 0.67f + 2.1f)),
        0.05f + 2.95f * (0.5f + 0.5f * std::sin(seconds * 0.89f + 4.2f)),
        1.0f,
    };
}

void setup(Commands commands) {
    spdlog::info("Tonemapping viewport map -- {}", kGridDescription);

    for (std::size_t index = 0; index < kTiles.size(); ++index) {
        const auto& tile = kTiles[index];
        camera::Camera camera;
        camera.order       = static_cast<std::ptrdiff_t>(index);
        camera.viewport    = ::epix::camera::Viewport{.physical_position = tile.position,
                                                       .physical_size     = kTileSize};
        camera.clear_color = camera::ClearColorConfig::Custom{animated_hdr_color(0.0f)};

        commands.spawn(TonemappingComparisonCamera{}, camera::Camera2d{}, std::move(camera), render::view::Hdr{},
                       tile.method, core_graph::DebandDither::Disabled, transform::Transform{});
    }
}

void animate_source_color(Res<time::Time<>> time,
                          Query<Item<camera::Camera&>, With<TonemappingComparisonCamera>> cameras) {
    const auto color = animated_hdr_color(time->elapsed_secs());
    for (auto&& [camera] : cameras.iter()) {
        camera.clear_color = camera::ClearColorConfig::Custom{color};
    }

    static float last_log = -2.0f;
    if (time->elapsed_secs() - last_log >= 2.0f) {
        spdlog::info("Shared changing HDR input: ({:.2f}, {:.2f}, {:.2f}); {}", color.r, color.g, color.b,
                     kGridDescription);
        last_log = time->elapsed_secs();
    }
}

}  // namespace

int main() {
    App app = App::create();

    window::Window primary_window;
    primary_window.title = kWindowTitle;
    primary_window.size  = {static_cast<int>(kWindowSize.x), static_cast<int>(kWindowSize.y)};

    app.add_plugins(TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{
            .primary_window = primary_window,
            .exit_condition = window::ExitCondition::OnPrimaryClosed,
        })
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{});

    app.add_systems(Startup, into(setup).set_name("set up tonemapping comparison"));
    app.add_systems(Update, into(animate_source_color).set_name("animate shared HDR source color"));

    app.run();
}
