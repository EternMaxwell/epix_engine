#include <ranges>

#include "epix/mesh/material.hpp"
#include "epix/mesh/mesh.hpp"
#include "epix/mesh/render.hpp"
#include "epix/text.hpp"
#include "epix/text/font.hpp"
#include "epix/text/render.hpp"
#include "epix/text/text.hpp"
#include "epix/transform.hpp"

using namespace epix;
using namespace epix::text;

namespace epix::text {
void shape_changed_text(Commands cmd,
                        Query<Item<Entity, Ref<Text>, Ref<TextFont>, Ref<TextLayout>, Ref<TextBounds>>> texts,
                        ResMut<font::FontAtlasSets> atlas_sets) {
    for (auto&& [entity, text_item, font_item, layout_item, bounds_item] : texts.iter()) {
        if (!text_item.is_modified() && !font_item.is_modified() && !layout_item.is_modified() &&
            !bounds_item.is_modified())
            continue;
        auto& text     = text_item.get();
        auto& font     = font_item.get();
        auto& layout   = layout_item.get();
        auto& bounds   = bounds_item.get();
        auto atlas_opt = atlas_sets->get_mut(font.font).transform(
            [&](font::FontAtlasSet& ref) { return std::ref(ref.get_or_insert(font)); });
        if (!atlas_opt) continue;
        auto& atlas         = atlas_opt->get();
        auto shaped         = shape_text(text, font, layout, bounds, atlas);
        TextMeasure measure = TextMeasure{shaped.width(), shaped.height()};
        cmd.entity(entity).insert(std::move(shaped), measure);
    }
}
/// generate a mesh that originates at the center of the text block
void regen_mesh_for_shaped_text(Commands cmd,
                                Query<Item<Entity, Ref<ShapedText>, Ref<TextFont>, Ref<TextMeasure>>> shaped_texts,
                                ResMut<assets::Assets<mesh::Mesh>> mesh_assets,
                                ResMut<font::FontAtlasSets> atlas_sets) {
    for (auto&& [entity, shaped_item, font_item, measure_item] : shaped_texts.iter()) {
        if (!shaped_item.is_modified()) continue;
        auto& shaped   = shaped_item.get();
        auto& font     = font_item.get();
        auto atlas_opt = atlas_sets->get_mut(font.font).transform(
            [&](font::FontAtlasSet& ref) { return std::ref(ref.get_or_insert(font)); });
        if (!atlas_opt) continue;
        auto& atlas = atlas_opt->get();

        cmd.entity(entity).insert(TextMesh::from_shaped_text(shaped, mesh_assets, atlas),
                                  TextImage{.image = atlas.image_handle().value()});
    }
}
}  // namespace epix::text

void TextPlugin::build(App& app) {
    app.add_plugins(font::FontPlugin{});
    app.add_systems(PostUpdate, into(text::shape_changed_text)
                                    .set_name("shape_changed_text")
                                    .after(font::FontSystems::AddFontAtlasSet)
                                    .before(font::FontSystems::ApplyPendingFontAtlasUpdates));
    app.add_systems(Last, into(text::regen_mesh_for_shaped_text).set_name("regen_mesh_for_shaped_text"));
}