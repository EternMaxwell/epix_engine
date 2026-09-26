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

struct AnnulusRhombusTriangleCapsuleExamplePlugin {
    void ready(app::App& app) {
        auto& world       = app.world_mut();
        auto& mesh_assets = world.resource_mut<assets::Assets<mesh::Mesh>>();
        auto& materials   = world.resource_mut<assets::Assets<sprite_render::ColorMaterial>>();

        // Every shape uses its Bevy-facing primitive default plus an explicit resolution, so the
        // generated vertex counts and attribute sets are the ones Bevy would produce.
        const mesh::Annulus annulus_shape{0.30f, 0.58f};
        const mesh::Rhombus rhombus_shape{0.62f, 0.98f};
        const mesh::Triangle2d triangle_shape{{-0.34f, -0.44f}, {0.34f, -0.44f}, {0.0f, 0.46f}};
        const mesh::Capsule2d capsule_shape{0.22f, 0.40f};
        mesh::Mesh annulus  = annulus_shape;
        mesh::Mesh rhombus  = rhombus_shape;
        mesh::Mesh triangle = triangle_shape;
        mesh::Mesh capsule  = capsule_shape;
        // `Ring` keeps the ring in the shape API as a pair of insetted shapes; the mesh builder
        // stitches them into one closed mesh with a continuous radial UV mapping.
        const auto ring      = mesh::Ring<mesh::Circle>::from_primitive_and_thickness(mesh::Circle{0.42f}, 0.18f);
        mesh::Mesh ring_mesh = ring.mesh().build();

        std::println("Annulus/Rhombus/Triangle2d/Capsule2d/Ring: {} {} {} {} {} vertices", annulus.count_vertices(),
                     rhombus.count_vertices(), triangle.count_vertices(), capsule.count_vertices(),
                     ring_mesh.count_vertices());
        std::println("Bevy-aligned attributes: annulus=[{}] rhombus=[{}] triangle=[{}] capsule=[{}] ring=[{}]",
                     attribute_names(annulus), attribute_names(rhombus), attribute_names(triangle),
                     attribute_names(capsule), attribute_names(ring_mesh));
        // Bevy's `Measured2d`: `area` and `perimeter` for every shape, including the `Ring` pair.
        std::println(
            "Bevy Measured2d (area, perimeter): annulus=({}, {}) rhombus=({}, {}) triangle=({}, {}) capsule=({}, {}) "
            "ring=({}, {})",
            mesh::area(annulus_shape), mesh::perimeter(annulus_shape), mesh::area(rhombus_shape),
            mesh::perimeter(rhombus_shape), mesh::area(triangle_shape), mesh::perimeter(triangle_shape),
            mesh::area(capsule_shape), mesh::perimeter(capsule_shape), mesh::area(ring), mesh::perimeter(ring));

        const auto annulus_handle  = mesh_assets.emplace(std::move(annulus));
        const auto rhombus_handle  = mesh_assets.emplace(std::move(rhombus));
        const auto triangle_handle = mesh_assets.emplace(std::move(triangle));
        const auto capsule_handle  = mesh_assets.emplace(std::move(capsule));
        const auto ring_handle     = mesh_assets.emplace(std::move(ring_mesh));

        // The Bevy primitives have unit-scale dimensions, so the camera fixes a 5-unit tall
        // viewport instead of the default 1-world-unit-per-pixel window projection.
        camera::OrthographicProjection orthographic{};
        orthographic.scaling_mode = camera::ScalingMode{camera::ScalingMode::FixedVertical{.viewport_height = 2.4f}};
        world.spawn(camera::Camera2d{}, transform::Transform{}, camera::Projection::orthographic(orthographic));

        const auto place = [&world, &materials](const assets::Handle<mesh::Mesh>& handle, glm::vec3 translation,
                                                glm::vec4 color) {
            world.spawn(mesh::Mesh2d{handle},
                        sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{
                            materials.emplace(sprite_render::ColorMaterial{
                                .color = color, .mode = sprite_render::AlphaMode2dOpaque{}})},
                        transform::Transform{.translation = translation});
        };
        place(annulus_handle, {-1.30f, 0.0f, 0.0f}, glm::vec4(0.98f, 0.72f, 0.30f, 1.0f));
        place(rhombus_handle, {-0.62f, 0.0f, 0.0f}, glm::vec4(0.42f, 0.80f, 0.98f, 1.0f));
        place(triangle_handle, {0.0f, 0.0f, 0.0f}, glm::vec4(0.55f, 0.92f, 0.60f, 1.0f));
        place(capsule_handle, {0.60f, 0.0f, 0.0f}, glm::vec4(0.90f, 0.52f, 0.88f, 1.0f));
        place(ring_handle, {1.35f, 0.0f, 0.0f}, glm::vec4(0.72f, 0.86f, 0.98f, 1.0f));
    }
};
}  // namespace

int main() {
    auto app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Bevy 2D Primitives | annulus | rhombus | triangle | capsule";
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
        .add_plugins(AnnulusRhombusTriangleCapsuleExamplePlugin{});

    app.run();
}
