#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/sdl3/render.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
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
        auto& materials   = world.resource_mut<assets::Assets<sprite_render::ColorMaterial>>();

        // Every shape uses Bevy's default primitive, so the vertex counts and attribute sets are
        // the ones Bevy produces. Core2D has no Core3d graph yet, so the meshes are rendered on
        // the XY plane through the existing 2D pipeline: this exercises the generated data and
        // the mesh pipeline without requiring the missing 3D graph.
        // The shape values are kept around so the `Measured2d`/`Measured3d` helpers below report the
        // same primitives the meshes were built from.
        const mesh::Cuboid cuboid_shape{1.0f, 1.0f, 1.0f};
        const mesh::Plane3d plane_shape{};
        const mesh::Triangle3d triangle_shape{{-0.6f, -0.5f, 0.0f}, {0.6f, -0.5f, 0.0f}, {0.0f, 0.7f, 0.0f}};
        const mesh::Tetrahedron tetrahedron_shape{};
        const mesh::Cylinder cylinder_shape{0.32f, 0.86f};
        const mesh::Torus torus_shape{};
        const mesh::Cone cone_shape{0.36f, 1.0f};
        const mesh::ConicalFrustum frustum_shape{0.18f, 0.4f, 0.9f};
        const mesh::Capsule3d capsule_shape{0.26f, 0.6f};

        mesh::Mesh cuboid      = cuboid_shape;
        mesh::Mesh plane       = std::move(plane_shape.mesh()).with_subdivisions(3).build();
        mesh::Mesh triangle    = triangle_shape;
        mesh::Mesh tetrahedron = tetrahedron_shape;
        mesh::Mesh segment     = mesh::Segment3d{{-0.6f, 0.0f, 0.0f}, {0.6f, 0.0f, 0.0f}};
        mesh::Mesh cylinder    = cylinder_shape;
        mesh::Mesh torus = std::move(torus_shape.mesh()).with_minor_resolution(12).with_major_resolution(24).build();
        // The two rotational solids that close Bevy's cone/frustum builders. The
        // cone's anchor is left at Bevy's default midpoint so its tip sits above
        // its base, exactly as Bevy's own test fixture does.
        mesh::Mesh cone    = std::move(cone_shape.mesh()).with_resolution(24).build();
        mesh::Mesh frustum = std::move(frustum_shape.mesh()).with_resolution(24).build();
        // The capsule closes Bevy's rotational solids. Its UV profile is left at
        // Bevy's default `Aspect`, and the ring count at the default 0.
        mesh::Mesh capsule = std::move(capsule_shape.mesh()).with_longitudes(24).with_latitudes(12).build();

        std::println(
            "3D primitives: cuboid={} plane={} triangle={} tetrahedron={} segment={} cylinder={} torus={} cone={} "
            "frustum={} capsule={} vertices",
            cuboid.count_vertices(), plane.count_vertices(), triangle.count_vertices(), tetrahedron.count_vertices(),
            segment.count_vertices(), cylinder.count_vertices(), torus.count_vertices(), cone.count_vertices(),
            frustum.count_vertices(), capsule.count_vertices());
        std::println(
            "Bevy-aligned attributes: cuboid=[{}] plane=[{}] triangle=[{}] tetrahedron=[{}] segment=[{}] cylinder=[{}] "
            "torus=[{}] cone=[{}] frustum=[{}] capsule=[{}]",
            attribute_names(cuboid), attribute_names(plane), attribute_names(triangle), attribute_names(tetrahedron),
            attribute_names(segment), attribute_names(cylinder), attribute_names(torus), attribute_names(cone),
            attribute_names(frustum), attribute_names(capsule));
        // Bevy's `Measured3d` (area, volume) and the two 3D shapes Bevy measures as 2D shapes.
        std::println(
            "Bevy Measured3d (area, volume): cuboid=({}, {}) cylinder=({}, {}) capsule=({}, {}) cone=({}, {}) "
            "frustum=({}, {}) torus=({}, {}) tetrahedron=({}, {})",
            mesh::area(cuboid_shape), mesh::volume(cuboid_shape), mesh::area(cylinder_shape),
            mesh::volume(cylinder_shape), mesh::area(capsule_shape), mesh::volume(capsule_shape),
            mesh::area(cone_shape), mesh::volume(cone_shape), mesh::area(frustum_shape), mesh::volume(frustum_shape),
            mesh::area(torus_shape), mesh::volume(torus_shape), mesh::area(tetrahedron_shape),
            mesh::volume(tetrahedron_shape));
        std::println("Bevy Measured2d (area, perimeter): plane=({}, {}) triangle3d=({}, {})", mesh::area(plane_shape),
                     mesh::perimeter(plane_shape), mesh::area(triangle_shape), mesh::perimeter(triangle_shape));

        const auto cuboid_handle      = asset_store.emplace(std::move(cuboid));
        const auto plane_handle       = asset_store.emplace(std::move(plane));
        const auto triangle_handle    = asset_store.emplace(std::move(triangle));
        const auto tetrahedron_handle = asset_store.emplace(std::move(tetrahedron));
        const auto segment_handle     = asset_store.emplace(std::move(segment));
        const auto cylinder_handle    = asset_store.emplace(std::move(cylinder));
        const auto torus_handle       = asset_store.emplace(std::move(torus));
        const auto cone_handle        = asset_store.emplace(std::move(cone));
        const auto frustum_handle     = asset_store.emplace(std::move(frustum));
        const auto capsule_handle     = asset_store.emplace(std::move(capsule));

        // The Bevy primitives are unit-scale, so the camera fixes a 4.0-unit tall viewport and the
        // ten shapes are laid out on two rows of five, both inside the visible width.
        camera::OrthographicProjection orthographic{};
        orthographic.scaling_mode = camera::ScalingMode{camera::ScalingMode::FixedVertical{.viewport_height = 4.0f}};
        world.spawn(camera::Camera2d{}, transform::Transform{}, camera::Projection::orthographic(orthographic));

        const auto place = [&world, &materials](const assets::Handle<mesh::Mesh>& handle,
                                                glm::vec3 translation,
                                                glm::vec4 color) {
            world.spawn(mesh::Mesh2d{handle},
                        sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{
                            materials.emplace(sprite_render::ColorMaterial{
                                .color = ::epix::color::Color::linear_rgba(color.r, color.g, color.b, color.a), .mode = sprite_render::AlphaMode2dOpaque{}})},
                        transform::Transform{.translation = translation});
        };
        // Upper row: cuboid, plane, triangle, tetrahedron, segment.
        place(cuboid_handle, {-1.80f, 1.0f, 0.0f}, glm::vec4(0.42f, 0.80f, 0.98f, 1.0f));
        place(plane_handle, {-0.90f, 1.0f, 0.0f}, glm::vec4(0.55f, 0.92f, 0.60f, 1.0f));
        place(triangle_handle, {0.0f, 1.0f, 0.0f}, glm::vec4(0.90f, 0.52f, 0.88f, 1.0f));
        place(tetrahedron_handle, {0.90f, 1.0f, 0.0f}, glm::vec4(0.98f, 0.72f, 0.30f, 1.0f));
        place(segment_handle, {1.80f, 1.0f, 0.0f}, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        // Lower row: cylinder, torus, cone, frustum, capsule.
        place(cylinder_handle, {-1.80f, -1.1f, 0.0f}, glm::vec4(0.98f, 0.55f, 0.45f, 1.0f));
        place(torus_handle, {-0.90f, -1.1f, 0.0f}, glm::vec4(0.72f, 0.86f, 0.98f, 1.0f));
        place(cone_handle, {0.0f, -1.1f, 0.0f}, glm::vec4(0.86f, 0.94f, 0.36f, 1.0f));
        place(frustum_handle, {0.90f, -1.1f, 0.0f}, glm::vec4(0.98f, 0.80f, 0.56f, 1.0f));
        place(capsule_handle, {1.80f, -1.1f, 0.0f}, glm::vec4(0.66f, 0.78f, 0.98f, 1.0f));
    }
};
}  // namespace

int main() {
    auto app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Bevy 3D Primitives | cuboid | plane | triangle | tetrahedron | segment | cylinder | torus | cone | frustum | capsule";
    primary_window.size  = {1500, 420};

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
        .add_plugins(Primitives3dExamplePlugin{});

    app.run();
}
