module;

#include <spdlog/spdlog.h>

module epix.mesh;

import epix.core_graph;
import epix.image;
import epix.render;
import epix.transform;
import std;

using namespace epix;
using namespace epix::core;
using namespace epix::mesh;

namespace {
constexpr std::string_view kMeshSolidVertexShader = R"(
struct ViewUniform {
    projection : mat4x4<f32>,
    view : mat4x4<f32>,
};

struct MeshUniform {
    model : mat4x4<f32>,
    color : vec4<f32>,
};

@group(0) @binding(0) var<uniform> view_uniform : ViewUniform;
@group(1) @binding(0) var<storage, read> mesh_instances : array<MeshUniform>;

struct VertexInput {
    @location(0) position : vec3<f32>,
    @builtin(instance_index) instance_index : u32,
};

struct VertexOutput {
    @builtin(position) position : vec4<f32>,
    @location(0) color : vec4<f32>,
};

@vertex
fn main(input : VertexInput) -> VertexOutput {
    let mesh = mesh_instances[input.instance_index];
    var output : VertexOutput;
    output.position = view_uniform.projection * view_uniform.view * mesh.model * vec4<f32>(input.position, 1.0);
    output.color = mesh.color;
    return output;
}
)";

constexpr std::string_view kMeshVertexColorVertexShader = R"(
struct ViewUniform {
    projection : mat4x4<f32>,
    view : mat4x4<f32>,
};

struct MeshUniform {
    model : mat4x4<f32>,
    color : vec4<f32>,
};

@group(0) @binding(0) var<uniform> view_uniform : ViewUniform;
@group(1) @binding(0) var<storage, read> mesh_instances : array<MeshUniform>;

struct VertexInput {
    @location(0) position : vec3<f32>,
    @location(1) color : vec4<f32>,
    @builtin(instance_index) instance_index : u32,
};

struct VertexOutput {
    @builtin(position) position : vec4<f32>,
    @location(0) color : vec4<f32>,
};

@vertex
fn main(input : VertexInput) -> VertexOutput {
    let mesh = mesh_instances[input.instance_index];
    var output : VertexOutput;
    output.position = view_uniform.projection * view_uniform.view * mesh.model * vec4<f32>(input.position, 1.0);
    output.color = input.color * mesh.color;
    return output;
}
)";

constexpr std::string_view kMeshTexturedVertexShader = R"(
struct ViewUniform {
    projection : mat4x4<f32>,
    view : mat4x4<f32>,
};

struct MeshUniform {
    model : mat4x4<f32>,
    color : vec4<f32>,
};

@group(0) @binding(0) var<uniform> view_uniform : ViewUniform;
@group(1) @binding(0) var<storage, read> mesh_instances : array<MeshUniform>;

struct VertexInput {
    @location(0) position : vec3<f32>,
    @location(3) uv : vec2<f32>,
    @builtin(instance_index) instance_index : u32,
};

struct VertexOutput {
    @builtin(position) position : vec4<f32>,
    @location(0) color : vec4<f32>,
    @location(1) uv : vec2<f32>,
};

@vertex
fn main(input : VertexInput) -> VertexOutput {
    let mesh = mesh_instances[input.instance_index];
    var output : VertexOutput;
    output.position = view_uniform.projection * view_uniform.view * mesh.model * vec4<f32>(input.position, 1.0);
    output.color = mesh.color;
    output.uv = input.uv;
    return output;
}
)";

constexpr std::string_view kMeshTexturedVertexColorVertexShader = R"(
struct ViewUniform {
    projection : mat4x4<f32>,
    view : mat4x4<f32>,
};

struct MeshUniform {
    model : mat4x4<f32>,
    color : vec4<f32>,
};

@group(0) @binding(0) var<uniform> view_uniform : ViewUniform;
@group(1) @binding(0) var<storage, read> mesh_instances : array<MeshUniform>;

struct VertexInput {
    @location(0) position : vec3<f32>,
    @location(1) color : vec4<f32>,
    @location(3) uv : vec2<f32>,
    @builtin(instance_index) instance_index : u32,
};

struct VertexOutput {
    @builtin(position) position : vec4<f32>,
    @location(0) color : vec4<f32>,
    @location(1) uv : vec2<f32>,
};

@vertex
fn main(input : VertexInput) -> VertexOutput {
    let mesh = mesh_instances[input.instance_index];
    var output : VertexOutput;
    output.position = view_uniform.projection * view_uniform.view * mesh.model * vec4<f32>(input.position, 1.0);
    output.color = input.color * mesh.color;
    output.uv = input.uv;
    return output;
}
)";

constexpr std::string_view kMeshColorFragmentShader = R"(
struct FragmentInput {
    @location(0) color : vec4<f32>,
};

@fragment
fn main(input : FragmentInput) -> @location(0) vec4<f32> {
    return input.color;
}
)";

constexpr std::string_view kMeshTexturedFragmentShader = R"(
@group(2) @binding(0) var mesh_sampler : sampler;
@group(2) @binding(1) var mesh_texture : texture_2d<f32>;

struct FragmentInput {
    @location(0) color : vec4<f32>,
    @location(1) uv : vec2<f32>,
};

@fragment
fn main(input : FragmentInput) -> @location(0) vec4<f32> {
    return textureSample(mesh_texture, mesh_sampler, input.uv) * input.color;
}
)";

const assets::AssetId<shader::Shader> kMeshSolidVertexShaderId(
    uuids::uuid::from_string("63191e5e-7ef8-4f38-b08a-c3e1fb7ebc31").value());
const assets::AssetId<shader::Shader> kMeshVertexColorShaderId(
    uuids::uuid::from_string("a9fbc001-c1df-4f55-8b42-a789aa0434e5").value());
const assets::AssetId<shader::Shader> kMeshTexturedVertexShaderId(
    uuids::uuid::from_string("4e8cc4d7-fffc-4d15-85a6-4661905f8a87").value());
const assets::AssetId<shader::Shader> kMeshTexturedVertexColorShaderId(
    uuids::uuid::from_string("cde0ce74-ed98-43ca-8995-b294fce1b372").value());
const assets::AssetId<shader::Shader> kMeshColorFragmentShaderId(
    uuids::uuid::from_string("906bb4fb-0cd4-44d0-adbf-5becf7352034").value());
const assets::AssetId<shader::Shader> kMeshTexturedFragmentShaderId(
    uuids::uuid::from_string("77d59064-22e7-42b1-a09c-c885320f4678").value());

enum class MeshShaderVariant : std::uint8_t {
    SolidColor,
    VertexColor,
    Textured,
    TexturedVertexColor,
};

struct Mesh2dPipelineKey {
    MeshShaderVariant variant;
    MeshAlphaMode2d alpha_mode;
    wgpu::PrimitiveTopology primitive_type;
    wgpu::TextureFormat color_format;

    bool operator==(const Mesh2dPipelineKey&) const = default;
};

struct Mesh2dPipelineKeyHash {
    std::size_t operator()(const Mesh2dPipelineKey& key) const {
        std::size_t hash = std::hash<std::uint8_t>()(static_cast<std::uint8_t>(key.variant));
        hash ^= std::hash<std::uint8_t>()(static_cast<std::uint8_t>(key.alpha_mode)) + 0x9e3779b9 + (hash << 6) +
                (hash >> 2);
        hash ^= std::hash<std::uint32_t>()(static_cast<std::uint32_t>(key.primitive_type)) + 0x9e3779b9 + (hash << 6) +
                (hash >> 2);
        hash ^= std::hash<std::uint32_t>()(static_cast<std::uint32_t>(key.color_format)) + 0x9e3779b9 + (hash << 6) +
                (hash >> 2);
        return hash;
    }
};

constexpr const char* shader_variant_name(MeshShaderVariant variant) {
    switch (variant) {
        case MeshShaderVariant::SolidColor:
            return "solid";
        case MeshShaderVariant::VertexColor:
            return "vertex-color";
        case MeshShaderVariant::Textured:
            return "textured";
        case MeshShaderVariant::TexturedVertexColor:
            return "textured-vertex-color";
        default:
            return "unknown";
    }
}

constexpr const char* alpha_mode_name(MeshAlphaMode2d alpha_mode) {
    switch (alpha_mode) {
        case MeshAlphaMode2d::Opaque:
            return "opaque";
        case MeshAlphaMode2d::Blend:
            return "blend";
        default:
            return "unknown";
    }
}

struct Mesh2dPipelineCache {
    wgpu::BindGroupLayout view_layout;
    wgpu::BindGroupLayout mesh_layout;
    wgpu::BindGroupLayout texture_layout;
    assets::Handle<shader::Shader> solid_vertex_shader{kMeshSolidVertexShaderId};
    assets::Handle<shader::Shader> vertex_color_shader{kMeshVertexColorShaderId};
    assets::Handle<shader::Shader> textured_vertex_shader{kMeshTexturedVertexShaderId};
    assets::Handle<shader::Shader> textured_vertex_color_shader{kMeshTexturedVertexColorShaderId};
    assets::Handle<shader::Shader> color_fragment_shader{kMeshColorFragmentShaderId};
    assets::Handle<shader::Shader> textured_fragment_shader{kMeshTexturedFragmentShaderId};
    std::unordered_map<Mesh2dPipelineKey, render::CachedPipelineId, Mesh2dPipelineKeyHash> pipelines;

    explicit Mesh2dPipelineCache(World& world)
        : view_layout(world.resource<render::view::ViewUniformBindingLayout>().layout),
          mesh_layout(world.resource<wgpu::Device>().createBindGroupLayout(
              wgpu::BindGroupLayoutDescriptor()
                  .setLabel("Mesh2dInstanceLayout")
                  .setEntries(std::array{
                      wgpu::BindGroupLayoutEntry()
                          .setBinding(0)
                          .setVisibility(wgpu::ShaderStage::eVertex)
                          .setBuffer(wgpu::BufferBindingLayout()
                                         .setType(wgpu::BufferBindingType::eReadOnlyStorage)
                                         .setHasDynamicOffset(false)
                                         .setMinBindingSize(sizeof(MeshInstanceData))),
                  }))),
          texture_layout(world.resource<wgpu::Device>().createBindGroupLayout(
              wgpu::BindGroupLayoutDescriptor()
                  .setLabel("Mesh2dTextureLayout")
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
                                                       const MeshAttributeLayout& layout,
                                                       wgpu::TextureFormat color_format,
                                                       MeshAlphaMode2d alpha_mode,
                                                       bool textured) {
        if (!layout.get_attribute(Mesh::ATTRIBUTE_POSITION)) {
            spdlog::warn("[mesh] Skip pipeline specialization: mesh layout is missing POSITION. Layout:\n{}",
                         layout.to_string());
            return std::nullopt;
        }

        bool has_color = layout.contains_attribute(Mesh::ATTRIBUTE_COLOR);
        bool has_uv    = layout.contains_attribute(Mesh::ATTRIBUTE_UV0);
        if (textured && !has_uv) {
            spdlog::warn("[mesh] Skip textured pipeline specialization: mesh layout is missing UV0. Layout:\n{}",
                         layout.to_string());
            return std::nullopt;
        }

        MeshShaderVariant variant = MeshShaderVariant::SolidColor;
        if (textured && has_color) {
            variant = MeshShaderVariant::TexturedVertexColor;
        } else if (textured) {
            variant = MeshShaderVariant::Textured;
        } else if (has_color) {
            variant = MeshShaderVariant::VertexColor;
        }

        Mesh2dPipelineKey key{
            .variant        = variant,
            .alpha_mode     = alpha_mode,
            .primitive_type = layout.primitive_type,
            .color_format   = color_format,
        };
        if (auto it = pipelines.find(key); it != pipelines.end()) {
            return it->second;
        }

        std::vector<wgpu::VertexBufferLayout> vertex_buffers =
            layout | std::views::values | std::views::transform([](const MeshAttribute& attribute) {
                return wgpu::VertexBufferLayout()
                    .setArrayStride(vertex_format_size(attribute.format))
                    .setStepMode(wgpu::VertexStepMode::eVertex)
                    .setAttributes(std::array{
                        wgpu::VertexAttribute()
                            .setShaderLocation(attribute.slot)
                            .setFormat(attribute.format)
                            .setOffset(0),
                    });
            }) |
            std::ranges::to<std::vector>();

        render::VertexState vertex_state{
            .shader = [&]() -> assets::Handle<shader::Shader> {
                switch (variant) {
                    case MeshShaderVariant::SolidColor:
                        return solid_vertex_shader;
                    case MeshShaderVariant::VertexColor:
                        return vertex_color_shader;
                    case MeshShaderVariant::Textured:
                        return textured_vertex_shader;
                    case MeshShaderVariant::TexturedVertexColor:
                        return textured_vertex_color_shader;
                    default:
                        return solid_vertex_shader;
                }
            }(),
        };
        vertex_state.set_buffers(vertex_buffers);

        render::FragmentState fragment_state{
            .shader = textured ? textured_fragment_shader : color_fragment_shader,
        };

        auto color_target = wgpu::ColorTargetState().setFormat(color_format).setWriteMask(wgpu::ColorWriteMask::eAll);
        if (alpha_mode == MeshAlphaMode2d::Blend) {
            auto color_blend = wgpu::BlendComponent()
                                   .setOperation(wgpu::BlendOperation::eAdd)
                                   .setSrcFactor(wgpu::BlendFactor::eSrcAlpha)
                                   .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha);
            auto alpha_blend = wgpu::BlendComponent()
                                   .setOperation(wgpu::BlendOperation::eAdd)
                                   .setSrcFactor(wgpu::BlendFactor::eOne)
                                   .setDstFactor(wgpu::BlendFactor::eOneMinusSrcAlpha);
            color_target.setBlend(wgpu::BlendState().setColor(color_blend).setAlpha(alpha_blend));
        }
        fragment_state.add_target(color_target);

        auto layouts = std::vector<wgpu::BindGroupLayout>{view_layout, mesh_layout};
        if (textured) {
            layouts.push_back(texture_layout);
        }

        render::RenderPipelineDescriptor pipeline_desc{
            .label     = std::format("mesh2d-{}-{}-{}", shader_variant_name(variant), alpha_mode_name(alpha_mode),
                                     wgpu::to_string(layout.primitive_type)),
            .layouts   = std::move(layouts),
            .vertex    = std::move(vertex_state),
            .primitive = wgpu::PrimitiveState()
                             .setTopology(layout.primitive_type)
                             .setFrontFace(wgpu::FrontFace::eCCW)
                             .setCullMode(wgpu::CullMode::eNone),
            .depth_stencil =
                wgpu::DepthStencilState()
                    .setFormat(wgpu::TextureFormat::eDepth32Float)
                    .setDepthWriteEnabled(alpha_mode == MeshAlphaMode2d::Opaque ? wgpu::OptionalBool::eTrue
                                                                                : wgpu::OptionalBool::eFalse)
                    .setDepthCompare(wgpu::CompareFunction::eLessEqual),
            .multisample = wgpu::MultisampleState().setCount(1).setMask(~0u).setAlphaToCoverageEnabled(false),
            .fragment    = std::move(fragment_state),
        };

        auto pipeline_id = pipeline_server.queue_render_pipeline(std::move(pipeline_desc));
        pipelines.emplace(key, pipeline_id);
        return pipeline_id;
    }
};

struct OpaqueMesh2dDrawFunction {
    render::phase::DrawFunctionId value;
};

struct TransparentMesh2dDrawFunction {
    render::phase::DrawFunctionId value;
};

struct MeshOpaqueBatchKey {
    std::uint64_t pipeline_id;
    assets::AssetId<Mesh> mesh_id;
    std::optional<assets::AssetId<image::Image>> texture_id;

    std::strong_ordering operator<=>(const MeshOpaqueBatchKey& other) const = default;
    bool operator==(const MeshOpaqueBatchKey&) const                        = default;
};

void insert_mesh_shaders(assets::Assets<shader::Shader>& shaders) {
    auto solid_vertex_path          = std::filesystem::path("embedded://mesh/solid_vertex.wgsl");
    auto vertex_color_path          = std::filesystem::path("embedded://mesh/vertex_color_vertex.wgsl");
    auto textured_vertex_path       = std::filesystem::path("embedded://mesh/textured_vertex.wgsl");
    auto textured_vertex_color_path = std::filesystem::path("embedded://mesh/textured_vertex_color_vertex.wgsl");
    auto color_fragment_path        = std::filesystem::path("embedded://mesh/color_fragment.wgsl");
    auto textured_fragment_path     = std::filesystem::path("embedded://mesh/textured_fragment.wgsl");
    [[maybe_unused]] auto solid_vertex =
        shaders.insert(kMeshSolidVertexShaderId,
                       shader::Shader::from_wgsl(std::string(kMeshSolidVertexShader), solid_vertex_path.string()));
    [[maybe_unused]] auto vertex_color_vertex = shaders.insert(
        kMeshVertexColorShaderId,
        shader::Shader::from_wgsl(std::string(kMeshVertexColorVertexShader), vertex_color_path.string()));
    [[maybe_unused]] auto textured_vertex = shaders.insert(
        kMeshTexturedVertexShaderId,
        shader::Shader::from_wgsl(std::string(kMeshTexturedVertexShader), textured_vertex_path.string()));
    [[maybe_unused]] auto textured_vertex_color = shaders.insert(
        kMeshTexturedVertexColorShaderId, shader::Shader::from_wgsl(std::string(kMeshTexturedVertexColorVertexShader),
                                                                    textured_vertex_color_path.string()));
    [[maybe_unused]] auto color_fragment =
        shaders.insert(kMeshColorFragmentShaderId,
                       shader::Shader::from_wgsl(std::string(kMeshColorFragmentShader), color_fragment_path.string()));
    [[maybe_unused]] auto textured_fragment = shaders.insert(
        kMeshTexturedFragmentShaderId,
        shader::Shader::from_wgsl(std::string(kMeshTexturedFragmentShader), textured_fragment_path.string()));
}

void extract_meshes_2d(Commands cmd,
                       Extract<Query<Item<Entity,
                                          const Mesh2d&,
                                          const transform::GlobalTransform&,
                                          Opt<const MeshMaterial2d&>,
                                          Opt<const MeshTextureMaterial2d&>>,
                                     Without<render::CustomRendered>>> meshes) {
    for (auto&& [entity, mesh_handle, transform, material, texture_material] : meshes.iter()) {
        glm::vec4 color = texture_material.transform([](const MeshTextureMaterial2d& value) { return value.color; })
                              .value_or(material.transform([](const MeshMaterial2d& value) { return value.color; })
                                            .value_or(glm::vec4(1.0f)));
        MeshAlphaMode2d alpha_mode =
            texture_material.transform([](const MeshTextureMaterial2d& value) { return value.alpha_mode; })
                .value_or(material.transform([](const MeshMaterial2d& value) { return value.alpha_mode; })
                              .value_or(MeshAlphaMode2d::Opaque));

        cmd.spawn(
            ExtractedMesh2d{
                .source_entity = entity,
                .mesh          = mesh_handle.handle.id(),
                .model         = transform.matrix,
                .color         = color,
                .depth         = transform.matrix[3][2],
                .alpha_mode    = alpha_mode,
                .texture =
                    texture_material.transform([](const MeshTextureMaterial2d& value) { return value.image.id(); }),
            },
            MeshBatch{});
    }
}

void ensure_mesh_instance_buffer(MeshInstanceBuffer& instance_buffer,
                                 const Mesh2dPipelineCache& pipeline_cache,
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
                                    .setLabel("Mesh2dInstanceBuffer")
                                    .setSize(buffer_size)
                                    .setUsage(wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst));
        instance_buffer.bind_group = device.createBindGroup(wgpu::BindGroupDescriptor()
                                                                .setLabel("Mesh2dInstanceBindGroup")
                                                                .setLayout(pipeline_cache.mesh_layout)
                                                                .setEntries(std::array{
                                                                    wgpu::BindGroupEntry()
                                                                        .setBinding(0)
                                                                        .setBuffer(instance_buffer.buffer)
                                                                        .setOffset(0)
                                                                        .setSize(buffer_size),
                                                                }));
    }
}

struct MeshBatchKey {
    render::CachedPipelineId pipeline_id;
    assets::AssetId<Mesh> mesh_id;
    std::optional<assets::AssetId<image::Image>> texture_id;

    bool operator==(const MeshBatchKey&) const = default;
};

void prepare_mesh_instances(Query<Item<render::phase::RenderPhase<core_graph::core_2d::Opaque2D>&>,
                                  With<render::camera::ExtractedCamera, render::view::ExtractedView>> opaque_views,
                            Query<Item<render::phase::RenderPhase<core_graph::core_2d::Transparent2D>&>,
                                  With<render::camera::ExtractedCamera, render::view::ExtractedView>> transparent_views,
                            Query<Item<MeshBatch&, const ExtractedMesh2d&>> meshes,
                            Res<render::RenderAssets<image::Image>> images,
                            Res<wgpu::Device> device,
                            Res<wgpu::Queue> queue,
                            Res<Mesh2dPipelineCache> pipeline_cache,
                            ResMut<MeshInstanceBuffer> instance_buffer) {
    instance_buffer->instances.clear();
    std::unordered_map<assets::AssetId<image::Image>, wgpu::BindGroup> texture_bind_group_cache;

    auto process_phase = [&](auto& phase) {
        std::optional<MeshBatchKey> current_key;
        std::size_t batch_head = std::numeric_limits<std::size_t>::max();

        for (std::size_t item_index = 0; item_index < phase.items.size(); ++item_index) {
            auto& item     = phase.items[item_index];
            auto mesh_item = meshes.get(item.entity());
            if (!mesh_item) {
                current_key.reset();
                batch_head = std::numeric_limits<std::size_t>::max();
                continue;
            }

            auto&& [batch, extracted] = *mesh_item;

            MeshBatchKey key{
                .pipeline_id = item.pipeline(),
                .mesh_id     = extracted.mesh,
                .texture_id  = extracted.texture,
            };

            if (!current_key || *current_key != key) {
                batch_head                          = item_index;
                phase.items[batch_head].batch_count = 0;
                batch.instance_start                = static_cast<std::uint32_t>(instance_buffer->instances.size());

                if (extracted.texture) {
                    if (auto it = texture_bind_group_cache.find(*extracted.texture);
                        it != texture_bind_group_cache.end()) {
                        batch.texture_bind_group = it->second;
                    } else if (auto gpu_image = images->try_get(*extracted.texture); gpu_image) {
                        auto tg = device->createBindGroup(
                            wgpu::BindGroupDescriptor()
                                .setLabel("Mesh2dTextureBindGroup")
                                .setLayout(pipeline_cache->texture_layout)
                                .setEntries(std::array{
                                    wgpu::BindGroupEntry().setBinding(0).setSampler(gpu_image->sampler),
                                    wgpu::BindGroupEntry().setBinding(1).setTextureView(gpu_image->view),
                                }));
                        texture_bind_group_cache.emplace(*extracted.texture, tg);
                        batch.texture_bind_group = std::move(tg);
                    } else {
                        batch.texture_bind_group.reset();
                    }
                } else {
                    batch.texture_bind_group.reset();
                }

                current_key = key;
            }

            instance_buffer->instances.push_back({extracted.model, extracted.color});
            phase.items[batch_head].batch_count++;
        }
    };

    for (auto&& [phase] : opaque_views.iter()) {
        process_phase(phase);
    }
    for (auto&& [phase] : transparent_views.iter()) {
        process_phase(phase);
    }

    auto required_bytes = instance_buffer->instances.size() * sizeof(MeshInstanceData);
    ensure_mesh_instance_buffer(*instance_buffer, *pipeline_cache, *device, required_bytes);
    if (required_bytes != 0) {
        queue->writeBuffer(instance_buffer->buffer, 0, instance_buffer->instances.data(), required_bytes);
    }
}

void queue_meshes_2d_opaque(
    Query<Item<render::phase::RenderPhase<core_graph::core_2d::Opaque2D>&, const render::view::ViewTarget&>,
          With<render::camera::ExtractedCamera>> views,
    Query<Item<Entity, const ExtractedMesh2d&>> meshes,
    Res<render::RenderAssets<Mesh>> gpu_meshes,
    Res<render::RenderAssets<image::Image>> images,
    Res<OpaqueMesh2dDrawFunction> draw_function_id,
    ResMut<Mesh2dPipelineCache> pipeline_cache,
    ResMut<render::PipelineServer> pipeline_server) {
    for (auto&& [phase, target] : views.iter()) {
        for (auto&& [entity, extracted_mesh] : meshes.iter()) {
            if (extracted_mesh.alpha_mode != MeshAlphaMode2d::Opaque) {
                continue;
            }

            auto* gpu_mesh = gpu_meshes->try_get(extracted_mesh.mesh);
            if (!gpu_mesh) {
                spdlog::warn("[mesh] Skip opaque mesh entity {:#x}: GPU mesh {} is not prepared yet.", entity.index,
                             extracted_mesh.mesh.to_string_short());
                continue;
            }
            if (gpu_mesh->vertex_count() == 0) {
                spdlog::debug("[mesh] Skip opaque mesh entity {:#x}: GPU mesh {} is empty.", entity.index,
                              extracted_mesh.mesh.to_string_short());
                continue;
            }
            if (extracted_mesh.texture && !images->try_get(*extracted_mesh.texture)) {
                spdlog::warn("[mesh] Skip opaque textured mesh entity {:#x}: GPU image {} is not available yet.",
                             entity.index, extracted_mesh.texture->to_string_short());
                continue;
            }

            auto pipeline_id =
                pipeline_cache->specialize(*pipeline_server, gpu_mesh->attribute_layout(), target.format,
                                           extracted_mesh.alpha_mode, extracted_mesh.texture.has_value());
            if (!pipeline_id) {
                spdlog::warn("[mesh] Skip opaque mesh entity {:#x}: failed to specialize pipeline for layout:\n{}",
                             entity.index, gpu_mesh->attribute_layout().to_string());
                continue;
            }

            phase.add(core_graph::core_2d::Opaque2D{
                .id          = entity,
                .pipeline_id = *pipeline_id,
                .draw_func   = draw_function_id->value,
                .batch_count = 1,
                .batch_key   = render::phase::OpaqueSortKey(MeshOpaqueBatchKey{
                    .pipeline_id = pipeline_id->get(),
                    .mesh_id     = extracted_mesh.mesh,
                    .texture_id  = extracted_mesh.texture,
                }),
            });
        }
    }
}

void queue_meshes_2d_transparent(
    Query<Item<render::phase::RenderPhase<core_graph::core_2d::Transparent2D>&, const render::view::ViewTarget&>,
          With<render::camera::ExtractedCamera>> views,
    Query<Item<Entity, const ExtractedMesh2d&>> meshes,
    Res<render::RenderAssets<Mesh>> gpu_meshes,
    Res<render::RenderAssets<image::Image>> images,
    Res<TransparentMesh2dDrawFunction> draw_function_id,
    ResMut<Mesh2dPipelineCache> pipeline_cache,
    ResMut<render::PipelineServer> pipeline_server) {
    for (auto&& [phase, target] : views.iter()) {
        for (auto&& [entity, extracted_mesh] : meshes.iter()) {
            if (extracted_mesh.alpha_mode != MeshAlphaMode2d::Blend) {
                continue;
            }

            auto* gpu_mesh = gpu_meshes->try_get(extracted_mesh.mesh);
            if (!gpu_mesh) {
                spdlog::warn("[mesh] Skip transparent mesh entity {:#x}: GPU mesh {} is not prepared yet.",
                             entity.index, extracted_mesh.mesh.to_string_short());
                continue;
            }
            if (gpu_mesh->vertex_count() == 0) {
                spdlog::debug("[mesh] Skip transparent mesh entity {:#x}: GPU mesh {} is empty.", entity.index,
                              extracted_mesh.mesh.to_string_short());
                continue;
            }
            if (extracted_mesh.texture && !images->try_get(*extracted_mesh.texture)) {
                spdlog::warn("[mesh] Skip transparent textured mesh entity {:#x}: GPU image {} is not available yet.",
                             entity.index, extracted_mesh.texture->to_string_short());
                continue;
            }

            auto pipeline_id =
                pipeline_cache->specialize(*pipeline_server, gpu_mesh->attribute_layout(), target.format,
                                           extracted_mesh.alpha_mode, extracted_mesh.texture.has_value());
            if (!pipeline_id) {
                spdlog::warn("[mesh] Skip transparent mesh entity {:#x}: failed to specialize pipeline for layout:\n{}",
                             entity.index, gpu_mesh->attribute_layout().to_string());
                continue;
            }

            phase.add(core_graph::core_2d::Transparent2D{
                .id          = entity,
                .depth       = extracted_mesh.depth,
                .pipeline_id = *pipeline_id,
                .draw_func   = draw_function_id->value,
                .batch_count = 1,
            });
        }
    }
}
}  // namespace

void MeshRenderPlugin::build(core::App& app) {
    spdlog::debug("[mesh] Building MeshRenderPlugin.");
    app.add_plugins(MeshPlugin{});
    app.add_plugins(core_graph::core_2d::Core2dPlugin{});
    app.add_plugins(render::ExtractAssetPlugin<Mesh>{});
}

void MeshRenderPlugin::finish(core::App& app) {
    spdlog::debug("[mesh] Finishing MeshRenderPlugin.");
    auto shaders = app.world_mut().get_resource_mut<assets::Assets<shader::Shader>>();
    if (shaders) {
        insert_mesh_shaders(shaders->get());
    } else {
        spdlog::warn(
            "[mesh] MeshRenderPlugin could not find Assets<shader::Shader> in the main world. Internal mesh shaders "
            "were not inserted.");
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app) {
        spdlog::error(
            "[mesh] MeshRenderPlugin requires the render sub-app, but it was not found. Did you add "
            "render::RenderPlugin before MeshRenderPlugin?");
        return;
    }

    auto& world = render_app->get().world_mut();
    if (!world.get_resource<MeshInstanceBuffer>()) {
        world.insert_resource(MeshInstanceBuffer{});
    }
    if (!world.get_resource<Mesh2dPipelineCache>()) {
        world.init_resource<Mesh2dPipelineCache>();
    }
    auto& render_subapp = render_app->get();
    world.insert_resource(OpaqueMesh2dDrawFunction{
        .value = render::phase::app_add_render_commands<
            core_graph::core_2d::Opaque2D, render::phase::SetItemPipeline, render::view::BindViewUniform<0>::Command,
            mesh::BindMesh2dInstances<1>::Command, mesh::BindMesh2dTexture<2>::Command, mesh::DrawMesh2dBatch>(
            render_subapp)});
    world.insert_resource(TransparentMesh2dDrawFunction{
        .value = render::phase::app_add_render_commands<
            core_graph::core_2d::Transparent2D, render::phase::SetItemPipeline,
            render::view::BindViewUniform<0>::Command, mesh::BindMesh2dInstances<1>::Command,
            mesh::BindMesh2dTexture<2>::Command, mesh::DrawMesh2dBatch>(render_subapp)});

    render_subapp.add_systems(render::ExtractSchedule, into(extract_meshes_2d).set_name("extract mesh2d"))
        .add_systems(render::Render, into(queue_meshes_2d_opaque, queue_meshes_2d_transparent)
                                         .in_set(render::RenderSet::Queue)
                                         .set_names(std::array{"queue opaque mesh2d", "queue transparent mesh2d"}))
        .add_systems(render::Render, into(prepare_mesh_instances)
                                         .in_set(render::RenderSet::PrepareResources)
                                         .set_name("prepare mesh2d instances"));
}
