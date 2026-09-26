// LDR sprite-tonemapping comparison. Each camera renders the same overbright
// sprite into one viewport, forcing tonemapping through the sprite fragment
// shader (not CoreGraph's HDR post-process pass).

#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/sdl3/render.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <string_view>
#include <vector>

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
    "LDR Sprite Tonemapping | top: None | Reinhard | ReinhardLuminance | AcesFitted || "
    "bottom: AgX | SomewhatBoringDisplayTransform | TonyMcMapface | BlenderFilmic";

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

void setup(Commands commands, ResMut<assets::Assets<image::Image>> images) {
    spdlog::info("LDR sprite tonemapping viewport map -- {}", kGridDescription);

    for (std::size_t index = 0; index < kTiles.size(); ++index) {
        const auto& tile = kTiles[index];
        camera::Camera view;
        view.order    = static_cast<std::ptrdiff_t>(index);
        view.viewport = camera::Viewport{
            .physical_position = tile.position,
            .physical_size     = kTileSize,
        };
        view.clear_color = camera::ClearColorConfig::Custom{camera::ClearColor{0.015f, 0.015f, 0.02f, 1.0f}};

        // Deliberately no render::view::Hdr component: the sprite pipeline
        // must select TONEMAP_IN_SHADER for this LDR view.
        commands.spawn(camera::Camera2d{}, std::move(view), tile.method, core_graph::DebandDither::Disabled,
                       transform::Transform{});
    }

    const std::vector<std::uint8_t> pixels{
        255, 255, 255, 255, 255, 96, 32, 255, 32, 255, 96, 255, 64, 96, 255, 255,
    };
    auto texture              = image::Image::create2d(2, 2, wgpu::TextureFormat::eRGBA8UnormSrgb, pixels).value();
    const auto texture_handle = images->emplace(std::move(texture));
    auto value                = sprite::Sprite::from_image(texture_handle);
    value.custom_size         = glm::vec2{250.0f, 250.0f};
    value.color               = glm::vec4{6.0f, 3.5f, 2.0f, 1.0f};
    commands.spawn(std::move(value), transform::Transform{});
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
        .add_plugins(sdl3::SDL3Plugin{})
        .add_plugins(sdl3::SDL3RenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(mesh::MeshPlugin{})
        .add_plugins(sprite::SpritePlugin{})
        .add_plugins(sprite_render::SpriteRenderPlugin{});

    app.add_systems(Startup, into(setup).set_name("set up LDR sprite tonemapping comparison"));
    app.run();
}
