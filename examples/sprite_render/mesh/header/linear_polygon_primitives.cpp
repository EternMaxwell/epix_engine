#include <epix/app.hpp>
#include <epix/assets.hpp>
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
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace epix;

namespace {
/** @brief Report the vertex attributes a generated mesh actually carries. */
std::string attribute_names(const mesh::Mesh& value) {
    std::string names;
    for (const auto& [attribute, values] : value.attributes()) {
        if (!names.empty()) names += ", ";
        names += std::string(attribute.name);
    }
    return std::format("{} ({} vertices)", names, value.count_vertices());
}

struct LinearPolygonPrimitivesExamplePlugin {
    void ready(app::App& app) {
        auto& world       = app.world_mut();
        auto& mesh_assets = world.resource_mut<assets::Assets<mesh::Mesh>>();
        auto& materials   = world.resource_mut<assets::Assets<sprite_render::ColorMaterial>>();

        mesh::Mesh segment  = mesh::Segment2d{{-105.0f, -75.0f}, {105.0f, 75.0f}};
        mesh::Mesh polyline = mesh::Polyline2d{{
            {-110.0f, -75.0f},
            {-55.0f, 75.0f},
            {0.0f, -40.0f},
            {55.0f, 80.0f},
            {110.0f, -60.0f},
        }};
        auto polygon_shape  = mesh::ConvexPolygon::create({
            {0.0f, 110.0f},
            {-105.0f, 35.0f},
            {-65.0f, -90.0f},
            {65.0f, -90.0f},
            {105.0f, 35.0f},
        });
        if (!polygon_shape) throw std::runtime_error(std::string(mesh::to_string(polygon_shape.error())));
        mesh::Mesh polygon = *polygon_shape;

        std::println("Linear/polygon primitives: segment={} polyline={} polygon={} vertices", segment.count_vertices(),
                     polyline.count_vertices(), polygon.count_vertices());
        // Bevy's line-list and ConvexPolygon builders insert Position only; the log states the
        // real attribute set so the capture can be checked against that contract.
        std::println("Bevy-aligned attributes: segment=[{}] polygon=[{}] polyline=[{}]", attribute_names(segment),
                     attribute_names(polygon), attribute_names(polyline));

        const auto segment_handle  = mesh_assets.emplace(std::move(segment));
        const auto polygon_handle  = mesh_assets.emplace(std::move(polygon));
        const auto polyline_handle = mesh_assets.emplace(std::move(polyline));

        world.spawn(camera::Camera2d{}, transform::Transform{});
        world.spawn(mesh::Mesh2d{segment_handle},
                    sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{
                        materials.emplace(sprite_render::ColorMaterial{
                            .color = glm::vec4(0.98f, 0.52f, 0.34f, 1.0f),
                            .mode = sprite_render::AlphaMode2dOpaque{}})},
                    transform::Transform{.translation = {-320.0f, 0.0f, 0.0f}});
        world.spawn(mesh::Mesh2d{polygon_handle},
                    sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{
                        materials.emplace(sprite_render::ColorMaterial{
                            .color = glm::vec4(0.39f, 0.84f, 0.62f, 1.0f),
                            .mode = sprite_render::AlphaMode2dOpaque{}})},
                    transform::Transform{.translation = {0.0f, 0.0f, 0.0f}});
        world.spawn(mesh::Mesh2d{polyline_handle},
                    sprite_render::MeshMaterial2d<sprite_render::ColorMaterial>{
                        materials.emplace(sprite_render::ColorMaterial{
                            .color = glm::vec4(0.40f, 0.67f, 1.0f, 1.0f),
                            .mode = sprite_render::AlphaMode2dOpaque{}})},
                    transform::Transform{.translation = {320.0f, 0.0f, 0.0f}});
    }
};
}  // namespace

int main() {
    auto app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Bevy 2D Primitives | segment | convex polygon | polyline";
    primary_window.size  = {1000, 550};

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
        .add_plugins(LinearPolygonPrimitivesExamplePlugin{});

    app.run();
}
