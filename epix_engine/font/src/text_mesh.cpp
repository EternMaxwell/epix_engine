#include "epix/font.h"

using namespace epix::font::vulkan2;

EPIX_API TextMesh::TextMesh() : Mesh<TextVertex>() {}
EPIX_API void TextMesh::draw_text(
    const Text& text,
    const glm::vec2& pos,
    const FontAtlas* font_atlas,
    const VulkanResources* res_manager
) {
    auto& color    = text.color;
    auto& font     = text.font;
    glm::vec2 size = {0, text.height ? text.height : font.pixels};
    float scale    = size.y / font.pixels;
    for (auto& c : text.text) {
        auto glyph_opt = font_atlas->get_glyph(font, c);
        if (!glyph_opt.has_value()) continue;
        auto& glyph = glyph_opt.value();
        if (glyph.size.x == 0 || glyph.size.y == 0) continue;
        size.x += glyph.advance.x * scale;
    }
    glm::vec2 cur_pos =
        pos - glm::vec2(size.x * text.center.x, size.y * text.center.y);
    for (auto& c : text.text) {
        auto glyph_opt = font_atlas->get_glyph(font, c);
        if (!glyph_opt.has_value()) continue;
        auto& glyph = glyph_opt.value();
        if (glyph.size.x == 0 || glyph.size.y == 0) continue;
        glm::vec2 offset =
            glm::vec2(glyph.bearing.x, glyph.bearing.y - (int)glyph.size.y) *
            scale;
        emplace_vertex(
            cur_pos + offset, color, glyph.uv_1, glyph.uv_2,
            glm::vec2(glyph.size.x * scale, glyph.size.y * scale),
            glyph.array_index, font_atlas->font_index(font),
            res_manager->sampler_index("font::sampler::default")
        );
        cur_pos += glm::vec2(glyph.advance.x * scale, glyph.advance.y * scale);
    }
}
EPIX_API void TextMesh::draw_text(
    const Text& text,
    const glm::vec2& pos,
    Res<FontAtlas> font_atlas,
    Res<VulkanResources> res_manager
) {
    draw_text(text, pos, font_atlas.get(), res_manager.get());
}

EPIX_API TextBatch::TextBatch(PipelineBase& pipeline, vk::CommandPool& pool)
    : Batch<Mesh<TextVertex>, glm::mat4>(
          pipeline,
          pool,
          [](auto& device, auto& pool, auto& set_layouts) {
              std::vector<vk::DescriptorSet> sets;
              sets.resize(2);
              sets[0] = device.allocateDescriptorSets(
                  vk::DescriptorSetAllocateInfo()
                      .setDescriptorPool(pool)
                      .setSetLayouts(set_layouts[0])
              )[0];
              return sets;
          },
          [](auto& device, auto& pool, auto& sets) {
              device.freeDescriptorSets(pool, sets[0]);
          }
      ) {}