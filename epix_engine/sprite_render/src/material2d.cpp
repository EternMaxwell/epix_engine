#include <epix/sprite_render/color_material.hpp>

void epix::sprite_render::ColorMaterialPlugin::attach(epix::app::App& app) {
    // Bevy `ColorMaterialPlugin::build` (color_material.rs:14-32): publish the
    // shader, install the material plugin, and insert the default material.
    register_color_material_shader(app.world_mut());
    app.add_plugins(Material2dPlugin<ColorMaterial>{});
    // Bevy inserts the default material *at the default-asset handle*, which is
    // what makes that handle resolve; Epix names the handle explicitly because its
    // `Handle` has no default constructor (see `DEFAULT_COLOR_MATERIAL_HANDLE`).
    (void)app.world_mut()
        .resource_mut<epix::assets::Assets<ColorMaterial>>()
        .insert(DEFAULT_COLOR_MATERIAL_HANDLE.id(), ColorMaterial{.color = ::epix::color::Color::srgb(1.0f, 0.0f, 1.0f)});
}
