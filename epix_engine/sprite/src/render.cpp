module;

#include <spdlog/spdlog.h>

module epix.sprite;

import epix.assets;
import epix.core_graph;
import epix.image;
import epix.render;
import epix.transform;
import std;

using namespace epix;
using namespace epix::core;
using namespace epix::sprite;

namespace {
constexpr std::string_view kSpriteVertexShader = R"(
import epix.view;

struct SpriteInstance {
    float4x4 model;
    float4 uv_offset_scale;
    float4 color;
    float4 pos_offset_scale;
};

[[vk::binding(0, 0)]] ConstantBuffer<epix::View> view_uniform;
[[vk::binding(0, 1)]] StructuredBuffer<SpriteInstance> sprite_instances;

struct VertexInput {
    [[vk::location(0)]] float2 position;
    [[vk::location(1)]] float2 uv;
    uint instance_index : SV_VulkanInstanceID;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
    [[vk::location(1)]] float4 color;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
    SpriteInstance instance = sprite_instances[input.instance_index];
    float4 local_position = float4((input.position + instance.pos_offset_scale.xy) * instance.pos_offset_scale.zw,
                                   0.0,
                                   1.0);

    VertexOutput output;
    output.position = mul(view_uniform.projection, mul(view_uniform.view, mul(instance.model, local_position)));
    output.uv = input.uv * instance.uv_offset_scale.zw + instance.uv_offset_scale.xy;
    output.color = instance.color;
    return output;
}
)";

constexpr std::string_view kSpriteFragmentShader = R"(
[[vk::binding(0, 2)]] SamplerState sprite_sampler;
[[vk::binding(1, 2)]] Texture2D<float4> sprite_texture;

struct FragmentInput {
    [[vk::location(0)]] float2 uv;
    [[vk::location(1)]] float4 color;
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target {
    return sprite_texture.Sample(sprite_sampler, input.uv) * input.color;
}
)";

const assets::AssetId<shader::Shader> kSpriteVertexShaderId(
    uuids::uuid::from_string("89282c3c-0eaa-4f75-bf12-5b7f78c763f0").value());
const assets::AssetId<shader::Shader> kSpriteFragmentShaderId(
    uuids::uuid::from_string("63e69646-4b35-4eb8-b095-78ef59bd3ea3").value());

struct SpritePipelineCache {
    wgpu::BindGroupLayout view_layout;
    wgpu::BindGroupLayout instance_layout;
    wgpu::BindGroupLayout texture_layout;
    assets::Handle<shader::Shader> vertex_shader{kSpriteVertexShaderId};
    assets::Handle<shader::Shader> fragment_shader{kSpriteFragmentShaderId};
    std::unordered_map<std::uint32_t, render::CachedPipelineId> pipelines;

    explicit SpritePipelineCache(World& world)
        : view_layout(world.resource<render::view::ViewUniformBindingLayout>().layout),
          instance_layout(world.resource<wgpu::Device>().createBindGroupLayout(
              wgpu::BindGroupLayoutDescriptor()
                  .setLabel("SpriteInstanceLayout")
                  .setEntries(std::array{
                      wgpu::BindGroupLayoutEntry()
                          .setBinding(0)
                          .setVisibility(wgpu::ShaderStage::eVertex)
                          .setBuffer(wgpu::BufferBindingLayout()
                                         .setType(wgpu::BufferBindingType::eReadOnlyStorage)
                                         .setHasDynamicOffset(false)
                                         .setMinBindingSize(sizeof(SpriteInstanceData))),
                  }))),
          texture_layout(world.resource<wgpu::Device>().createBindGroupLayout(
              wgpu::BindGroupLayoutDescriptor()
                  .setLabel("SpriteTextureLayout")
                  .setEntries(std::array{
                      wgpu::BindGroupLayoutEntry()
                          .setBinding(0)
                          .setVisibility(wgpu::ShaderStage::eFragment)
                          .setSampler(wgpu::SamplerBindingLayout().setType(wgpu::SamplerBindingType::eFiltering)),
                      wgpu::BindGroupLayoutEntry()
                          .setBinding(1)
                          .setVisibility(wgpu::ShaderStage::eFragment)
                          .setTexture(wgpu::TextureBindingLayout()
                                          .setSampleType(wgpu::TextureSampleType::eFloat)
                                          .setViewDimension(wgpu::TextureViewDimension::e2D)
                                          .setMultisampled(false)),
                  }))) {}

    std::optional<render::CachedPipelineId> specialize(render::PipelineServer& pipeline_server,
                                                       wgpu::TextureFormat color_format) {
        auto key = static_cast<std::uint32_t>(color_format);
        if (auto it = pipelines.find(key); it != pipelines.end()) {
            return it->second;
        }

        std::vector<wgpu::VertexBufferLayout> vertex_buffers;
        vertex_buffers.reserve(2);
        vertex_buffers.push_back(
            wgpu::VertexBufferLayout()
                .setArrayStride(sizeof(glm::vec2))
                .setStepMode(wgpu::VertexStepMode::eVertex)
                .setAttributes(std::array{
                    wgpu::VertexAttribute().setShaderLocation(0).setFormat(wgpu::VertexFormat::eFloat32x2).setOffset(0),
                }));
        vertex_buffers.push_back(
            wgpu::VertexBufferLayout()
                .setArrayStride(sizeof(glm::vec2))
                .setStepMode(wgpu::VertexStepMode::eVertex)
                .setAttributes(std::array{
                    wgpu::VertexAttribute().setShaderLocation(1).setFormat(wgpu::VertexFormat::eFloat32x2).setOffset(0),
                }));

        render::VertexState vertex_state{.shader = vertex_shader};
        vertex_state.set_buffers(vertex_buffers);

        auto color_blend = wgpu::BlendComponent()
                               .setOperation(wgpu::BlendOperation::eAdd)
                               .setSrcFactor(wgpu::BlendFactor::eSrcAlpha)
                               .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha);
        auto alpha_blend = wgpu::BlendComponent()
                               .setOperation(wgpu::BlendOperation::eAdd)
                               .setSrcFactor(wgpu::BlendFactor::eOne)
                               .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha);

        render::FragmentState fragment_state{.shader = fragment_shader};
        fragment_state.add_target(wgpu::ColorTargetState()
                                      .setFormat(color_format)
                                      .setWriteMask(wgpu::ColorWriteMask::eAll)
                                      .setBlend(wgpu::BlendState().setColor(color_blend).setAlpha(alpha_blend)));

        render::RenderPipelineDescriptor pipeline_desc{
            .label         = std::format("sprite-{}", wgpu::to_string(color_format)),
            .layouts       = std::vector<wgpu::BindGroupLayout>{view_layout, instance_layout, texture_layout},
            .vertex        = std::move(vertex_state),
            .primitive     = wgpu::PrimitiveState()
                                 .setTopology(wgpu::PrimitiveTopology::eTriangleList)
                                 .setFrontFace(wgpu::FrontFace::eCCW)
                                 .setCullMode(wgpu::CullMode::eNone),
            .depth_stencil = wgpu::DepthStencilState()
                                 .setFormat(wgpu::TextureFormat::eDepth32Float)
                                 .setDepthWriteEnabled(wgpu::OptionalBool::eFalse)
                                 .setDepthCompare(wgpu::CompareFunction::eLessEqual),
            .multisample   = wgpu::MultisampleState().setCount(1).setMask(~0u).setAlphaToCoverageEnabled(false),
            .fragment      = std::move(fragment_state),
        };

        auto pipeline_id = pipeline_server.queue_render_pipeline(std::move(pipeline_desc));
        pipelines.emplace(key, pipeline_id);
        return pipeline_id;
    }
};

struct TransparentSpriteDrawFunction {
    render::phase::DrawFunctionId value;
};

void insert_sprite_shaders(assets::Assets<shader::Shader>& shaders) {
    auto vertex_path             = std::filesystem::path("embedded://sprite/sprite_vertex.slang");
    auto fragment_path           = std::filesystem::path("embedded://sprite/sprite_fragment.slang");
    [[maybe_unused]] auto vertex = shaders.insert(
        kSpriteVertexShaderId, shader::Shader::from_slang(std::string(kSpriteVertexShader), vertex_path.string()));
    [[maybe_unused]] auto fragment =
        shaders.insert(kSpriteFragmentShaderId,
                       shader::Shader::from_slang(std::string(kSpriteFragmentShader), fragment_path.string()));
}

void ensure_instance_buffer(SpriteInstanceBuffer& instance_buffer,
                            const SpritePipelineCache& pipeline_cache,
                            const wgpu::Device& device,
                            std::size_t required_bytes) {
    if (required_bytes == 0) {
        return;
    }

    std::size_t current_size = instance_buffer.buffer ? instance_buffer.buffer.getSize() : 0;
    if (required_bytes > current_size) {
        std::size_t buffer_size = std::bit_ceil(required_bytes);
        instance_buffer.buffer =
            device.createBuffer(wgpu::BufferDescriptor()
                                    .setLabel("SpriteInstanceBuffer")
                                    .setSize(buffer_size)
                                    .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));
        instance_buffer.bind_group = device.createBindGroup(wgpu::BindGroupDescriptor()
                                                                .setLabel("SpriteInstanceBindGroup")
                                                                .setLayout(pipeline_cache.instance_layout)
                                                                .setEntries(std::array{
                                                                    wgpu::BindGroupEntry()
                                                                        .setBinding(0)
                                                                        .setBuffer(instance_buffer.buffer)
                                                                        .setOffset(0)
                                                                        .setSize(buffer_size),
                                                                }));
    }
}

SpriteInstanceData make_instance_data(const ExtractedSprite& sprite) {
    auto uv_rect = sprite.sprite.uv_rect.value_or(glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
    if (sprite.sprite.flip_x) {
        std::swap(uv_rect.x, uv_rect.z);
    }
    if (sprite.sprite.flip_y) {
        std::swap(uv_rect.y, uv_rect.w);
    }

    auto uv_size = glm::vec2(std::abs(uv_rect.z - uv_rect.x), std::abs(uv_rect.w - uv_rect.y));
    auto size    = sprite.sprite.size.value_or(sprite.image_size * uv_size);

    return SpriteInstanceData{
        .model            = sprite.model,
        .uv_offset_scale  = glm::vec4(uv_rect.x, uv_rect.y, uv_rect.z - uv_rect.x, uv_rect.w - uv_rect.y),
        .color            = sprite.sprite.color,
        .pos_offset_scale = glm::vec4(-sprite.sprite.anchor.x, -sprite.sprite.anchor.y, size.x, size.y),
    };
}

bool sprite_may_be_visible(const ExtractedSprite& sprite, const render::view::ExtractedView& view) {
    auto uv_rect = sprite.sprite.uv_rect.value_or(glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
    auto uv_size = glm::vec2(std::abs(uv_rect.z - uv_rect.x), std::abs(uv_rect.w - uv_rect.y));
    auto size    = sprite.sprite.size.value_or(sprite.image_size * uv_size);

    glm::vec2 min_corner     = (-glm::vec2(0.5f) - sprite.sprite.anchor) * size;
    glm::vec2 max_corner     = (glm::vec2(0.5f) - sprite.sprite.anchor) * size;
    std::array local_corners = {
        glm::vec4(min_corner.x, min_corner.y, 0.0f, 1.0f),
        glm::vec4(max_corner.x, min_corner.y, 0.0f, 1.0f),
        glm::vec4(max_corner.x, max_corner.y, 0.0f, 1.0f),
        glm::vec4(min_corner.x, max_corner.y, 0.0f, 1.0f),
    };

    glm::mat4 view_projection = view.projection * glm::inverse(view.transform.matrix);
    glm::mat4 mvp             = view_projection * sprite.model;

    bool outside_left   = true;
    bool outside_right  = true;
    bool outside_bottom = true;
    bool outside_top    = true;
    bool outside_near   = true;
    bool outside_far    = true;

    for (const auto& corner : local_corners) {
        glm::vec4 clip = mvp * corner;
        outside_left   = outside_left && (clip.x < -clip.w);
        outside_right  = outside_right && (clip.x > clip.w);
        outside_bottom = outside_bottom && (clip.y < -clip.w);
        outside_top    = outside_top && (clip.y > clip.w);
        outside_near   = outside_near && (clip.z < 0.0f);
        outside_far    = outside_far && (clip.z > clip.w);
    }

    return !(outside_left || outside_right || outside_bottom || outside_top || outside_near || outside_far);
}

void extract_sprites(
    Commands cmd,
    Extract<Query<Item<Entity, const Sprite&, const transform::GlobalTransform&, const assets::Handle<image::Image>&>,
                  Without<render::CustomRendered>>> sprites,
    Extract<Res<assets::Assets<image::Image>>> images) {
    for (auto&& [entity, sprite, global_transform, texture] : sprites.iter()) {
        glm::vec2 image_size = glm::vec2(1.0f, 1.0f);
        if (auto image = images->get(texture.id()); image) {
            image_size = glm::vec2(static_cast<float>(image->get().width()), static_cast<float>(image->get().height()));
        }

        cmd.spawn(
            ExtractedSprite{
                .source_entity = entity,
                .sprite        = sprite,
                .model         = global_transform.matrix,
                .depth         = global_transform.matrix[3][2],
                .texture       = texture.id(),
                .image_size    = image_size,
            },
            SpriteBatch{});
    }
}

void queue_sprites_2d(Query<Item<render::phase::RenderPhase<core_graph::core_2d::Transparent2D>&,
                                 const render::view::ExtractedView&,
                                 const render::view::ViewTarget&>,
                            With<render::camera::ExtractedCamera>> views,
                      Query<Item<Entity, const ExtractedSprite&>> sprites,
                      Res<render::RenderAssets<image::Image>> images,
                      Res<TransparentSpriteDrawFunction> draw_function_id,
                      ResMut<SpritePipelineCache> pipeline_cache,
                      ResMut<render::PipelineServer> pipeline_server) {
    for (auto&& [phase, view, target] : views.iter()) {
        auto pipeline_id = pipeline_cache->specialize(*pipeline_server, target.format);
        if (!pipeline_id) {
            spdlog::warn("[sprite] Failed to specialize sprite pipeline for target format {}.",
                         wgpu::to_string(target.format));
            continue;
        }

        for (auto&& [entity, sprite] : sprites.iter()) {
            if (!images->try_get(sprite.texture)) {
                continue;
            }
            if (!sprite_may_be_visible(sprite, view)) {
                continue;
            }

            phase.add(core_graph::core_2d::Transparent2D{
                .id          = entity,
                .depth       = sprite.depth,
                .pipeline_id = *pipeline_id,
                .draw_func   = draw_function_id->value,
                .batch_count = 1,
            });
        }
    }
}

void prepare_sprite_batches(Query<Item<render::phase::RenderPhase<core_graph::core_2d::Transparent2D>&>,
                                  With<render::camera::ExtractedCamera, render::view::ExtractedView>> views,
                            Query<Item<SpriteBatch&, const ExtractedSprite&>> sprites,
                            Res<render::RenderAssets<image::Image>> images,
                            Res<wgpu::Device> device,
                            Res<wgpu::Queue> queue,
                            Res<SpritePipelineCache> pipeline_cache,
                            ResMut<SpriteInstanceBuffer> instance_buffer) {
    instance_buffer->instances.clear();
    std::unordered_map<assets::AssetId<image::Image>, wgpu::BindGroup> texture_bind_group_cache;

    for (auto&& [phase] : views.iter()) {
        std::optional<assets::AssetId<image::Image>> current_texture;
        std::size_t batch_head = std::numeric_limits<std::size_t>::max();

        for (std::size_t item_index = 0; item_index < phase.items.size(); ++item_index) {
            auto& item       = phase.items[item_index];
            auto sprite_item = sprites.get(item.entity());
            if (!sprite_item) {
                current_texture.reset();
                batch_head = std::numeric_limits<std::size_t>::max();
                continue;
            }

            auto&& [batch, sprite] = *sprite_item;
            auto gpu_image         = images->try_get(sprite.texture);
            if (!gpu_image) {
                current_texture.reset();
                batch_head = std::numeric_limits<std::size_t>::max();
                continue;
            }

            if (!current_texture || *current_texture != sprite.texture) {
                batch_head                          = item_index;
                phase.items[batch_head].batch_count = 0;
                batch.instance_start                = static_cast<std::uint32_t>(instance_buffer->instances.size());
                if (auto it = texture_bind_group_cache.find(sprite.texture); it != texture_bind_group_cache.end()) {
                    batch.texture_bind_group = it->second;
                } else {
                    batch.texture_bind_group = device->createBindGroup(
                        wgpu::BindGroupDescriptor()
                            .setLabel("SpriteTextureBindGroup")
                            .setLayout(pipeline_cache->texture_layout)
                            .setEntries(std::array{
                                wgpu::BindGroupEntry().setBinding(0).setSampler(gpu_image->sampler),
                                wgpu::BindGroupEntry().setBinding(1).setTextureView(gpu_image->view),
                            }));
                    texture_bind_group_cache.emplace(sprite.texture, batch.texture_bind_group);
                }
                current_texture = sprite.texture;
            }

            instance_buffer->instances.push_back(make_instance_data(sprite));
            phase.items[batch_head].batch_count++;
        }
    }

    auto required_bytes = instance_buffer->instances.size() * sizeof(SpriteInstanceData);
    ensure_instance_buffer(*instance_buffer, *pipeline_cache, *device, required_bytes);
    if (required_bytes != 0) {
        queue->writeBuffer(instance_buffer->buffer, 0, instance_buffer->instances.data(), required_bytes);
    }
}
}  // namespace

void SpritePlugin::build(core::App& app) {
    spdlog::debug("[sprite] Building SpritePlugin.");
    app.add_plugins(core_graph::core_2d::Core2dPlugin{});
}

void SpritePlugin::finish(core::App& app) {
    spdlog::debug("[sprite] Finishing SpritePlugin.");
    auto shaders = app.world_mut().get_resource_mut<assets::Assets<shader::Shader>>();
    if (shaders) {
        insert_sprite_shaders(shaders->get());
    } else {
        spdlog::warn("[sprite] Assets<shader::Shader> is not available in the main world.");
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app) {
        spdlog::error("[sprite] SpritePlugin requires render::RenderPlugin to be added before it.");
        return;
    }

    auto& world = render_app->get().world_mut();
    if (!world.get_resource<SpriteGeometryBuffers>()) {
        world.insert_resource(SpriteGeometryBuffers(world));
    }
    if (!world.get_resource<SpriteInstanceBuffer>()) {
        world.insert_resource(SpriteInstanceBuffer{});
    }
    if (!world.get_resource<SpritePipelineCache>()) {
        world.insert_resource(SpritePipelineCache(world));
    }
    auto& render_subapp = render_app->get();
    world.insert_resource(TransparentSpriteDrawFunction{
        .value = render::phase::app_add_render_commands<
            core_graph::core_2d::Transparent2D, render::phase::SetItemPipeline,
            render::view::BindViewUniform<0>::Command, sprite::BindSpriteInstances<1>::Command,
            sprite::BindSpriteTexture<2>::Command, sprite::DrawSpriteBatch>(render_subapp)});

    render_subapp.add_systems(render::ExtractSchedule, into(extract_sprites).set_name("extract sprites"))
        .add_systems(render::Render, into(queue_sprites_2d).in_set(render::RenderSet::Queue).set_name("queue sprites"))
        .add_systems(render::Render, into(prepare_sprite_batches)
                                         .in_set(render::RenderSet::PrepareResources)
                                         .set_name("prepare sprite batches"));
}
