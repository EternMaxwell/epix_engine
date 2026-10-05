#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/sdl3/core.hpp>
#include <epix/sdl3/render.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
#include <epix/time.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <format>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <print>
#include <string>
#include <utility>

using namespace epix;

namespace {
/** @brief The plane's corners as the builder emitted them, in order. */
std::string corner_list(const mesh::Mesh& value) {
    const auto positions = value.attribute(mesh::Mesh::ATTRIBUTE_POSITION);
    if (!positions) return "(no positions)";
    const auto* corners = positions->get().get_if<mesh::VertexAttributeValues::Float32x3>();
    if (corners == nullptr) return "(positions are not Float32x3)";

    std::string text;
    for (const auto& corner : *corners) {
        if (!text.empty()) text += " ";
        text += std::format("({:.1f},{:.1f},{:.1f})", corner[0], corner[1], corner[2]);
    }
    return text;
}

/** @brief Renders a `Plane3d` built from an antiparallel normal next to the default one.
 *
 *  `Plane3d::default()` faces `+Y`, which takes the plane builder's ordinary rotation
 *  branch. `-Y` is exactly antiparallel, and that branch is what MSH-47 fixed: glam
 *  turns an antiparallel pair about `from.any_orthonormal_vector()`, which for `+Y` is
 *  `-Z`, not the `X` axis a naive port reaches for.
 *
 *  The two meshes are therefore both quads in the `XZ` plane, and each entity rotates
 *  its quad by 90 degrees about `X` so a 2D camera sees it face on. The frame is
 *  diagnostic: with the fix both quads show as filled squares, while the pre-fix
 *  rotation about `X` would leave the antiparallel one lying in `XY` - and so, after
 *  the entity's own 90-degree rotation, edge on and invisible. The corner positions of
 *  both meshes are printed as well, so the log states the geometry the frame shows. */
struct PlaneAntiparallelExamplePlugin {
    void ready(app::App& app) {
        auto& world       = app.world_mut();
        auto& asset_store = world.resource_mut<assets::Assets<mesh::Mesh>>();
        auto& materials   = world.resource_mut<assets::Assets<sprite_render::ColorMaterial>>();

        const mesh::Plane3d default_plane{};
        const mesh::Plane3d antiparallel_plane{glm::vec3{0.0f, -1.0f, 0.0f}, glm::vec2{0.5f}};

        mesh::Mesh default_mesh      = default_plane;
        mesh::Mesh antiparallel_mesh = antiparallel_plane;

        std::println("plane +Y corners: {}", corner_list(default_mesh));
        std::println("plane -Y corners: {}", corner_list(antiparallel_mesh));

        const auto default_handle      = asset_store.emplace(std::move(default_mesh));
        const auto antiparallel_handle = asset_store.emplace(std::move(antiparallel_mesh));

        camera::OrthographicProjection orthographic{};
        orthographic.scaling_mode = camera::ScalingMode{camera::ScalingMode::FixedVertical{.viewport_height = 2.0f}};
        world.spawn(camera::Camera2d{}, transform::Transform{}, camera::Projection::orthographic(orthographic));

        // Turns a quad lying in the XZ plane to face a camera looking down -Z.
        const glm::quat face_the_camera = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        const auto place = [&world, &materials, face_the_camera](const assets::Handle<mesh::Mesh>& handle,
                                                                 glm::vec3 translation, glm::vec4 color) {
            world.spawn(mesh::Mesh2d{handle},
                        sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{materials.emplace(
                            sprite_render::ColorMaterial{
                                .color = ::epix::color::Color::linear_rgba(color.r, color.g, color.b, color.a),
                                .mode  = sprite_render::AlphaMode2dOpaque{}})},
                        transform::Transform{.translation = translation, .rotation = face_the_camera});
        };
        place(default_handle, {-0.55f, 0.0f, 0.0f}, glm::vec4(0.55f, 0.92f, 0.60f, 1.0f));
        place(antiparallel_handle, {0.55f, 0.0f, 0.0f}, glm::vec4(0.98f, 0.72f, 0.30f, 1.0f));
    }
};
}  // namespace

int main() {
    auto app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Mesh Plane3d | +Y (default) and antiparallel -Y | MSH-47";
    primary_window.size  = {900, 420};

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
        .add_plugins(PlaneAntiparallelExamplePlugin{});

    app.run();
}
