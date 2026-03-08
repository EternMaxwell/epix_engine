module;

#include <spdlog/spdlog.h>

module epix.sprite;

import epix.assets;
import epix.core_graph;
import epix.image;
import epix.render;
import epix.transform;
import std;

using namespace core;
using namespace sprite;

namespace {
constexpr std::string_view kSpriteVertexShader = R"(
struct ViewUniform {
    projection : mat4x4<f32>,
    view : mat4x4<f32>,
};

struct SpriteInstance {
    model : mat4x4<f32>,
    uv_offset_scale : vec4<f32>,
    color : vec4<f32>,
    pos_offset_scale : vec4<f32>,
};

@group(0) @binding(0) var<uniform> view_uniform : ViewUniform;
@group(1) @binding(0) var<storage, read> sprite_instances : array<SpriteInstance>;

struct VertexInput {
    @location(0) position : vec2<f32>,
    @location(1) uv : vec2<f32>,
    @builtin(instance_index) instance_index : u32,
};

struct VertexOutput {
    @builtin(position) position : vec4<f32>,
    @location(0) uv : vec2<f32>,
    @location(1) color : vec4<f32>,
};

@vertex
fn main(input : VertexInput) -> VertexOutput {
    let instance = sprite_instances[input.instance_index];
    let local_position = vec4<f32>((input.position + instance.pos_offset_scale.xy) * instance.pos_offset_scale.zw,
                                   0.0,
                                   1.0);

    var output : VertexOutput;
    output.position = view_uniform.projection * view_uniform.view * instance.model * local_position;
    output.uv = input.uv * instance.uv_offset_scale.zw + instance.uv_offset_scale.xy;
    output.color = instance.color;
    return output;
}
)";

constexpr std::string_view kSpriteFragmentShader = R"(
@group(2) @binding(0) var sprite_sampler : sampler;
@group(2) @binding(1) var sprite_texture : texture_2d<f32>;

struct FragmentInput {
    @location(0) uv : vec2<f32>,
    @location(1) color : vec4<f32>,
};

@fragment
fn main(input : FragmentInput) -> @location(0) vec4<f32> {
    return textureSample(sprite_texture, sprite_sampler, input.uv) * input.color;
}
)";

const assets::AssetId<render::Shader> kSpriteVertexShaderId(
    uuids::uuid::from_string("89282c3c-0eaa-4f75-bf12-5b7f78c763f0").value());
const assets::AssetId<render::Shader> kSpriteFragmentShaderId(
    uuids::uuid::from_string("63e69646-4b35-4eb8-b095-78ef59bd3ea3").value());

struct ExtractedSprite {
    Entity source_entity;
    Sprite sprite;
    glm::mat4 model;
    float depth;
    assets::AssetId<image::Image> texture;
    glm::vec2 image_size;
};

struct SpriteBatch {
    wgpu::BindGroup texture_bind_group;
    std::uint32_t instance_start = 0;
};

struct SpriteInstanceData {
    glm::mat4 model;
    glm::vec4 uv_offset_scale;
    glm::vec4 color;
    glm::vec4 pos_offset_scale;
};

struct SpriteGeometryBuffers {
    wgpu::Buffer position_buffer;
    wgpu::Buffer uv_buffer;
    wgpu::Buffer index_buffer;
    std::uint32_t index_count = 0;

    explicit SpriteGeometryBuffers(World& world) {
        auto& device = world.resource<wgpu::Device>();
        auto& queue  = world.resource<wgpu::Queue>();

        constexpr std::array quad_positions = {
            glm::vec2(-0.5f, -0.5f),
            glm::vec2(0.5f, -0.5f),
            glm::vec2(0.5f, 0.5f),
            glm::vec2(-0.5f, 0.5f),
        };
        constexpr std::array quad_uvs = {
            glm::vec2(0.0f, 0.0f),
            glm::vec2(1.0f, 0.0f),
            glm::vec2(1.0f, 1.0f),
            glm::vec2(0.0f, 1.0f),
        };
        constexpr std::array<std::uint16_t, 6> quad_indices = {0, 1, 2, 2, 3, 0};

        position_buffer = device.createBuffer(wgpu::BufferDescriptor()
                                                  .setLabel("SpriteQuadPositions")
                                                  .setSize(sizeof(quad_positions))
                                                  .setUsage(wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst));
        uv_buffer       = device.createBuffer(wgpu::BufferDescriptor()
                                                  .setLabel("SpriteQuadUvs")
                                                  .setSize(sizeof(quad_uvs))
                                                  .setUsage(wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst));
        index_buffer    = device.createBuffer(wgpu::BufferDescriptor()
                                                  .setLabel("SpriteQuadIndices")
                                                  .setSize(sizeof(quad_indices))
                                                  .setUsage(wgpu::BufferUsage::eIndex | wgpu::BufferUsage::eCopyDst));

        queue.writeBuffer(position_buffer, 0, quad_positions.data(), sizeof(quad_positions));
        queue.writeBuffer(uv_buffer, 0, quad_uvs.data(), sizeof(quad_uvs));
        queue.writeBuffer(index_buffer, 0, quad_indices.data(), sizeof(quad_indices));
        index_count = static_cast<std::uint32_t>(quad_indices.size());
    }
};

struct SpriteInstanceBuffer {
    wgpu::Buffer buffer;
    wgpu::BindGroup bind_group;
    std::vector<SpriteInstanceData> instances;
};

struct SpritePipelineCache {
    wgpu::BindGroupLayout view_layout;
    wgpu::BindGroupLayout instance_layout;
    wgpu::BindGroupLayout texture_layout;
    assets::Handle<render::Shader> vertex_shader{kSpriteVertexShaderId};
    assets::Handle<render::Shader> fragment_shader{kSpriteFragmentShaderId};
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

void insert_sprite_shaders(assets::Assets<render::Shader>& shaders) {
    [[maybe_unused]] auto vertex = shaders.insert(
        kSpriteVertexShaderId, render::Shader{std::filesystem::path("internal://sprite/sprite_vertex.wgsl"),
                                              render::ShaderSource::wgsl(std::string(kSpriteVertexShader))});
    [[maybe_unused]] auto fragment = shaders.insert(
        kSpriteFragmentShaderId, render::Shader{std::filesystem::path("internal://sprite/sprite_fragment.wgsl"),
                                                render::ShaderSource::wgsl(std::string(kSpriteFragmentShader))});
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

template <typename PhaseItem>
struct SpriteDrawFunction : render::phase::DrawFunction<PhaseItem> {
    std::optional<QueryState<Item<const render::view::ViewBindGroup&>, Filter<>>> view_query;
    std::optional<QueryState<Item<const SpriteBatch&>, Filter<>>> sprite_query;

    void prepare(const World& world) override {
        if (!view_query) {
            view_query = world.try_query<Item<const render::view::ViewBindGroup&>>();
        } else {
            view_query->update_archetypes(world);
        }
        if (!sprite_query) {
            sprite_query = world.try_query<Item<const SpriteBatch&>>();
        } else {
            sprite_query->update_archetypes(world);
        }
    }

    std::expected<void, render::phase::DrawError> draw(const World& world,
                                                       const wgpu::RenderPassEncoder& encoder,
                                                       Entity view,
                                                       const PhaseItem& item) override {
        if (!view_query || !sprite_query) {
            return std::unexpected(render::phase::DrawError::render_command_failure(
                "[sprite] SpriteDrawFunction was used before prepare finished."));
        }

        auto view_item = view_query->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(view);
        if (!view_item) {
            return std::unexpected(render::phase::DrawError::view_entity_missing(
                std::format("[sprite] View entity {:#x} is missing ViewBindGroup while drawing sprite entity {:#x}.",
                            view.index, item.entity().index)));
        }

        auto sprite_item =
            sprite_query->query_with_ticks(world, world.last_change_tick(), world.change_tick()).get(item.entity());
        if (!sprite_item) {
            return std::unexpected(render::phase::DrawError::invalid_entity_query(
                std::format("[sprite] Sprite batch entity {:#x} is missing SpriteBatch.", item.entity().index)));
        }

        auto pipeline = world.resource<render::PipelineServer>().get_render_pipeline(item.pipeline());
        if (!pipeline) {
            return std::unexpected(render::phase::DrawError::render_command_failure(
                std::format("[sprite] Cached pipeline {} is not ready for sprite entity {:#x}.", item.pipeline().get(),
                            item.entity().index)));
        }

        auto&& [view_bind_group] = *view_item;
        auto&& [sprite_batch]    = *sprite_item;
        auto& geometry           = world.resource<SpriteGeometryBuffers>();
        auto& instances          = world.resource<SpriteInstanceBuffer>();

        if (!sprite_batch.texture_bind_group || !instances.bind_group) {
            return std::unexpected(render::phase::DrawError::render_command_failure(
                std::format("[sprite] Sprite entity {:#x} is missing prepared bind groups.", item.entity().index)));
        }

        encoder.setPipeline(pipeline->get().pipeline());
        encoder.setBindGroup(0, view_bind_group.bind_group, std::span<const std::uint32_t>{});
        encoder.setBindGroup(1, instances.bind_group, std::span<const std::uint32_t>{});
        encoder.setBindGroup(2, sprite_batch.texture_bind_group, std::span<const std::uint32_t>{});
        encoder.setVertexBuffer(0, geometry.position_buffer, 0, geometry.position_buffer.getSize());
        encoder.setVertexBuffer(1, geometry.uv_buffer, 0, geometry.uv_buffer.getSize());
        encoder.setIndexBuffer(geometry.index_buffer, wgpu::IndexFormat::eUint16, 0, geometry.index_buffer.getSize());
        encoder.drawIndexed(geometry.index_count, static_cast<std::uint32_t>(item.batch_size()), 0, 0,
                            sprite_batch.instance_start);
        return {};
    }
};

void queue_sprites_2d(Query<Item<render::phase::RenderPhase<core_graph::core_2d::Transparent2D>&,
                                 const render::view::ExtractedView&,
                                 const render::view::ViewTarget&>,
                            With<render::camera::ExtractedCamera>> views,
                      Query<Item<Entity, const ExtractedSprite&>> sprites,
                      Res<render::RenderAssets<image::Image>> images,
                      ResMut<SpritePipelineCache> pipeline_cache,
                      ResMut<render::PipelineServer> pipeline_server,
                      ResMut<render::phase::DrawFunctions<core_graph::core_2d::Transparent2D>> draw_functions) {
    auto draw_function_id =
        draw_functions->template get_id<SpriteDrawFunction<core_graph::core_2d::Transparent2D>>().value_or(
            draw_functions->template add<SpriteDrawFunction<core_graph::core_2d::Transparent2D>>());

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
                .draw_func   = draw_function_id,
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

void SpritePlugin::build(core::App& app) { app.add_plugins(core_graph::core_2d::Core2dPlugin{}); }

void SpritePlugin::finish(core::App& app) {
    auto shaders = app.world_mut().get_resource_mut<assets::Assets<render::Shader>>();
    if (shaders) {
        insert_sprite_shaders(shaders->get());
    } else {
        spdlog::warn("[sprite] Assets<render::Shader> is not available in the main world.");
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

    render_app->get()
        .add_systems(render::ExtractSchedule, into(extract_sprites).set_name("extract sprites"))
        .add_systems(render::Render, into(queue_sprites_2d).in_set(render::RenderSet::Queue).set_name("queue sprites"))
        .add_systems(render::Render, into(prepare_sprite_batches)
                                         .in_set(render::RenderSet::PrepareResources)
                                         .set_name("prepare sprite batches"));
}