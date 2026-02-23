#include <epix/core.hpp>
#include <epix/core_graph.hpp>
#include <epix/glfw.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>

struct TestPlugin {
    void finish(epix::App& app) {
        auto& assets = app.world_mut().resource_mut<epix::assets::Assets<epix::mesh::Mesh>>();
        app.world_mut().spawn(epix::render::core_2d::Camera2DBundle{});
        app.world_mut().spawn(epix::mesh::Mesh2d{assets.emplace(epix::mesh::make_circle(10.0f))},
                              epix::mesh::ColorMaterial{.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)},
                              epix::transform::Transform{
                                  .translation = glm::vec3(-100.0f, 0.0f, 0.0f),
                                  .scaler      = glm::vec3(5.0f),
                              });
        app.world_mut().spawn(
            epix::mesh::Mesh2d{assets.emplace(epix::mesh::make_circle(10.0f, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)))},
            epix::mesh::NoMaterial{},
            epix::transform::Transform{
                .translation = glm::vec3(100.0f, 0.0f, 0.0f),
                .scaler      = glm::vec3(5.0f),
            });
    }
};

int main() {
    epix::App app = epix::App::create();

    app.add_plugins(epix::window::WindowPlugin{})
        .add_plugins(epix::input::InputPlugin{})
        .add_plugins(epix::glfw::GLFWPlugin{})
        .add_plugins(epix::transform::TransformPlugin{})
        .add_plugins(epix::render::RenderPlugin{}.set_validation(0))
        .add_plugins(epix::core_graph::CoreGraphPlugin{})
        .add_plugins(epix::mesh::MeshPlugin{})
        .add_plugins(epix::mesh::MeshRenderPlugin{})
        .add_plugins(epix::mesh::ColorMaterialPlugin{})
        .add_plugins(TestPlugin{});

    app.run();
}