#pragma once

#include <epix/assets.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>

namespace epix::sprite {
struct Sprite {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    bool flip_x = false;
    bool flip_y = false;
    std::optional<glm::vec4> uv_rect;  // (u_min, v_min, u_max, v_max), if not set, use full image
    std::optional<glm::vec2> size;     // if not set, use image size
    glm::vec2 anchor{0.f, 0.f};        // (0,0) is center, (-0.5,-0.5) is bottom-left, (0.5,0.5) is top-right
};

struct ExtractedSprite {
    Sprite sprite;
    transform::GlobalTransform transform;
    assets::AssetId<image::Image> texture;
};

struct SpritePipeline {
    render::RenderPipelineId pipeline_id;
    nvrhi::BindingLayoutHandle image_layout;
    nvrhi::BindingLayoutHandle uniform_layout;

    static SpritePipeline from_world(World& world);
};
struct SpriteShadersPlugin {
    void build(App& app);
};
struct VertexBuffers {
    nvrhi::BufferHandle position_buffer;
    nvrhi::BufferHandle texcoord_buffer;
    nvrhi::BufferHandle index_buffer;

    static VertexBuffers from_world(World& world);
};
struct SpriteInstanceData {
    glm::mat4 model;
    glm::vec4 uv_offset_scale;  // (u_offset, v_offset, u_scale, v_scale)
                                // the final uv = uv * uv_scale + uv_offset
    glm::vec4 color;
    glm::vec4 pos_offset_scale;  // (x_offset, y_offset, x_scale, y_scale)
                                 // the final position.xy = (position.xy + pos_offset) * pos_scale
                                 // the offset is basically -anchor, the scale is size
};
struct SpriteInstanceBuffer {
   private:
    std::vector<SpriteInstanceData> data;
    nvrhi::BufferHandle buffer;

   public:
    void clear() { data.clear(); }
    size_t push(const SpriteInstanceData& instance) {
        data.push_back(instance);
        return data.size() - 1;
    }
    size_t size() const { return data.size(); }
    nvrhi::BufferHandle handle() const { return buffer; }
    void upload(nvrhi::DeviceHandle device, nvrhi::CommandListHandle cmd_list);
    void upload(nvrhi::DeviceHandle device);
};

struct SpriteBatch {
    nvrhi::BindingSetHandle binding_set;
    uint32_t instance_start;
};

template <render::render_phase::PhaseItem P>
struct BindResourceCommand {
    nvrhi::BindingSetHandle binding_set;
    void prepare(const World&) { binding_set = nullptr; }
    bool render(const P& item,
                Item<const render::view::UniformBuffer&> view_item,
                std::optional<Item<const SpriteBatch&>> entity_item,
                ParamSet<Res<SpriteInstanceBuffer>, Res<nvrhi::DeviceHandle>, Res<VertexBuffers>> params,
                render::render_phase::DrawContext& ctx) {
        auto&& [instance_buffer, device, vertex_buffers] = params.get();
        if (!entity_item) {
            spdlog::error("[sprite render] Entity {} has no SpriteBatch, skipping.", item.entity().index);
            return false;
        }
        auto&& [view_uniform] = *view_item;
        auto&& [sprite_batch] = **entity_item;
        nvrhi::BindingSetHandle uniform_and_instance_set;
        // get or create the binding set for the view uniform and instance buffer
        nvrhi::BindingSetDesc desc =
            nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::RawBuffer_SRV(0, instance_buffer->handle()));
        auto instance_set =
            device.get()->createBindingSet(desc, ctx.graphics_state.pipeline->getDesc().bindingLayouts[1]);

        // set the binding sets
        ctx.graphics_state.bindings.resize(3);
        ctx.graphics_state.bindings[1] = instance_set;  // does not add ref count
        ctx.graphics_state.bindings[2] = sprite_batch.binding_set;

        binding_set = instance_set;

        // add vertex and index buffers
        ctx.graphics_state.vertexBuffers.resize(0);
        ctx.graphics_state.addVertexBuffer(
            nvrhi::VertexBufferBinding().setBuffer(vertex_buffers->position_buffer).setSlot(0));
        ctx.graphics_state.addVertexBuffer(
            nvrhi::VertexBufferBinding().setBuffer(vertex_buffers->texcoord_buffer).setSlot(1));
        ctx.graphics_state.setIndexBuffer(
            nvrhi::IndexBufferBinding().setBuffer(vertex_buffers->index_buffer).setFormat(nvrhi::Format::R16_UINT));

        return true;
    }
};
template <render::render_phase::PhaseItem P>
struct DrawSpriteBatchCommand {
    bool render(const P& item,
                Item<> view_item,
                std::optional<Item<const SpriteBatch&>> entity_item,
                ParamSet<> params,
                render::render_phase::DrawContext& ctx) {
        if (!entity_item) {
            spdlog::error("[sprite render] Entity {} has no SpriteBatch, skipping.", item.entity().index);
            return false;
        }
        auto&& [sprite_batch] = **entity_item;
        ctx.commandlist->setGraphicsState(ctx.graphics_state);
        ctx.commandlist->drawIndexed(nvrhi::DrawArguments()
                                         .setInstanceCount(item.batch_size())
                                         .setStartInstanceLocation(sprite_batch.instance_start)
                                         .setVertexCount(6));

        return true;
    }
};

void extract_sprites(
    Commands cmd,
    Extract<Query<Item<Entity, const Sprite&, const transform::GlobalTransform&, const assets::Handle<image::Image>&>,
                  Without<render::CustomRendered>>> sprites);
void queue_sprites(Query<Item<render::render_phase::RenderPhase<render::core_2d::Transparent2D>&>,
                         With<render::camera::ExtractedCamera, render::view::ViewTarget>> views,
                   Res<SpritePipeline> pipeline,
                   ResMut<render::render_phase::DrawFunctions<render::core_2d::Transparent2D>> draw_functions,
                   Query<Item<Entity, const ExtractedSprite&>> extracted_sprites);
void prepare_sprites(Query<Item<render::render_phase::RenderPhase<render::core_2d::Transparent2D>&>,
                           With<render::camera::ExtractedCamera, render::view::ExtractedView>> views,
                     Query<Item<SpriteBatch&, const ExtractedSprite&>> batches,
                     Res<SpritePipeline> pipeline,
                     ResMut<SpriteInstanceBuffer> instance_buffer,
                     Res<render::assets::RenderAssets<image::Image>> images,
                     Res<nvrhi::DeviceHandle> device,
                     Res<render::DefaultSampler> default_sampler,
                     ResMut<VertexBuffers> vertex_buffers);

struct SpritePlugin {
    void build(App& app);
    void finish(App& app);
};

struct SpriteBundle {
    Sprite sprite;
    transform::Transform transform;
    assets::Handle<image::Image> texture;
};
}  // namespace epix::sprite

template <>
struct epix::core::bundle::Bundle<epix::sprite::SpriteBundle> {
    static size_t write(epix::sprite::SpriteBundle& bundle, std::span<void*> dests) {
        new (dests[0]) epix::sprite::Sprite(std::move(bundle.sprite));
        new (dests[1]) epix::transform::Transform(std::move(bundle.transform));
        new (dests[2]) epix::assets::Handle<epix::image::Image>(std::move(bundle.texture));
        return 3;
    }
    static std::array<epix::TypeId, 3> type_ids(const epix::TypeRegistry& registry) {
        return std::array{registry.type_id<epix::sprite::Sprite>(), registry.type_id<epix::transform::Transform>(),
                          registry.type_id<epix::assets::Handle<epix::image::Image>>()};
    }
    static void register_components(const epix::TypeRegistry&, epix::core::Components& components) {
        components.register_info<epix::sprite::Sprite>();
        components.register_info<epix::transform::Transform>();
        components.register_info<epix::assets::Handle<epix::image::Image>>();
    }
};

static_assert(epix::core::bundle::is_bundle<epix::sprite::SpriteBundle>);