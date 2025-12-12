#include <epix/assets.hpp>
#include <epix/core.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/input.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/text.hpp>
#include <string_view>

#include "epix/mesh/render.hpp"
#include "epix/text/render.hpp"
#include "epix/text/text.hpp"
#include "epix/transform.hpp"
#include "epix/window.hpp"
#include "font_array.hpp"

using namespace epix;

auto run_once = [run = false]() mutable {
    if (!run) {
        run = true;
        return true;
    }
    return false;
};

struct CamControllPlugin {
    void build(epix::App& app) {
        app.add_systems(
            epix::Update,
            epix::into([](epix::Query<epix::Item<const epix::render::camera::Camera&, epix::render::camera::Projection&,
                                                 epix::transform::Transform&>> camera,
                          epix::EventReader<epix::input::MouseScroll> scroll_input,
                          epix::Res<epix::input::ButtonInput<epix::input::KeyCode>> key_states) {
                if (auto opt = camera.single(); opt.has_value()) {
                    auto&& [cam, proj, trans] = *opt;
                    if (key_states->pressed(epix::input::KeyCode::KeySpace)) {
                        trans.translation = glm::vec3(0, 0, 0);
                        proj.as_orthographic().transform([&](epix::render::camera::OrthographicProjection* ortho) {
                            *ortho = epix::render::camera::OrthographicProjection{};
                            return true;
                        });
                        return;
                    }
                    glm::vec3 delta(0.0f);
                    if (key_states->pressed(epix::input::KeyCode::KeyW)) {
                        delta += glm::vec3(0, 0.1f, 0);
                    }
                    if (key_states->pressed(epix::input::KeyCode::KeyS)) {
                        delta -= glm::vec3(0, 0.1f, 0);
                    }
                    if (key_states->pressed(epix::input::KeyCode::KeyA)) {
                        delta -= glm::vec3(0.1f, 0, 0);
                    }
                    if (key_states->pressed(epix::input::KeyCode::KeyD)) {
                        delta += glm::vec3(0.1f, 0, 0);
                    }
                    if (glm::length(delta) > 0.0f) {
                        delta = glm::normalize(delta) * 0.1f;
                        trans.translation += delta;
                    }
                    proj.as_orthographic().transform([&](epix::render::camera::OrthographicProjection* ortho) {
                        for (const auto& e : scroll_input.read()) {
                            float scale = std::exp(-static_cast<float>(e.yoffset) * 0.1f);
                            ortho->scale *= scale;
                        }
                        // Key space reset
                        return true;
                    });
                }
            }).set_name("camera control"));
    }
};

int main(int argc, char** argv) {
    int render_validation = 0;

    if (argc > 1) {
        std::string_view arg1 = argv[1];
        if (arg1 == "--render-validation=1") {
            render_validation = 1;
        } else if (arg1 == "--render-validation=2") {
            render_validation = 2;
        }
    }

    App app = App::create();
    app.add_plugins(epix::window::WindowPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::input::InputPlugin{})
        .add_plugins(epix::glfw::GLFWPlugin{})
        .add_plugins(epix::transform::TransformPlugin{})
        .add_plugins(CamControllPlugin{})
        .add_plugins(epix::render::RenderPlugin{}.set_validation(render_validation))
        .add_plugins(epix::mesh::MeshPlugin{})
        .add_plugins(epix::core_graph::CoreGraphPlugin{})
        .add_plugins(epix::mesh::MeshRenderPlugin{})
        .add_plugins(epix::sprite::SpritePlugin{})
        .add_plugins(epix::text::TextPlugin{})
        .add_plugins(text::TextRenderPlugin{});
    app.add_systems(Update, into(input::log_inputs, window::log_events));
    app.world_mut().spawn(render::core_2d::Camera2DBundle{});

    std::optional<assets::Handle<text::font::Font>> font_handle;

    app.add_systems(PreStartup, into([&](Commands cmd, ResMut<assets::Assets<text::font::Font>> fonts) {
                                    text::font::Font font{std::make_unique<std::byte[]>(font_data_array_size),
                                                          font_data_array_size};
                                    std::memcpy(font.data.get(), font_data_array, font_data_array_size);
                                    font_handle = fonts->emplace(std::move(font));
                                    cmd.spawn(text::TextBundle{.text{"Hello, Epix Engine!"},
                                                               .font{
                                                                   .font            = *font_handle,
                                                                   .size            = 48.0f,
                                                                   .line_height     = 48.0f,
                                                                   .relative_height = false,
                                                               },
                                                               .layout{
                                                                   .justify = text::Justify::Center,
                                                               }},
                                              text::Text2d{}, transform::Transform{}, text::TextColor{});
                                })
                                    .before(text::font::FontSystems::AddFontAtlasSet)
                                    .before(assets::AssetSystems::WriteEvents));
    app.add_systems(
        Update, into([](EventReader<window::WindowResized> resize_events, Query<Mut<text::TextBounds>> text_bounds) {
            for (auto&& e : resize_events.read()) {
                for (auto&& tb : text_bounds.iter()) {
                    tb.get_mut().width = static_cast<float>(e.width) - 50.0f;
                }
            }
        }));
    // app.add_systems(Update,
    //                 into([&](ResMut<text::font::FontAtlasSets> font_atlas_sets) {
    //                     if (!font_handle) return;
    //                     text::font::FontAtlasSet& atlas_set = font_atlas_sets->get_mut(*font_handle).value();
    //                     text::font::FontAtlas& atlas = atlas_set.get_or_insert(text::font::FontAtlasKey{32, false});
    //                     for (auto c : std::string_view("Hello, Epix Engine!")) {
    //                         auto&& loc = atlas.get_glyph_atlas_loc(atlas.get_glyph_index(static_cast<char32_t>(c)));
    //                         spdlog::info("Character '{}' at atlas loc x={}, y={}, layer={}, w={}, h={}", c, loc.x,
    //                                      loc.y, loc.layer, loc.width, loc.height);
    //                     }
    //                 }).run_if(run_once));
    // app.add_systems(Update, into([](EventReader<assets::AssetEvent<image::Image>> image_events, Commands cmd,
    //                                 ResMut<assets::Assets<image::Image>> images) {
    //                     for (auto&& id : image_events.read() | std::views::filter([](auto&& event) {
    //                                          return event.is_added() || event.is_modified();
    //                                      }) | std::views::transform([](auto&& event) { return event.id; }) |
    //                                          std::ranges::to<std::unordered_set>()) {
    //                         spdlog::info("Image asset {} added or modified.", id.to_string_short());
    //                         cmd.spawn(sprite::SpriteBundle{
    //                             .sprite{.size = glm::vec2{500.0f, 500.0f}},
    //                             .texture = images->get_strong_handle(id).value(),
    //                         });
    //                     }
    //                 }));

    app.run();
}