module;

export module epix.text:render;

import :font;
import :text;
import epix.assets;
import epix.core;
import epix.image;
import epix.mesh;
import epix.transform;
import glm;
import std;

namespace epix::text {
/** @brief Component holding a generated mesh and bounding metrics for
 * rendered text.
 *
 * Created from a ShapedText via `from_shaped_text()`. Provides read-only
 * access to the mesh handle and the text's bounding box and baseline
 * metrics.
 */
export struct TextMesh {
   public:
    /** @brief Get the handle to the generated mesh asset. */
    const assets::Handle<mesh::Mesh>& mesh() const { return mesh_handle_; }
    /** @brief Get the left edge of the text bounding box. */
    float left() const { return left_; }
    /** @brief Get the right edge of the text bounding box. */
    float right() const { return right_; }
    /** @brief Get the top edge of the text bounding box. */
    float top() const { return top_; }
    /** @brief Get the bottom edge of the text bounding box. */
    float bottom() const { return bottom_; }
    /** @brief Get the font ascent metric. */
    float ascent() const { return ascent_; }
    /** @brief Get the font descent metric. */
    float descent() const { return descent_; }
    /** @brief Get the line height (ascent minus descent). */
    float line_height() const { return ascent_ - descent_; }
    /** @brief Get the total width of the text bounding box. */
    float width() const { return right_ - left_; }
    /** @brief Get the total height of the text bounding box. */
    float height() const { return top_ - bottom_; }

    /** @brief Create a TextMesh from shaped text, generating a glyph quad
     * mesh.
     * @param shaped The shaped text containing glyph positions.
     * @param mesh_assets Mesh asset storage for the generated mesh.
     * @param atlas Font atlas providing glyph UV coordinates. */
    static TextMesh from_shaped_text(const ShapedText& shaped,
                                     assets::Assets<mesh::Mesh>& mesh_assets,
                                     font::FontAtlas& atlas);

   private:
    TextMesh(assets::Handle<mesh::Mesh> mesh_handle,
             float left,
             float right,
             float top,
             float bottom,
             float ascent,
             float descent)
        : mesh_handle_(std::move(mesh_handle)),
          left_(left),
          right_(right),
          top_(top),
          bottom_(bottom),
          ascent_(ascent),
          descent_(descent) {}

    assets::Handle<mesh::Mesh> mesh_handle_;
    float left_    = 0.0f;
    float right_   = 0.0f;
    float top_     = 0.0f;
    float bottom_  = 0.0f;
    float ascent_  = 0.0f;
    float descent_ = 0.0f;
};

/** @brief Component linking a text entity to its font atlas image asset.
 */
export struct TextImage {
    /** @brief Asset ID of the atlas image used for this text's glyphs. */
    assets::AssetId<image::Image> image;
};

/** @brief Component marking an entity for 2D text rendering with an
 * optional pixel offset. */
export struct Text2d {
    /** @brief Pixel offset from the entity's transform position. */
    glm::vec2 offset = glm::vec2(0.0f);
};

/** @brief Bundle for spawning a 2D text rendering entity with transform
 * and color. */
export struct Text2dBundle {
    /** @brief 2D text rendering component. */
    Text2d text2d;
    /** @brief World-space transform for the text. */
    transform::Transform transform;
    /** @brief Color applied to the rendered text. */
    TextColor color;
};

/** @brief Plugin that registers text mesh generation, texture extraction,
 * and 2D text draw systems. */
export struct TextRenderPlugin {
    void build(core::App& app);
    void finish(core::App& app);
};
}  // namespace text

template <>
struct epix::core::Bundle<epix::text::Text2dBundle> {
    static std::size_t write(text::Text2dBundle& bundle, std::span<void*> dests) {
        new (dests[0]) text::Text2d(std::move(bundle.text2d));
        new (dests[1]) transform::Transform(std::move(bundle.transform));
        new (dests[2]) text::TextColor(std::move(bundle.color));
        return 3;
    }

    static std::array<TypeId, 3> type_ids(const TypeRegistry& registry) {
        return std::array{
            registry.type_id<text::Text2d>(),
            registry.type_id<transform::Transform>(),
            registry.type_id<text::TextColor>(),
        };
    }

    static void register_components(const TypeRegistry&, Components& components) {
        components.register_info<text::Text2d>();
        components.register_info<transform::Transform>();
        components.register_info<text::TextColor>();
    }
};

static_assert(epix::core::is_bundle<epix::text::Text2dBundle>);