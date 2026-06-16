#include <epix/assets.hpp>
#include <epix/core.hpp>
#include <epix/core_graph.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/render/screenshot.hpp>
#include <epix/sprite.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

using namespace epix;

#include "cam_controll.hpp"

namespace {
struct BasicSpriteVisualTestPlugin {
    void ready(core::App& app) {
        auto& world  = app.world_mut();
        auto& images = world.resource_mut<assets::Assets<image::Image>>();

        world.spawn(core_graph::core_2d::Camera2DBundle{});

        std::vector<std::uint8_t> texture_data = {
            255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 255, 255,
        };
        auto texture = image::Image::create2d(2, 2, image::Format::RGBA8, texture_data).value();
        auto handle  = images.emplace(std::move(texture));

        world.spawn(sprite::SpriteBundle{
            .sprite =
                sprite::Sprite{
                    .size = glm::vec2(160.0f, 160.0f),
                },
            .texture = handle,
        });
    }
};
}  // namespace

int main() {
    core::App app = core::App::create();

    window::Window primary_window;
    primary_window.title = "Sprite Basic Visual Test";
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
        .add_plugins(render::screenshot::ScreenshotPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(sprite::SpritePlugin{})
        .add_plugins(CamControllPlugin{})
        .add_plugins(BasicSpriteVisualTestPlugin{});

    app.run();
}