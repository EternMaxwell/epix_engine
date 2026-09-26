#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/camera.hpp>
#include <epix/core_graph.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
#include <epix/text.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <utility>
#include <vector>

using namespace epix::ecs;
using namespace epix::app;
using namespace epix::text;
using namespace epix;

void Text2d::on_add(ecs::World& world, ecs::HookContext context) {
    // Bevy: `#[component(on_add = visibility::add_visibility_class::<Sprite>)]`
    // (`bevy_sprite/src/text2d.rs:95`). A `Text2d` entity is collected under
    // the `Sprite` visibility class, which is how the sprite queue finds its
    // glyph sprites without a `Sprite` component.
    camera::add_visibility_class<sprite::Sprite>(world, context);
}

void Text2d::register_required_components(ecs::RequiredComponentsRegistrator& registrator) {
    registrator.register_required<transform::Transform>([] { return transform::Transform{}; });
    // Bevy's `Text2d` requires the three text components as well
    // (`bevy_sprite/src/text2d.rs:82-94`): without `TextFont` a bare `Text2d` never
    // matches `shape_changed_text`'s `Ref<TextFont>` query, and without
    // `TextLayout`/`TextBounds` the shaping and bounds systems have nothing to read.
    //
    // Bevy's `#[require(TextFont)]` supplies `TextFont::default()`, whose font is
    // `Handle::<Font>::default()` - the default-font asset its `TextPlugin` loads.
    // Epix has no default-font convention (`DefaultFontHandle` in `font.cpp` is
    // declared but never filled, and `Handle` has no default constructor), so the
    // required font is an invalid handle: the component and its query parity are
    // here, and text renders once a real font is set, exactly as before this change.
    registrator.register_required<TextFont>(
        [] { return TextFont{assets::Handle<Font>{assets::AssetId<Font>::invalid()}}; });
    registrator.register_required<TextLayout>([] { return TextLayout{}; });
    registrator.register_required<TextBounds>([] { return TextBounds{}; });
    registrator.register_required<TextColor>([] { return TextColor{}; });
    registrator.register_required<camera::Visibility>([] { return camera::Visibility{}; });
    registrator.register_required<sprite::Anchor>([] { return sprite::Anchor{}; });
    // Bevy's `Text2d` requires `VisibilityClass` for its `on_add` hook to have
    // something to append to (`bevy_sprite/src/text2d.rs:82-90`).
    registrator.register_required<camera::VisibilityClass>([] { return camera::VisibilityClass{}; });
}

namespace {
/** @brief Extract 2D text into the sprite render path (Bevy
 * `extract_text2d_sprite`, `bevy_sprite_render/src/text2d/mod.rs:25-344`).
 *
 * Each glyph becomes one `ExtractedSlice` whose offset is the glyph quad's
 * center in world space, and every run of consecutive glyphs sharing an atlas
 * texture becomes one `ExtractedSprite` with `ExtractedSpriteKind::Slices`.
 * The sprite queue, batching and draw commands are Bevy's own, so text is
 * rendered by `SpritePipeline` exactly as `Sprite` is. */
void extract_text2d_sprite(Commands cmd,
                           ResMut<sprite_render::ExtractedSprites> extracted_sprites,
                           ResMut<sprite_render::ExtractedSlices> extracted_slices,
                           Extract<Res<assets::Assets<image::TextureAtlasLayout>>> texture_atlases,
                           Extract<Query<Item<Entity,
                                              const camera::ViewVisibility&,
                                              const ShapedText&,
                                              const TextBounds&,
                                              const sprite::Anchor&,
                                              Opt<const TextColor&>,
                                              const transform::GlobalTransform&>,
                                         Filter<With<Text2d>>>> texts) {
    for (auto&& [main_entity, view_visibility, layout_info, text_bounds, anchor, text_color, global_transform] :
         texts.iter()) {
        // Bevy gates 2D text extraction on `ViewVisibility` (`text2d/mod.rs:71-73`).
        if (!view_visibility.get()) continue;

        glm::vec4 color{1.0f};
        if (text_color) {
            auto&& value = text_color->get();
            color        = glm::vec4(value.r, value.g, value.b, value.a);
        }

        detail::extract_text2d_layout(layout_info, main_entity, text_bounds, anchor, color, global_transform,
                                      *texture_atlases, extracted_sprites->sprites, extracted_slices->slices,
                                      [&cmd] { return cmd.spawn(render::sync_world::TemporaryRenderEntity{}).id(); });
    }
}
}  // namespace

/** @brief Compute and insert the `Aabb` of a 2D text entity (Bevy
 * `calculate_bounds_text2d`, `bevy_sprite/src/text2d.rs:316-347`; declared in the
 * header because Bevy's own function is public). */
void text::calculate_bounds_text2d(
    Commands commands,
    Query<Item<Entity, const ShapedText&, const sprite::Anchor&, const TextBounds&>,
          Filter<Without<camera::Aabb>, Without<camera::NoFrustumCulling>>> new_text_aabb,
    Query<Item<const ShapedText&, const sprite::Anchor&, const TextBounds&, Mut<camera::Aabb>>,
          Filter<Modified<ShapedText>, Without<camera::NoFrustumCulling>>> update_text_aabb) {
    auto text_aabb = [](const ShapedText& layout_info, const sprite::Anchor& anchor, const TextBounds& text_bounds) {
        const glm::vec2 size{text_bounds.width.value_or(layout_info.size().x),
                             text_bounds.height.value_or(layout_info.size().y)};
        const auto top_left = sprite::Anchor::TOP_LEFT.as_vec() - anchor.as_vec();
        const float x1      = top_left.x * size.x;
        const float x2      = (top_left.x + 1.0f) * size.x;
        const float y1      = (top_left.y - 1.0f) * size.y;
        const float y2      = top_left.y * size.y;
        return camera::Aabb::from_min_max(glm::vec3{x1, y1, 0.0f}, glm::vec3{x2, y2, 0.0f});
    };

    for (auto&& [entity, layout_info, anchor, text_bounds] : new_text_aabb.iter()) {
        commands.entity(entity).insert_if_new(text_aabb(layout_info, anchor, text_bounds));
    }
    for (auto&& [layout_info, anchor, text_bounds, aabb] : update_text_aabb.iter()) {
        aabb.get_mut() = text_aabb(layout_info, anchor, text_bounds);
    }
}

void TextRenderPlugin::attach(App& app) {
    spdlog::debug("[text] Attaching TextRenderPlugin.");
    app.add_plugins(core_graph::core_2d::Core2dPlugin{});
    // Bevy's 2D text extraction lives in `bevy_sprite_render` behind the
    // `bevy_text` feature, so text rendering depends on the sprite render
    // plugin being present.
    if (!app.get_plugin<sprite_render::SpriteRenderPlugin>()) {
        app.add_plugins(sprite_render::SpriteRenderPlugin{});
    }
    if (!app.get_plugin<sprite::SpritePlugin>()) {
        app.add_plugins(sprite::SpritePlugin{});
    }
    // Bevy registers this in `PostUpdate` within `VisibilitySystems::CalculateBounds`,
    // which is itself ordered `.before(CheckVisibility)` and
    // `.after(TransformSystems::Propagate)` (`bevy_sprite/src/text2d.rs:316-328`),
    // exactly as `sprite::calculate_bounds_2d` is registered here. Without the set the
    // AABB can be computed after visibility has already been decided.
    app.add_systems(app::PostUpdate, into(calculate_bounds_text2d)
                                         .in_set(camera::VisibilitySystems::CalculateBounds)
                                         .before(camera::VisibilitySystems::CheckVisibility)
                                         .set_name("calculate bounds text 2d"));
}

void TextRenderPlugin::ready(App& app) {
    spdlog::debug("[text] Readying TextRenderPlugin.");
    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app) {
        spdlog::error("[text] TextRenderPlugin requires render::RenderPlugin to be added before it.");
        return;
    }

    auto& render_subapp = render_app->get();
    // Bevy registers the text extractor `.after(SpriteSystems::ExtractSprites)`
    // (`bevy_sprite_render/src/lib.rs:104-105`) because sprite extraction
    // clears the extracted sprite and slice stores each frame.
    render_subapp.add_systems(render::ExtractSchedule, into(extract_text2d_sprite)
                                                           .after(sprite_render::SpriteSystems::ExtractSprites)
                                                           .set_name("extract text2d sprite"));
}
