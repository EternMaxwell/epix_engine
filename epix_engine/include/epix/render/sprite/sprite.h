#pragma once

#include <epix/assets.h>
#include <epix/image.h>
#include <epix/render.h>

#include <source_location>

namespace epix::sprite::shaders {
#include "shaders/shader.frag.h"
#include "shaders/shader.vert.h"

}  // namespace epix::sprite::shaders

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

inline static assets::Handle<render::Shader> sprite_vertex_shader =
    assets::AssetId<render::Shader>(uuids::uuid::from_string("1a75cef9-87bb-4325-a50f-a81f96fe414c").value());
inline static assets::Handle<render::Shader> sprite_fragment_shader =
    assets::AssetId<render::Shader>(uuids::uuid::from_string("a799ba44-0bb4-4dd9-98b1-e70deea84e1f").value());

struct SpritePipeline {
    render::RenderPipelineId pipeline_id;
    nvrhi::BindingLayoutHandle image_layout;
    nvrhi::BindingLayoutHandle uniform_layout;

    static std::optional<SpritePipeline> from_world(World& world) {
        if (auto pipeline_server = world.get_resource<render::PipelineServer>()) {
            SpritePipeline pipeline;
            auto device = world.resource<nvrhi::DeviceHandle>();

            // a texture and a sampler
            pipeline.image_layout =
                device->createBindingLayout(nvrhi::BindingLayoutDesc()
                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
                                                .addItem(nvrhi::BindingLayoutItem::Sampler(1))
                                                .setVisibility(nvrhi::ShaderType::Pixel)
                                                .setBindingOffsets(nvrhi::VulkanBindingOffsets{0, 0, 0, 0}));

            // a uniform buffer and a push constant for the transform
            pipeline.uniform_layout = device->createBindingLayout(
                nvrhi::BindingLayoutDesc()
                    .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0))
                    .addItem(nvrhi::BindingLayoutItem::RawBuffer_SRV(1))  // storage buffer for batched model matrices
                    .setVisibility(nvrhi::ShaderType::Vertex)
                    .setBindingOffsets(nvrhi::VulkanBindingOffsets{0, 0, 0, 0}));

            render::RenderPipelineDesc pipeline_desc;
            pipeline_desc.setRenderState(
                nvrhi::RenderState()
                    .setDepthStencilState(nvrhi::DepthStencilState().setDepthTestEnable(true).setDepthWriteEnable(true))
                    .setBlendState(
                        nvrhi::BlendState().setRenderTarget(0, nvrhi::BlendState::RenderTarget()
                                                                   .setBlendEnable(true)
                                                                   .setBlendOp(nvrhi::BlendOp::Add)
                                                                   .setBlendOpAlpha(nvrhi::BlendOp::Add)
                                                                   .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                                                                   .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                                                                   .setSrcBlendAlpha(nvrhi::BlendFactor::One)
                                                                   .setDestBlendAlpha(nvrhi::BlendFactor::Zero)))
                    .setRasterState(
                        nvrhi::RasterState().setCullMode(nvrhi::RasterCullMode::None).setFrontCounterClockwise(true)));
            pipeline_desc.addBindingLayout(pipeline.uniform_layout).addBindingLayout(pipeline.image_layout);
            pipeline_desc.setVertexShader(render::ShaderInfo{
                .shader    = sprite_vertex_shader,
                .debugName = "SpriteVertex",
            });
            pipeline_desc.setFragmentShader(render::ShaderInfo{
                .shader    = sprite_fragment_shader,
                .debugName = "SpriteFragment",
            });
            auto input_layouts = std::array{
                nvrhi::VertexAttributeDesc()
                    .setName("Position")
                    .setFormat(nvrhi::Format::RG32_FLOAT)
                    .setOffset(0)
                    .setElementStride(sizeof(glm::vec2))
                    .setBufferIndex(0),
                nvrhi::VertexAttributeDesc()
                    .setName("TexCoord")
                    .setFormat(nvrhi::Format::RG32_FLOAT)
                    .setOffset(0)
                    .setElementStride(sizeof(glm::vec2))
                    .setBufferIndex(1),
            };
            nvrhi::InputLayoutHandle input_layout =
                device->createInputLayout(input_layouts.data(), (uint32_t)input_layouts.size(), nullptr);
            pipeline_desc.setInputLayout(input_layout);
            pipeline.pipeline_id = pipeline_server->queue_render_pipeline(pipeline_desc);
            return pipeline;
        } else {
            return std::nullopt;
        }
    }
};
struct SpriteShadersPlugin {
    void build(App& app) {
        app.add_systems(Startup, into([](ResMut<assets::Assets<render::Shader>> shaders) {
                                     shaders->insert(sprite_vertex_shader,
                                                     render::Shader::spirv(std::vector<uint32_t>(
                                                         sprite::shaders::sprite_vert,
                                                         sprite::shaders::sprite_vert +
                                                             sizeof(sprite::shaders::sprite_vert) / sizeof(uint32_t))));
                                     shaders->insert(sprite_fragment_shader,
                                                     render::Shader::spirv(std::vector<uint32_t>(
                                                         sprite::shaders::sprite_frag,
                                                         sprite::shaders::sprite_frag +
                                                             sizeof(sprite::shaders::sprite_frag) / sizeof(uint32_t))));
                                 }).set_name("insert sprite shaders"));
    }
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

    static std::optional<VertexBuffers> from_world(World& world) {
        if (auto pdevice = world.get_resource<nvrhi::DeviceHandle>()) {
            auto pos = std::array{
                glm::vec2(-0.5f, -0.5f),
                glm::vec2(0.5f, -0.5f),
                glm::vec2(0.5f, 0.5f),
                glm::vec2(-0.5f, 0.5f),
            };
            auto texcoord = std::array{
                glm::vec2(0.0f, 0.0f),
                glm::vec2(1.0f, 0.0f),
                glm::vec2(1.0f, 1.0f),
                glm::vec2(0.0f, 1.0f),
            };
            auto indices  = std::array<uint16_t, 6>{0, 1, 2, 2, 3, 0};
            auto&& device = *pdevice;
            VertexBuffers buffers;
            buffers.position_buffer = device->createBuffer(nvrhi::BufferDesc()
                                                               .setByteSize(sizeof(pos))
                                                               .setIsVertexBuffer(true)
                                                               .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                                               .setKeepInitialState(true)
                                                               .setDebugName("Sprite Position Buffer"));
            buffers.texcoord_buffer = device->createBuffer(nvrhi::BufferDesc()
                                                               .setByteSize(sizeof(texcoord))
                                                               .setIsVertexBuffer(true)
                                                               .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                                               .setKeepInitialState(true)
                                                               .setDebugName("Sprite Texcoord Buffer"));
            buffers.index_buffer    = device->createBuffer(nvrhi::BufferDesc()
                                                               .setByteSize(sizeof(indices))
                                                               .setIsIndexBuffer(true)
                                                               .setInitialState(nvrhi::ResourceStates::IndexBuffer)
                                                               .setKeepInitialState(true)
                                                               .setDebugName("Sprite Index Buffer"));
            auto cmd_list =
                device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
            cmd_list->open();
            cmd_list->writeBuffer(buffers.position_buffer, pos.data(), sizeof(pos));
            cmd_list->writeBuffer(buffers.texcoord_buffer, texcoord.data(), sizeof(texcoord));
            cmd_list->writeBuffer(buffers.index_buffer, indices.data(), sizeof(indices));
            cmd_list->close();
            device->executeCommandList(cmd_list);
            return buffers;
        }
        return std::nullopt;
    }
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
    void upload(nvrhi::DeviceHandle device, nvrhi::CommandListHandle cmd_list) {
        if (data.empty()) return;
        if (!buffer || buffer->getDesc().byteSize < data.size() * sizeof(SpriteInstanceData) ||
            buffer->getDesc().byteSize > data.size() * sizeof(SpriteInstanceData) * 2) {
            // new buffer if no buffer, or too small, or too large
            // buffer size should be power of two
            size_t new_size = 1;
            while (new_size < data.size() * sizeof(SpriteInstanceData)) new_size <<= 1;
            buffer = device->createBuffer(nvrhi::BufferDesc()
                                              .setByteSize(new_size)
                                              .setCanHaveRawViews(true)
                                              .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                              .setKeepInitialState(true)
                                              .setDebugName("Sprite Instance Buffer"));
        }
        cmd_list->writeBuffer(buffer, data.data(), data.size() * sizeof(SpriteInstanceData));
    }
    void upload(nvrhi::DeviceHandle device) {
        auto cmd_list = device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
        cmd_list->open();
        upload(device, cmd_list);
        cmd_list->close();
        device->executeCommandList(cmd_list);
    }
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
            spdlog::error("[sprite render] Entity {} has no SpriteBatch, skipping. at\n\t", item.entity().index(),
                          std::source_location::current().function_name());
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
            spdlog::error("[sprite render] Entity {} has no SpriteBatch, skipping. at\n\t", item.entity().index(),
                          std::source_location::current().function_name());
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
    void build(App& app) {
        app.insert_resource(DefaultSampler{
            .desc = nvrhi::SamplerDesc().setMagFilter(false),
        });
        if (auto render_app = app.get_sub_app(render::Render)) {
            render_app->insert_resource(DefaultSampler{});
            render_app->add_systems(
                render::ExtractSchedule,
                into([](Res<nvrhi::DeviceHandle> device, ResMut<DefaultSampler> gpu_sampler,
                        Extract<Res<DefaultSampler>> sampler) {
                    if (!gpu_sampler->handle || !desc_equal(gpu_sampler->handle->getDesc(), sampler->desc)) {
                        gpu_sampler->handle = device.get()->createSampler(sampler->desc);
                    }
                }).set_names({"extract and update default sampler"}));
        }
    }
};

inline void extract_sprites(
    Commands cmd,
    Extract<Query<Item<Entity, Sprite, transform::GlobalTransform, assets::Handle<image::Image>>>> sprites) {
    for (auto&& [entity, sprite, transform, texture] : sprites.iter()) {
        if (!texture) {
            spdlog::error("[sprite extract] Entity {} has invalid texture handle, skipping.", entity.index());
            continue;
        }
        cmd.spawn(
            ExtractedSprite{
                .sprite    = sprite,
                .transform = transform,
                .texture   = texture.id(),
            },
            SpriteBatch{});
    }
}
inline void queue_sprites(Query<Item<Mut<render::render_phase::RenderPhase<render::core_2d::Transparent2D>>>,
                                With<render::camera::ExtractedCamera, render::view::ViewTarget>> views,
                          Res<SpritePipeline> pipeline,
                          ResMut<render::render_phase::DrawFunctions<render::core_2d::Transparent2D>> draw_functions,
                          Query<Item<Entity, ExtractedSprite>> extracted_sprites) {
    for (auto&& [phase] : views.iter()) {
        for (auto&& [entity, sprite] : extracted_sprites.iter()) {
            phase.add(render::core_2d::Transparent2D{
                .id          = entity,
                .depth       = sprite.transform.matrix[3][2],  // use the z position as depth
                .pipeline_id = pipeline->pipeline_id,
                .draw_func   = render::render_phase::get_or_add_render_commands<
                      render::core_2d::Transparent2D, render::render_phase::SetItemPipeline, BindResourceCommand,
                      DrawSpriteBatchCommand>(*draw_functions),
            });
        }
    }
}
inline void create_uniform_for_view(
    Commands cmd,
    Query<Item<Entity, render::view::ExtractedView, render::camera::ExtractedCamera>> views,
    ResMut<ViewUniformCache> uniform_cache,
    Res<nvrhi::DeviceHandle> device) {
    auto cmd_list = device.get()->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
    cmd_list->open();
    for (auto&& [entity, view, camera] : views.iter()) {
        if (camera.render_graph != render::camera::CameraRenderGraph(render::core_2d::Core2d)) {
            continue;
        }

        // create a uniform buffer for the view
        nvrhi::BufferHandle view_buffer;
        if (uniform_cache->cache.empty()) {
            view_buffer = device.get()->createBuffer(nvrhi::BufferDesc()
                                                         .setByteSize(sizeof(glm::mat4) * 2)
                                                         .setIsConstantBuffer(true)
                                                         .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
                                                         .setKeepInitialState(true)
                                                         .setDebugName("Sprite View Uniform"));
        } else {
            view_buffer = uniform_cache->cache.back().view_buffer;
            uniform_cache->cache.pop_back();
        }

        // upload data
        std::array<glm::mat4, 2> matrices = {
            view.projection,
            glm::inverse(view.transform.matrix),
        };
        cmd_list->writeBuffer(view_buffer, matrices.data(), sizeof(matrices));

        // insert the uniform into the view entity
        cmd.entity(entity).emplace(ViewUniform{.view_buffer = view_buffer});
    }
    cmd_list->close();
    // {
    //     // print execute submit time
    //     auto now = std::chrono::high_resolution_clock::now();
    //     spdlog::info("[sprite] Submit view uniform upload at {}ms",
    //                  std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    // }
    device.get()->executeCommandList(cmd_list);

    // clean up unused view uniforms
    uniform_cache->cache.clear();
}

inline void prepare_sprites(Query<Item<Mut<render::render_phase::RenderPhase<render::core_2d::Transparent2D>>>,
                                  With<render::camera::ExtractedCamera, render::view::ExtractedView>> views,
                            Query<Item<Mut<SpriteBatch>, ExtractedSprite>> batches,
                            Res<SpritePipeline> pipeline,
                            ResMut<SpriteInstanceBuffer> instance_buffer,
                            Res<render::assets::RenderAssets<image::Image>> images,
                            Res<nvrhi::DeviceHandle> device,
                            Res<DefaultSampler> default_sampler,
                            ResMut<VertexBuffers> vertex_buffers) {
    instance_buffer->clear();

    for (auto&& [phase] : views.iter()) {
        size_t batch_size                         = 0;
        size_t batch_item                         = 0;
        assets::AssetId<image::Image> batch_image = assets::AssetId<image::Image>::invalid();

        for (auto&& [item_index, item] : phase.items | std::views::enumerate) {
            if (!batches.contains(item.entity())) {
                // not a sprite, reset batch image since this item cannot be batched sequentially and skip
                batch_image = assets::AssetId<image::Image>::invalid();
                continue;
            }

            auto&& [batch_data, sprite] = batches.get(item.entity());
            bool batch_image_changed    = batch_image != sprite.texture;
            glm::vec2 image_size;

            // batch image changed, start a new batch if possible
            if (batch_image_changed) {
                if (auto gpu_image = images->try_get(sprite.texture); gpu_image && *gpu_image) {
                    // begin new batch
                    batch_item                = item_index;
                    item.batch_count          = 0;
                    batch_data.instance_start = (uint32_t)instance_buffer->size();
                    batch_data.binding_set    = device.get()->createBindingSet(
                        nvrhi::BindingSetDesc()
                            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, *gpu_image))
                            .addItem(nvrhi::BindingSetItem::Sampler(1, default_sampler->handle)),
                        pipeline->image_layout);
                    image_size = glm::vec2((*gpu_image)->getDesc().width, (*gpu_image)->getDesc().height);
                } else {
                    spdlog::warn(
                        "[sprite prepare] Sprite with id {} does not have a valid GPU image. Loading or failed to "
                        "load.",
                        item.entity().index());
                    continue;
                }
            }

            // compute instance data and push to instance buffer.
            SpriteInstanceData instance;
            instance.model = sprite.transform.matrix;
            instance.color = sprite.sprite.color;

            // uv offset and scale
            glm::vec4 uv_rect = sprite.sprite.uv_rect.value_or(glm::vec4(0.f, 0.f, 1.f, 1.f));
            if (sprite.sprite.flip_x) std::swap(uv_rect.x, uv_rect.z);
            if (sprite.sprite.flip_y) std::swap(uv_rect.y, uv_rect.w);
            instance.uv_offset_scale = glm::vec4(uv_rect.x, uv_rect.y, uv_rect.z - uv_rect.x, uv_rect.w - uv_rect.y);

            // position offset and scale
            glm::vec2 size = sprite.sprite.size.value_or(glm::vec2(image_size.x * std::abs(uv_rect.z - uv_rect.x),
                                                                   image_size.y * std::abs(uv_rect.w - uv_rect.y)));
            instance.pos_offset_scale = glm::vec4(-sprite.sprite.anchor.x, -sprite.sprite.anchor.y, size.x, size.y);

            instance_buffer->push(instance);

            // add this item to the current batch
            phase.items[batch_item].batch_count++;
        }
    }

    // upload instance buffer

    instance_buffer->upload(device.get());
}

struct SpritePlugin {
    void build(App& app) {
        app.add_plugins(DefaultSamplerPlugin{});
        app.add_plugins(SpriteShadersPlugin{});
        if (auto render_app = app.get_sub_app(render::Render)) {
            render_app->init_resource<SpritePipeline>()
                .init_resource<VertexBuffers>()
                .insert_resource(SpriteInstanceBuffer{})
                .insert_resource(ViewUniformCache{})
                .add_systems(render::ExtractSchedule, into(extract_sprites).set_names({"extract sprites"}))
                .add_systems(render::Render,
                             into(into(queue_sprites).in_set(render::RenderSet::Queue),
                                  into(create_uniform_for_view).in_set(render::RenderSet::PrepareResources),
                                  into(prepare_sprites).in_set(render::RenderSet::Prepare))
                                 .set_names({"queue sprites", "create sprite view uniforms", "prepare sprites"}));
        }
    }
};
}  // namespace epix::sprite