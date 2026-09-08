#ifndef EPIX_IMPORT_STD
#include <array>
#include <cstdint>
#include <vector>
#endif

#include <spdlog/spdlog.h>

#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.app;
import epix.assets;
import epix.camera;
import epix.core_graph;
import epix.glfw.core;
import epix.glfw.render;
import epix.image;
import epix.input;
import epix.mesh;
import epix.render;
import epix.sprite;
import epix.sprite_render;
import epix.transform;
import epix.window;
import glm;

using namespace epix;

namespace {
struct TextureAtlasVisualTestPlugin {
    void ready(app::App& app) {
        auto& world   = app.world_mut();
        auto& images  = world.resource_mut<assets::Assets<image::Image>>();
        auto& layouts = world.resource_mut<assets::Assets<image::TextureAtlasLayout>>();

        world.spawn(camera::Camera2d{}, transform::Transform{});

        constexpr std::array<std::array<std::uint8_t, 4>, 4> colors{{
            {240, 65, 65, 255},
            {55, 210, 95, 255},
            {55, 115, 245, 255},
            {245, 205, 45, 255},
        }};
        std::vector<std::uint8_t> pixels(8u * 8u * 4u);
        for (std::uint32_t y = 0; y < 8; ++y) {
            for (std::uint32_t x = 0; x < 8; ++x) {
                const auto region = (y / 4u) * 2u + (x / 4u);
                const auto offset = (y * 8u + x) * 4u;
                for (std::size_t channel = 0; channel < 4; ++channel) {
                    pixels[offset + channel] = colors[region][channel];
                }
            }
        }

        const auto atlas_image = images.emplace(
            image::Image::create2d(8, 8, image::Format::RGBA8, pixels).value());
        const auto layout = layouts.emplace(
            image::TextureAtlasLayout::from_grid(glm::uvec2(4), 2, 2));
        constexpr std::array positions{
            glm::vec2(-150.0f, 120.0f),
            glm::vec2(150.0f, 120.0f),
            glm::vec2(-150.0f, -120.0f),
            glm::vec2(150.0f, -120.0f),
        };

        for (std::size_t index = 0; index < positions.size(); ++index) {
            auto sprite = sprite::Sprite::from_atlas_image(
                atlas_image, image::TextureAtlas{layout, index});
            sprite.custom_size = glm::vec2(220.0f, 160.0f);
            world.spawn(std::move(sprite),
                        transform::Transform::from_xyz(positions[index].x, positions[index].y, 0.0f));
        }

        spdlog::info("Texture atlas regions: top-left red, top-right green, bottom-left blue, bottom-right yellow");
    }
};
}  // namespace

int main() {
    window::Window primary_window;
    primary_window.title = "Texture Atlas - TL red | TR green | BL blue | BR yellow";
    primary_window.size  = {960, 640};

    auto app = app::App::create();
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
        .add_plugins(image::ImagePlugin::default_nearest())
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(mesh::MeshPlugin{})
        .add_plugins(sprite::SpritePlugin{})
        .add_plugins(sprite_render::SpriteRenderPlugin{})
        .add_plugins(TextureAtlasVisualTestPlugin{});
    app.run();
}
