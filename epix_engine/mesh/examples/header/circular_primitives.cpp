#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/core_graph.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render/mesh2d.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <glm/glm.hpp>
#include <print>
#include <utility>

using namespace epix;

namespace {
struct CircularPrimitivesExamplePlugin {
    void ready(app::App& app) {
        auto& world       = app.world_mut();
        auto& mesh_assets = world.resource_mut<assets::Assets<mesh::Mesh>>();

        mesh::Mesh sector  = mesh::CircularSector::from_degrees(115.0f, 240.0f).mesh().with_resolution(48);
        mesh::Mesh segment = mesh::CircularSegment::from_degrees(125.0f, 120.0f)
                                 .mesh()
                                 .with_resolution(48)
                                 .with_uv_mode(mesh::CircularMeshUvMode::Mask{.angle = 0.35f});

        std::println("Circular primitives: sector={} segment={} vertices", sector.count_vertices(),
                     segment.count_vertices());

        const auto sector_handle  = mesh_assets.emplace(std::move(sector));
        const auto segment_handle = mesh_assets.emplace(std::move(segment));

        world.spawn(camera::Camera2d{}, transform::Transform{});
        world.spawn(mesh::Mesh2d{sector_handle},
                    sprite_render::MeshMaterial2d{.color = glm::vec4(0.98f, 0.55f, 0.30f, 1.0f)},
                    transform::Transform{.translation = {-190.0f, 0.0f, 0.0f}});
        world.spawn(mesh::Mesh2d{segment_handle},
                    sprite_render::MeshMaterial2d{.color = glm::vec4(0.35f, 0.75f, 0.98f, 1.0f)},
                    transform::Transform{.translation = {190.0f, 0.0f, 0.0f}});
    }
};
}  // namespace

int main() {
    auto app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Bevy Circular Primitives | left: sector | right: segment";
    primary_window.size  = {800, 500};

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
        .add_plugins(CircularPrimitivesExamplePlugin{});

    app.run();
}
