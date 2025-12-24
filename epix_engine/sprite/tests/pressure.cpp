#include <epix/core.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>

#include "cam_controll.hpp"

epix::assets::Handle<epix::image::Image> image_handle =
    epix::assets::AssetId<epix::image::Image>(uuids::uuid::from_string("c6637d5a-1fbe-4778-87cd-ea5820986a9e").value());

int main() {
    using namespace epix;
    App app = App::create();

    app.add_plugins(epix::window::WindowPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::input::InputPlugin{})
        .add_plugins(epix::glfw::GLFWPlugin{})
        .add_plugins(epix::transform::TransformPlugin{})
        .add_plugins(epix::render::RenderPlugin{})
        .add_plugins(epix::core_graph::CoreGraphPlugin{})
        .add_plugins(epix::sprite::SpritePlugin{})
        .add_plugins(CamControllPlugin{});
    app.world_mut().spawn(epix::render::core_2d::Camera2DBundle{});
    app.add_systems(Startup, into([](Commands cmd, ResMut<assets::Assets<image::Image>> assets) {
                        spdlog::info("Adding sprite");
                        auto image = image::Image::srgba8unorm_render(2, 2);
                        auto res   = image.set_data(image::Rect::rect2d(0, 0, 2, 2),
                                                    std::span<const uint8_t>(std::vector<uint8_t>{
                                                      0xff, 0x00, 0xff, 0xff,  // purple
                                                      0x00, 0x00, 0x00, 0xff,  // black
                                                      0x00, 0x00, 0x00, 0xff,  // black
                                                      0xff, 0x00, 0xff, 0xff   // purple
                                                  }));
                        image.flip_vertical();
                        auto res2 = assets->insert(image_handle, std::move(image));
                        static thread_local std::random_device rd;
                        static thread_local std::mt19937 gen(rd());
                        static thread_local std::uniform_real_distribution<float> dis(-100.0f, 100.0f);
                        for (auto&& i : std::views::iota(0, 5000)) {
                            cmd.spawn(sprite::SpriteBundle{
                                .transform = transform::Transform::from_xyz(dis(gen), dis(gen), 0.0f),
                                .texture   = image_handle,
                            });
                        }
                    }));

    app.run();
}