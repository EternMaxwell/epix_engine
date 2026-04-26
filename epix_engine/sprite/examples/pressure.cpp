import std;
import epix.core;
import epix.window;
import epix.glfw.core;
import epix.glfw.render;
import epix.render;
import epix.core_graph;
import epix.transform;
import epix.input;
import epix.image;
import epix.sprite;
import epix.assets;

using namespace epix;

#include "cam_controll.hpp"

namespace {
struct SpritePressureVisualTestPlugin {
    void finish(core::App& app) {
        auto& world  = app.world_mut();
        auto& images = world.resource_mut<assets::Assets<image::Image>>();

        world.spawn(core_graph::core_2d::Camera2DBundle{});

        std::vector<std::uint8_t> texture_data = {
            255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 255, 255,
        };
        auto texture = image::Image::create2d(2, 2, image::Format::RGBA8, texture_data).value();
        auto handle  = images.emplace(std::move(texture));

        std::mt19937 generator(42);
        std::uniform_real_distribution<float> position(-1000.0f, 1000.0f);
        std::uniform_real_distribution<float> tint(0.45f, 1.0f);

        for ([[maybe_unused]] auto i : std::views::iota(0, 5000)) {
            world.spawn(sprite::SpriteBundle{
                .sprite =
                    sprite::Sprite{
                        .color = glm::vec4(tint(generator), tint(generator), tint(generator), 1.0f),
                        .size  = glm::vec2(24.0f, 24.0f),
                    },
                .transform = transform::Transform::from_xyz(position(generator), position(generator), 0.0f),
                .texture   = handle,
            });
        }
    }
};
}  // namespace

int main() {
    core::App app = core::App::create();

    window::Window primary_window;
    primary_window.title = "Sprite Pressure Visual Test";
    primary_window.size  = {1280, 720};

    app.add_plugins(core::TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{
            .primary_window = primary_window,
            .exit_condition = window::ExitCondition::OnPrimaryClosed,
        })
        .add_plugins(input::InputPlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(render::RenderPlugin{}.set_validation(0))
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(sprite::SpritePlugin{})
        .add_plugins(CamControllPlugin{})
        .add_plugins(SpritePressureVisualTestPlugin{});

    app.run();
}