#ifndef EPIX_IMPORT_STD
#include <algorithm>
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
assets::Handle<image::Image> make_scaling_image(assets::Assets<image::Image>& images) {
    std::vector<std::uint8_t> pixels(96u * 48u * 4u);
    constexpr std::array colors{
        std::array<std::uint8_t, 4>{235, 65, 85, 255},
        std::array<std::uint8_t, 4>{245, 205, 55, 255},
        std::array<std::uint8_t, 4>{50, 125, 240, 255},
    };
    for (std::uint32_t y = 0; y < 48; ++y) {
        for (std::uint32_t x = 0; x < 96; ++x) {
            const auto& color = colors[x / 32u];
            const auto offset = (y * 96u + x) * 4u;
            std::copy(color.begin(), color.end(), pixels.begin() + offset);
        }
    }
    return images.emplace(image::Image::create2d(96, 48, image::Format::RGBA8, pixels).value());
}

assets::Handle<image::Image> make_slice_image(assets::Assets<image::Image>& images) {
    std::vector<std::uint8_t> pixels(32u * 32u * 4u);
    for (std::uint32_t y = 0; y < 32; ++y) {
        for (std::uint32_t x = 0; x < 32; ++x) {
            const bool border = x < 8 || x >= 24 || y < 8 || y >= 24;
            const std::array<std::uint8_t, 4> color = border
                ? std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(70 + x * 5),
                                              static_cast<std::uint8_t>(220 - y * 4), 235, 255}
                : std::array<std::uint8_t, 4>{45, 35, 75, 255};
            const auto offset = (y * 32u + x) * 4u;
            std::copy(color.begin(), color.end(), pixels.begin() + offset);
        }
    }
    return images.emplace(image::Image::create2d(32, 32, image::Format::RGBA8, pixels).value());
}

struct TextureSliceVisualPlugin {
    void ready(app::App& app) {
        auto& world  = app.world_mut();
        auto& images = world.resource_mut<assets::Assets<image::Image>>();
        world.spawn(camera::Camera2d{}, transform::Transform{});

        const auto scaling_image = make_scaling_image(images);
        const auto slice_image   = make_slice_image(images);
        auto backing_pixels = std::array<std::uint8_t, 4>{28, 31, 42, 255};
        const auto backing = images.emplace(
            image::Image::create2d(1, 1, image::Format::RGBA8, backing_pixels).value());

        constexpr std::array modes{
            sprite::SpriteScalingMode::FillCenter, sprite::SpriteScalingMode::FillStart,
            sprite::SpriteScalingMode::FillEnd, sprite::SpriteScalingMode::FitCenter,
            sprite::SpriteScalingMode::FitStart, sprite::SpriteScalingMode::FitEnd,
        };
        constexpr std::array x_positions{-475.0f, -285.0f, -95.0f, 95.0f, 285.0f, 475.0f};
        for (std::size_t i = 0; i < modes.size(); ++i) {
            auto panel = sprite::Sprite::from_image(backing);
            panel.custom_size = glm::vec2(170.0f, 130.0f);
            world.spawn(std::move(panel), transform::Transform::from_xyz(x_positions[i], 190.0f, -0.1f));

            auto scaled = sprite::Sprite::from_image(scaling_image);
            scaled.custom_size = glm::vec2(160.0f, 120.0f);
            scaled.image_mode  = sprite::SpriteImageMode::Scale{modes[i]};
            world.spawn(std::move(scaled), transform::Transform::from_xyz(x_positions[i], 190.0f, 0.0f));
        }

        auto sliced = sprite::Sprite::from_image(slice_image);
        sliced.custom_size = glm::vec2(480.0f, 230.0f);
        sliced.image_mode = sprite::SpriteImageMode::Sliced{sprite::TextureSlicer{
            .border = sprite::BorderRect::all(8.0f),
            .center_scale_mode = sprite::SliceScaleMode::Stretch{},
            .sides_scale_mode = sprite::SliceScaleMode::Tile{1.0f},
        }};
        world.spawn(std::move(sliced), transform::Transform::from_xyz(-280.0f, -120.0f, 0.0f));

        auto tiled = sprite::Sprite::from_image(slice_image);
        tiled.custom_size = glm::vec2(480.0f, 230.0f);
        tiled.image_mode = sprite::SpriteImageMode::Tiled{
            .tile_x = true, .tile_y = true, .stretch_value = 1.0f};
        world.spawn(std::move(tiled), transform::Transform::from_xyz(280.0f, -120.0f, 0.0f));

        spdlog::info("Top left-to-right: FillCenter, FillStart, FillEnd, FitCenter, FitStart, FitEnd");
        spdlog::info("Bottom: nine-slice with tiled sides (left), fully tiled sprite (right)");
    }
};
}  // namespace

int main() {
    window::Window primary_window;
    primary_window.title = "Sprite scaling: Fill C/S/E | Fit C/S/E; bottom: sliced | tiled";
    primary_window.size  = {1200, 720};

    auto app = app::App::create();
    app.add_plugins(app::TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{.primary_window = primary_window,
                                          .exit_condition = window::ExitCondition::OnPrimaryClosed})
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
        .add_plugins(TextureSliceVisualPlugin{});
    app.run();
}
