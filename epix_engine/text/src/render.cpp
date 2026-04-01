module;

#include <spdlog/spdlog.h>

module epix.text;

import epix.core_graph;
import epix.render;
import std;

using namespace epix::core;
using namespace epix::text;
using namespace epix;

namespace {
constexpr std::string_view kTextVertexShader = R"(
struct ViewUniform {
    projection : mat4x4<f32>,
    view : mat4x4<f32>,
};

struct TextInstance {
    model : mat4x4<f32>,
    color : vec4<f32>,
};

@group(0) @binding(0) var<uniform> view_uniform : ViewUniform;
@group(1) @binding(0) var<storage, read> text_instances : array<TextInstance>;

struct VertexInput {
    @location(0) position : vec3<f32>,
    @location(5) uv_layer : vec3<f32>,
    @builtin(instance_index) instance_index : u32,
};

struct VertexOutput {
    @builtin(position) position : vec4<f32>,
    @location(0) color : vec4<f32>,
    @location(1) uv_layer : vec3<f32>,
};

@vertex
fn main(input : VertexInput) -> VertexOutput {
    let instance = text_instances[input.instance_index];

    var output : VertexOutput;
    output.position = view_uniform.projection * view_uniform.view * instance.model * vec4<f32>(input.position, 1.0);
    output.color = instance.color;
    output.uv_layer = input.uv_layer;
    return output;
}
)";

constexpr std::string_view kTextFragmentShader = R"(
@group(2) @binding(0) var text_sampler : sampler;
@group(2) @binding(1) var text_texture : texture_2d_array<f32>;

struct FragmentInput {
    @location(0) color : vec4<f32>,
    @location(1) uv_layer : vec3<f32>,
};

@fragment
fn main(input : FragmentInput) -> @location(0) vec4<f32> {
    let layer = i32(input.uv_layer.z);
    return textureSample(text_texture, text_sampler, input.uv_layer.xy, layer) * input.color;
}
)";

const assets::AssetId<shader::Shader> kTextVertexShaderId(
    uuids::uuid::from_string("33aad04b-4639-46bb-98b1-4f372e9c1f75").value());
const assets::AssetId<shader::Shader> kTextFragmentShaderId(
    uuids::uuid::from_string("7c271b37-4baa-4e9f-974f-1cfe0d5d6b2f").value());

const mesh::MeshAttribute kTextUvLayerAttribute{"text_uv_layer", 5, wgpu::VertexFormat::eFloat32x3};

struct ExtractedText2d {
    Entity source_entity;
    assets::AssetId<mesh::Mesh> mesh;
    glm::mat4 model;
    glm::vec4 color;
    float depth;
    assets::AssetId<image::Image> font_image;
};

struct TextBatch {
    std::optional<wgpu::BindGroup> texture_bind_group;
    std::uint32_t instance_start = 0;
};

struct TextInstanceData {
    glm::mat4 model;
    glm::vec4 color;
};

struct TextInstanceBuffer {
    wgpu::Buffer buffer;
    wgpu::BindGroup bind_group;
    std::vector<TextInstanceData> instances;
};

struct Text2dPipelineCache {
    wgpu::BindGroupLayout view_layout;
    wgpu::BindGroupLayout instance_layout;
    wgpu::BindGroupLayout texture_layout;
    assets::Handle<shader::Shader> vertex_shader{kTextVertexShaderId};
    assets::Handle<shader::Shader> fragment_shader{kTextFragmentShaderId};
    std::unordered_map<std::uint32_t, render::CachedPipelineId> pipelines;

    explicit Text2dPipelineCache(World& world)
        : view_layout(world.resource<render::view::ViewUniformBindingLayout>().layout),
          instance_layout(world.resource<wgpu::Device>().createBindGroupLayout(
              wgpu::BindGroupLayoutDescriptor()
                  .setLabel("TextInstanceLayout")
                  .setEntries(std::array{
                      wgpu::BindGroupLayoutEntry()
                          .setBinding(0)
                          .setVisibility(wgpu::ShaderStage::eVertex)
                          .setBuffer(wgpu::BufferBindingLayout()
                                         .setType(wgpu::BufferBindingType::eReadOnlyStorage)
                                         .setHasDynamicOffset(false)
                                         .setMinBindingSize(sizeof(TextInstanceData))),
                  }))),
          texture_layout(world.resource<wgpu::Device>().createBindGroupLayout(
              wgpu::BindGroupLayoutDescriptor()
                  .setLabel("TextTextureLayout")
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
                                          .setViewDimension(wgpu::TextureViewDimension::e2DArray)
                                          .setMultisampled(false)),
                  }))) {}

    std::optional<render::CachedPipelineId> specialize(render::PipelineServer& pipeline_server,
                                                       wgpu::TextureFormat color_format) {
        auto key = static_cast<std::uint32_t>(color_format);
        if (auto it = pipelines.find(key); it != pipelines.end()) {
            return it->second;
        }

        std::array vertex_buffers = {
            wgpu::VertexBufferLayout()
                .setArrayStride(sizeof(glm::vec3))
                .setStepMode(wgpu::VertexStepMode::eVertex)
                .setAttributes(std::array{
                    wgpu::VertexAttribute().setShaderLocation(0).setFormat(wgpu::VertexFormat::eFloat32x3).setOffset(0),
                }),
            wgpu::VertexBufferLayout()
                .setArrayStride(sizeof(glm::vec3))
                .setStepMode(wgpu::VertexStepMode::eVertex)
                .setAttributes(std::array{
                    wgpu::VertexAttribute().setShaderLocation(5).setFormat(wgpu::VertexFormat::eFloat32x3).setOffset(0),
                }),
        };

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
            .label         = std::format("text-{}", wgpu::to_string(color_format)),
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

struct TransparentTextDrawFunction {
    render::phase::DrawFunctionId value;
};

template <size_t Slot>
struct BindTextInstances {
    template <render::phase::PhaseItem PhaseItem>
    struct Command {
        void prepare(const World&) {}

        std::expected<void, render::phase::RenderCommandError> render(
            const PhaseItem& item,
            core::Item<const render::view::ViewBindGroup&>,
            std::optional<core::Item<const TextBatch&, const ExtractedText2d&>>,
            core::ParamSet<core::Res<TextInstanceBuffer>> params,
            const wgpu::RenderPassEncoder& encoder) {
            auto&& [instances] = params.get();
            if (!instances->bind_group) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type    = render::phase::RenderCommandError::Type::Failure,
                    .message = std::format("[text] Text instance buffer bind group is not ready for entity {:#x}.",
                                           item.entity().index),
                });
            }

            encoder.setBindGroup(Slot, instances->bind_group, std::span<const std::uint32_t>{});
            return {};
        }
    };
};

template <size_t Slot>
struct BindTextTexture {
    template <render::phase::PhaseItem PhaseItem>
    struct Command {
        void prepare(const World&) {}

        std::expected<void, render::phase::RenderCommandError> render(
            const PhaseItem& item,
            core::Item<const render::view::ViewBindGroup&>,
            std::optional<core::Item<const TextBatch&, const ExtractedText2d&>> entity_item,
            core::ParamSet<>,
            const wgpu::RenderPassEncoder& encoder) {
            if (!entity_item) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type = render::phase::RenderCommandError::Type::Failure,
                    .message =
                        std::format("[text] Text entity {:#x} is missing TextBatch or ExtractedText2d during draw.",
                                    item.entity().index),
                });
            }

            auto&& [batch, extracted] = **entity_item;
            if (!batch.texture_bind_group) {
                return std::unexpected(render::phase::RenderCommandError{
                    .type = render::phase::RenderCommandError::Type::Failure,
                    .message =
                        std::format("[text] Entity {:#x} requires texture {} but TextBatch has no texture bind group.",
                                    item.entity().index, extracted.font_image.to_string_short()),
                });
            }

            encoder.setBindGroup(Slot, *batch.texture_bind_group, std::span<const std::uint32_t>{});
            return {};
        }
    };
};

template <render::phase::PhaseItem PhaseItem>
struct DrawTextBatch {
    void prepare(const World&) {}

    std::expected<void, render::phase::RenderCommandError> render(
        const PhaseItem& item,
        core::Item<const render::view::ViewBindGroup&>,
        std::optional<core::Item<const TextBatch&, const ExtractedText2d&>> entity_item,
        core::ParamSet<core::Res<render::RenderAssets<mesh::Mesh>>> params,
        const wgpu::RenderPassEncoder& encoder) {
        if (!entity_item) {
            return std::unexpected(render::phase::RenderCommandError{
                .type    = render::phase::RenderCommandError::Type::Failure,
                .message = std::format("[text] Text entity {:#x} is missing TextBatch or ExtractedText2d during draw.",
                                       item.entity().index),
            });
        }

        auto&& [batch, extracted] = **entity_item;
        auto&& [gpu_meshes]       = params.get();
        auto* gpu_mesh            = gpu_meshes->try_get(extracted.mesh);
        if (!gpu_mesh) {
            return std::unexpected(render::phase::RenderCommandError{
                .type    = render::phase::RenderCommandError::Type::Failure,
                .message = std::format("[text] GPU mesh {} for entity {:#x} is missing at draw time.",
                                       extracted.mesh.to_string_short(), item.entity().index),
            });
        }

        gpu_mesh->bind_to(encoder);
        if (gpu_mesh->is_indexed()) {
            encoder.drawIndexed(static_cast<std::uint32_t>(gpu_mesh->vertex_count()), item.batch_size(), 0, 0,
                                batch.instance_start);
        } else {
            encoder.draw(static_cast<std::uint32_t>(gpu_mesh->vertex_count()), item.batch_size(), 0,
                         batch.instance_start);
        }
        return {};
    }
};

void insert_text_shaders(assets::Assets<shader::Shader>& shaders) {
    auto vertex_path             = std::filesystem::path("embedded://text/text_vertex.wgsl");
    auto fragment_path           = std::filesystem::path("embedded://text/text_fragment.wgsl");
    [[maybe_unused]] auto vertex = shaders.insert(
        kTextVertexShaderId,
        shader::Shader{vertex_path, vertex_path.string(), shader::ShaderSource::wgsl(std::string(kTextVertexShader))});
    [[maybe_unused]] auto fragment = shaders.insert(
        kTextFragmentShaderId, shader::Shader{fragment_path, fragment_path.string(),
                                              shader::ShaderSource::wgsl(std::string(kTextFragmentShader))});
}

void ensure_text_instance_buffer(TextInstanceBuffer& instance_buffer,
                                 const Text2dPipelineCache& pipeline_cache,
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
                                    .setLabel("TextInstanceBuffer")
                                    .setSize(buffer_size)
                                    .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));
        instance_buffer.bind_group = device.createBindGroup(wgpu::BindGroupDescriptor()
                                                                .setLabel("TextInstanceBindGroup")
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

void validate_text_2d(Query<Item<Entity, Has<TextColor>, Has<transform::Transform>>, With<Text2d>> query) {
    for (auto&& [entity, has_color, has_transform] : query.iter()) {
        if (has_color && has_transform) {
            continue;
        }

        std::string missing;
        if (!has_color) {
            missing += "TextColor";
        }
        if (!has_transform) {
            if (!missing.empty()) {
                missing += ", ";
            }
            missing += "Transform";
        }
        spdlog::warn("[text] Entity {:#x} with Text2d is missing required components: {}.", entity.index, missing);
    }
}

void extract_texts_2d(Commands cmd,
                      Extract<Query<Item<Entity,
                                         const TextMesh&,
                                         const Text2d&,
                                         Opt<const TextColor&>,
                                         const TextImage&,
                                         const transform::GlobalTransform&>,
                                    Without<render::CustomRendered>>> texts) {
    for (auto&& [entity, text_mesh, text2d, text_color, text_image, transform] : texts.iter()) {
        glm::vec4 color{1.0f};
        if (text_color) {
            auto&& value = text_color->get();
            color        = glm::vec4(value.r, value.g, value.b, value.a);
        }

        auto model = transform.matrix;
        model[3] += glm::vec4(text2d.offset, 0.0f, 0.0f);
        cmd.spawn(
            ExtractedText2d{
                .source_entity = entity,
                .mesh          = text_mesh.mesh().id(),
                .model         = model,
                .color         = color,
                .depth         = model[3][2],
                .font_image    = text_image.image,
            },
            TextBatch{});
    }
}

void queue_texts_2d(Query<Item<render::phase::RenderPhase<core_graph::core_2d::Transparent2D>&,
                               const render::view::ExtractedView&,
                               const render::view::ViewTarget&>,
                          With<render::camera::ExtractedCamera>> views,
                    Query<Item<Entity, const ExtractedText2d&>> texts,
                    Res<render::RenderAssets<image::Image>> images,
                    Res<TransparentTextDrawFunction> draw_function_id,
                    ResMut<Text2dPipelineCache> pipeline_cache,
                    ResMut<render::PipelineServer> pipeline_server) {
    for (auto&& [phase, view, target] : views.iter()) {
        auto pipeline_id = pipeline_cache->specialize(*pipeline_server, target.format);
        if (!pipeline_id) {
            spdlog::warn("[text] Failed to specialize text pipeline for target format {}.",
                         wgpu::to_string(target.format));
            continue;
        }

        for (auto&& [entity, text] : texts.iter()) {
            if (!images->try_get(text.font_image)) {
                continue;
            }

            phase.add(core_graph::core_2d::Transparent2D{
                .id          = entity,
                .depth       = text.depth,
                .pipeline_id = *pipeline_id,
                .draw_func   = draw_function_id->value,
                .batch_count = 1,
            });
        }
    }
}

void prepare_text_batches(Query<Item<render::phase::RenderPhase<core_graph::core_2d::Transparent2D>&>,
                                With<render::camera::ExtractedCamera, render::view::ExtractedView>> views,
                          Query<Item<TextBatch&, const ExtractedText2d&>> texts,
                          Res<render::RenderAssets<image::Image>> images,
                          Res<wgpu::Device> device,
                          Res<wgpu::Queue> queue,
                          Res<Text2dPipelineCache> pipeline_cache,
                          ResMut<TextInstanceBuffer> instance_buffer) {
    instance_buffer->instances.clear();
    std::unordered_map<assets::AssetId<image::Image>, wgpu::BindGroup> texture_bind_group_cache;

    for (auto&& [phase] : views.iter()) {
        for (std::size_t item_index = 0; item_index < phase.items.size(); ++item_index) {
            auto& item     = phase.items[item_index];
            auto text_item = texts.get(item.entity());
            if (!text_item) {
                continue;
            }

            auto&& [batch, text] = *text_item;
            auto gpu_image       = images->try_get(text.font_image);
            if (!gpu_image) {
                continue;
            }

            batch.instance_start = static_cast<std::uint32_t>(instance_buffer->instances.size());
            if (auto it = texture_bind_group_cache.find(text.font_image); it != texture_bind_group_cache.end()) {
                batch.texture_bind_group = it->second;
            } else {
                batch.texture_bind_group = device->createBindGroup(
                    wgpu::BindGroupDescriptor()
                        .setLabel("TextTextureBindGroup")
                        .setLayout(pipeline_cache->texture_layout)
                        .setEntries(std::array{
                            wgpu::BindGroupEntry().setBinding(0).setSampler(gpu_image->sampler),
                            wgpu::BindGroupEntry().setBinding(1).setTextureView(gpu_image->view),
                        }));
                texture_bind_group_cache.emplace(text.font_image, *batch.texture_bind_group);
            }

            instance_buffer->instances.push_back(TextInstanceData{.model = text.model, .color = text.color});
            item.batch_count = 1;
        }
    }

    auto required_bytes = instance_buffer->instances.size() * sizeof(TextInstanceData);
    ensure_text_instance_buffer(*instance_buffer, *pipeline_cache, *device, required_bytes);
    if (required_bytes != 0) {
        queue->writeBuffer(instance_buffer->buffer, 0, instance_buffer->instances.data(), required_bytes);
    }
}
}  // namespace

TextMesh TextMesh::from_shaped_text(const ShapedText& shaped,
                                    assets::Assets<mesh::Mesh>& mesh_assets,
                                    font::FontAtlas& atlas) {
    mesh::Mesh mesh;
    mesh.set_primitive_type(wgpu::PrimitiveTopology::eTriangleList);
    (void)mesh.insert_attribute(mesh::Mesh::ATTRIBUTE_POSITION,
                                shaped.glyphs() | std::views::transform([&](const GlyphInfo& glyph_info) {
                                    std::array<glm::vec3, 4> positions;
                                    auto glyph   = atlas.get_glyph(glyph_info.glyph_index);
                                    float x0     = glyph_info.x_offset + glyph.horiBearingX;
                                    float y0     = glyph_info.y_offset - glyph.height + glyph.horiBearingY;
                                    float x1     = x0 + glyph.width;
                                    float y1     = y0 + glyph.height;
                                    positions[0] = glm::vec3{x0, y0, 0.0f};
                                    positions[1] = glm::vec3{x1, y0, 0.0f};
                                    positions[2] = glm::vec3{x1, y1, 0.0f};
                                    positions[3] = glm::vec3{x0, y1, 0.0f};
                                    return positions;
                                }) | std::views::join);
    (void)mesh.insert_attribute(kTextUvLayerAttribute,
                                shaped.glyphs() | std::views::transform([&](const GlyphInfo& glyph_info) {
                                    std::array<glm::vec3, 4> uvs;
                                    auto uv = atlas.get_glyph_uv_rect(glyph_info.glyph_index);
                                    uvs[0]  = glm::vec3{uv[0], uv[1], uv[4]};
                                    uvs[1]  = glm::vec3{uv[2], uv[1], uv[4]};
                                    uvs[2]  = glm::vec3{uv[2], uv[3], uv[4]};
                                    uvs[3]  = glm::vec3{uv[0], uv[3], uv[4]};
                                    return uvs;
                                }) | std::views::join);
    mesh.insert_indices<std::uint32_t>(std::views::iota(0u, static_cast<std::uint32_t>(shaped.glyphs().size())) |
                                       std::views::transform([](std::uint32_t glyph_index) {
                                           auto base = glyph_index * 4;
                                           return std::array<std::uint32_t, 6>{base + 0, base + 1, base + 2,
                                                                               base + 2, base + 3, base + 0};
                                       }) |
                                       std::views::join);
    auto mesh_handle = mesh_assets.emplace(std::move(mesh));
    return TextMesh(mesh_handle, shaped.left(), shaped.right(), shaped.top(), shaped.bottom(), shaped.ascent(),
                    shaped.descent());
}

void TextRenderPlugin::build(App& app) {
    spdlog::debug("[text] Building TextRenderPlugin.");
    app.add_plugins(core_graph::core_2d::Core2dPlugin{});
}

void TextRenderPlugin::finish(App& app) {
    spdlog::debug("[text] Finishing TextRenderPlugin.");
    auto shaders = app.world_mut().get_resource_mut<assets::Assets<shader::Shader>>();
    if (shaders) {
        insert_text_shaders(shaders->get());
    } else {
        spdlog::warn("[text] Assets<shader::Shader> is not available in the main world.");
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app) {
        spdlog::error("[text] TextRenderPlugin requires render::RenderPlugin to be added before it.");
        return;
    }

    auto& world = render_app->get().world_mut();
    if (!world.get_resource<TextInstanceBuffer>()) {
        world.insert_resource(TextInstanceBuffer{});
    }
    if (!world.get_resource<Text2dPipelineCache>()) {
        world.insert_resource(Text2dPipelineCache(world));
    }
    auto& render_subapp = render_app->get();
    world.insert_resource(TransparentTextDrawFunction{
        .value =
            render::phase::app_add_render_commands<core_graph::core_2d::Transparent2D, render::phase::SetItemPipeline,
                                                   render::view::BindViewUniform<0>::Command,
                                                   BindTextInstances<1>::Command, BindTextTexture<2>::Command,
                                                   DrawTextBatch>(render_subapp)});

#ifndef NDEBUG
    app.add_systems(Last, into(validate_text_2d).set_name("validate_text_2d"));
#endif
    render_subapp.add_systems(render::ExtractSchedule, into(extract_texts_2d).set_name("extract texts"))
        .add_systems(render::Render, into(queue_texts_2d).in_set(render::RenderSet::Queue).set_name("queue texts"))
        .add_systems(
            render::Render,
            into(prepare_text_batches).in_set(render::RenderSet::PrepareResources).set_name("prepare text batches"));
}
