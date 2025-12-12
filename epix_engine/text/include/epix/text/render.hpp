#pragma once

#include <concepts>
#include <epix/assets/handle.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>

#include "font.hpp"
#include "text.hpp"

namespace epix::text {
struct TextMesh {
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
        : mesh_handle_(mesh_handle),
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
struct TextImage {
    assets::AssetId<image::Image> image;
};
struct RenderText {
    assets::AssetId<mesh::Mesh> mesh;
    assets::AssetId<image::Image> font_image;
    glm::mat4 transform;
    glm::vec4 color;
};
struct RenderTextInstances : std::unordered_map<Entity, RenderText> {};
template <std::derived_from<RenderTextInstances> res_t>
struct BindFontResources {
    template <render::render_phase::PhaseItem P>
    struct Command {
        nvrhi::BindingSetHandle binding_set;
        void prepare(World&) { binding_set = nullptr; }
        bool render(const P& item,
                    Item<>,
                    std::optional<Item<>>,
                    ParamSet<Res<nvrhi::DeviceHandle>,
                             Res<render::assets::RenderAssets<image::Image>>,
                             Res<render::DefaultSampler>,
                             Res<res_t>> params,
                    render::render_phase::DrawContext& ctx) {
            auto&& [device, images, default_sampler, render_texts] = params.get();
            auto image_layout = ctx.graphics_state.pipeline->getDesc().bindingLayouts[1];
            auto entity       = item.entity();
            auto it           = render_texts->find(entity);
            if (it == render_texts->end()) {
                spdlog::error("Entity {:#x} has no RenderText. Skipping.", entity.index);
                return false;
            }
            const RenderText& render_text = it->second;
            auto* image                   = images->try_get(render_text.font_image);
            if (!image) return false;
            binding_set =
                device.get()->createBindingSet(nvrhi::BindingSetDesc()
                                                   .addItem(nvrhi::BindingSetItem::Texture_SRV(0, *image))
                                                   .addItem(nvrhi::BindingSetItem::Sampler(1, default_sampler->handle)),
                                               image_layout);
            ctx.graphics_state.bindings.resize(2);
            ctx.graphics_state.bindings[1] = binding_set;
            ctx.setPushConstants(&render_text.transform, sizeof(glm::mat4));
            return true;
        }
    };
};
template <std::derived_from<RenderTextInstances> res_t>
struct DrawTextMesh {
    template <render::render_phase::PhaseItem P>
    struct Command {
        void prepare(World&) {}
        bool render(const P& item,
                    Item<>,
                    std::optional<Item<>>,
                    ParamSet<Res<render::assets::RenderAssets<mesh::Mesh>>, Res<res_t>> params,
                    render::render_phase::DrawContext& ctx) {
            auto&& [render_assets, render_texts] = params.get();
            auto entity                          = item.entity();
            const RenderText& render_text        = render_texts->at(entity);
            const auto* gpu_mesh                 = render_assets->try_get(render_text.mesh);
            if (!gpu_mesh) return false;
            gpu_mesh->bind_state(ctx.graphics_state);
            ctx.setGraphicsState();
            ctx.commandlist->drawIndexed(nvrhi::DrawArguments().setVertexCount(gpu_mesh->vertex_count()));
            return true;
        }
    };
};

struct TextPipeline {
   public:
    TextPipeline(World&);

    render::RenderPipelineId pipeline_id() const { return pipeline_id_; }

   private:
    render::RenderPipelineId pipeline_id_;
};
struct TextPipelinePlugin {
    void build(epix::App& app);
};
struct TextRenderPlugin {
    void build(epix::App& app);
};

struct Text2d {
    glm::vec2 offset = glm::vec2(0.0f);
};

struct Text2dBundle {
    Text2d text2d;
    transform::Transform transform;
    TextColor color;
};
}  // namespace epix::text

namespace epix::core::bundle {
template <>
struct Bundle<epix::text::Text2dBundle> {
    static size_t write(epix::text::Text2dBundle& bundle, std::span<void*> dests) {
        new (dests[0]) epix::text::Text2d(std::move(bundle.text2d));
        new (dests[1]) epix::transform::Transform(std::move(bundle.transform));
        new (dests[2]) epix::text::TextColor(std::move(bundle.color));
        return 3;
    }
    static auto type_ids(const epix::TypeRegistry& registry) {
        return std::array{
            registry.type_id<epix::text::Text2d>(),
            registry.type_id<epix::transform::Transform>(),
            registry.type_id<epix::text::TextColor>(),
        };
    }
    static void register_components(const epix::TypeRegistry&, epix::core::Components& components) {
        components.register_info<epix::text::Text2d>();
        components.register_info<epix::transform::Transform>();
        components.register_info<epix::text::TextColor>();
    }
};
}  // namespace epix::core::bundle