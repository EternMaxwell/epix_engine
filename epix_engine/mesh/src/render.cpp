
#include <spdlog/spdlog.h>

#include <epix/core_graph.hpp>
#include <epix/image.hpp>
#include <epix/mesh.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>
using namespace epix;
using namespace epix::ecs;
using namespace epix::app;
using namespace epix::mesh;

static_assert(render::HasTakeGpuData<Mesh>,
              "Mesh uses RENDER_WORLD-only extraction and must transfer its GPU payload.");

namespace {

void calculate_mesh2d_bounds(Commands cmd,
                             Query<Item<Entity,
                                        const Mesh2d&,
                                        Opt<Mut<camera::Aabb>>,
                                        Opt<const camera::NoAutoAabb&>,
                                        Opt<const camera::NoFrustumCulling&>>> meshes,
                             Res<assets::Assets<Mesh>> mesh_assets) {
    for (auto&& [entity, mesh2d, existing_aabb, no_auto_aabb, no_frustum_culling] : meshes.iter()) {
        if (no_auto_aabb || no_frustum_culling) continue;
        const auto mesh = mesh_assets->get(mesh2d.handle.id());
        if (!mesh) continue;
        const auto aabb = mesh->get().compute_aabb();
        if (!aabb) continue;
        if (existing_aabb) {
            existing_aabb->get_mut() = *aabb;
        } else {
            cmd.entity(entity).insert(*aabb);
        }
    }
}

constexpr std::string_view kMeshSolidVertexShader = R"(
import epix.view;

struct MeshUniform {
    float4x4 model;
    float4 color;
    float alpha_cutoff;
};

[[vk::binding(0, 0)]] ConstantBuffer<epix::view::View> view_uniform;
[[vk::binding(0, 1)]] StructuredBuffer<MeshUniform> mesh_instances;

struct VertexInput {
    [[vk::location(0)]] float3 position;
    uint instance_index : SV_VulkanInstanceID;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color;
    [[vk::location(2)]] float alpha_cutoff;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
    MeshUniform mesh = mesh_instances[input.instance_index];
    VertexOutput output;
    output.position = mul(view_uniform.clip_from_view, mul(view_uniform.view_from_world, mul(mesh.model, float4(input.position, 1.0))));
    output.color = mesh.color;
    output.alpha_cutoff = mesh.alpha_cutoff;
    return output;
}
)";

constexpr std::string_view kMeshVertexColorVertexShader = R"(
import epix.view;

struct MeshUniform {
    float4x4 model;
    float4 color;
    float alpha_cutoff;
};

[[vk::binding(0, 0)]] ConstantBuffer<epix::view::View> view_uniform;
[[vk::binding(0, 1)]] StructuredBuffer<MeshUniform> mesh_instances;

struct VertexInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float4 color;
    uint instance_index : SV_VulkanInstanceID;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color;
    [[vk::location(2)]] float alpha_cutoff;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
    MeshUniform mesh = mesh_instances[input.instance_index];
    VertexOutput output;
    output.position = mul(view_uniform.clip_from_view, mul(view_uniform.view_from_world, mul(mesh.model, float4(input.position, 1.0))));
    output.color = input.color * mesh.color;
    output.alpha_cutoff = mesh.alpha_cutoff;
    return output;
}
)";

constexpr std::string_view kMeshTexturedVertexShader = R"(
import epix.view;

struct MeshUniform {
    float4x4 model;
    float4 color;
    float alpha_cutoff;
};

[[vk::binding(0, 0)]] ConstantBuffer<epix::view::View> view_uniform;
[[vk::binding(0, 1)]] StructuredBuffer<MeshUniform> mesh_instances;

struct VertexInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(3)]] float2 uv;
    uint instance_index : SV_VulkanInstanceID;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color;
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float alpha_cutoff;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
    MeshUniform mesh = mesh_instances[input.instance_index];
    VertexOutput output;
    output.position = mul(view_uniform.clip_from_view, mul(view_uniform.view_from_world, mul(mesh.model, float4(input.position, 1.0))));
    output.color = mesh.color;
    output.uv = input.uv;
    output.alpha_cutoff = mesh.alpha_cutoff;
    return output;
}
)";

constexpr std::string_view kMeshTexturedVertexColorVertexShader = R"(
import epix.view;

struct MeshUniform {
    float4x4 model;
    float4 color;
    float alpha_cutoff;
};

[[vk::binding(0, 0)]] ConstantBuffer<epix::view::View> view_uniform;
[[vk::binding(0, 1)]] StructuredBuffer<MeshUniform> mesh_instances;

struct VertexInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float4 color;
    [[vk::location(3)]] float2 uv;
    uint instance_index : SV_VulkanInstanceID;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color;
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float alpha_cutoff;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
    MeshUniform mesh = mesh_instances[input.instance_index];
    VertexOutput output;
    output.position = mul(view_uniform.clip_from_view, mul(view_uniform.view_from_world, mul(mesh.model, float4(input.position, 1.0))));
    output.color = input.color * mesh.color;
    output.uv = input.uv;
    output.alpha_cutoff = mesh.alpha_cutoff;
    return output;
}
)";

constexpr std::string_view kMeshColorFragmentShader = R"(
struct FragmentInput {
    [[vk::location(0)]] float4 color;
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target {
    return input.color;
}
)";

constexpr std::string_view kMeshTexturedFragmentShader = R"(
[[vk::binding(0, 2)]] SamplerState mesh_sampler;
[[vk::binding(1, 2)]] Texture2D<float4> mesh_texture;

struct FragmentInput {
    [[vk::location(0)]] float4 color;
    [[vk::location(1)]] float2 uv;
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target {
    return mesh_texture.Sample(mesh_sampler, input.uv) * input.color;
}
)";

constexpr std::string_view kMeshColorMaskFragmentShader = R"(
struct FragmentInput {
    [[vk::location(0)]] float4 color;
    [[vk::location(2)]] float alpha_cutoff;
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target {
    if (input.color.a < input.alpha_cutoff) discard;
    return float4(input.color.rgb, 1.0);
}
)";

constexpr std::string_view kMeshTexturedMaskFragmentShader = R"(
[[vk::binding(0, 2)]] SamplerState mesh_sampler;
[[vk::binding(1, 2)]] Texture2D<float4> mesh_texture;

struct FragmentInput {
    [[vk::location(0)]] float4 color;
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float alpha_cutoff;
};

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target {
    float4 color = mesh_texture.Sample(mesh_sampler, input.uv) * input.color;
    if (color.a < input.alpha_cutoff) discard;
    return float4(color.rgb, 1.0);
}
)";

constexpr std::string_view kMeshSolidVertexShaderAssetPath         = "mesh/solid_vertex.slang";
constexpr std::string_view kMeshVertexColorShaderAssetPath         = "mesh/vertex_color_vertex.slang";
constexpr std::string_view kMeshTexturedVertexShaderAssetPath      = "mesh/textured_vertex.slang";
constexpr std::string_view kMeshTexturedVertexColorShaderAssetPath = "mesh/textured_vertex_color_vertex.slang";
constexpr std::string_view kMeshColorFragmentShaderAssetPath       = "mesh/color_fragment.slang";
constexpr std::string_view kMeshTexturedFragmentShaderAssetPath    = "mesh/textured_fragment.slang";
constexpr std::string_view kMeshColorMaskFragmentShaderAssetPath   = "mesh/color_mask_fragment.slang";
constexpr std::string_view kMeshTexturedMaskFragmentShaderAssetPath = "mesh/textured_mask_fragment.slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(source.data()), source.size());
}

struct MeshShaderHandles {
    assets::Handle<shader::Shader> solid_vertex_shader;
    assets::Handle<shader::Shader> vertex_color_shader;
    assets::Handle<shader::Shader> textured_vertex_shader;
    assets::Handle<shader::Shader> textured_vertex_color_shader;
    assets::Handle<shader::Shader> color_fragment_shader;
    assets::Handle<shader::Shader> textured_fragment_shader;
    assets::Handle<shader::Shader> color_mask_fragment_shader;
    assets::Handle<shader::Shader> textured_mask_fragment_shader;
};

std::optional<MeshShaderHandles> load_mesh_shader_handles(World& world) {
    auto registry = world.get_resource_mut<assets::EmbeddedAssetRegistry>();
    auto server   = world.get_resource<assets::AssetServer>();
    if (!registry || !server) {
        spdlog::warn(
            "[mesh] EmbeddedAssetRegistry or AssetServer is not available. Internal mesh shaders were not registered.");
        return std::nullopt;
    }

    registry->get().insert_asset_static(kMeshSolidVertexShaderAssetPath, shader_bytes(kMeshSolidVertexShader));
    registry->get().insert_asset_static(kMeshVertexColorShaderAssetPath, shader_bytes(kMeshVertexColorVertexShader));
    registry->get().insert_asset_static(kMeshTexturedVertexShaderAssetPath, shader_bytes(kMeshTexturedVertexShader));
    registry->get().insert_asset_static(kMeshTexturedVertexColorShaderAssetPath,
                                        shader_bytes(kMeshTexturedVertexColorVertexShader));
    registry->get().insert_asset_static(kMeshColorFragmentShaderAssetPath, shader_bytes(kMeshColorFragmentShader));
    registry->get().insert_asset_static(kMeshTexturedFragmentShaderAssetPath,
                                        shader_bytes(kMeshTexturedFragmentShader));
    registry->get().insert_asset_static(kMeshColorMaskFragmentShaderAssetPath, shader_bytes(kMeshColorMaskFragmentShader));
    registry->get().insert_asset_static(kMeshTexturedMaskFragmentShaderAssetPath,
                                        shader_bytes(kMeshTexturedMaskFragmentShader));

    return MeshShaderHandles{
        .solid_vertex_shader    = server->get().load<shader::Shader>("embedded://mesh/solid_vertex.slang"),
        .vertex_color_shader    = server->get().load<shader::Shader>("embedded://mesh/vertex_color_vertex.slang"),
        .textured_vertex_shader = server->get().load<shader::Shader>("embedded://mesh/textured_vertex.slang"),
        .textured_vertex_color_shader =
            server->get().load<shader::Shader>("embedded://mesh/textured_vertex_color_vertex.slang"),
        .color_fragment_shader    = server->get().load<shader::Shader>("embedded://mesh/color_fragment.slang"),
        .textured_fragment_shader = server->get().load<shader::Shader>("embedded://mesh/textured_fragment.slang"),
        .color_mask_fragment_shader = server->get().load<shader::Shader>("embedded://mesh/color_mask_fragment.slang"),
        .textured_mask_fragment_shader =
            server->get().load<shader::Shader>("embedded://mesh/textured_mask_fragment.slang"),
    };
}

enum class MeshShaderVariant : std::uint8_t {
    SolidColor,
    VertexColor,
    Textured,
    TexturedVertexColor,
};

struct Mesh2dPipelineKey {
    MeshShaderVariant variant;
    enum class AlphaMode : std::uint8_t {
        Opaque,
        Mask,
        Blend,
    } alpha_mode;
    wgpu::PrimitiveTopology primitive_type;
    wgpu::TextureFormat color_format;
    std::uint32_t sample_count;
    /// Identity of the interned mesh vertex-buffer layout (Bevy
    /// Mesh2dPipelineKey's MeshVertexBufferLayoutId).
    std::uintptr_t layout_id = 0;

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
        hash ^= std::hash<std::uint32_t>()(key.sample_count) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<std::uintptr_t>()(key.layout_id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
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

Mesh2dPipelineKey::AlphaMode pipeline_alpha_mode(const MeshAlphaMode2d& alpha_mode) noexcept {
    return std::visit(
        []<typename Mode>(const Mode&) {
            if constexpr (std::same_as<Mode, MeshAlphaMode2dOpaque>) return Mesh2dPipelineKey::AlphaMode::Opaque;
            if constexpr (std::same_as<Mode, MeshAlphaMode2dMask>) return Mesh2dPipelineKey::AlphaMode::Mask;
            return Mesh2dPipelineKey::AlphaMode::Blend;
        },
        alpha_mode);
}

constexpr const char* alpha_mode_name(Mesh2dPipelineKey::AlphaMode alpha_mode) {
    switch (alpha_mode) {
        case Mesh2dPipelineKey::AlphaMode::Opaque:
            return "opaque";
        case Mesh2dPipelineKey::AlphaMode::Mask:
            return "mask";
        case Mesh2dPipelineKey::AlphaMode::Blend:
            return "blend";
        default:
            return "unknown";
    }
}

struct Mesh2dPipelineCache {
    wgpu::BindGroupLayout view_layout;
    wgpu::BindGroupLayout mesh_layout;
    wgpu::BindGroupLayout texture_layout;
    assets::Handle<shader::Shader> solid_vertex_shader;
    assets::Handle<shader::Shader> vertex_color_shader;
    assets::Handle<shader::Shader> textured_vertex_shader;
    assets::Handle<shader::Shader> textured_vertex_color_shader;
    assets::Handle<shader::Shader> color_fragment_shader;
    assets::Handle<shader::Shader> textured_fragment_shader;
    assets::Handle<shader::Shader> color_mask_fragment_shader;
    assets::Handle<shader::Shader> textured_mask_fragment_shader;
    std::unordered_map<Mesh2dPipelineKey, render::CachedPipelineId, Mesh2dPipelineKeyHash> pipelines;

    explicit Mesh2dPipelineCache(World& world, const MeshShaderHandles& shader_handles)
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
                  }))),
          solid_vertex_shader(shader_handles.solid_vertex_shader),
          vertex_color_shader(shader_handles.vertex_color_shader),
          textured_vertex_shader(shader_handles.textured_vertex_shader),
          textured_vertex_color_shader(shader_handles.textured_vertex_color_shader),
          color_fragment_shader(shader_handles.color_fragment_shader),
          textured_fragment_shader(shader_handles.textured_fragment_shader),
          color_mask_fragment_shader(shader_handles.color_mask_fragment_shader),
          textured_mask_fragment_shader(shader_handles.textured_mask_fragment_shader) {}

    std::optional<render::CachedPipelineId> specialize(render::PipelineServer& pipeline_server,
                                                       const MeshVertexBufferLayoutRef& layout_ref,
                                                       wgpu::PrimitiveTopology primitive_type,
                                                       wgpu::TextureFormat color_format,
                                                       std::uint32_t sample_count,
                                                       const MeshAlphaMode2d& alpha_mode,
                                                       bool textured) {
        if (!layout_ref.value) {
            spdlog::warn("[mesh] Skip pipeline specialization: mesh has no vertex buffer layout.");
            return std::nullopt;
        }
        const auto& layout = layout_ref.value->layout;
        const auto has_attribute = [&layout_ref](std::uint64_t slot) {
            return std::ranges::any_of(layout_ref.value->attribute_ids,
                                       [slot](const MeshVertexAttributeId& id) { return id.value == slot; });
        };
        if (!has_attribute(Mesh::ATTRIBUTE_POSITION.slot)) {
            spdlog::warn("[mesh] Skip pipeline specialization: mesh layout is missing POSITION.");
            return std::nullopt;
        }

        const bool has_color = has_attribute(Mesh::ATTRIBUTE_COLOR.slot);
        const bool has_uv    = has_attribute(Mesh::ATTRIBUTE_UV0.slot);
        if (textured && !has_uv) {
            spdlog::warn("[mesh] Skip textured pipeline specialization: mesh layout is missing UV0.");
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

        const auto pipeline_mode = pipeline_alpha_mode(alpha_mode);
        Mesh2dPipelineKey key{
            .variant        = variant,
            .alpha_mode     = pipeline_mode,
            .primitive_type = primitive_type,
            .color_format   = color_format,
            .sample_count   = sample_count,
            .layout_id      = reinterpret_cast<std::uintptr_t>(layout_ref.value.get()),
        };
        if (auto it = pipelines.find(key); it != pipelines.end()) {
            return it->second;
        }

        // Bevy Mesh2dPipeline: ONE interleaved vertex buffer described by the
        // mesh's MeshVertexBufferLayout (array_stride + interleaved attributes).
        std::vector<wgpu::VertexBufferLayout> vertex_buffers;
        if (!layout.attributes.empty()) {
            const auto attributes = std::ranges::to<std::vector<wgpu::VertexAttribute>>(
                std::views::transform(layout.attributes, [](const VertexAttributeDescriptor& attribute) {
                    return wgpu::VertexAttribute()
                        .setShaderLocation(attribute.shader_location)
                        .setFormat(attribute.format)
                        .setOffset(attribute.offset);
                }));
            vertex_buffers.push_back(wgpu::VertexBufferLayout()
                                         .setArrayStride(layout.array_stride)
                                         .setStepMode(layout.step_mode)
                                         .setAttributes(attributes));
        }

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

        render::FragmentState fragment_state{.shader =
                                                 pipeline_mode == Mesh2dPipelineKey::AlphaMode::Mask
                                                     ? (textured ? textured_mask_fragment_shader : color_mask_fragment_shader)
                                                     : (textured ? textured_fragment_shader : color_fragment_shader)};

        auto color_target = wgpu::ColorTargetState().setFormat(color_format).setWriteMask(wgpu::ColorWriteMask::eAll);
        if (pipeline_mode == Mesh2dPipelineKey::AlphaMode::Blend) {
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
            .label     = std::format("mesh2d-{}-{}-{}", shader_variant_name(variant), alpha_mode_name(pipeline_mode),
                                     wgpu::to_string(primitive_type)),
            .layouts   = std::move(layouts),
            .vertex    = std::move(vertex_state),
            .primitive = wgpu::PrimitiveState()
                             .setTopology(primitive_type)
                             .setFrontFace(wgpu::FrontFace::eCCW)
                             .setCullMode(wgpu::CullMode::eNone),
            .depth_stencil =
                wgpu::DepthStencilState()
                    .setFormat(wgpu::TextureFormat::eDepth32Float)
                    .setDepthWriteEnabled(pipeline_mode == Mesh2dPipelineKey::AlphaMode::Blend
                                              ? wgpu::OptionalBool::eFalse
                                              : wgpu::OptionalBool::eTrue)
                    // Core2D clears the Bevy reverse-Z depth buffer to 0.
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

struct OpaqueMesh2dDrawFunction {
    render::phase::DrawFunctionId value;
};

struct AlphaMaskMesh2dDrawFunction {
    render::phase::DrawFunctionId value;
};

struct TransparentMesh2dDrawFunction {
    render::phase::DrawFunctionId value;
};

void extract_meshes_2d(ResMut<RenderMesh2dInstances> render_mesh_instances,
                       Extract<Query<Item<Entity,
                                          const Mesh2d&,
                                          const transform::GlobalTransform&,
                                          const camera::ViewVisibility&,
                                          Opt<const MeshMaterial2d&>,
                                          Opt<const MeshTextureMaterial2d&>,
                                          Opt<const camera::RenderLayers&>>,
                                     Without<render::CustomRendered>>> meshes) {
    // Bevy keeps these data in a main-entity keyed resource. A batchable
    // binned phase deliberately has no render entity to query at draw time.
    render_mesh_instances->clear();
    for (auto&& [entity, mesh_handle, transform, view_visibility, material, texture_material, opt_layer] :
         meshes.iter()) {
        // Bevy extract_meshes gates on ViewVisibility (visibility/mod.rs:448-458).
        if (!view_visibility.get()) continue;
        glm::vec4 color = texture_material.transform([](const MeshTextureMaterial2d& value) { return value.color; })
                              .value_or(material.transform([](const MeshMaterial2d& value) { return value.color; })
                                            .value_or(glm::vec4(1.0f)));
        MeshAlphaMode2d alpha_mode =
            texture_material.transform([](const MeshTextureMaterial2d& value) { return value.alpha_mode; })
                .value_or(material.transform([](const MeshMaterial2d& value) { return value.alpha_mode; })
                              .value_or(MeshAlphaMode2d{MeshAlphaMode2dOpaque{}}));

        render_mesh_instances->instances.emplace(
            render::sync_world::MainEntity{entity},
            RenderMesh2dInstance{.extracted = ExtractedMesh2d{
                      .source_entity = entity,
                      .mesh          = mesh_handle.handle.id(),
                      .model         = transform.matrix,
                      .color         = color,
                      .depth         = transform.matrix[3][2],
                      .alpha_mode    = alpha_mode,
                      .texture       = texture_material.transform(
                          [](const MeshTextureMaterial2d& value) { return value.image.id(); }),
                      .render_layer = opt_layer ? *opt_layer : camera::RenderLayers::layer(0),
                  }});
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

float alpha_cutoff(const MeshAlphaMode2d& alpha_mode) noexcept {
    if (const auto* mask = std::get_if<MeshAlphaMode2dMask>(&alpha_mode)) return mask->cutoff;
    return 0.0f;
}

void prepare_mesh_instances(ResMut<render::phase::ViewBinnedRenderPhases<core_graph::core_2d::Opaque2D>> opaque_phases,
                            ResMut<render::phase::ViewBinnedRenderPhases<core_graph::core_2d::AlphaMask2D>> alpha_mask_phases,
                            ResMut<render::phase::ViewSortedRenderPhases<core_graph::core_2d::Transparent2D>> transparent_phases,
                            ResMut<RenderMesh2dInstances> mesh_instances,
                            Res<render::RenderAssets<image::Image>> images,
                            Res<wgpu::Device> device,
                            Res<wgpu::Queue> queue,
                            Res<Mesh2dPipelineCache> pipeline_cache,
                            ResMut<MeshInstanceBuffer> instance_buffer) {
    instance_buffer->instances.clear();
    std::unordered_map<assets::AssetId<image::Image>, wgpu::BindGroup> texture_bind_group_cache;

    auto configure_batch = [&](MeshBatch& batch, const ExtractedMesh2d& extracted) {
        batch.instance_start = static_cast<std::uint32_t>(instance_buffer->instances.size());
        if (!extracted.texture) {
            batch.texture_bind_group.reset();
            return;
        }
        if (auto it = texture_bind_group_cache.find(*extracted.texture); it != texture_bind_group_cache.end()) {
            batch.texture_bind_group = it->second;
        } else if (auto gpu_image = images->try_get(*extracted.texture); gpu_image) {
            auto texture_bind_group = device->createBindGroup(
                wgpu::BindGroupDescriptor()
                    .setLabel("Mesh2dTextureBindGroup")
                    .setLayout(pipeline_cache->texture_layout)
                    .setEntries(std::array{
                        wgpu::BindGroupEntry().setBinding(0).setSampler(gpu_image->sampler),
                        wgpu::BindGroupEntry().setBinding(1).setTextureView(gpu_image->texture_view),
                    }));
            texture_bind_group_cache.emplace(*extracted.texture, texture_bind_group);
            batch.texture_bind_group = std::move(texture_bind_group);
        } else {
            batch.texture_bind_group.reset();
        }
    };

    auto process_sorted_phase = [&](auto& phase) {
        std::optional<MeshBatchKey> current_key;
        std::size_t batch_head = std::numeric_limits<std::size_t>::max();

        for (std::size_t item_index = 0; item_index < phase.items.size(); ++item_index) {
            auto& item     = phase.items[item_index];
            auto mesh_instance = mesh_instances->find(item.main_entity());
            if (mesh_instance == mesh_instances->end()) {
                current_key.reset();
                batch_head = std::numeric_limits<std::size_t>::max();
                continue;
            }

            auto&& [extracted, batch] = mesh_instance->second;

            MeshBatchKey key{
                .pipeline_id = item.cached_pipeline(),
                .mesh_id     = extracted.mesh,
                .texture_id  = extracted.texture,
            };

            if (!current_key || *current_key != key) {
                batch_head                          = item_index;
                configure_batch(batch, extracted);
                phase.items[batch_head].batch_range() = {batch.instance_start, batch.instance_start};
                current_key = key;
            }

            instance_buffer->instances.push_back({extracted.model, extracted.color, alpha_cutoff(extracted.alpha_mode)});
            phase.items[batch_head].batch_range().second =
                static_cast<std::uint32_t>(instance_buffer->instances.size());
        }
    };

    auto process_binned_phases = [&](auto& phases) {
        for (auto& [retained_view_entity, phase] : phases->phases) {
            (void)retained_view_entity;
            auto* batches = std::get_if<0>(&phase.batch_sets);
            if (!batches) continue;
            batches->clear();
            for (auto&& [key, bin] : phase.batchable_meshes.iter()) {
                (void)key;
                bin.clear_batches();
                for (const auto& [main_entity, input_uniform_index] : bin.entities().iter()) {
                    (void)input_uniform_index;
                    const auto mesh_instance = mesh_instances->find(main_entity);
                    if (mesh_instance == mesh_instances->end()) continue;
                    auto&& [extracted, batch] = mesh_instance->second;
                    if (bin.batches.empty()) configure_batch(batch, extracted);
                    const auto instance_index = static_cast<std::uint32_t>(instance_buffer->instances.size());
                    instance_buffer->instances.push_back(
                        {extracted.model, extracted.color, alpha_cutoff(extracted.alpha_mode)});
                    if (bin.batches.empty()) {
                        bin.batches.push_back({.representative_entity = {ecs::Entity::PLACEHOLDER, main_entity},
                                               .instance_range        = {instance_index, instance_index},
                                               .extra_index           = render::phase::PhaseItemExtraIndex::None});
                    }
                    bin.batches.back().instance_range.second = instance_index + 1;
                }
                batches->push_back(bin.batches);
            }
        }
    };

    process_binned_phases(opaque_phases);
    process_binned_phases(alpha_mask_phases);
    for (auto& [retained_view_entity, phase] : *transparent_phases) {
        (void)retained_view_entity;
        process_sorted_phase(phase);
    }

    auto required_bytes = instance_buffer->instances.size() * sizeof(MeshInstanceData);
    ensure_mesh_instance_buffer(*instance_buffer, *pipeline_cache, *device, required_bytes);
    if (required_bytes != 0) {
        queue->writeBuffer(instance_buffer->buffer, 0, instance_buffer->instances.data(), required_bytes);
    }
}

void queue_meshes_2d_opaque(Query<Item<const render::view::ExtractedView&,
                                       const render::view::ViewTarget&,
                                       Opt<const ::epix::camera::RenderLayers&>,
                                       const ::epix::render::view::RenderVisibleEntities&>> views,
                            Res<RenderMesh2dInstances> mesh_instances,
                            Res<render::RenderAssets<Mesh>> gpu_meshes,
                            Res<render::RenderAssets<image::Image>> images,
                            Res<OpaqueMesh2dDrawFunction> opaque_draw_function_id,
                            Res<AlphaMaskMesh2dDrawFunction> alpha_mask_draw_function_id,
                            ResMut<Mesh2dPipelineCache> pipeline_cache,
                            ResMut<render::PipelineServer> pipeline_server,
                            ResMut<render::phase::ViewBinnedRenderPhases<core_graph::core_2d::Opaque2D>> opaque_phases,
                            ResMut<render::phase::ViewBinnedRenderPhases<core_graph::core_2d::AlphaMask2D>> alpha_mask_phases) {
    for (auto&& [view, target, opt_camera_layers, visible_entities] : views.iter()) {
        auto opaque_phase = opaque_phases->phases.find(view.retained_view_entity);
        auto alpha_mask_phase = alpha_mask_phases->phases.find(view.retained_view_entity);
        if (opaque_phase == opaque_phases->phases.end() || alpha_mask_phase == alpha_mask_phases->phases.end()) continue;
        const auto& camera_layers =
            opt_camera_layers ? opt_camera_layers->get() : ::epix::camera::RenderLayers::layer(0);
        const auto& visible = visible_entities.template get<Mesh2d>();
        for (const auto& [main_entity, instance] : mesh_instances->instances) {
            const auto& extracted_mesh = instance.extracted;
            if (std::holds_alternative<MeshAlphaMode2dBlend>(extracted_mesh.alpha_mode)) {
                continue;
            }
            if (!camera_layers.intersects(extracted_mesh.render_layer)) {
                continue;
            }
            if (std::ranges::find(visible, extracted_mesh.source_entity,
                                  [](const auto& entity) { return entity.second.id(); }) == visible.end()) {
                continue;
            }

            auto* render_mesh = gpu_meshes->try_get(extracted_mesh.mesh);
            if (!render_mesh) {
                spdlog::warn("[mesh] Skip opaque/alpha-mask mesh entity {:#x}: GPU mesh {} is not prepared yet.", extracted_mesh.source_entity.index,
                             extracted_mesh.mesh.to_string_short());
                continue;
            }
            if (render_mesh->vertex_count == 0) {
                spdlog::debug("[mesh] Skip opaque/alpha-mask mesh entity {:#x}: GPU mesh {} is empty.", extracted_mesh.source_entity.index,
                              extracted_mesh.mesh.to_string_short());
                continue;
            }
            if (extracted_mesh.texture && !images->try_get(*extracted_mesh.texture)) {
                spdlog::warn("[mesh] Skip opaque/alpha-mask textured mesh entity {:#x}: GPU image {} is not available yet.",
                             extracted_mesh.source_entity.index, extracted_mesh.texture->to_string_short());
                continue;
            }

            auto pipeline_id = pipeline_cache->specialize(
                *pipeline_server, render_mesh->layout, render_mesh->primitive_type(), target.format,
                target.color_attachment_sample_count(), extracted_mesh.alpha_mode, extracted_mesh.texture.has_value());
            if (!pipeline_id) {
                spdlog::warn("[mesh] Skip opaque/alpha-mask mesh entity {:#x}: failed to specialize pipeline for layout.",
                             extracted_mesh.source_entity.index);
                continue;
            }
            const auto batch_set_key = core_graph::core_2d::BatchSetKey2D{.indexed_value = render_mesh->indexed()};
            const auto material_bind_group_id = extracted_mesh.texture.transform(
                [](const assets::AssetId<image::Image>& id) { return assets::UntypedAssetId(id); });
            if (std::holds_alternative<MeshAlphaMode2dOpaque>(extracted_mesh.alpha_mode)) {
                opaque_phase->second.add(
                    batch_set_key,
                    core_graph::core_2d::Opaque2DBinKey{.pipeline_id = *pipeline_id,
                                                        .draw_func = opaque_draw_function_id->value,
                                                        .asset_id = assets::UntypedAssetId(extracted_mesh.mesh),
                                                        .material_bind_group_id = material_bind_group_id},
                    {ecs::Entity::PLACEHOLDER, main_entity}, render::phase::InputUniformIndex{}, render::phase::BinnedRenderPhaseType::BatchableMesh,
                    ecs::Tick{});
            } else {
                alpha_mask_phase->second.add(
                    batch_set_key,
                    core_graph::core_2d::AlphaMask2DBinKey{.pipeline_id = *pipeline_id,
                                                           .draw_func = alpha_mask_draw_function_id->value,
                                                           .asset_id = assets::UntypedAssetId(extracted_mesh.mesh),
                                                           .material_bind_group_id = material_bind_group_id},
                    {ecs::Entity::PLACEHOLDER, main_entity}, render::phase::InputUniformIndex{}, render::phase::BinnedRenderPhaseType::BatchableMesh,
                    ecs::Tick{});
            }
        }
    }
}

void queue_meshes_2d_transparent(Query<Item<const render::view::ExtractedView&,
                                            const render::view::ViewTarget&,
                                            Opt<const ::epix::camera::RenderLayers&>,
                                            const ::epix::render::view::RenderVisibleEntities&>> views,
                                 Res<RenderMesh2dInstances> mesh_instances,
                                 Res<render::RenderAssets<Mesh>> gpu_meshes,
                                 Res<render::RenderAssets<image::Image>> images,
                                 Res<TransparentMesh2dDrawFunction> draw_function_id,
                                 ResMut<Mesh2dPipelineCache> pipeline_cache,
                                 ResMut<render::PipelineServer> pipeline_server,
                                 ResMut<render::phase::ViewSortedRenderPhases<core_graph::core_2d::Transparent2D>> phases) {
    for (auto&& [view, target, opt_camera_layers, visible_entities] : views.iter()) {
        auto phase = phases->find(view.retained_view_entity);
        if (phase == phases->end()) continue;
        const auto& camera_layers =
            opt_camera_layers ? opt_camera_layers->get() : ::epix::camera::RenderLayers::layer(0);
        const auto& visible = visible_entities.template get<Mesh2d>();
        for (const auto& [main_entity, instance] : mesh_instances->instances) {
            const auto& extracted_mesh = instance.extracted;
            if (!std::holds_alternative<MeshAlphaMode2dBlend>(extracted_mesh.alpha_mode)) {
                continue;
            }
            if (!camera_layers.intersects(extracted_mesh.render_layer)) {
                continue;
            }
            if (std::ranges::find(visible, extracted_mesh.source_entity,
                                  [](const auto& entity) { return entity.second.id(); }) == visible.end()) {
                continue;
            }

            auto* render_mesh = gpu_meshes->try_get(extracted_mesh.mesh);
            if (!render_mesh) {
                spdlog::warn("[mesh] Skip transparent mesh entity {:#x}: GPU mesh {} is not prepared yet.",
                             extracted_mesh.source_entity.index, extracted_mesh.mesh.to_string_short());
                continue;
            }
            if (render_mesh->vertex_count == 0) {
                spdlog::debug("[mesh] Skip transparent mesh entity {:#x}: GPU mesh {} is empty.", extracted_mesh.source_entity.index,
                              extracted_mesh.mesh.to_string_short());
                continue;
            }
            if (extracted_mesh.texture && !images->try_get(*extracted_mesh.texture)) {
                spdlog::warn("[mesh] Skip transparent textured mesh entity {:#x}: GPU image {} is not available yet.",
                             extracted_mesh.source_entity.index, extracted_mesh.texture->to_string_short());
                continue;
            }

            auto pipeline_id = pipeline_cache->specialize(
                *pipeline_server, render_mesh->layout, render_mesh->primitive_type(), target.format,
                target.color_attachment_sample_count(), extracted_mesh.alpha_mode, extracted_mesh.texture.has_value());
            if (!pipeline_id) {
                spdlog::warn("[mesh] Skip transparent mesh entity {:#x}: failed to specialize pipeline for layout.",
                             extracted_mesh.source_entity.index);
                continue;
            }

            phase->second.add(core_graph::core_2d::Transparent2D{
                .representative_entity = {ecs::Entity::PLACEHOLDER, main_entity},
                .depth                 = extracted_mesh.depth,
                .pipeline_id           = *pipeline_id,
                .draw_func             = draw_function_id->value,
                .batch_range_value     = {0, 1},
                .indexed_value         = render_mesh->indexed(),
            });
        }
    }
}

}  // namespace

namespace epix::mesh::detail {
struct MeshAllocatorAccess {
    static void process(MeshAllocator& allocator,
                        const MeshAllocatorSettings& settings,
                        const render::ExtractedAssets<Mesh>& extracted_meshes,
                        MeshVertexBufferLayouts& mesh_vertex_buffer_layouts,
                        const wgpu::Device& device,
                        const wgpu::Queue& queue) {
        // Bevy frees removed and modified assets before allocating their new
        // payloads.
        for (const auto& id : extracted_meshes.removed) allocator.free_all(id);
        for (const auto& id : extracted_meshes.modified) allocator.free_all(id);

        SlabsToReallocate slabs_to_reallocate;

        // Allocate every payload first. Growth is only recorded here; no GPU
        // buffer is created or copied until the complete frame is known.
        for (const auto& [id, mesh] : extracted_meshes.extracted) {
            const auto vertex_layout = mesh.get_mesh_vertex_buffer_layout(mesh_vertex_buffer_layouts);
            if (!vertex_layout.value || vertex_layout.value->layout.array_stride == 0) continue;
            const auto vertex_stride = vertex_layout.value->layout.array_stride;
            const auto vertex_bytes = static_cast<std::uint64_t>(mesh.count_vertices()) * vertex_stride;
            if (vertex_bytes == 0) continue;

            const auto element_layout = ElementLayout::make(ElementClass::Vertex, vertex_stride);
            if (allocator.general_vertex_slabs_supported) {
                allocator.allocate(id, vertex_bytes, element_layout, slabs_to_reallocate, settings);
            } else {
                allocator.allocate_large(id, element_layout);
            }

            if (const auto indices = mesh.get_indices()) {
                const auto& index = indices->get();
                const std::uint32_t element_size = index.is_u16() ? sizeof(std::uint16_t) : sizeof(std::uint32_t);
                allocator.allocate(id, static_cast<std::uint64_t>(index.size()) * element_size,
                                   ElementLayout::make(ElementClass::Index, element_size), slabs_to_reallocate,
                                   settings);
            }
        }

        // A slab that grew repeatedly above is allocated/reallocated exactly
        // once, preserving the capacity it had at the beginning of the frame.
        for (const auto& [slab_id, reallocate] : slabs_to_reallocate) {
            allocator.reallocate_slab(device, queue, slab_id, reallocate);
        }

        // Only after final buffer placement is known do uploads become
        // resident, matching Bevy's third phase.
        for (const auto& [id, mesh] : extracted_meshes.extracted) {
            if (const auto vertex_slab = allocator.mesh_id_to_vertex_slab.find(id);
                vertex_slab != allocator.mesh_id_to_vertex_slab.end()) {
                const auto packed = packed_vertex_bytes(mesh);
                if (!packed.empty()) {
                    allocator.copy_element_data(device, queue, vertex_slab->second, id, packed.data(), packed.size(),
                                                wgpu::BufferUsage::eVertex);
                }
            }
            if (const auto index_slab = allocator.mesh_id_to_index_slab.find(id);
                index_slab != allocator.mesh_id_to_index_slab.end()) {
                if (const auto indices = mesh.get_indices()) {
                    const auto& index = indices->get();
                    const std::size_t element_size = index.is_u16() ? sizeof(std::uint16_t) : sizeof(std::uint32_t);
                    allocator.copy_element_data(device, queue, index_slab->second, id, index.data.cdata(),
                                                index.size() * element_size, wgpu::BufferUsage::eIndex);
                }
            }
        }
    }
};
}  // namespace epix::mesh::detail

epix::mesh::MeshAllocator epix::mesh::MeshAllocator::from_world(epix::ecs::World& world) {
    (void)world;
    // wgpu-native's C API does not expose the wgpu-core downlevel-capability
    // query used by Bevy. Epix currently coerces every selected backend to
    // Vulkan for Slang SPIR-V passthrough, and Vulkan guarantees BASE_VERTEX.
    // Remove this local fallback together with that renderer workaround once
    // the native API can report the capability directly.
    return MeshAllocator(true);
}

void epix::mesh::allocate_and_free_meshes(ResMut<MeshAllocator> mesh_allocator,
                                          Res<MeshAllocatorSettings> mesh_allocator_settings,
                                          Res<render::ExtractedAssets<Mesh>> extracted_meshes,
                                          ResMut<MeshVertexBufferLayouts> mesh_vertex_buffer_layouts,
                                          Res<wgpu::Device> device,
                                          Res<wgpu::Queue> queue) {
    detail::MeshAllocatorAccess::process(mesh_allocator.get_mut(), *mesh_allocator_settings, *extracted_meshes,
                                         mesh_vertex_buffer_layouts.get_mut(), *device, *queue);
}

void MeshAllocatorPlugin::attach(App& app) {
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().world_mut().init_resource<MeshAllocatorSettings>();
        render_app->get().add_systems(
            render::Render,
            into(allocate_and_free_meshes)
                .before(render::prepare_assets<Mesh>)
                .in_set(render::RenderSystems::PrepareAssets)
                .set_name("allocate and free meshes"));
    }
}

void MeshAllocatorPlugin::ready(App& app) {
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        auto& world = render_app->get().world_mut();
        world.init_resource<MeshAllocator>();
    }
}

void MeshRenderPlugin::attach(app::App& app) {
    spdlog::debug("[mesh] Attaching MeshRenderPlugin.");
    // Bevy Mesh2d requires Visibility, pulling in InheritedVisibility +
    // ViewVisibility so hidden/layer culling works.
    app.world_mut().register_required_components<Mesh2d, camera::Visibility>();
    app.world_mut().register_required_components_with<Mesh2d>(
        [] { return camera::VisibilityClass{meta::type_index(meta::type_id<Mesh2d>())}; });
    app.add_systems(app::PostUpdate, into(calculate_mesh2d_bounds)
                                         .in_set(camera::VisibilitySystems::CalculateBounds)
                                         .before(camera::VisibilitySystems::CheckVisibility)
                                         .set_name("calculate mesh2d bounds"));
    app.add_plugins(MeshPlugin{});
    app.add_plugins(core_graph::core_2d::Core2dPlugin{});
    app.add_plugins(MeshAllocatorPlugin{});
    app.add_plugins(render::RenderAssetPlugin<Mesh>{});

    if (!app.world_mut().get_resource<MeshShaderHandles>()) {
        if (auto shader_handles = load_mesh_shader_handles(app.world_mut())) {
            app.world_mut().insert_resource(std::move(*shader_handles));
        }
    }
}

void MeshRenderPlugin::ready(app::App& app) {
    spdlog::debug("[mesh] Readying MeshRenderPlugin.");
    if (!app.world_mut().get_resource<MeshShaderHandles>()) {
        if (auto shader_handles = load_mesh_shader_handles(app.world_mut())) {
            app.world_mut().insert_resource(std::move(*shader_handles));
        }
    }

    auto shader_handles = app.world_mut().get_resource<MeshShaderHandles>();
    if (!shader_handles) {
        spdlog::error("[mesh] MeshRenderPlugin could not load internal mesh shaders through AssetServer.");
        return;
    }

    auto render_app = app.get_sub_app_mut(render::Render);
    if (!render_app) {
        spdlog::error(
            "[mesh] MeshRenderPlugin requires the render sub-app, but it was not found. Did you add "
            "render::RenderPlugin before MeshRenderPlugin?");
        return;
    }

    auto& world = render_app->get().world_mut();
    // Bevy MeshRenderAssetPlugin initializes the shared layout store in the
    // render world (mesh/mod.rs:39).
    if (!world.get_resource<MeshVertexBufferLayouts>()) {
        world.insert_resource(MeshVertexBufferLayouts{});
    }
    if (!world.get_resource<MeshInstanceBuffer>()) {
        world.insert_resource(MeshInstanceBuffer{});
    }
    if (!world.get_resource<RenderMesh2dInstances>()) {
        world.insert_resource(RenderMesh2dInstances{});
    }
    if (!world.get_resource<Mesh2dPipelineCache>()) {
        world.insert_resource(Mesh2dPipelineCache(world, shader_handles->get()));
    }
    auto& render_subapp = render_app->get();
    world.insert_resource(OpaqueMesh2dDrawFunction{
        .value = render::phase::app_add_render_commands<
            core_graph::core_2d::Opaque2D, render::phase::SetItemPipeline, render::view::BindViewUniform<0>::Command,
            mesh::BindMesh2dInstances<1>::Command, mesh::BindMesh2dTexture<2>::Command, mesh::DrawMesh2dBatch>(
            render_subapp)});
    world.insert_resource(AlphaMaskMesh2dDrawFunction{
        .value = render::phase::app_add_render_commands<
            core_graph::core_2d::AlphaMask2D, render::phase::SetItemPipeline,
            render::view::BindViewUniform<0>::Command, mesh::BindMesh2dInstances<1>::Command,
            mesh::BindMesh2dTexture<2>::Command, mesh::DrawMesh2dBatch>(render_subapp)});
    world.insert_resource(TransparentMesh2dDrawFunction{
        .value = render::phase::app_add_render_commands<
            core_graph::core_2d::Transparent2D, render::phase::SetItemPipeline,
            render::view::BindViewUniform<0>::Command, mesh::BindMesh2dInstances<1>::Command,
            mesh::BindMesh2dTexture<2>::Command, mesh::DrawMesh2dBatch>(render_subapp)});

    render_subapp.add_systems(render::ExtractSchedule, into(extract_meshes_2d).set_name("extract mesh2d"))
        .add_systems(render::Render, into(queue_meshes_2d_opaque, queue_meshes_2d_transparent)
                                         .in_set(render::RenderSystems::Queue)
                                         .set_names(std::array{"queue opaque mesh2d", "queue transparent mesh2d"}))
        .add_systems(render::Render,
                     into(render::phase::sweep_old_entities<core_graph::core_2d::Opaque2D>,
                          render::phase::sweep_old_entities<core_graph::core_2d::AlphaMask2D>)
                         .in_set(render::RenderSystems::QueueSweep)
                         .set_names(std::array{"sweep opaque mesh2d", "sweep alpha-mask mesh2d"}))
        .add_systems(render::Render, into(prepare_mesh_instances)
                                         .in_set(render::RenderSystems::PrepareResources)
                                         .set_name("prepare mesh2d instances"));
}
