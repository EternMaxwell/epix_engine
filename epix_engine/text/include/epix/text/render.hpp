#pragma once


#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/camera.hpp>
#include <epix/ecs.hpp>
#include <epix/image.hpp>
#include <epix/transform.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <utility>
#include <vector>
#include <epix/common.hpp>
#endif

#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
#include <epix/text/font.hpp>
#include <epix/text/text.hpp>

namespace epix::text {
/** @brief Component marking an entity for 2D text rendering with an optional
 * pixel offset (Bevy `Text2d`, `bevy_sprite/src/text2d.rs:80-96`).
 *
 * Like Bevy's, `Text2d` requires a `Transform`, `Visibility`,
 * `VisibilityClass`, `Anchor` and `TextColor`, and registers itself under the
 * `Sprite` visibility class so that the sprite queue renders its glyphs
 * (`text2d.rs:95`). The `offset` field is an Epix extension: Bevy has no
 * per-entity text offset.
 */
EPIX_EXPORT struct EPIX_API Text2d {
    /** @brief Pixel offset from the entity's transform position. */
    glm::vec2 offset = glm::vec2(0.0f);

    static void register_required_components(ecs::RequiredComponentsRegistrator& registrator);

    /** @brief Component-add hook adding the `Sprite` visibility class to the
     * entity (Bevy `#[component(on_add = visibility::add_visibility_class::<Sprite>)]`,
     * `bevy_camera/src/visibility/mod.rs:729-738`). */
    static void on_add(ecs::World& world, ecs::HookContext context);
};

/** @brief Compute and insert the `Aabb` of a 2D text entity (Bevy
 *  `calculate_bounds_text2d`, `bevy_sprite/src/text2d.rs:316-347`).
 *
 *  Bevy's function is public, and so is this one: `TextRenderPlugin` registers it in
 *  `PostUpdate` inside `VisibilitySystems::CalculateBounds` and before
 *  `CheckVisibility`, exactly as `sprite::calculate_bounds_2d` is registered, so a
 *  caller with its own schedule can do the same. */
EPIX_EXPORT EPIX_API void calculate_bounds_text2d(
    ecs::Commands commands,
    ecs::Query<ecs::Item<ecs::Entity, const ShapedText&, const sprite::Anchor&, const TextBounds&>,
               ecs::Filter<ecs::Without<camera::Aabb>, ecs::Without<camera::NoFrustumCulling>>>
        new_text_aabb,
    ecs::Query<ecs::Item<const ShapedText&, const sprite::Anchor&, const TextBounds&, ecs::Mut<camera::Aabb>>,
               ecs::Filter<ecs::Modified<ShapedText>, ecs::Without<camera::NoFrustumCulling>>>
        update_text_aabb);

/** @brief Bundle for spawning a 2D text rendering entity with transform
 * and color. */
EPIX_EXPORT struct Text2dBundle {
    /** @brief 2D text rendering component. */
    Text2d text2d;
    /** @brief World-space transform for the text. */
    transform::Transform transform;
    /** @brief Color applied to the rendered text. */
    TextColor color;
};

/** @brief Plugin that registers 2D text extraction into the sprite render
 * path.
 *
 * Bevy extracts `Text2d` glyphs in `bevy_sprite_render`
 * (`extract_text2d_sprite`, `text2d/mod.rs:25`), which Epix mirrors by
 * extracting `ExtractedSprite`s from the text module and handing them to the
 * sprite pipeline. */
EPIX_EXPORT struct EPIX_API TextRenderPlugin {
    void attach(app::App& app);
    void ready(app::App& app);
};

EPIX_EXPORT namespace detail {
/** @brief Emit one laid-out text entity's glyph slices and sprites.
 *
 * This is the per-entity body of Bevy's `extract_text2d_sprite`
 * (`bevy_sprite_render/src/text2d/mod.rs:54-273`), separated from the ECS
 * plumbing so that the emitted geometry is directly testable.
 *
 * @param layout_info The shaped text (Bevy `TextLayoutInfo`).
 * @param main_entity The text entity the glyphs belong to (Bevy
 * `main_entity`, `text2d/mod.rs:58`).
 * @param text_bounds The entity's bounds, whose unset axes fall back to the
 * layout size (`text2d/mod.rs:75-78`).
 * @param anchor The entity's `Anchor`; the block's top-left corner is
 * `(Anchor::TOP_LEFT - anchor) * size` (`text2d/mod.rs:80`).
 * @param color The text color applied to every emitted sprite.
 * @param global_transform The entity's transform; glyph offsets are relative to
 * it.
 * @param texture_atlases Atlas layouts, used to look up each glyph's rectangle
 * (`text2d/mod.rs:242-251`).
 * @param sprites Destination extracted-sprite list.
 * @param slices Destination extracted-slice list.
 * @param alloc_render_entity Returns a fresh render entity for each emitted
 * sprite; the extractor spawns a `TemporaryRenderEntity`, exactly as Bevy does
 * (`text2d/mod.rs:256`). */
template <typename AllocRenderEntity>
void extract_text2d_layout(const ShapedText& layout_info,
                           ecs::Entity main_entity,
                           const TextBounds& text_bounds,
                           const sprite::Anchor& anchor,
                           const glm::vec4& color,
                           const transform::GlobalTransform& global_transform,
                           const assets::Assets<image::TextureAtlasLayout>& texture_atlases,
                           std::vector<sprite_render::ExtractedSprite>& sprites,
                           std::vector<sprite_render::ExtractedSlice>& slices,
                           AllocRenderEntity&& alloc_render_entity) {
    const glm::vec2 size{text_bounds.width.value_or(layout_info.size().x),
                         text_bounds.height.value_or(layout_info.size().y)};
    const glm::vec2 top_left = (sprite::Anchor::TOP_LEFT.as_vec() - anchor.as_vec()) * size;

    const glm::mat4 transform =
        global_transform.matrix * glm::translate(glm::mat4(1.0f), glm::vec3(top_left, 0.0f));

    std::size_t start  = slices.size();
    const auto& glyphs = layout_info.glyphs();
    for (std::size_t index = 0; index < glyphs.size(); ++index) {
        const auto& glyph = glyphs[index];
        const auto atlas  = texture_atlases.get(glyph.atlas_info.texture_atlas);
        if (!atlas || glyph.atlas_info.location.glyph_index >= atlas->get().textures.size()) {
            continue;
        }

        const auto rect = atlas->get().textures[glyph.atlas_info.location.glyph_index].as_rect();
        slices.push_back(sprite_render::ExtractedSlice{
            // Bevy negates the layout's +Y-down position into 2D world space
            // (`text2d/mod.rs:248`); the slice is instantiated around its
            // center (`render/mod.rs:783-788`).
            .offset = glm::vec2{glyph.position.x, -glyph.position.y},
            .rect   = rect,
            .size   = rect.size(),
        });

        // Bevy flushes one sprite per run of consecutive glyphs that share an
        // atlas texture and span (`text2d/mod.rs:253-270`); with a single-span
        // `Text` only the atlas texture can change.
        const bool is_last        = index + 1 == glyphs.size();
        const bool texture_change = !is_last && glyphs[index + 1].atlas_info.texture != glyph.atlas_info.texture;
        if (is_last || texture_change) {
            sprites.push_back(sprite_render::ExtractedSprite{
                .main_entity     = main_entity,
                .render_entity   = alloc_render_entity(),
                .transform       = transform,
                .color           = color,
                .image_handle_id = glyph.atlas_info.texture,
                .flip_x          = false,
                .flip_y          = false,
                .kind            = sprite_render::ExtractedSpriteSlices{start, slices.size()},
            });
            start = slices.size();
        }
    }
}
}  // namespace detail
}  // namespace epix::text

template <>
struct epix::ecs::Bundle<epix::text::Text2dBundle> {
    static void get_components(text::Text2dBundle& bundle,
                               std::invocable<utils::function_ref<void(void*)>> auto&& write_component) noexcept {
        write_component([&](void* ptr) { new (ptr) text::Text2d(std::move(bundle.text2d)); });
        write_component([&](void* ptr) { new (ptr) transform::Transform(std::move(bundle.transform)); });
        write_component([&](void* ptr) { new (ptr) text::TextColor(std::move(bundle.color)); });
    }

    static std::array<std::optional<TypeId>, 3> type_ids(const ecs::Components& components) {
        return std::array{
            components.get_id<text::Text2d>(),
            components.get_id<transform::Transform>(),
            components.get_id<text::TextColor>(),
        };
    }

    static std::vector<TypeId> register_components(ecs::ComponentsRegistrator& components) {
        std::vector<TypeId> ids;
        ids.push_back(components.template register_component<text::Text2d>());
        ids.push_back(components.template register_component<transform::Transform>());
        ids.push_back(components.template register_component<text::TextColor>());
        return ids;
    }
};

static_assert(epix::ecs::is_bundle<epix::text::Text2dBundle>);
