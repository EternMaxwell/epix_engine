#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/core_graph.hpp>
#include <epix/ecs.hpp>
#include <epix/glfw/core.hpp>
#include <epix/glfw/render.hpp>
#include <epix/input.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/text.hpp>
#include <epix/transform.hpp>
#include <epix/window.hpp>
#include <glm/glm.hpp>

using namespace epix;

auto run_once = [run = false]() mutable {
    if (!run) {
        run = true;
        return true;
    }
    return false;
};

struct CamControllPlugin {
    void attach(app::App& app) {
        app.add_systems(
            app::Update,
            ecs::into([](ecs::Query<ecs::Item<const camera::Camera&, camera::Projection&, transform::Transform&>>
                             camera,
                         ecs::EventReader<input::MouseScroll> scroll_input,
                         ecs::Res<input::ButtonInput<input::KeyCode>> key_states) {
                if (auto opt = camera.single(); opt.has_value()) {
                    auto&& [cam, proj, trans] = *opt;
                    if (key_states->pressed(input::KeyCode::KeySpace)) {
                        trans.translation = glm::vec3(0, 0, 0);
                        proj.as_orthographic().transform([&](camera::OrthographicProjection* ortho) {
                            *ortho = camera::OrthographicProjection{};
                            return true;
                        });
                        return;
                    }
                    glm::vec3 delta(0.0f);
                    if (key_states->pressed(input::KeyCode::KeyW)) {
                        delta += glm::vec3(0, 0.1f, 0);
                    }
                    if (key_states->pressed(input::KeyCode::KeyS)) {
                        delta -= glm::vec3(0, 0.1f, 0);
                    }
                    if (key_states->pressed(input::KeyCode::KeyA)) {
                        delta -= glm::vec3(0.1f, 0, 0);
                    }
                    if (key_states->pressed(input::KeyCode::KeyD)) {
                        delta += glm::vec3(0.1f, 0, 0);
                    }
                    if (glm::length(delta) > 0.0f) {
                        delta = glm::normalize(delta) * 0.1f;
                        trans.translation += delta;
                    }
                    proj.as_orthographic().transform([&](camera::OrthographicProjection* ortho) {
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

int main() {
    app::App app = app::App::create();
    app.add_plugins(app::TaskPoolPlugin{})
        .add_plugins(window::WindowPlugin{})
        .add_plugins(input::InputPlugin{})
        .add_plugins(time::TimePlugin{})
        .add_plugins(glfw::GLFWPlugin{})
        .add_plugins(glfw::GLFWRenderPlugin{})
        .add_plugins(transform::TransformPlugin{})
        .add_plugins(CamControllPlugin{})
        .add_plugins(render::FrameCountPlugin{})
        .add_plugins(render::RenderPlugin{})
        .add_plugins(core_graph::CoreGraphPlugin{})
        .add_plugins(mesh::MeshRenderPlugin{})
        .add_plugins(sprite::SpritePlugin{})
        .add_plugins(text::TextPlugin{})
        .add_plugins(text::TextRenderPlugin{});
    app.add_systems(app::Update, ecs::into(input::log_inputs, window::log_events));
    app.world_mut().spawn(camera::Camera2d{}, transform::Transform{});

    std::optional<assets::Handle<text::font::Font>> font_handle;

    app.add_systems(
        app::PreStartup,
        ecs::into([&](ecs::Commands cmd, ecs::Res<assets::AssetServer> asset_server) {
            font_handle = asset_server->load<text::font::Font>("embedded://fonts/default.ttf");
            cmd.spawn(text::TextBundle{.text{"Hello, Epix Engine!"},
                                       .font{
                                           .font            = *font_handle,
                                           .size            = 48.0f,
                                           .line_height     = 48.0f,
                                           .relative_height = false,
                                       },
                                       .layout{.justify = text::Justify::Center}},
                      text::Text2d{},
                      transform::Transform{
                          .translation = glm::vec3(0.0f, 400.0f, 0.0f),
                      },
                      text::TextColor{});
            cmd.spawn(text::TextBundle{.text{"Hhagio4ejhioawjgoijhewaiopgjoeipwajoi930y2598016758904321"},
                                       .font{
                                           .font            = *font_handle,
                                           .size            = 48.0f,
                                           .line_height     = 48.0f,
                                           .relative_height = false,
                                       },
                                       .layout{
                                           .justify   = text::Justify::Center,
                                           .wrap_mode = text::TextWrap::CharWrap,
                                       }},
                      text::Text2d{},
                      transform::Transform{
                          .translation = glm::vec3(0.0f, 200.0f, 0.0f),
                      },
                      text::TextColor{});
            cmd.spawn(text::TextBundle{.text{"Hhagio4ejhioawjgoijhe waiopgj oeipw ajoi930y2 598016 75890 4321"},
                                       .font{
                                           .font            = *font_handle,
                                           .size            = 48.0f,
                                           .line_height     = 48.0f,
                                           .relative_height = false,
                                       },
                                       .layout{
                                           .justify   = text::Justify::Center,
                                           .wrap_mode = text::TextWrap::WordOrCharWrap,
                                       }},
                      text::Text2d{},
                      transform::Transform{
                          .translation = glm::vec3(0.0f, 0.0f, 0.0f),
                      },
                      text::TextColor{});
            cmd.spawn(text::TextBundle{.text{"Hhagio4ejhioawjgoijhe waiopgj oeipw ajoi930y2 598016 75890 4321"},
                                       .font{
                                           .font            = *font_handle,
                                           .size            = 48.0f,
                                           .line_height     = 48.0f,
                                           .relative_height = false,
                                       },
                                       .layout{
                                           .justify   = text::Justify::Center,
                                           .wrap_mode = text::TextWrap::NoWrap,
                                       }},
                      text::Text2d{},
                      transform::Transform{
                          .translation = glm::vec3(0.0f, -200.0f, 0.0f),
                      },
                      text::TextColor{});
        })
            .before(text::font::FontSystems::AddFontAtlasSet)
            .before(assets::AssetSystems::WriteEvents));
    app.add_systems(app::Update, ecs::into([](ecs::EventReader<window::WindowResized> resize_events,
                                              ecs::Query<ecs::Mut<text::TextBounds>> text_bounds) {
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
