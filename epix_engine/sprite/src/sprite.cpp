#include <epix/sprite.hpp>

namespace epix::sprite::shaders {
#include "shaders/shader.frag.h"
#include "shaders/shader.vert.h"
}  // namespace epix::sprite::shaders

using namespace epix;
using namespace epix::sprite;

static assets::Handle<render::Shader> sprite_vertex_shader =
    assets::AssetId<render::Shader>(uuids::uuid::from_string("1a75cef9-87bb-4325-a50f-a81f96fe414c").value());
static assets::Handle<render::Shader> sprite_fragment_shader =
    assets::AssetId<render::Shader>(uuids::uuid::from_string("a799ba44-0bb4-4dd9-98b1-e70deea84e1f").value());

SpritePipeline SpritePipeline::from_world(World& world) {
    if (auto pipeline_server_opt = world.get_resource_mut<render::PipelineServer>()) {
        auto&& pipeline_server = pipeline_server_opt->get();
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
                .setBlendState(nvrhi::BlendState().setRenderTarget(0, nvrhi::BlendState::RenderTarget()
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
        pipeline.pipeline_id = pipeline_server.queue_render_pipeline(pipeline_desc);
        return pipeline;
    } else {
        throw std::runtime_error("SpritePipeline::from_world: PipelineServer resource not found in world");
    }
}

void SpriteShadersPlugin::build(App& app) {
    app.add_systems(
        Startup, into([](ResMut<assets::Assets<render::Shader>> shaders) {
                     auto res = shaders->insert(
                         sprite_vertex_shader,
                         render::Shader::spirv(std::vector<uint32_t>(
                             sprite::shaders::sprite_vert,
                             sprite::shaders::sprite_vert + sizeof(sprite::shaders::sprite_vert) / sizeof(uint32_t))));
                     res = shaders->insert(
                         sprite_fragment_shader,
                         render::Shader::spirv(std::vector<uint32_t>(
                             sprite::shaders::sprite_frag,
                             sprite::shaders::sprite_frag + sizeof(sprite::shaders::sprite_frag) / sizeof(uint32_t))));
                 }).set_name("insert sprite shaders"));
}

VertexBuffers VertexBuffers::from_world(World& world) {
    if (auto pdevice = world.get_resource<nvrhi::DeviceHandle>()) {
        auto device = pdevice->get();
        auto pos    = std::array{
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
        auto indices = std::array<uint16_t, 6>{0, 1, 2, 2, 3, 0};
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
        auto cmd_list = device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
        cmd_list->open();
        cmd_list->writeBuffer(buffers.position_buffer, pos.data(), sizeof(pos));
        cmd_list->writeBuffer(buffers.texcoord_buffer, texcoord.data(), sizeof(texcoord));
        cmd_list->writeBuffer(buffers.index_buffer, indices.data(), sizeof(indices));
        cmd_list->close();
        device->executeCommandList(cmd_list);
        return buffers;
    } else {
        throw std::runtime_error("VertexBuffers::from_world: nvrhi::DeviceHandle resource not found in world");
    }
}

void SpriteInstanceBuffer::upload(nvrhi::DeviceHandle device, nvrhi::CommandListHandle cmd_list) {
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

void SpriteInstanceBuffer::upload(nvrhi::DeviceHandle device) {
    auto cmd_list = device->createCommandList(nvrhi::CommandListParameters().setEnableImmediateExecution(false));
    cmd_list->open();
    upload(device, cmd_list);
    cmd_list->close();
    device->executeCommandList(cmd_list);
}

void DefaultSamplerPlugin::finish(App& app) {
    app.world_mut().insert_resource(DefaultSampler{
        .desc = nvrhi::SamplerDesc().setMagFilter(false),
    });
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().insert_resource(DefaultSampler{});
        render_app->get().add_systems(
            render::ExtractSchedule,
            into([](Res<nvrhi::DeviceHandle> device, ResMut<DefaultSampler> gpu_sampler,
                    Extract<Res<DefaultSampler>> sampler) {
                if (!gpu_sampler->handle || !desc_equal(gpu_sampler->handle->getDesc(), sampler->desc)) {
                    gpu_sampler->handle = device.get()->createSampler(sampler->desc);
                }
            }).set_name("extract and update default sampler"));
    }
}

void sprite::extract_sprites(
    Commands cmd,
    Extract<Query<Item<Entity, const Sprite&, const transform::GlobalTransform&, const assets::Handle<image::Image>&>,
                  Without<render::CustomRendered>>> sprites) {
    for (auto&& [entity, sprite, transform, texture] : sprites.iter()) {
        cmd.spawn(
            ExtractedSprite{
                .sprite    = sprite,
                .transform = transform,
                .texture   = texture.id(),
            },
            SpriteBatch{});
    }
}

void sprite::queue_sprites(Query<Item<render::render_phase::RenderPhase<render::core_2d::Transparent2D>&>,
                                 With<render::camera::ExtractedCamera, render::view::ViewTarget>> views,
                           Res<SpritePipeline> pipeline,
                           ResMut<render::render_phase::DrawFunctions<render::core_2d::Transparent2D>> draw_functions,
                           Query<Item<Entity, const ExtractedSprite&>> extracted_sprites) {
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

void sprite::create_uniform_for_view(
    Commands cmd,
    Query<Item<Entity, const render::view::ExtractedView&, const render::camera::ExtractedCamera&>> views,
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
        cmd.entity(entity).insert(ViewUniform{.view_buffer = view_buffer});
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

void sprite::prepare_sprites(Query<Item<render::render_phase::RenderPhase<render::core_2d::Transparent2D>&>,
                                   With<render::camera::ExtractedCamera, render::view::ExtractedView>> views,
                             Query<Item<SpriteBatch&, const ExtractedSprite&>> batches,
                             Res<SpritePipeline> pipeline,
                             ResMut<SpriteInstanceBuffer> instance_buffer,
                             Res<render::assets::RenderAssets<image::Image>> images,
                             Res<nvrhi::DeviceHandle> device,
                             Res<DefaultSampler> default_sampler,
                             ResMut<VertexBuffers> vertex_buffers) {
    instance_buffer->clear();

    std::unordered_map<nvrhi::TextureHandle, nvrhi::BindingSetHandle> binding_set_cache;

    for (auto&& [phase] : views.iter()) {
        size_t batch_size = 0;
        size_t batch_item = 0;
        glm::vec2 image_size;
        assets::AssetId<image::Image> batch_image = assets::AssetId<image::Image>::invalid();

        for (auto&& [item_index, item] : phase.items | std::views::enumerate) {
            if (!batches.contains(item.entity()) || item.pipeline() != pipeline->pipeline_id) {
                // not a sprite drawn by standard render process, reset batch image and continue to trigger a new batch
                batch_image = assets::AssetId<image::Image>::invalid();
                continue;
            }

            auto [batch_data, sprite] = batches.get(item.entity()).value();
            bool batch_image_changed  = batch_image != sprite.texture;

            // batch image changed, start a new batch if possible
            if (batch_image_changed) {
                if (auto gpu_image = images->try_get(sprite.texture); gpu_image && *gpu_image) {
                    // begin new batch
                    batch_item                = item_index;
                    item.batch_count          = 0;
                    batch_data.instance_start = (uint32_t)instance_buffer->size();
                    // check binding set cache
                    if (auto it = binding_set_cache.find((*gpu_image)); it != binding_set_cache.end()) {
                        batch_data.binding_set = it->second;
                    } else {
                        nvrhi::BindingSetDesc desc =
                            nvrhi::BindingSetDesc()
                                .addItem(nvrhi::BindingSetItem::Texture_SRV(0, *gpu_image))
                                .addItem(nvrhi::BindingSetItem::Sampler(1, default_sampler->handle));
                        batch_data.binding_set          = device.get()->createBindingSet(desc, pipeline->image_layout);
                        binding_set_cache[(*gpu_image)] = batch_data.binding_set;
                    }
                    image_size  = glm::vec2((*gpu_image)->getDesc().width, (*gpu_image)->getDesc().height);
                    batch_image = sprite.texture;
                } else {
                    spdlog::warn(
                        "[sprite prepare] Sprite with id {} does not have a valid GPU image. Loading or failed to "
                        "load.",
                        item.entity().index);
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

void SpritePlugin::build(App& app) {
    app.add_plugins(DefaultSamplerPlugin{});
    app.add_plugins(SpriteShadersPlugin{});
}
void SpritePlugin::finish(App& app) {
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<SpritePipeline>();
        render_app->get().world_mut().init_resource<VertexBuffers>();
        render_app->get().world_mut().insert_resource(SpriteInstanceBuffer{});
        render_app->get().world_mut().insert_resource(ViewUniformCache{});
        render_app->get()
            .add_systems(render::ExtractSchedule, into(extract_sprites).set_name("extract sprites"))
            .add_systems(render::Render,
                         into(into(queue_sprites).in_set(render::RenderSet::Queue),
                              into(create_uniform_for_view).in_set(render::RenderSet::PrepareResources),
                              into(prepare_sprites).in_set(render::RenderSet::Prepare))
                             .set_names(std::array{"queue sprites", "create sprite view uniforms", "prepare sprites"}));
    }
}