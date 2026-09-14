// Visual verification for Bevy-shaped AlphaMode2d::Mask. The foreground is a
// textured quad with alternating zero/one alpha texels; every transparent
// texel must reveal the orange opaque quad behind it.

#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstdint>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.app;
import epix.assets;
import epix.camera;
import epix.core_graph;
import epix.glfw.core;
import epix.glfw.render;
import epix.image;
import epix.input;
import epix.mesh;
import epix.render;
import epix.sprite;
import epix.sprite_render;
import epix.time;
import epix.transform;
import epix.window;

using namespace epix;

namespace {
struct AlphaMaskVisualTestPlugin {
    void ready(app::App& app) {
        auto& world = app.world_mut();
        auto& meshes = world.resource_mut<assets::Assets<mesh::Mesh>>();
        auto& images = world.resource_mut<assets::Assets<image::Image>>();

        world.spawn(camera::Camera2d{}, transform::Transform{});

        constexpr std::array<std::uint8_t, 4 * 4 * 4> checker = {
            20, 210, 255, 255, 20, 210, 255, 0,   20, 210, 255, 255, 20, 210, 255, 0,
            20, 210, 255, 0,   20, 210, 255, 255, 20, 210, 255, 0,   20, 210, 255, 255,
            20, 210, 255, 255, 20, 210, 255, 0,   20, 210, 255, 255, 20, 210, 255, 0,
            20, 210, 255, 0,   20, 210, 255, 255, 20, 210, 255, 0,   20, 210, 255, 255,
        };
        const auto image = images.emplace(image::Image::create2d(4, 4, wgpu::TextureFormat::eRGBA8Unorm, checker).value());
        const auto background = meshes.emplace(mesh::make_box2d(440.0f, 300.0f));
        const auto foreground = meshes.emplace(mesh::make_box2d_uv(440.0f, 300.0f));

        world.spawn(mesh::Mesh2d{background},
                    sprite_render::MeshMaterial2d{.color = glm::vec4(0.98f, 0.35f, 0.08f, 1.0f)},
                    transform::Transform{.translation = glm::vec3(0.0f, 0.0f, -0.1f)});
        world.spawn(mesh::Mesh2d{foreground},
                    sprite_render::MeshTextureMaterial2d{.image = image,
                                                 .alpha_mode = sprite_render::AlphaMode2dMask{.cutoff = 0.5f}},
                    transform::Transform{});
    }
};
}  // namespace

int main() {
    app::App app = app::App::create();
    window::Window primary_window;
    primary_window.title = "Mesh Alpha Mask Visual Test";
    primary_window.size = {960, 540};

    app.add_plugins(app::TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{.primary_window = primary_window,
                                           .exit_condition = window::ExitCondition::OnPrimaryClosed})
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
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(mesh::MeshPlugin{})
        .add_plugins(sprite::SpritePlugin{})
        .add_plugins(sprite_render::Mesh2dRenderPlugin{})
        .add_plugins(AlphaMaskVisualTestPlugin{});
    app.run();
}
