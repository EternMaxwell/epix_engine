#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/render/screenshot.hpp>
#include <epix/sprite.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>

#include <optional>

using namespace epix;

#include "cam_controll.hpp"

namespace {
struct SpriteReextractVisualState {
    std::optional<assets::AssetId<image::Image>> image;
    bool requested = false;
};

void request_sprite_image_reextract(std::optional<ecs::ResMut<SpriteReextractVisualState>> state,
                                    std::optional<ecs::ResMut<render::RenderAssetReextract<image::Image>>> reextract) {
    if (!state || !reextract || state->get().requested || !state->get().image) return;
    reextract->get_mut().request(*state->get().image);
    state->get_mut().requested = true;
}

struct BasicSpriteVisualTestPlugin {
    void attach(app::App& app) {
        app.add_systems(app::Update,
                        ecs::into(request_sprite_image_reextract).set_name("request sprite image render-asset reextract"));
    }

    void ready(app::App& app) {
        auto& world  = app.world_mut();
        auto& images = world.resource_mut<assets::Assets<image::Image>>();

        world.spawn(camera::Camera2d{}, transform::Transform{});

        std::vector<std::uint8_t> texture_data = {
            255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 255, 255,
        };
        auto texture = image::Image::create2d(2, 2, image::Format::RGBA8, texture_data).value();
        auto handle  = images.emplace(std::move(texture));

        // The first normal update explicitly asks the render-asset pipeline to
        // re-extract this unchanged image. The request is intentionally made
        // after plugin initialization so the example exercises the public
        // recovery hook rather than depending on AssetEvent delivery.
        world.insert_resource(SpriteReextractVisualState{.image = handle.id()});

        world.spawn(sprite::SpriteBundle{
            .sprite =
                sprite::Sprite{
                    .size = glm::vec2(160.0f, 160.0f),
                },
            .texture = handle,
        });

        // Component-oriented screenshot example: the request entity is marked
        // Capturing, emits ScreenshotCaptured, then is cleaned up by the
        // plugin. The saved file is the same real swapchain image displayed
        // by this visual example (rather than a synthetic success color).
        world.spawn(render::screenshot::Screenshot::primary_window());
    }
};
}  // namespace

int main() {
    app::App app = app::App::create();

    window::Window primary_window;
    primary_window.title = "Sprite Basic Visual Test";
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
        .add_plugins(camera::CameraPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(assets::AssetPlugin{})
        .add_plugins(image::ImagePlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(render::screenshot::ScreenshotPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(sprite::SpritePlugin{})
        .add_plugins(CamControllPlugin{})
        .add_plugins(BasicSpriteVisualTestPlugin{});

    app.run();
}
