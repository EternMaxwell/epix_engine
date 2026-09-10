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
#include <stdexcept>
#include <utility>
#include <vector>
#include <webgpu.hpp>

using namespace epix;

namespace {
mesh::Mesh make_indexed_gradient_quad() {
    std::vector<glm::vec3> positions{
        {-90.0f, -90.0f, 0.0f},
        {90.0f, -90.0f, 0.0f},
        {90.0f, 90.0f, 0.0f},
        {-90.0f, 90.0f, 0.0f},
    };
    std::vector<glm::vec4> colors{
        {1.0f, 0.20f, 0.12f, 1.0f},
        {1.0f, 0.78f, 0.12f, 1.0f},
        {0.12f, 0.78f, 0.94f, 1.0f},
        {0.48f, 0.22f, 0.96f, 1.0f},
    };

    return mesh::Mesh(wgpu::PrimitiveTopology::eTriangleList, assets::RenderAssetUsages::RENDER_WORLD)
        .with_inserted_attribute(mesh::Mesh::ATTRIBUTE_POSITION, positions)
        .with_inserted_attribute(mesh::Mesh::ATTRIBUTE_COLOR, colors)
        .with_inserted_indices(mesh::Indices{std::vector<std::uint16_t>{0, 1, 2, 2, 3, 0}});
}

struct MeshAlgorithmsExamplePlugin {
    void ready(app::App& app) {
        auto& world       = app.world_mut();
        auto& mesh_assets = world.resource_mut<assets::Assets<mesh::Mesh>>();

        auto indexed    = make_indexed_gradient_quad();
        auto duplicated = indexed;
        auto inverted   = indexed;

        duplicated.duplicate_vertices();
        if (auto result = inverted.invert_winding(); !result) {
            throw std::runtime_error(result.error().to_string());
        }

        const auto indexed_handle    = mesh_assets.emplace(std::move(indexed));
        const auto duplicated_handle = mesh_assets.emplace(std::move(duplicated));
        const auto inverted_handle   = mesh_assets.emplace(std::move(inverted));

        world.spawn(camera::Camera2d{}, transform::Transform{});
        world.spawn(mesh::Mesh2d{indexed_handle},
                    sprite_render::MeshMaterial2d{.color = glm::vec4(1.0f, 0.78f, 0.72f, 1.0f)},
                    transform::Transform{.translation = {-280.0f, 0.0f, 0.0f}});
        world.spawn(mesh::Mesh2d{duplicated_handle},
                    sprite_render::MeshMaterial2d{.color = glm::vec4(0.78f, 1.0f, 0.88f, 1.0f)},
                    transform::Transform{.translation = {0.0f, 0.0f, 0.0f}});
        world.spawn(mesh::Mesh2d{inverted_handle},
                    sprite_render::MeshMaterial2d{.color = glm::vec4(0.76f, 0.86f, 1.0f, 1.0f)},
                    transform::Transform{.translation = {280.0f, 0.0f, 0.0f}});

        std::println("Mesh algorithms: left=indexed, center=duplicate_vertices (non-indexed), "
                     "right=invert_winding");
    }
};
}  // namespace

int main() {
    auto app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Mesh Algorithms | left: indexed | center: duplicated | right: inverted";
    primary_window.size  = {960, 540};

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
        .add_plugins(MeshAlgorithmsExamplePlugin{});

    app.run();
}
