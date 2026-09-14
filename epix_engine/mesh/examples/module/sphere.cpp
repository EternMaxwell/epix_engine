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

struct SphereExamplePlugin {
    void ready(app::App& app) {
        auto& world       = app.world_mut();
        auto& mesh_assets = world.resource_mut<assets::Assets<mesh::Mesh>>();

        // `SphereKind::Uv { sectors, stacks }` walks `stacks + 1` rings of `sectors + 1` vertices with
        // the seam duplicated, so the vertex count is `(stacks + 1) * (sectors + 1)`.
        const mesh::Sphere sphere_shape{0.45f};
        mesh::Mesh coarse = sphere_shape.mesh().with_kind(mesh::SphereKind{mesh::SphereKind::Uv{6, 4}}).build();
        mesh::Mesh medium = sphere_shape.mesh().with_kind(mesh::SphereKind{mesh::SphereKind::Uv{12, 8}}).build();
        mesh::Mesh fine   = sphere_shape.mesh().with_kind(mesh::SphereKind{mesh::SphereKind::Uv{24, 16}}).build();
        // The default kind is `Ico { subdivisions: 5 }`, which `build` unwraps exactly as Bevy's does.
        mesh::Mesh icosphere = sphere_shape.mesh().build();

        std::println("UV spheres: coarse(6x4)={} medium(12x8)={} fine(24x16)={} vertices", coarse.count_vertices(),
                     medium.count_vertices(), fine.count_vertices());
        std::println("Icosphere (default Ico{{5}}): {} vertices, Bevy's 10(s + 1)^2 + 2 = {}",
                     icosphere.count_vertices(), 10 * (5 + 1) * (5 + 1) + 2);
        std::println("Indices: coarse={} medium={} fine={} icosphere={}", coarse.indices()->get().len(),
                     medium.indices()->get().len(), fine.indices()->get().len(), icosphere.indices()->get().len());
        std::println("Bevy-aligned attributes: coarse=[{}] medium=[{}] fine=[{}] icosphere=[{}]",
                     attribute_names(coarse), attribute_names(medium), attribute_names(fine),
                     attribute_names(icosphere));
        // Bevy's `Measured3d`: `4 PI r^2` and `4/3 PI r^3`, independent of the mesh kind and resolution.
        std::println("Bevy Measured3d (area, volume): sphere=({}, {}) diameter={}", mesh::area(sphere_shape),
                     mesh::volume(sphere_shape), sphere_shape.diameter());
        // The icosphere guard rejects `subdivisions >= 80` before generating any vertex.
        const auto rejected = sphere_shape.mesh().ico(80);
        std::println("Icosphere guard at 80: rejected={} message=\"{}\"", !rejected.has_value(),
                     rejected.has_value() ? std::string{"<none>"} : rejected.error().to_string());

        const auto coarse_handle    = mesh_assets.emplace(std::move(coarse));
        const auto medium_handle    = mesh_assets.emplace(std::move(medium));
        const auto fine_handle      = mesh_assets.emplace(std::move(fine));
        const auto icosphere_handle = mesh_assets.emplace(std::move(icosphere));

        camera::OrthographicProjection orthographic{};
        orthographic.scaling_mode = camera::ScalingMode{camera::ScalingMode::FixedVertical{.viewport_height = 2.4f}};
        world.spawn(camera::Camera2d{}, transform::Transform{}, camera::Projection::orthographic(orthographic));

        const auto place = [&world](const assets::Handle<mesh::Mesh>& handle, glm::vec3 translation, glm::vec4 color) {
            world.spawn(mesh::Mesh2d{handle}, sprite_render::MeshMaterial2d{.color = color},
                        transform::Transform{.translation = translation});
        };
        place(coarse_handle, {-1.5f, 0.0f, 0.0f}, glm::vec4(0.98f, 0.72f, 0.30f, 1.0f));
        place(medium_handle, {-0.5f, 0.0f, 0.0f}, glm::vec4(0.42f, 0.80f, 0.98f, 1.0f));
        place(fine_handle, {0.5f, 0.0f, 0.0f}, glm::vec4(0.55f, 0.92f, 0.60f, 1.0f));
        place(icosphere_handle, {1.5f, 0.0f, 0.0f}, glm::vec4(0.90f, 0.52f, 0.88f, 1.0f));
    }
};
}  // namespace

int main() {
    auto app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Bevy Sphere | UV 6x4 | UV 12x8 | UV 24x16 | Ico 5";
    primary_window.size  = {1100, 420};

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
        .add_plugins(SphereExamplePlugin{});

    app.run();
}
