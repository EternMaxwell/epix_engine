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
import epix.sdl3.core;
import epix.sdl3.render;
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

/** @brief Report the depth range the generated prism spans. */
std::pair<float, float> depth_range(const mesh::Mesh& value) {
    const auto positions = value.attribute(mesh::Mesh::ATTRIBUTE_POSITION);
    if (!positions) return {0.0f, 0.0f};
    const auto* values = positions->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    if (values == nullptr || values->empty()) return {0.0f, 0.0f};
    float minimum = (*values)[0][2];
    float maximum = (*values)[0][2];
    for (const auto& position : *values) {
        minimum = std::min(minimum, position[2]);
        maximum = std::max(maximum, position[2]);
    }
    return {minimum, maximum};
}

struct ExtrusionExamplePlugin {
    void ready(app::App& app) {
        auto& world       = app.world_mut();
        auto& mesh_assets = world.resource_mut<assets::Assets<mesh::Mesh>>();
        auto& materials   = world.resource_mut<assets::Assets<sprite_render::ColorMaterial>>();

        // `Extrusion<P>` thickens a 2D shape's mesh: the base mesh is the front cap, a mirrored copy is
        // the back cap, and the mantel connects them over `segments + 1` layers. Only `Circle`,
        // `Ellipse`, `Annulus` and `Capsule2d` accept `resolution` directly, so the ring is configured
        // through `with_inner`, exactly as in Bevy.
        const auto with_ring_resolution = [](mesh::CircleMeshBuilder circle) {
            circle.resolution = 6;
            return circle;
        };
        const auto ring = mesh::Ring<mesh::Circle>::from_primitive_and_thickness(mesh::Circle{0.42f}, 0.16f);

        const mesh::Extrusion<mesh::Circle> circle_shape{mesh::Circle{0.45f}, 1.0f};
        const mesh::Extrusion<mesh::Rectangle> rectangle_shape{mesh::Rectangle{0.7f, 0.7f}, 1.0f};
        const mesh::Extrusion<mesh::Capsule2d> capsule_shape{mesh::Capsule2d{0.22f, 0.4f}, 1.0f};
        const mesh::Extrusion<mesh::Ring<mesh::Circle>> ring_shape{ring, 1.0f};

        mesh::Mesh circle    = circle_shape.mesh().resolution(8);
        mesh::Mesh rectangle = rectangle_shape.mesh().with_segments(3);
        mesh::Mesh capsule   = capsule_shape.mesh().resolution(6);
        mesh::Mesh ring_mesh =
            ring_shape.mesh().with_inner([&with_ring_resolution](mesh::RingMeshBuilder<mesh::Circle> builder) {
                return builder.with_inner(with_ring_resolution);
            });

        std::println("Extrusions: circle={} rectangle={} capsule={} ring={} vertices", circle.count_vertices(),
                     rectangle.count_vertices(), capsule.count_vertices(), ring_mesh.count_vertices());
        std::println("Indices: circle={} rectangle={} capsule={} ring={}", circle.indices()->get().len(),
                     rectangle.indices()->get().len(), capsule.indices()->get().len(),
                     ring_mesh.indices()->get().len());
        std::println("Bevy-aligned attributes: circle=[{}] rectangle=[{}] capsule=[{}] ring=[{}]",
                     attribute_names(circle), attribute_names(rectangle), attribute_names(capsule),
                     attribute_names(ring_mesh));
        // The caps sit at `+half_depth` and `-half_depth`, so every prism spans the same depth.
        std::println("Depth ranges: circle=({}, {}) rectangle=({}, {}) capsule=({}, {}) ring=({}, {})",
                     depth_range(circle).first, depth_range(circle).second, depth_range(rectangle).first,
                     depth_range(rectangle).second, depth_range(capsule).first, depth_range(capsule).second,
                     depth_range(ring_mesh).first, depth_range(ring_mesh).second);
        // Bevy's `Measured3d`: `2 * (base.area + half_depth * base.perimeter)` and `2 * base.area * half_depth`.
        std::println(
            "Bevy Measured3d (area, volume): circle=({}, {}) rectangle=({}, {}) capsule=({}, {}) ring=({}, {})",
            mesh::area(circle_shape), mesh::volume(circle_shape), mesh::area(rectangle_shape),
            mesh::volume(rectangle_shape), mesh::area(capsule_shape), mesh::volume(capsule_shape),
            mesh::area(ring_shape), mesh::volume(ring_shape));

        const auto circle_handle    = mesh_assets.emplace(std::move(circle));
        const auto rectangle_handle = mesh_assets.emplace(std::move(rectangle));
        const auto capsule_handle   = mesh_assets.emplace(std::move(capsule));
        const auto ring_handle      = mesh_assets.emplace(std::move(ring_mesh));

        // The 3D prisms are presented through the existing 2D path: an extrusion of `P` projects onto
        // the same silhouette as `P`, which is what the capture shows.
        camera::OrthographicProjection orthographic{};
        orthographic.scaling_mode = camera::ScalingMode{camera::ScalingMode::FixedVertical{.viewport_height = 2.4f}};
        world.spawn(camera::Camera2d{}, transform::Transform{}, camera::Projection::orthographic(orthographic));

        const auto place = [&world, &materials](const assets::Handle<mesh::Mesh>& handle,
                                                glm::vec3 translation,
                                                glm::vec4 color) {
            world.spawn(mesh::Mesh2d{handle},
                        sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{
                            materials.emplace(sprite_render::ColorMaterial{
                                .color = color, .mode = sprite_render::AlphaMode2dOpaque{}})},
                        transform::Transform{.translation = translation});
        };
        place(circle_handle, {-1.35f, 0.0f, 0.0f}, glm::vec4(0.98f, 0.72f, 0.30f, 1.0f));
        place(rectangle_handle, {-0.45f, 0.0f, 0.0f}, glm::vec4(0.42f, 0.80f, 0.98f, 1.0f));
        place(capsule_handle, {0.45f, 0.0f, 0.0f}, glm::vec4(0.55f, 0.92f, 0.60f, 1.0f));
        place(ring_handle, {1.35f, 0.0f, 0.0f}, glm::vec4(0.90f, 0.52f, 0.88f, 1.0f));
    }
};
}  // namespace

int main() {
    auto app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Bevy Extrusion | circle | rectangle (3 segments) | capsule | ring";
    primary_window.size  = {1100, 420};

    app.add_plugins(app::TaskPoolPlugin{})
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
        .add_plugins(sprite_render::Mesh2dRenderPlugin{})
        .add_plugins(sprite_render::ColorMaterialPlugin{})
        .add_plugins(ExtrusionExamplePlugin{});

    app.run();
}
