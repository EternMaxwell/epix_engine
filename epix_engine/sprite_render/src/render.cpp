#include <spdlog/spdlog.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/render.hpp>
#include <epix/sprite.hpp>
#include <epix/sprite_render.hpp>
#include <epix/transform.hpp>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace epix;
using namespace epix::ecs;
using namespace epix::app;
using namespace epix::sprite_render;

namespace {
constexpr std::string_view kSpriteVertexShader = R"(
import epix.view;

struct SpriteInstance {
    float4x4 model;
    float4 uv_offset_scale;
    float4 color;
    float4 pos_offset_scale;
};

[[vk::binding(0, 0)]] ConstantBuffer<epix::view::View> view_uniform;
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
    float4 local_position = float4(input.position * instance.pos_offset_scale.zw + instance.pos_offset_scale.xy,
                                   0.0,
                                   1.0);

    VertexOutput output;
    output.position = mul(view_uniform.clip_from_view, mul(view_uniform.view_from_world, mul(instance.model, local_position)));
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

constexpr std::string_view kSpriteVertexShaderAssetPath   = "sprite/sprite_vertex.slang";
constexpr std::string_view kSpriteFragmentShaderAssetPath = "sprite/sprite_fragment.slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(source.data()), source.size());
}

struct SpriteShaderHandles {
    assets::Handle<shader::Shader> vertex_shader;
    assets::Handle<shader::Shader> fragment_shader;
};

std::optional<SpriteShaderHandles> load_sprite_shader_handles(World& world) {
    auto registry = world.get_resource_mut<assets::EmbeddedAssetRegistry>();
    auto server   = world.get_resource<assets::AssetServer>();
    if (!registry || !server) {
        spdlog::warn(
            "[sprite] EmbeddedAssetRegistry or AssetServer is not available. Internal sprite shaders were not "
            "registered.");
        return std::nullopt;
    }

    registry->get().insert_asset_static(kSpriteVertexShaderAssetPath, shader_bytes(kSpriteVertexShader));
    registry->get().insert_asset_static(kSpriteFragmentShaderAssetPath, shader_bytes(kSpriteFragmentShader));

    return SpriteShaderHandles{
        .vertex_shader   = server->get().load<shader::Shader>("embedded://sprite/sprite_vertex.slang"),
        .fragment_shader = server->get().load<shader::Shader>("embedded://sprite/sprite_fragment.slang"),
    };
}

struct SpritePipelineCache {
    wgpu::BindGroupLayout view_layout;
    wgpu::BindGroupLayout instance_layout;
    wgpu::BindGroupLayout texture_layout;
    assets::Handle<shader::Shader> vertex_shader;
    assets::Handle<shader::Shader> fragment_shader;
    std::unordered_map<std::uint64_t, render::CachedPipelineId> pipelines;

    explicit SpritePipelineCache(World& world, const SpriteShaderHandles& shader_handles)
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
                  }))),
          vertex_shader(shader_handles.vertex_shader),
          fragment_shader(shader_handles.fragment_shader) {}

    std::optional<render::CachedPipelineId> specialize(render::PipelineServer& pipeline_server,
                                                       wgpu::TextureFormat color_format,
                                                       std::uint32_t sample_count) {
        const auto key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(color_format)) << 32) | sample_count;
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
                                 // Core 2D clears reverse-Z depth to 0.0, as
                                 // Bevy 0.18 does.  Transparent sprites must
                                 // therefore use the matching greater test.
                                 .setDepthCompare(wgpu::CompareFunction::eGreaterEqual),
            .multisample =
                wgpu::MultisampleState().setCount(sample_count).setMask(~0u).setAlphaToCoverageEnabled(false),
            .fragment = std::move(fragment_state),
        };

        auto pipeline_id = pipeline_server.queue_render_pipeline(std::move(pipeline_desc));
        pipelines.emplace(key, pipeline_id);
        return pipeline_id;
    }
};

struct TransparentSpriteDrawFunction {
    render::phase::DrawFunctionId value;
};

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

glm::vec4 sprite_uv(image::Rect rect, glm::vec2 image_size, bool flip_x, bool flip_y) {
    auto uv_min = rect.min / image_size;
    auto uv_max = rect.max / image_size;
    if (flip_x) {
        std::swap(uv_min.x, uv_max.x);
    }
    if (flip_y) {
        std::swap(uv_min.y, uv_max.y);
    }
    return glm::vec4(uv_min, uv_max - uv_min);
}

void apply_scaling(sprite::SpriteScalingMode mode,
                   glm::vec2 texture_size,
                   glm::vec2& quad_size,
                   glm::vec2& quad_translation,
                   glm::vec4& uv_offset_scale) {
    if (texture_size.x == 0.0f || texture_size.y == 0.0f || quad_size.x == 0.0f || quad_size.y == 0.0f) return;
    const auto quad_ratio     = quad_size.x / quad_size.y;
    const auto texture_ratio  = texture_size.x / texture_size.y;
    const auto tex_quad_scale = texture_ratio / quad_ratio;
    const auto quad_tex_scale = quad_ratio / texture_ratio;

    switch (mode) {
        case sprite::SpriteScalingMode::FillCenter:
            if (quad_ratio > texture_ratio) {
                uv_offset_scale.y += (uv_offset_scale.w - uv_offset_scale.w * tex_quad_scale) * 0.5f;
                uv_offset_scale.w *= tex_quad_scale;
            } else {
                uv_offset_scale.x += (uv_offset_scale.z - uv_offset_scale.z * quad_tex_scale) * 0.5f;
                uv_offset_scale.z *= quad_tex_scale;
            }
            break;
        case sprite::SpriteScalingMode::FillStart:
            if (quad_ratio > texture_ratio) {
                uv_offset_scale.y += uv_offset_scale.w - uv_offset_scale.w * tex_quad_scale;
                uv_offset_scale.w *= tex_quad_scale;
            } else {
                uv_offset_scale.z *= quad_tex_scale;
            }
            break;
        case sprite::SpriteScalingMode::FillEnd:
            if (quad_ratio > texture_ratio) {
                uv_offset_scale.w *= tex_quad_scale;
            } else {
                uv_offset_scale.x += uv_offset_scale.z - uv_offset_scale.z * quad_tex_scale;
                uv_offset_scale.z *= quad_tex_scale;
            }
            break;
        case sprite::SpriteScalingMode::FitCenter:
            if (texture_ratio > quad_ratio) quad_size.y *= quad_tex_scale;
            else quad_size.x *= tex_quad_scale;
            break;
        case sprite::SpriteScalingMode::FitStart: {
            const auto next = texture_ratio > quad_ratio ? quad_size * glm::vec2(1.0f, quad_tex_scale)
                                                         : quad_size * glm::vec2(tex_quad_scale, 1.0f);
            const auto offset = quad_size - next;
            quad_translation  = texture_ratio > quad_ratio ? glm::vec2(0.0f, -offset.y)
                                                           : glm::vec2(offset.x, 0.0f);
            quad_size = next;
            break;
        }
        case sprite::SpriteScalingMode::FitEnd: {
            const auto next = texture_ratio > quad_ratio ? quad_size * glm::vec2(1.0f, quad_tex_scale)
                                                         : quad_size * glm::vec2(tex_quad_scale, 1.0f);
            const auto offset = quad_size - next;
            quad_translation  = texture_ratio > quad_ratio ? glm::vec2(0.0f, offset.y)
                                                           : glm::vec2(-offset.x, 0.0f);
            quad_size = next;
            break;
        }
    }
}

SpriteInstanceData make_single_instance_data(const ExtractedSprite& sprite,
                                             const ExtractedSpriteSingle& single,
                                             glm::vec2 image_size) {
    const auto rect   = single.rect.value_or(image::Rect{glm::vec2(0.0f), image_size});
    auto quad_size    = single.custom_size.value_or(rect.size());
    auto uv           = sprite_uv(rect, image_size, sprite.flip_x, sprite.flip_y);
    glm::vec2 translation{0.0f};
    if (single.scaling_mode) apply_scaling(*single.scaling_mode, rect.size(), quad_size, translation, uv);

    const auto center = -single.anchor * quad_size - (single.anchor + glm::vec2(0.5f)) * translation;
    return SpriteInstanceData{
        .model            = sprite.model,
        .uv_offset_scale  = uv,
        .color            = sprite.color,
        .pos_offset_scale = glm::vec4(center, quad_size),
    };
}

SpriteInstanceData make_slice_instance_data(const ExtractedSprite& sprite,
                                            const ExtractedSlice& slice,
                                            glm::vec2 image_size) {
    return SpriteInstanceData{
        .model            = sprite.model,
        .uv_offset_scale  = sprite_uv(slice.rect, image_size, sprite.flip_x, sprite.flip_y),
        .color            = sprite.color,
        .pos_offset_scale = glm::vec4(slice.offset, slice.size),
    };
}

std::optional<ComputedTextureSlices> compute_sprite_slices(
    const sprite::Sprite& value,
    const assets::Assets<image::Image>& images,
    const assets::Assets<image::TextureAtlasLayout>& atlas_layouts) {
    glm::vec2 image_size;
    image::Rect texture_rect;
    if (value.texture_atlas) {
        const auto layout = atlas_layouts.get(value.texture_atlas->layout.id());
        if (!layout || value.texture_atlas->index >= layout->get().textures.size()) return std::nullopt;
        image_size   = glm::vec2(layout->get().size);
        texture_rect = layout->get().textures[value.texture_atlas->index].as_rect();
    } else {
        const auto source = images.get(value.image.id());
        if (!source) return std::nullopt;
        image_size = glm::vec2(static_cast<float>(source->get().width()), static_cast<float>(source->get().height()));
        texture_rect = value.rect.value_or(image::Rect{glm::vec2(0.0f), image_size});
    }

    if (const auto* sliced = std::get_if<sprite::SpriteImageMode::Sliced>(&value.image_mode)) {
        return ComputedTextureSlices{sliced->slicer.compute_slices(texture_rect, value.custom_size)};
    }
    if (const auto* tiled = std::get_if<sprite::SpriteImageMode::Tiled>(&value.image_mode)) {
        const sprite::TextureSlice whole{
            .texture_rect = texture_rect,
            .draw_size    = value.custom_size.value_or(image_size),
            .offset       = glm::vec2(0.0f),
        };
        return ComputedTextureSlices{whole.tiled(tiled->stretch_value, {tiled->tile_x, tiled->tile_y})};
    }
    return std::nullopt;
}

void extract_sprites(Commands cmd,
                     ResMut<ExtractedSlices> extracted_slices,
                     Extract<Query<Item<Entity,
                                        const sprite::Sprite&,
                                        const sprite::Anchor&,
                                        const transform::GlobalTransform&,
                                        const camera::ViewVisibility&,
                                        Opt<const ComputedTextureSlices&>,
                                        Opt<const camera::RenderLayers&>>,
                                   Without<render::CustomRendered>>> sprites,
                     Extract<Res<assets::Assets<image::TextureAtlasLayout>>> atlases) {
    extracted_slices->slices.clear();
    for (auto&& [entity, sprite, anchor, global_transform, view_visibility, computed, opt_layer] : sprites.iter()) {
        // Bevy extract_sprites gates on ViewVisibility (visibility/mod.rs:448-458).
        if (!view_visibility.get()) continue;
        std::optional<image::Rect> texture_rect;
        if (sprite.texture_atlas) {
            if (const auto atlas_rect = sprite.texture_atlas->texture_rect(*atlases)) {
                texture_rect = atlas_rect->as_rect();
            }
        }
        if (sprite.rect) {
            texture_rect = sprite.texture_atlas && texture_rect
                               ? image::Rect{texture_rect->min + sprite.rect->min, texture_rect->min + sprite.rect->max}
                               : *sprite.rect;
        }

        ExtractedSpriteKind kind;
        if (computed) {
            const auto begin = extracted_slices->slices.size();
            for (const auto& slice : computed->get().extract_slices(sprite, anchor.as_vec())) {
                extracted_slices->slices.push_back(slice);
            }
            kind = ExtractedSpriteSlices{begin, extracted_slices->slices.size()};
        } else {
            kind = ExtractedSpriteSingle{
                .anchor       = anchor.as_vec(),
                .rect         = texture_rect,
                .scaling_mode = sprite.image_mode.scale(),
                .custom_size  = sprite.custom_size,
            };
        }

        cmd.spawn(epix::render::sync_world::TemporaryRenderEntity{},
                  ExtractedSprite{
                      .source_entity = entity,
                      .color         = sprite.color,
                      .model         = global_transform.matrix,
                      .depth         = global_transform.matrix[3][2],
                      .texture       = sprite.image.id(),
                      .flip_x        = sprite.flip_x,
                      .flip_y        = sprite.flip_y,
                      .kind          = std::move(kind),
                      .render_layer  = opt_layer ? *opt_layer : camera::RenderLayers::layer(0),
                  },
                  SpriteBatch{});
    }
}

void queue_sprites_2d(Query<Item<const render::view::ExtractedView&,
                                 const render::view::ViewTarget&,
                                 Opt<const ::epix::camera::RenderLayers&>,
                                 const ::epix::render::view::Msaa&,
                                 const ::epix::render::view::RenderVisibleEntities&>> views,
                      Query<Item<Entity, const ExtractedSprite&>> sprites,
                      Res<render::RenderAssets<image::Image>> images,
                      Res<TransparentSpriteDrawFunction> draw_function_id,
                      ResMut<SpritePipelineCache> pipeline_cache,
                      ResMut<render::PipelineServer> pipeline_server,
                      ResMut<render::phase::ViewSortedRenderPhases<core_graph::core_2d::Transparent2D>> phases) {
    for (auto&& [view, target, opt_camera_layers, msaa, visible_entities] : views.iter()) {
        auto phase = phases->find(view.retained_view_entity);
        if (phase == phases->end()) continue;
        const auto& camera_layers =
            opt_camera_layers ? opt_camera_layers->get() : ::epix::camera::RenderLayers::layer(0);
        auto pipeline_id =
            pipeline_cache->specialize(*pipeline_server, target.format, ::epix::render::view::samples(msaa));
        if (!pipeline_id) {
            spdlog::warn("[sprite] Failed to specialize sprite pipeline for target format {}.",
                         wgpu::to_string(target.format));
            continue;
        }

        for (auto&& [entity, sprite] : sprites.iter()) {
            if (!images->try_get(sprite.texture)) {
                continue;
            }
            if (!camera_layers.intersects(sprite.render_layer)) {
                continue;
            }
            if (const auto& visible = visible_entities.template get<sprite::Sprite>();
                std::ranges::find(visible, sprite.source_entity,
                                  [](const auto& entity) { return entity.second.id(); }) == visible.end()) {
                continue;
            }

            phase->second.add(core_graph::core_2d::Transparent2D{
                .representative_entity = {entity, render::sync_world::MainEntity{sprite.source_entity}},
                .depth                 = sprite.depth,
                .pipeline_id           = *pipeline_id,
                .draw_func             = draw_function_id->value,
                .batch_range_value     = {0, 1},
                .indexed_value         = false,
            });
        }
    }
}

void prepare_sprite_batches(ResMut<render::phase::ViewSortedRenderPhases<core_graph::core_2d::Transparent2D>> phases,
                            Query<Item<SpriteBatch&, const ExtractedSprite&>> sprites,
                            Res<ExtractedSlices> extracted_slices,
                            Res<render::RenderAssets<image::Image>> images,
                            Res<wgpu::Device> device,
                            Res<wgpu::Queue> queue,
                            Res<SpritePipelineCache> pipeline_cache,
                            ResMut<SpriteInstanceBuffer> instance_buffer) {
    instance_buffer->instances.clear();
    std::unordered_map<assets::AssetId<image::Image>, wgpu::BindGroup> texture_bind_group_cache;

    for (auto& [retained_view_entity, phase] : *phases) {
        (void)retained_view_entity;
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
                batch.instance_start                = static_cast<std::uint32_t>(instance_buffer->instances.size());
                phase.items[batch_head].batch_range() = {batch.instance_start, batch.instance_start};
                if (auto it = texture_bind_group_cache.find(sprite.texture); it != texture_bind_group_cache.end()) {
                    batch.texture_bind_group = it->second;
                } else {
                    batch.texture_bind_group = device->createBindGroup(
                        wgpu::BindGroupDescriptor()
                            .setLabel("SpriteTextureBindGroup")
                            .setLayout(pipeline_cache->texture_layout)
                            .setEntries(std::array{
                                wgpu::BindGroupEntry().setBinding(0).setSampler(gpu_image->sampler),
                                wgpu::BindGroupEntry().setBinding(1).setTextureView(gpu_image->texture_view),
                            }));
                    texture_bind_group_cache.emplace(sprite.texture, batch.texture_bind_group);
                }
                current_texture = sprite.texture;
            }

            const auto image_size = glm::vec2(gpu_image->size_2d());
            if (const auto* single = std::get_if<ExtractedSpriteSingle>(&sprite.kind)) {
                instance_buffer->instances.push_back(make_single_instance_data(sprite, *single, image_size));
            } else {
                const auto& slices = std::get<ExtractedSpriteSlices>(sprite.kind);
                for (auto index = slices.begin; index < slices.end; ++index) {
                    instance_buffer->instances.push_back(
                        make_slice_instance_data(sprite, extracted_slices->slices[index], image_size));
                }
            }
            phase.items[batch_head].batch_range().second =
                static_cast<std::uint32_t>(instance_buffer->instances.size());
        }
    }

    auto required_bytes = instance_buffer->instances.size() * sizeof(SpriteInstanceData);
    ensure_instance_buffer(*instance_buffer, *pipeline_cache, *device, required_bytes);
    if (required_bytes != 0) {
        queue->writeBuffer(instance_buffer->buffer, 0, instance_buffer->instances.data(), required_bytes);
    }
}
}  // namespace

void sprite_render::compute_slices_on_asset_event(
    Commands commands,
    EventReader<assets::AssetEvent<image::Image>> events,
    Res<assets::Assets<image::Image>> images,
    Res<assets::Assets<image::TextureAtlasLayout>> atlas_layouts,
    Query<Item<Entity, const sprite::Sprite&>> sprites) {
    std::unordered_set<assets::AssetId<image::Image>> changed;
    for (const auto& event : events.read()) {
        if (event.type == assets::AssetEvent<image::Image>::Type::Added ||
            event.type == assets::AssetEvent<image::Image>::Type::Modified) {
            changed.insert(event.id);
        }
    }
    if (changed.empty()) return;

    for (auto&& [entity, value] : sprites.iter()) {
        if (!value.image_mode.uses_slices() || !changed.contains(value.image.id())) continue;
        if (auto slices = compute_sprite_slices(value, *images, *atlas_layouts)) {
            commands.entity(entity).insert(std::move(*slices));
        }
    }
}

void sprite_render::compute_slices_on_sprite_change(
    Commands commands,
    Res<assets::Assets<image::Image>> images,
    Res<assets::Assets<image::TextureAtlasLayout>> atlas_layouts,
    Query<Item<Entity, const sprite::Sprite&>, Filter<Modified<sprite::Sprite>>> changed_sprites) {
    for (auto&& [entity, value] : changed_sprites.iter()) {
        if (!value.image_mode.uses_slices()) continue;
        if (auto slices = compute_sprite_slices(value, *images, *atlas_layouts)) {
            commands.entity(entity).insert(std::move(*slices));
        }
    }
}

void SpriteRenderPlugin::attach(app::App& app) {
    spdlog::debug("[sprite] Attaching SpriteRenderPlugin.");
    app.add_plugins(Mesh2dRenderPlugin{});
    app.add_systems(app::PostUpdate,
                    into(sprite_render::compute_slices_on_asset_event)
                        .in_set(SpriteSystems::ComputeSlices)
                        .set_name("compute slices on image event"));
    app.add_systems(app::PostUpdate,
                    into(sprite_render::compute_slices_on_sprite_change)
                        .in_set(SpriteSystems::ComputeSlices)
                        .set_name("compute slices on sprite change"));

    if (!app.world_mut().get_resource<SpriteShaderHandles>()) {
        if (auto shader_handles = load_sprite_shader_handles(app.world_mut())) {
            app.world_mut().insert_resource(std::move(*shader_handles));
        }
    }
}

void SpriteRenderPlugin::ready(app::App& app) {
    spdlog::debug("[sprite] Readying SpriteRenderPlugin.");
    if (!app.world_mut().get_resource<SpriteShaderHandles>()) {
        if (auto shader_handles = load_sprite_shader_handles(app.world_mut())) {
            app.world_mut().insert_resource(std::move(*shader_handles));
        }
    }

    auto shader_handles = app.world_mut().get_resource<SpriteShaderHandles>();
    if (!shader_handles) {
        spdlog::error("[sprite] SpriteRenderPlugin could not load internal sprite shaders through AssetServer.");
        return;
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app) {
        spdlog::error("[sprite] SpriteRenderPlugin requires render::RenderPlugin to be added before it.");
        return;
    }

    auto& world = render_app->get().world_mut();
    if (!world.get_resource<SpriteGeometryBuffers>()) {
        world.insert_resource(SpriteGeometryBuffers(world));
    }
    if (!world.get_resource<SpriteInstanceBuffer>()) {
        world.insert_resource(SpriteInstanceBuffer{});
    }
    if (!world.get_resource<ExtractedSlices>()) {
        world.insert_resource(ExtractedSlices{});
    }
    if (!world.get_resource<SpritePipelineCache>()) {
        world.insert_resource(SpritePipelineCache(world, shader_handles->get()));
    }
    auto& render_subapp = render_app->get();
    world.insert_resource(TransparentSpriteDrawFunction{
        .value = render::phase::app_add_render_commands<
            core_graph::core_2d::Transparent2D, render::phase::SetItemPipeline,
            render::view::BindViewUniform<0>::Command, sprite_render::BindSpriteInstances<1>::Command,
            sprite_render::BindSpriteTexture<2>::Command, sprite_render::DrawSpriteBatch>(render_subapp)});

    render_subapp.add_systems(render::ExtractSchedule, into(extract_sprites).set_name("extract sprites"))
        .add_systems(render::Render,
                     into(queue_sprites_2d).in_set(render::RenderSystems::Queue).set_name("queue sprites"))
        .add_systems(render::Render, into(prepare_sprite_batches)
                                         .in_set(render::RenderSystems::PrepareResources)
                                         .set_name("prepare sprite batches"));
}
