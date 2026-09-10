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

struct AnnulusRhombusTriangleCapsuleExamplePlugin {
    void ready(app::App& app) {
        auto& world       = app.world_mut();
        auto& mesh_assets = world.resource_mut<assets::Assets<mesh::Mesh>>();

        // Every shape uses its Bevy-facing primitive default plus an explicit resolution, so the
        // generated vertex counts and attribute sets are the ones Bevy would produce.
        mesh::Mesh annulus  = mesh::Annulus{0.35f, 0.7f};
        mesh::Mesh rhombus  = mesh::Rhombus{1.0f, 1.4f};
        mesh::Mesh triangle = mesh::Triangle2d{{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.0f, 0.6f}};
        mesh::Mesh capsule  = mesh::Capsule2d{0.3f, 0.6f};

        std::println("Annulus/Rhombus/Triangle2d/Capsule2d: {} {} {} {} vertices", annulus.count_vertices(),
                     rhombus.count_vertices(), triangle.count_vertices(), capsule.count_vertices());
        std::println("Bevy-aligned attributes: annulus=[{}] rhombus=[{}] triangle=[{}] capsule=[{}]",
                     attribute_names(annulus), attribute_names(rhombus), attribute_names(triangle),
                     attribute_names(capsule));

        const auto annulus_handle  = mesh_assets.emplace(std::move(annulus));
        const auto rhombus_handle  = mesh_assets.emplace(std::move(rhombus));
        const auto triangle_handle = mesh_assets.emplace(std::move(triangle));
        const auto capsule_handle  = mesh_assets.emplace(std::move(capsule));

        // The Bevy primitives have unit-scale dimensions, so the camera fixes a 5-unit tall
        // viewport instead of the default 1-world-unit-per-pixel window projection.
        camera::OrthographicProjection orthographic{};
        orthographic.scaling_mode = camera::ScalingMode{camera::ScalingMode::FixedVertical{.viewport_height = 2.0f}};
        world.spawn(camera::Camera2d{}, transform::Transform{}, camera::Projection::orthographic(orthographic));

        const auto place = [&world](const assets::Handle<mesh::Mesh>& handle, glm::vec3 translation, glm::vec4 color) {
            world.spawn(mesh::Mesh2d{handle}, sprite_render::MeshMaterial2d{.color = color},
                        transform::Transform{.translation = translation});
        };
        place(annulus_handle, {-1.45f, 0.0f, 0.0f}, glm::vec4(0.98f, 0.72f, 0.30f, 1.0f));
        place(rhombus_handle, {-0.47f, 0.0f, 0.0f}, glm::vec4(0.42f, 0.80f, 0.98f, 1.0f));
        place(triangle_handle, {0.47f, 0.0f, 0.0f}, glm::vec4(0.55f, 0.92f, 0.60f, 1.0f));
        place(capsule_handle, {1.45f, 0.0f, 0.0f}, glm::vec4(0.90f, 0.52f, 0.88f, 1.0f));
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
        .add_plugins(AnnulusRhombusTriangleCapsuleExamplePlugin{});

    app.run();
}
