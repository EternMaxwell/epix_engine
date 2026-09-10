#ifndef EPIX_IMPORT_STD
#include <print>
#include <utility>
#endif
#ifdef EPIX_IMPORT_STD
import std;
#endif
import glm;
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
struct PrimitiveBuildersExamplePlugin {
    void ready(app::App& app) {
        auto& world       = app.world_mut();
        auto& mesh_assets = world.resource_mut<assets::Assets<mesh::Mesh>>();

        mesh::Mesh circle    = mesh::Circle{80.0f};
        mesh::Mesh ellipse   = mesh::Ellipse{105.0f, 70.0f}.mesh().with_resolution(48);
        mesh::Mesh hexagon   = mesh::RegularPolygon{85.0f, 6};
        mesh::Mesh rectangle = mesh::Rectangle{170.0f, 120.0f};

        std::println("Primitive builders: circle={} ellipse={} hexagon={} rectangle={} vertices",
                     circle.count_vertices(), ellipse.count_vertices(), hexagon.count_vertices(),
                     rectangle.count_vertices());

        const auto circle_handle    = mesh_assets.emplace(std::move(circle));
        const auto ellipse_handle   = mesh_assets.emplace(std::move(ellipse));
        const auto hexagon_handle   = mesh_assets.emplace(std::move(hexagon));
        const auto rectangle_handle = mesh_assets.emplace(std::move(rectangle));

        world.spawn(camera::Camera2d{}, transform::Transform{});
        world.spawn(mesh::Mesh2d{circle_handle},
                    sprite_render::MeshMaterial2d{.color = glm::vec4(0.98f, 0.43f, 0.36f, 1.0f)},
                    transform::Transform{.translation = {-390.0f, 0.0f, 0.0f}});
        world.spawn(mesh::Mesh2d{ellipse_handle},
                    sprite_render::MeshMaterial2d{.color = glm::vec4(0.98f, 0.72f, 0.27f, 1.0f)},
                    transform::Transform{.translation = {-130.0f, 0.0f, 0.0f}});
        world.spawn(mesh::Mesh2d{hexagon_handle},
                    sprite_render::MeshMaterial2d{.color = glm::vec4(0.30f, 0.82f, 0.58f, 1.0f)},
                    transform::Transform{.translation = {130.0f, 0.0f, 0.0f}});
        world.spawn(mesh::Mesh2d{rectangle_handle},
                    sprite_render::MeshMaterial2d{.color = glm::vec4(0.36f, 0.61f, 0.96f, 1.0f)},
                    transform::Transform{.translation = {390.0f, 0.0f, 0.0f}});
    }
};
}  // namespace

int main() {
    auto app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Bevy Primitive Builders | circle | ellipse | hexagon | rectangle";
    primary_window.size  = {1100, 600};

    app.add_plugins(app::TaskPoolPlugin{})
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
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(mesh::MeshPlugin{})
        .add_plugins(sprite::SpritePlugin{})
        .add_plugins(sprite_render::Mesh2dRenderPlugin{})
        .add_plugins(PrimitiveBuildersExamplePlugin{});

    app.run();
}
