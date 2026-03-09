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

namespace text {
export struct TextMesh {
   public:
    const assets::Handle<mesh::Mesh>& mesh() const { return mesh_handle_; }
    float left() const { return left_; }
    float right() const { return right_; }
    float top() const { return top_; }
    float bottom() const { return bottom_; }
    float ascent() const { return ascent_; }
    float descent() const { return descent_; }
    float line_height() const { return ascent_ - descent_; }
    float width() const { return right_ - left_; }
    float height() const { return top_ - bottom_; }

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

export struct TextImage {
    assets::AssetId<image::Image> image;
};

export struct Text2d {
    glm::vec2 offset = glm::vec2(0.0f);
};

export struct Text2dBundle {
    Text2d text2d;
    transform::Transform transform;
    TextColor color;
};

export struct TextRenderPlugin {
    void build(core::App& app);
    void finish(core::App& app);
};
}  // namespace text

template <>
struct core::Bundle<text::Text2dBundle> {
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

static_assert(core::is_bundle<text::Text2dBundle>);