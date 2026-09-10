#ifndef EPIX_IMPORT_STD
#include <print>
#include <string>
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
        mesh::Mesh cylinder    = mesh::Cylinder{0.32f, 0.86f};
        mesh::Mesh torus = std::move(mesh::Torus{}.mesh()).with_minor_resolution(12).with_major_resolution(24).build();

        std::println(
            "3D primitives: cuboid={} plane={} triangle={} tetrahedron={} segment={} cylinder={} torus={} vertices",
            cuboid.count_vertices(), plane.count_vertices(), triangle.count_vertices(), tetrahedron.count_vertices(),
            segment.count_vertices(), cylinder.count_vertices(), torus.count_vertices());
        std::println(
            "Bevy-aligned attributes: cuboid=[{}] plane=[{}] triangle=[{}] tetrahedron=[{}] segment=[{}] cylinder=[{}] "
            "torus=[{}]",
            attribute_names(cuboid), attribute_names(plane), attribute_names(triangle), attribute_names(tetrahedron),
            attribute_names(segment), attribute_names(cylinder), attribute_names(torus));

        const auto cuboid_handle      = asset_store.emplace(std::move(cuboid));
        const auto plane_handle       = asset_store.emplace(std::move(plane));
        const auto triangle_handle    = asset_store.emplace(std::move(triangle));
        const auto tetrahedron_handle = asset_store.emplace(std::move(tetrahedron));
        const auto segment_handle     = asset_store.emplace(std::move(segment));
        const auto cylinder_handle    = asset_store.emplace(std::move(cylinder));
        const auto torus_handle       = asset_store.emplace(std::move(torus));

        // The Bevy primitives are unit-scale, so the camera fixes a 4.0-unit tall viewport and the
        // seven shapes are laid out on two rows that both fit inside the visible width.
        camera::OrthographicProjection orthographic{};
        orthographic.scaling_mode = camera::ScalingMode{camera::ScalingMode::FixedVertical{.viewport_height = 4.0f}};
        world.spawn(camera::Camera2d{}, transform::Transform{}, camera::Projection::orthographic(orthographic));

        const auto place = [&world](const assets::Handle<mesh::Mesh>& handle, glm::vec3 translation, glm::vec4 color) {
            world.spawn(mesh::Mesh2d{handle}, sprite_render::MeshMaterial2d{.color = color},
                        transform::Transform{.translation = translation});
        };
        // Upper row: cuboid, plane, triangle, tetrahedron.
        place(cuboid_handle, {-1.80f, 1.0f, 0.0f}, glm::vec4(0.42f, 0.80f, 0.98f, 1.0f));
        place(plane_handle, {-0.60f, 1.0f, 0.0f}, glm::vec4(0.55f, 0.92f, 0.60f, 1.0f));
        place(triangle_handle, {0.60f, 1.0f, 0.0f}, glm::vec4(0.90f, 0.52f, 0.88f, 1.0f));
        place(tetrahedron_handle, {1.80f, 1.0f, 0.0f}, glm::vec4(0.98f, 0.72f, 0.30f, 1.0f));
        // Lower row: segment, cylinder, torus.
        place(segment_handle, {-1.80f, -1.1f, 0.0f}, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        place(cylinder_handle, {-0.60f, -1.1f, 0.0f}, glm::vec4(0.98f, 0.55f, 0.45f, 1.0f));
        place(torus_handle, {0.80f, -1.1f, 0.0f}, glm::vec4(0.72f, 0.86f, 0.98f, 1.0f));
    }
};
}  // namespace

int main() {
    auto app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Bevy 3D Primitives | cuboid | plane | triangle | tetrahedron | segment";
    primary_window.size  = {1500, 420};

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
