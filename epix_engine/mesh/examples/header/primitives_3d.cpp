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
#include <epix/sprite_render/mesh2d.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <glm/glm.hpp>
#include <print>
#include <string>
#include <utility>

using namespace epix;

namespace {
/** @brief Report the vertex attributes a generated mesh actually carries. */
std::string attribute_names(const mesh::Mesh& value) {
    std::string names;
    for (const auto& [attribute, values] : value.attributes()) {
        if (!names.empty()) names += ", ";
        names += std::string(attribute.name);
    }
    return names;
}

struct Primitives3dExamplePlugin {
    void ready(app::App& app) {
        auto& world       = app.world_mut();
        auto& asset_store = world.resource_mut<assets::Assets<mesh::Mesh>>();

        // Every shape uses Bevy's default primitive, so the vertex counts and attribute sets are
        // the ones Bevy produces. Core2D has no Core3d graph yet, so the meshes are rendered on
        // the XY plane through the existing 2D pipeline: this exercises the generated data and
        // the mesh pipeline without requiring the missing 3D graph.
        mesh::Mesh cuboid      = mesh::Cuboid{1.0f, 1.0f, 1.0f};
        mesh::Mesh plane       = std::move(mesh::Plane3d{}.mesh()).with_subdivisions(3).build();
        mesh::Mesh triangle    = mesh::Triangle3d{{-0.6f, -0.5f, 0.0f}, {0.6f, -0.5f, 0.0f}, {0.0f, 0.7f, 0.0f}};
        mesh::Mesh tetrahedron = mesh::Tetrahedron{};
        mesh::Mesh segment     = mesh::Segment3d{{-0.6f, 0.0f, 0.0f}, {0.6f, 0.0f, 0.0f}};

        std::println("3D primitives: cuboid={} plane={} triangle={} tetrahedron={} segment={} vertices",
                     cuboid.count_vertices(), plane.count_vertices(), triangle.count_vertices(),
                     tetrahedron.count_vertices(), segment.count_vertices());
        std::println("Bevy-aligned attributes: cuboid=[{}] plane=[{}] triangle=[{}] tetrahedron=[{}] segment=[{}]",
                     attribute_names(cuboid), attribute_names(plane), attribute_names(triangle),
                     attribute_names(tetrahedron), attribute_names(segment));

        const auto cuboid_handle      = asset_store.emplace(std::move(cuboid));
        const auto plane_handle       = asset_store.emplace(std::move(plane));
        const auto triangle_handle    = asset_store.emplace(std::move(triangle));
        const auto tetrahedron_handle = asset_store.emplace(std::move(tetrahedron));
        const auto segment_handle     = asset_store.emplace(std::move(segment));

        // The Bevy primitives are unit-scale, so the camera fixes a 3.2-unit tall viewport.
        camera::OrthographicProjection orthographic{};
        orthographic.scaling_mode = camera::ScalingMode{camera::ScalingMode::FixedVertical{.viewport_height = 3.2f}};
        world.spawn(camera::Camera2d{}, transform::Transform{}, camera::Projection::orthographic(orthographic));

        const auto place = [&world](const assets::Handle<mesh::Mesh>& handle, glm::vec3 translation, glm::vec4 color) {
            world.spawn(mesh::Mesh2d{handle}, sprite_render::MeshMaterial2d{.color = color},
                        transform::Transform{.translation = translation});
        };
        place(cuboid_handle, {-1.30f, 0.60f, 0.0f}, glm::vec4(0.42f, 0.80f, 0.98f, 1.0f));
        place(plane_handle, {0.10f, 0.62f, 0.0f}, glm::vec4(0.55f, 0.92f, 0.60f, 1.0f));
        place(triangle_handle, {1.35f, 0.55f, 0.0f}, glm::vec4(0.90f, 0.52f, 0.88f, 1.0f));
        place(tetrahedron_handle, {-0.90f, -0.85f, 0.0f}, glm::vec4(0.98f, 0.72f, 0.30f, 1.0f));
        place(segment_handle, {0.65f, -0.95f, 0.0f}, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    }
};
}  // namespace

int main() {
    auto app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Bevy 3D Primitives | cuboid | plane | triangle | tetrahedron | segment";
    primary_window.size  = {1000, 460};

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
        .add_plugins(Primitives3dExamplePlugin{});

    app.run();
}
