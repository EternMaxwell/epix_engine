#include <epix/assets.hpp>
#include <epix/core.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/text/font.hpp>

#include "epix/input.hpp"
#include "font_array.hpp"

using namespace epix;

int main() {
    App app = App::create();
    app.add_plugins(epix::window::WindowPlugin{})
        .add_plugins(epix::assets::AssetPlugin{})
        .add_plugins(epix::input::InputPlugin{})
        .add_plugins(epix::glfw::GLFWPlugin{})
        .add_plugins(epix::transform::TransformPlugin{})
        .add_plugins(epix::render::RenderPlugin{})
        .add_plugins(epix::core_graph::CoreGraphPlugin{})
        .add_plugins(epix::sprite::SpritePlugin{})
        .add_plugins(epix::text::font::FontPlugin{});
    app.add_systems(Update, into(input::log_inputs, window::log_events));
    std::optional<assets::Handle<text::font::Font>> font_handle;

    app.add_systems(PreStartup, into([&](ResMut<assets::Assets<text::font::Font>> fonts) {
                                    text::font::Font font{std::make_unique<std::byte[]>(font_data_array_size),
                                                          font_data_array_size};
                                    std::memcpy(font.data.get(), font_data_array, font_data_array_size);
                                    font_handle = fonts->emplace(std::move(font));
                                })
                                    .before(text::font::FontSystems::AddFontAtlasSet)
                                    .before(assets::AssetSystems::WriteEvents));
    // app.add_systems(Startup, into([&](ResMut<text::font::FontAtlasSets> font_atlas_sets) {
    //                     if (!font_handle) return;
    //                     text::font::FontAtlasSet& atlas_set = font_atlas_sets->get_mut(*font_handle).value();
    //                     text::font::FontAtlas& atlas = atlas_set.get_or_insert(text::font::FontAtlasKey{32, true});
    //                     for (auto c : "Hello, Epix Engine!") {
    //                         auto&& loc = atlas.get_char_atlas_loc(static_cast<char32_t>(c));
    //                         spdlog::info("Character '{}' at atlas loc x={}, y={}, layer={}, w={}, h={}", c, loc.x,
    //                                      loc.y, loc.layer, loc.width, loc.height);
    //                     }
    //                 }));

    app.run();
}