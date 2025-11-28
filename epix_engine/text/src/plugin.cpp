#include <ranges>

#include "epix/mesh/material.hpp"
#include "epix/mesh/mesh.hpp"
#include "epix/mesh/render.hpp"
#include "epix/text.hpp"
#include "epix/text/font.hpp"
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
        TextMeasure measure = TextMeasure{shaped.width, shaped.height};
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
        glm::vec3 origin{measure_item.get().width / 2.0f, measure_item.get().height / 2.0f, 0.0f};
        mesh::Mesh mesh;
        mesh.set_primitive_type(nvrhi::PrimitiveType::TriangleList);
        auto res = mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                                         shaped.glyphs | std::views::transform([&](const GlyphInfo& gi) {
                                             std::array<glm::vec3, 4> positions;
                                             const auto& glyph = atlas.get_glyph(gi.glyph_index);

                                             float x0     = gi.x_offset - origin.x + glyph.horiBearingX;
                                             float y0     = origin.y - gi.y_offset - glyph.height + glyph.horiBearingY;
                                             float x1     = x0 + glyph.width;
                                             float y1     = y0 + glyph.height;
                                             positions[0] = glm::vec3{x0, y0, 0.0f};
                                             positions[1] = glm::vec3{x1, y0, 0.0f};
                                             positions[2] = glm::vec3{x1, y1, 0.0f};
                                             positions[3] = glm::vec3{x0, y1, 0.0f};
                                             return positions;
                                         }) | std::views::join);
        res      = mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_UV0,
                                         shaped.glyphs | std::views::transform([&](const GlyphInfo& gi) {
                                        std::array<glm::vec2, 4> uvs;
                                        auto&& uv_rect = atlas.get_glyph_uv_rect(gi.glyph_index);
                                        uvs[0]         = glm::vec2{uv_rect[0], uv_rect[1]};
                                        uvs[1]         = glm::vec2{uv_rect[2], uv_rect[1]};
                                        uvs[2]         = glm::vec2{uv_rect[2], uv_rect[3]};
                                        uvs[3]         = glm::vec2{uv_rect[0], uv_rect[3]};
                                        return uvs;
                                    }) | std::views::join);
        mesh.insert_indices<uint32_t>(
            std::views::iota(0u, static_cast<uint32_t>(shaped.glyphs.size())) | std::views::transform([](uint32_t gi) {
                uint32_t base = gi * 4;
                return std::array<uint32_t, 6>{base + 0, base + 1, base + 2, base + 2, base + 3, base + 0};
            }) |
            std::views::join);
        cmd.entity(entity).insert(mesh::Mesh2d(mesh_assets->emplace(std::move(mesh))),
                                  mesh::ImageMaterial{.image = atlas.image_handle().value()}, transform::Transform{});
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