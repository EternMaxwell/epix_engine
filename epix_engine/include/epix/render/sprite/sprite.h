#pragma once

#include <epix/assets.h>
#include <epix/image.h>
#include <epix/render.h>

namespace epix::sprite {
struct Sprite {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    bool flip_x = false;
    bool flip_y = false;
    std::optional<glm::vec4> uv_rect;  // (u_min, v_min, u_max, v_max), if not set, use full image
    std::optional<glm::vec2> size;     // if not set, use image size
    glm::vec2 anchor{0.f, 0.f};        // (0,0) is center, (-0.5,-0.5) is bottom-left, (0.5,0.5) is top-right
};

struct SpriteBundle : Bundle {
    Sprite sprite;
    transform::Transform transform;
    assets::Handle<image::Image> texture;

    auto unpack() { return std::make_tuple(sprite, transform, std::move(texture)); }
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

    EPIX_API static std::optional<SpritePipeline> from_world(World& world);
};
struct SpriteShadersPlugin {
    EPIX_API void build(App& app);
};

struct ViewUniform {
    nvrhi::BufferHandle view_buffer;
};
struct ViewUniformCache {
    std::deque<ViewUniform> cache;
};
struct VertexBuffers {
    nvrhi::BufferHandle position_buffer;
    nvrhi::BufferHandle texcoord_buffer;
    nvrhi::BufferHandle index_buffer;

    EPIX_API static std::optional<VertexBuffers> from_world(World& world);
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
    EPIX_API void upload(nvrhi::DeviceHandle device, nvrhi::CommandListHandle cmd_list);
    EPIX_API void upload(nvrhi::DeviceHandle device);
};

struct SpriteBatch {
    nvrhi::BindingSetHandle binding_set;
    uint32_t instance_start;
};

struct DefaultSampler {
    nvrhi::SamplerHandle handle;
    nvrhi::SamplerDesc desc;
};

template <render::render_phase::PhaseItem P>
struct BindResourceCommand {
    entt::dense_map<nvrhi::BufferHandle, nvrhi::BindingSetHandle> uniform_set_cache;
    void prepare(const World&) { uniform_set_cache.clear(); }
    bool render(
        const P& item,
        Item<ViewUniform> view_item,
        std::optional<Item<SpriteBatch>> entity_item,
        ParamSet<Res<SpriteInstanceBuffer>, Res<nvrhi::DeviceHandle>, Res<SpritePipeline>, Res<VertexBuffers>> params,
        render::render_phase::DrawContext& ctx) {
        auto&& [instance_buffer, device, pipeline, vertex_buffers] = params.get();
        if (!entity_item) {
            spdlog::error("[sprite render] Entity {} has no SpriteBatch, skipping.", item.entity().index());
            return false;
        }
        auto&& [view_uniform] = view_item;
        auto&& [sprite_batch] = *entity_item;
        nvrhi::BindingSetHandle uniform_and_instance_set;
        // get or create the binding set for the view uniform and instance buffer
        if (auto it = uniform_set_cache.find(view_uniform.view_buffer); it != uniform_set_cache.end()) {
            uniform_and_instance_set = it->second;
        } else {
            nvrhi::BindingSetDesc desc =
                nvrhi::BindingSetDesc()
                    .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, view_uniform.view_buffer))
                    .addItem(nvrhi::BindingSetItem::RawBuffer_SRV(1, instance_buffer->handle()));
            uniform_and_instance_set = device.get()->createBindingSet(desc, pipeline->uniform_layout);
            uniform_set_cache[view_uniform.view_buffer] = uniform_and_instance_set;
        }

        // set the binding sets
        ctx.graphics_state.bindings.resize(0);
        ctx.graphics_state.addBindingSet(uniform_and_instance_set);
        ctx.graphics_state.addBindingSet(sprite_batch.binding_set);

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
                std::optional<Item<SpriteBatch>> entity_item,
                ParamSet<> params,
                render::render_phase::DrawContext& ctx) {
        if (!entity_item) {
            spdlog::error("[sprite render] Entity {} has no SpriteBatch, skipping.", item.entity().index());
            return false;
        }
        auto&& [sprite_batch] = *entity_item;
        ctx.commandlist->setGraphicsState(ctx.graphics_state);
        ctx.commandlist->drawIndexed(nvrhi::DrawArguments()
                                         .setInstanceCount(item.batch_size())
                                         .setStartInstanceLocation(sprite_batch.instance_start)
                                         .setVertexCount(6));

        return true;
    }
};

struct DefaultSamplerPlugin {
    static bool desc_equal(const nvrhi::SamplerDesc& a, const nvrhi::SamplerDesc& b) {
        return a.borderColor == b.borderColor && a.maxAnisotropy == b.maxAnisotropy && a.mipBias == b.mipBias &&
               a.minFilter == b.minFilter && a.magFilter == b.magFilter && a.mipFilter == b.mipFilter &&
               a.addressU == b.addressU && a.addressV == b.addressV && a.addressW == b.addressW &&
               a.reductionType == b.reductionType;
    }
    EPIX_API void build(App& app);
};

EPIX_API void extract_sprites(
    Commands cmd,
    Extract<Query<Item<Entity, Sprite, transform::GlobalTransform, assets::Handle<image::Image>>>> sprites);
EPIX_API void queue_sprites(Query<Item<Mut<render::render_phase::RenderPhase<render::core_2d::Transparent2D>>>,
                                  With<render::camera::ExtractedCamera, render::view::ViewTarget>> views,
                            Res<SpritePipeline> pipeline,
                            ResMut<render::render_phase::DrawFunctions<render::core_2d::Transparent2D>> draw_functions,
                            Query<Item<Entity, ExtractedSprite>> extracted_sprites);
EPIX_API void create_uniform_for_view(
    Commands cmd,
    Query<Item<Entity, render::view::ExtractedView, render::camera::ExtractedCamera>> views,
    ResMut<ViewUniformCache> uniform_cache,
    Res<nvrhi::DeviceHandle> device);
EPIX_API void prepare_sprites(Query<Item<Mut<render::render_phase::RenderPhase<render::core_2d::Transparent2D>>>,
                                    With<render::camera::ExtractedCamera, render::view::ExtractedView>> views,
                              Query<Item<Mut<SpriteBatch>, ExtractedSprite>> batches,
                              Res<SpritePipeline> pipeline,
                              ResMut<SpriteInstanceBuffer> instance_buffer,
                              Res<render::assets::RenderAssets<image::Image>> images,
                              Res<nvrhi::DeviceHandle> device,
                              Res<DefaultSampler> default_sampler,
                              ResMut<VertexBuffers> vertex_buffers);

struct SpritePlugin {
    EPIX_API void build(App& app);
};
}  // namespace epix::sprite