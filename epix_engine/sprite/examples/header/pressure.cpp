#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

using namespace epix;

#include "cam_controll.hpp"

namespace {
struct SpritePressureVisualTestPlugin {
    void ready(app::App& app) {
        auto& world  = app.world_mut();
        auto& images = world.resource_mut<assets::Assets<image::Image>>();

        world.spawn(camera::Camera2d{}, transform::Transform{});

        std::vector<std::uint8_t> texture_data = {
            255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 255, 255,
        };
        auto texture = image::Image::create2d(2, 2, image::Format::RGBA8, texture_data).value();
        auto handle  = images.emplace(std::move(texture));

        std::mt19937 generator(42);
        std::uniform_real_distribution<float> position(-1000.0f, 1000.0f);
        std::uniform_real_distribution<float> tint(0.45f, 1.0f);

        for ([[maybe_unused]] auto i : std::views::iota(0, 5000)) {
            auto sprite        = sprite::Sprite::from_image(handle);
            sprite.color       = glm::vec4(tint(generator), tint(generator), tint(generator), 1.0f);
            sprite.custom_size = glm::vec2(24.0f, 24.0f);
            world.spawn(std::move(sprite),
                        transform::Transform::from_xyz(position(generator), position(generator), 0.0f));
        }
    }
};
}  // namespace

int main() {
    app::App app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Sprite Pressure Visual Test";
    primary_window.size  = {1280, 720};

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
        .add_plugins(sprite_render::SpriteRenderPlugin{})
        .add_plugins(CamControllPlugin{})
        .add_plugins(SpritePressureVisualTestPlugin{});

    app.run();
}
