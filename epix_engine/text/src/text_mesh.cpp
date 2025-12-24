#include "epix/text/font.hpp"
#include "epix/text/render.hpp"

using namespace epix;
using namespace epix::text;

namespace epix::text {
TextMesh TextMesh::from_shaped_text(const ShapedText& shaped,
                                    assets::Assets<mesh::Mesh>& mesh_assets,
                                    font::FontAtlas& atlas) {
    mesh::Mesh mesh;
    mesh.set_primitive_type(nvrhi::PrimitiveType::TriangleList);
    auto res = mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                                     shaped.glyphs() | std::views::transform([&](const GlyphInfo& gi) {
                                         std::array<glm::vec3, 4> positions;
                                         const auto& glyph = atlas.get_glyph(gi.glyph_index);

                                         float x0     = gi.x_offset + glyph.horiBearingX;
                                         float y0     = gi.y_offset - glyph.height + glyph.horiBearingY;
                                         float x1     = x0 + glyph.width;
                                         float y1     = y0 + glyph.height;
                                         positions[0] = glm::vec3{x0, y0, 0.0f};
                                         positions[1] = glm::vec3{x1, y0, 0.0f};
                                         positions[2] = glm::vec3{x1, y1, 0.0f};
                                         positions[3] = glm::vec3{x0, y1, 0.0f};
                                         return positions;
                                     }) | std::views::join);
    res      = mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_UV0,
                                     shaped.glyphs() | std::views::transform([&](const GlyphInfo& gi) {
                                    std::array<glm::vec2, 4> uvs;
                                    auto&& uv_rect = atlas.get_glyph_uv_rect(gi.glyph_index);
                                    uvs[0]         = glm::vec2{uv_rect[0], uv_rect[1]};
                                    uvs[1]         = glm::vec2{uv_rect[2], uv_rect[1]};
                                    uvs[2]         = glm::vec2{uv_rect[2], uv_rect[3]};
                                    uvs[3]         = glm::vec2{uv_rect[0], uv_rect[3]};
                                    return uvs;
                                }) | std::views::join);
    mesh.insert_indices<uint32_t>(
        std::views::iota(0u, static_cast<uint32_t>(shaped.glyphs().size())) | std::views::transform([](uint32_t gi) {
            uint32_t base = gi * 4;
            return std::array<uint32_t, 6>{base + 0, base + 1, base + 2, base + 2, base + 3, base + 0};
        }) |
        std::views::join);
    auto mesh_handle = mesh_assets.emplace(std::move(mesh));
    return TextMesh(mesh_handle, shaped.left(), shaped.right(), shaped.top(), shaped.bottom(), shaped.ascent(),
                    shaped.descent());
}
}  // namespace epix::text