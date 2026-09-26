// Visual verification for Bevy-shaped 2D wireframes (S11). The scene matches
// Bevy's `examples/2d/wireframe_2d.rs`: global wireframes are enabled, one shape
// overrides the colour with `Wireframe2dColor`, and one opts out with
// `NoWireframe2d`, so all three paths are visible in one frame.

#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

#include <cstdint>
#include <optional>
#include <vector>

using namespace epix;

namespace {
mesh::Mesh make_triangle() {
    std::vector<glm::vec3> positions = {{0.0f, 120.0f, 0.0f}, {-120.0f, -90.0f, 0.0f}, {120.0f, -90.0f, 0.0f}};
    std::vector<std::uint16_t> indices = {0, 1, 2};
    return mesh::Mesh(wgpu::PrimitiveTopology::eTriangleList, assets::RenderAssetUsages::RENDER_WORLD)
        .with_inserted_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
        .with_inserted_indices(mesh::Indices{std::move(indices)});
}

struct Wireframe2dVisualTestPlugin {
    void ready(app::App& app) {
        auto& world     = app.world_mut();
        auto& meshes    = world.resource_mut<assets::Assets<mesh::Mesh>>();
        auto& materials = world.resource_mut<assets::Assets<sprite_render::ColorMaterial>>();

        world.insert_resource(sprite_render::Wireframe2dConfig{
            .global        = true,
            .default_color = glm::vec4{0.65f, 0.65f, 0.65f, 1.0f},
        });

        world.spawn(camera::Camera2d{}, transform::Transform{});

        const auto triangle  = meshes.emplace(make_triangle());
        const auto rectangle = meshes.emplace(mesh::make_box2d(200.0f, 200.0f));
        const auto circle    = meshes.emplace(mesh::make_circle(100.0f, std::nullopt, 64));

        // Global wireframes: the configured default colour applies.
        world.spawn(mesh::Mesh2d{triangle},
                    sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{
                        materials.emplace(sprite_render::ColorMaterial{.color = glm::vec4{0.20f, 0.40f, 0.80f, 1.0f}})},
                    transform::Transform{.translation = glm::vec3{-260.0f, 0.0f, 0.0f}});

        // `Wireframe2dColor` overrides the global colour for its entity.
        world.spawn(mesh::Mesh2d{rectangle},
                    sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{
                        materials.emplace(sprite_render::ColorMaterial{.color = glm::vec4{0.20f, 0.80f, 0.40f, 1.0f}})},
                    sprite_render::Wireframe2d{},
                    sprite_render::Wireframe2dColor{.color = glm::vec4{1.0f, 0.20f, 0.20f, 1.0f}},
                    transform::Transform{});

        // `NoWireframe2d` opts this entity out of the global setting.
        world.spawn(mesh::Mesh2d{circle},
                    sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{
                        materials.emplace(sprite_render::ColorMaterial{.color = glm::vec4{0.80f, 0.60f, 0.20f, 1.0f}})},
                    sprite_render::NoWireframe2d{},
                    transform::Transform{.translation = glm::vec3{260.0f, 0.0f, 0.0f}});
    }
};
}  // namespace

int main() {
    app::App app = app::App::create();
    window::Window primary_window;
    primary_window.title = "Wireframe 2D Visual Test";
    primary_window.size  = {960, 540};

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
        .add_plugins(sprite_render::ColorMaterialPlugin{})
        .add_plugins(sprite_render::Wireframe2dPlugin{})
        .add_plugins(Wireframe2dVisualTestPlugin{});
    app.run();
}
