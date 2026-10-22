#include <spdlog/spdlog.h>

#include <epix/core_graph.hpp>
#include <epix/sprite_render/mesh2d.hpp>
#include <epix/sprite_render/mesh2d_pipeline.hpp>

// `Mesh2dPipeline` must satisfy both batching concepts — this is the C4b
// re-alignment, where the full-batch bound became `PodElement` rather than
// `ShaderWritable` so that `BufferInputData = ()` is accepted. Asserted directly
// because MSVC names the failing clause here, whereas an `into(...)`
// registration failure reports only "could not deduce template argument ... from
// 'overloaded-function'".
static_assert(epix::render::batching::GetBatchDataImpl<epix::sprite_render::Mesh2dPipeline>,
              "GetBatchData<Mesh2dPipeline> must satisfy its own concept");
static_assert(epix::render::batching::GetFullBatchDataImpl<epix::sprite_render::Mesh2dPipeline>,
              "GetFullBatchData<Mesh2dPipeline> must satisfy its own concept");

namespace epix::sprite_render {
namespace {

/** @brief Embedded path of the mesh2d shader (Bevy `mesh2d.wgsl`). */
constexpr std::string_view kMesh2dShaderPath = "sprite/mesh2d.slang";
/** @brief Embedded path of the mesh2d vertex output (Bevy
 *  `mesh2d_vertex_output.wgsl`). It is its own module because a material's
 *  fragment shader imports it. */
constexpr std::string_view kMesh2dVertexOutputShaderPath = "sprite/mesh2d_vertex_output.slang";
/** @brief Embedded path of the mesh2d view bindings (Bevy
 *  `mesh2d_view_bindings.wgsl`), its own module for the same reason. */
constexpr std::string_view kMesh2dViewBindingsShaderPath = "sprite/mesh2d_view_bindings.slang";
constexpr std::string_view kMesh2dTypesShaderPath = "sprite/mesh2d_types.slang";
constexpr std::string_view kMesh2dBindingsShaderPath = "sprite/mesh2d_bindings.slang";
constexpr std::string_view kMesh2dFunctionsShaderPath = "sprite/mesh2d_functions.slang";

/**
 * @brief Slang representation of pinned Bevy 0.18 `mesh2d.wgsl`,
 * `mesh2d_functions.wgsl`, `mesh2d_types.wgsl`, `mesh2d_bindings.wgsl` and
 * `mesh2d_view_bindings.wgsl`.
 *
 * The same reusable libraries are separate importable Slang modules. Their
 * matrix reconstruction uses the shared render maths library, as Bevy does.
 *
 * The one representation change: Bevy's `Mesh2d.world_from_local` is a WGSL
 * `mat3x4` (three columns of `vec4`) and its `local_from_world_transpose_a` a
 * `mat2x4`. Slang matrix orientation is the opposite of WGSL's, so both are
 * declared as the explicit `float4` arrays the C++ `Mesh2dUniform` stores; the
 * byte layout is identical, and the accessors below spell the transform out
 * with dot products instead of a matrix constructor.
 */
/** @brief Bevy `mesh2d_view_bindings.wgsl`: the group-0 bindings every mesh2d
 *  shader and material fragment reads. */
constexpr std::string_view kMesh2dViewBindingsShader = R"slang(
module epix.mesh2d_view_bindings;

import epix.view;
import epix.globals;

public [[vk::binding(0, 0)]] ConstantBuffer<epix::view::View> view;
public [[vk::binding(1, 0)]] ConstantBuffer<epix::globals::Globals> globals;
public [[vk::binding(2, 0)]] Texture3D<float4> dt_lut_texture;
public [[vk::binding(3, 0)]] SamplerState dt_lut_sampler;
)slang";
/** @brief Bevy `mesh2d_vertex_output.wgsl`: the struct a material's fragment
 *  shader consumes. The `VERTEX_TANGENTS`/`VERTEX_COLORS` definitions come from
 *  the pipeline, so the struct matches whichever attributes the mesh has. */
constexpr std::string_view kMesh2dVertexOutputShader = R"slang(
module epix.mesh2d_vertex_output;

namespace epix::mesh2d_vertex_output {
public struct VertexOutput {
    public float4 position : SV_Position;
    public [[vk::location(0)]] float4 world_position;
    public [[vk::location(1)]] float3 world_normal;
    public [[vk::location(2)]] float2 uv;
#ifdef VERTEX_TANGENTS
    public [[vk::location(3)]] float4 world_tangent;
#endif
#ifdef VERTEX_COLORS
    public [[vk::location(4)]] float4 color;
#endif
};
}
)slang";

/** @brief Shared 96-byte mesh record, used by stock and custom vertex shaders. */
constexpr std::string_view kMesh2dTypesShader = R"slang(
module epix.mesh2d_types;
namespace epix::mesh2d_types {
public struct Mesh2d {
    public float4 world_from_local[3];
    public float4 local_from_world_transpose_a[2];
    public float local_from_world_transpose_b;
    public uint flags;
    public uint tag;
    public float _padding;
};
}
)slang";

constexpr std::string_view kMesh2dBindingsShader = R"slang(
module epix.mesh2d_bindings;
import epix.mesh2d_types;
namespace epix::mesh2d_bindings {
#ifdef PER_OBJECT_BUFFER_BATCH_SIZE
public struct Mesh2dBatch {
    public epix::mesh2d_types::Mesh2d values[PER_OBJECT_BUFFER_BATCH_SIZE];
};
public [[vk::binding(0, 1)]] ConstantBuffer<Mesh2dBatch> mesh;
#else
public [[vk::binding(0, 1)]] StructuredBuffer<epix::mesh2d_types::Mesh2d> mesh;
#endif

// The accessor unifies Slang's uniform-wrapper and storage-array syntax.
public epix::mesh2d_types::Mesh2d get_mesh(uint instance_index) {
#ifdef PER_OBJECT_BUFFER_BATCH_SIZE
    return mesh.values[instance_index];
#else
    return mesh[instance_index];
#endif
}
}
)slang";

constexpr std::string_view kMesh2dFunctionsShader = R"slang(
module epix.mesh2d_functions;
import epix.maths;
import epix.mesh2d_types;
import epix.mesh2d_bindings;
import epix.mesh2d_view_bindings;
namespace epix::mesh2d_functions {
public float4x4 get_world_from_local(uint instance_index) {
    epix::mesh2d_types::Mesh2d instance = epix::mesh2d_bindings::get_mesh(instance_index);
    return epix::maths::affine3_to_square(float3x4(
        instance.world_from_local[0], instance.world_from_local[1], instance.world_from_local[2]));
}
public float4 mesh2d_position_local_to_world(float4x4 world_from_local, float4 vertex_position) {
    return mul(world_from_local, vertex_position);
}
public float4 mesh2d_position_world_to_clip(float4 world_position) {
    return mul(view.clip_from_world, world_position);
}
public float4 mesh2d_position_local_to_clip(float4x4 world_from_local, float4 vertex_position) {
    float4 world_position = mesh2d_position_local_to_world(world_from_local, vertex_position);
    return mesh2d_position_world_to_clip(world_position);
}
public float3 mesh2d_normal_local_to_world(float3 vertex_normal, uint instance_index) {
    epix::mesh2d_types::Mesh2d instance = epix::mesh2d_bindings::get_mesh(instance_index);
    return mul(epix::maths::mat2x4_f32_to_mat3x3_unpack(
        float2x4(instance.local_from_world_transpose_a[0], instance.local_from_world_transpose_a[1]),
        instance.local_from_world_transpose_b), vertex_normal);
}
public float4 mesh2d_tangent_local_to_world(float4x4 world_from_local, float4 vertex_tangent) {
    return float4(mul(float3x3(world_from_local[0].xyz, world_from_local[1].xyz, world_from_local[2].xyz),
                      vertex_tangent.xyz), vertex_tangent.w);
}
public uint get_tag(uint instance_index) {
    return epix::mesh2d_bindings::get_mesh(instance_index).tag;
}
}
)slang";

constexpr std::string_view kMesh2dShader = R"slang(
import epix.view;
import epix.globals;
import epix.mesh2d_view_bindings;
import epix.mesh2d_vertex_output;
import epix.mesh2d_functions;
#ifdef TONEMAP_IN_SHADER
import epix.tonemapping;
#endif
using namespace epix::mesh2d_functions;

struct VertexInput {
    // Raw Vulkan `InstanceIndex` (base instance included), matching what the WGSL
    // path gets from `@builtin(instance_index)`. The HLSL `SV_InstanceID` would
    // instead read `BaseInstance` and declare the `DrawParameters` capability,
    // which the SPIR-V frontend rejects.
    uint instance_index : SV_VulkanInstanceID;
#ifdef VERTEX_POSITIONS
    [[vk::location(0)]] float3 position;
#endif
#ifdef VERTEX_NORMALS
    [[vk::location(1)]] float3 normal;
#endif
#ifdef VERTEX_UVS
    [[vk::location(2)]] float2 uv;
#endif
#ifdef VERTEX_TANGENTS
    [[vk::location(3)]] float4 tangent;
#endif
#ifdef VERTEX_COLORS
    [[vk::location(4)]] float4 color;
#endif
};

[shader("vertex")]
epix::mesh2d_vertex_output::VertexOutput vertex(VertexInput input) {
    epix::mesh2d_vertex_output::VertexOutput output;
    float4x4 world_from_local = get_world_from_local(input.instance_index);
#ifdef VERTEX_UVS
    output.uv = input.uv;
#endif

#ifdef VERTEX_POSITIONS
    float4 world_position =
        mesh2d_position_local_to_world(world_from_local, float4(input.position, 1.0));
    output.world_position = world_position;
    // The intermediate world_position assignment matters for precision when the
    // depth comparison uses the 'equals' function.
    output.position = mesh2d_position_world_to_clip(world_position);
#endif

#ifdef VERTEX_NORMALS
    output.world_normal = mesh2d_normal_local_to_world(input.normal, input.instance_index);
#endif

#ifdef VERTEX_TANGENTS
    output.world_tangent = mesh2d_tangent_local_to_world(world_from_local, input.tangent);
#endif

#ifdef VERTEX_COLORS
    output.color = input.color;
#endif
    return output;
}

[shader("fragment")]
float4 fragment(epix::mesh2d_vertex_output::VertexOutput input) : SV_Target {
#ifdef VERTEX_COLORS
    float4 color = input.color;
#ifdef TONEMAP_IN_SHADER
    epix::view::ColorGrading grading = view.color_grading;
    color = epix::tonemapping::tone_mapping(color, grading, dt_lut_texture, dt_lut_sampler);
#endif
    return color;
#else
    return float4(1.0, 0.0, 1.0, 1.0);
#endif
}
)slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(source.data()), source.size());
}
}  // namespace

namespace {
/** @brief Handles keeping the stock shader and reusable mesh2d libraries loaded.
 *
 * The embedded registry only publishes bytes; the shader processor resolves
 * custom material imports as well as the stock shader's dependencies
 * through the asset server, so the modules must be *loaded* and the handles held
 * alive. This mirrors `render.cpp`'s `load_sprite_shader`/`SpriteShaderHandle`,
 * which is how this module publishes its own shaders. */
struct Mesh2dShaderHandles {
    assets::Handle<shader::Shader> shader;
    assets::Handle<shader::Shader> vertex_output;
    assets::Handle<shader::Shader> view_bindings;
    assets::Handle<shader::Shader> types;
    assets::Handle<shader::Shader> functions;
};

struct Mesh2dBindingsShaderHandle {
    assets::Handle<shader::Shader> shader;
};

void register_mesh2d_shader(ecs::World& world) {
    auto registry = world.get_resource_mut<assets::EmbeddedAssetRegistry>();
    auto server   = world.get_resource<assets::AssetServer>();
    if (!registry || !server) {
        spdlog::warn(
            "[sprite.mesh2d] EmbeddedAssetRegistry or AssetServer not available. Mesh2d shaders not registered.");
        return;
    }
    registry->get().insert_asset_static(kMesh2dShaderPath, shader_bytes(kMesh2dShader));
    registry->get().insert_asset_static(kMesh2dVertexOutputShaderPath, shader_bytes(kMesh2dVertexOutputShader));
    registry->get().insert_asset_static(kMesh2dViewBindingsShaderPath, shader_bytes(kMesh2dViewBindingsShader));
    registry->get().insert_asset_static(kMesh2dTypesShaderPath, shader_bytes(kMesh2dTypesShader));
    registry->get().insert_asset_static(kMesh2dBindingsShaderPath, shader_bytes(kMesh2dBindingsShader));
    registry->get().insert_asset_static(kMesh2dFunctionsShaderPath, shader_bytes(kMesh2dFunctionsShader));
    world.insert_resource(Mesh2dShaderHandles{
        .shader = server->get().load<shader::Shader>(std::string("embedded://") + std::string(kMesh2dShaderPath)),
        .vertex_output =
            server->get().load<shader::Shader>(std::string("embedded://") + std::string(kMesh2dVertexOutputShaderPath)),
        .view_bindings =
            server->get().load<shader::Shader>(std::string("embedded://") + std::string(kMesh2dViewBindingsShaderPath)),
        .types = server->get().load<shader::Shader>(std::string("embedded://") + std::string(kMesh2dTypesShaderPath)),
        .functions = server->get().load<shader::Shader>(std::string("embedded://") + std::string(kMesh2dFunctionsShaderPath)),
    });
}

void load_mesh2d_bindings(ecs::Commands commands, ecs::Res<wgpu::Device> device,
                         ecs::Res<assets::AssetServer> asset_server) {
    // Bevy mesh2d/mesh.rs:164-192: the imported library, not a pipeline
    // descriptor, owns the batch size selected from the actual device limits.
    wgpu::Limits limits{};
    device->getLimits(&limits);
    std::vector<shader::ShaderDefVal> defs;
    if (const auto batch = render::render_resource::GpuArrayBuffer<Mesh2dUniform>::batch_size(limits))
        defs.push_back(shader::ShaderDefVal::from_uint("PER_OBJECT_BUFFER_BATCH_SIZE", *batch));
    auto handle = asset_server->load_with_settings<shader::Shader, shader::ShaderSettings>(
        assets::AssetPath{std::string("embedded://") + std::string(kMesh2dBindingsShaderPath)},
        [defs = std::move(defs)](shader::ShaderSettings& settings) { settings.shader_defs = defs; });
    // Retain per app instead of leaking the strong handle as native mem::forget
    // does. It must not be loaded earlier with default (storage-only) settings.
    commands.insert_resource(Mesh2dBindingsShaderHandle{std::move(handle)});
}
}  // namespace

namespace {
render::BindGroupLayoutDescriptor mesh2d_view_layout_descriptor() {
    using render::render_resource::BindGroupLayoutEntries;
    using render::render_resource::binding_types::uniform_buffer;
    // Bevy `init_mesh_2d_pipeline` (mesh.rs:297-309): a dynamic view uniform, the
    // globals uniform, then the two tonemapping LUT entries at fragment
    // visibility, all at group 0. The LUT builders are ref-qualified on their
    // `visibility` member, so the array must stay mutable.
    auto lut           = core_graph::get_lut_bind_group_layout_entries();
    const auto entries = BindGroupLayoutEntries<>::sequential(
        wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment,
        uniform_buffer(true, sizeof(render::view::ViewUniform)), uniform_buffer(false, sizeof(render::GlobalsUniform)),
        lut[0].visibility(wgpu::ShaderStage::eFragment), lut[1].visibility(wgpu::ShaderStage::eFragment));
    return render::BindGroupLayoutDescriptor().set_label("mesh2d_view_layout").set_entries(entries.entries());
}

render::BindGroupLayoutDescriptor mesh2d_mesh_layout_descriptor(const wgpu::Limits& limits) {
    using render::render_resource::GpuArrayBuffer;
    // Bevy `init_mesh_2d_pipeline` (mesh.rs:311-317): one vertex/fragment binding
    // holding the mesh2d uniform array, as a storage buffer where the adapter has
    // them and as a dynamic-offset uniform array otherwise.
    const auto buffer_binding = GpuArrayBuffer<Mesh2dUniform>::binding_layout(limits);
    const std::array<wgpu::BindGroupLayoutEntry, 1> entries{
        wgpu::BindGroupLayoutEntry()
            .setBinding(0)
            .setVisibility(wgpu::ShaderStage::eVertex | wgpu::ShaderStage::eFragment)
            .setBuffer(buffer_binding),
    };
    const std::span<const wgpu::BindGroupLayoutEntry> entry_span{entries};
    return render::BindGroupLayoutDescriptor().set_label("mesh2d_layout").set_entries(entry_span);
}

std::optional<std::uint32_t> mesh2d_per_object_buffer_batch_size(const wgpu::Limits& limits) {
    using render::render_resource::BatchedUniformBuffer;
    // Bevy's `GpuArrayBuffer::<T>::batch_size` only produces a batch size for the
    // uniform fallback; the storage-buffer path has none.
    if (limits.maxStorageBuffersPerShaderStage == 0) {
        return static_cast<std::uint32_t>(BatchedUniformBuffer<Mesh2dUniform>::batch_size(limits));
    }
    return std::nullopt;
}
}  // namespace

void init_mesh_2d_pipeline(ecs::Commands commands,
                           ecs::Res<wgpu::Device> device,
                           ecs::Res<assets::AssetServer> asset_server) {
    wgpu::Limits limits{};
    device.get().getLimits(&limits);
    commands.insert_resource(Mesh2dPipeline{
        .view_layout                  = mesh2d_view_layout_descriptor(),
        .mesh_layout                  = mesh2d_mesh_layout_descriptor(limits),
        .shader                       = asset_server.get().load<shader::Shader>("embedded://sprite/mesh2d.slang"),
        .per_object_buffer_batch_size = mesh2d_per_object_buffer_batch_size(limits),
    });
}

void prepare_mesh2d_view_bind_groups(ecs::Commands commands,
                                     ecs::Res<render::PipelineServer> pipeline_server,
                                     ecs::Res<wgpu::Device> device,
                                     ecs::Res<Mesh2dPipeline> mesh2d_pipeline,
                                     ecs::Res<render::view::ViewUniforms> view_uniforms,
                                     ecs::Query<ecs::Item<ecs::Entity, const core_graph::Tonemapping&>,
                                                ecs::With<render::view::ExtractedView, epix::camera::Camera2d>> views,
                                     ecs::Res<render::GlobalsBuffer> globals_buffer,
                                     ecs::Res<core_graph::TonemappingLuts> tonemapping_luts,
                                     ecs::Res<render::RenderAssets<render::texture::GpuImage>> images,
                                     ecs::Res<render::texture::FallbackImage> fallback_image) {
    // Bevy `prepare_mesh2d_view_bind_groups` (mesh.rs:680-717): one bind group
    // per Camera2d view, from the mesh2d view layout's interned native layout.
    const auto view_binding    = view_uniforms->uniforms.binding();
    const auto globals_binding = globals_buffer->buffer.binding();
    if (!view_binding || !globals_binding) return;

    const auto layout = pipeline_server->get_bind_group_layout(mesh2d_pipeline->view_layout);
    for (auto&& [entity, tonemapping] : views.iter()) {
        const auto [lut_view, lut_sampler] =
            core_graph::get_lut_bindings(*images, *tonemapping_luts, tonemapping, *fallback_image);
        const auto entries = render::render_resource::BindGroupEntries<>::sequential(
            render::render_resource::BindGroupEntries<>::buffer_binding(view_binding->get(), 0,
                                                                        sizeof(render::view::ViewUniform)),
            render::render_resource::BindGroupEntries<>::buffer_binding(globals_binding->get(), 0,
                                                                        sizeof(render::GlobalsUniform)),
            render::render_resource::BindGroupEntries<>::texture_binding(lut_view),
            render::render_resource::BindGroupEntries<>::sampler_binding(lut_sampler));
        commands.entity(entity).insert(Mesh2dViewBindGroup{
            device->createBindGroup(wgpu::BindGroupDescriptor()
                                        .setLabel("mesh2d_view_bind_group")
                                        .setLayout(layout)
                                        .setEntries(entries.entries())),
        });
    }
}

void prepare_mesh2d_bind_group(ecs::Commands commands,
                               ecs::Res<render::PipelineServer> pipeline_server,
                               ecs::Res<wgpu::Device> device,
                               ecs::Res<Mesh2dPipeline> mesh2d_pipeline,
                               ecs::Res<render::batching::BatchedInstanceBuffer<Mesh2dUniform>> mesh2d_uniforms) {
    // Bevy `prepare_mesh2d_bind_group` (mesh.rs:657-673): one bind group over
    // the shared instance buffer, inserted only while that buffer exists.
    const auto binding = mesh2d_uniforms->buffer.binding();
    if (!binding) return;

    commands.insert_resource(Mesh2dBindGroup{
        device->createBindGroup(
            wgpu::BindGroupDescriptor()
                .setLabel("mesh2d_bind_group")
                .setLayout(pipeline_server->get_bind_group_layout(mesh2d_pipeline->mesh_layout))
                // Retain the native binding range: a uniform array exposes one
                // batch even when the buffer holds multiple dynamic batches.
                .setEntries(std::array{binding->entry(0u)})),
    });
}

std::expected<render::RenderPipelineDescriptor, render::SpecializedMeshPipelineError> Mesh2dPipeline::specialize(
    const Mesh2dPipelineKey& key, const mesh::MeshVertexBufferLayoutRef& layout) const {
    // Bevy `Mesh2dPipeline::specialize` (mesh.rs:494-650): the shader
    // definitions and vertex attributes follow the mesh layout, then the
    // descriptor is assembled from the key's blend, depth and multisample
    // state. The single mesh2d shader is specialized through definitions
    // rather than through separate shader variants.
    std::vector<shader::ShaderDefVal> shader_defs;
    std::vector<mesh::VertexAttributeDescriptor> vertex_attributes;

    const auto request_attribute = [&](const mesh::MeshVertexAttribute& attribute, std::string_view definition,
                                       std::uint32_t location) {
        if (!layout.value->contains(attribute)) return;
        shader_defs.push_back(shader::ShaderDefVal::from_bool(std::string(definition)));
        vertex_attributes.push_back(attribute.at_shader_location(location));
    };
    request_attribute(mesh::Mesh::ATTRIBUTE_POSITION, "VERTEX_POSITIONS", 0);
    request_attribute(mesh::Mesh::ATTRIBUTE_NORMAL, "VERTEX_NORMALS", 1);
    request_attribute(mesh::Mesh::ATTRIBUTE_UV_0, "VERTEX_UVS", 2);
    request_attribute(mesh::Mesh::ATTRIBUTE_TANGENT, "VERTEX_TANGENTS", 3);
    request_attribute(mesh::Mesh::ATTRIBUTE_COLOR, "VERTEX_COLORS", 4);

    const auto bits = key.bits();
    if ((bits & Mesh2dPipelineKey::TONEMAP_IN_SHADER) != 0) {
        shader_defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_IN_SHADER"));
        shader_defs.push_back(shader::ShaderDefVal::from_uint("TONEMAPPING_LUT_TEXTURE_BINDING_INDEX", 2));
        shader_defs.push_back(shader::ShaderDefVal::from_uint("TONEMAPPING_LUT_SAMPLER_BINDING_INDEX", 3));

        switch (bits & Mesh2dPipelineKey::TONEMAP_METHOD_RESERVED_BITS) {
            case Mesh2dPipelineKey::TONEMAP_METHOD_NONE:
                shader_defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_NONE"));
                break;
            case Mesh2dPipelineKey::TONEMAP_METHOD_REINHARD:
                shader_defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_REINHARD"));
                break;
            case Mesh2dPipelineKey::TONEMAP_METHOD_REINHARD_LUMINANCE:
                shader_defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_REINHARD_LUMINANCE"));
                break;
            case Mesh2dPipelineKey::TONEMAP_METHOD_ACES_FITTED:
                shader_defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_ACES_FITTED"));
                break;
            case Mesh2dPipelineKey::TONEMAP_METHOD_AGX:
                shader_defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_AGX"));
                break;
            case Mesh2dPipelineKey::TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM:
                shader_defs.push_back(
                    shader::ShaderDefVal::from_bool("TONEMAP_METHOD_SOMEWHAT_BORING_DISPLAY_TRANSFORM"));
                break;
            case Mesh2dPipelineKey::TONEMAP_METHOD_BLENDER_FILMIC:
                shader_defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_BLENDER_FILMIC"));
                break;
            case Mesh2dPipelineKey::TONEMAP_METHOD_TONY_MC_MAPFACE:
                shader_defs.push_back(shader::ShaderDefVal::from_bool("TONEMAP_METHOD_TONY_MC_MAPFACE"));
                break;
            default:
                break;
        }
        // Debanding is tied to tonemapping in the shader, so it cannot run
        // without it (Bevy's comment).
        if ((bits & Mesh2dPipelineKey::DEBAND_DITHER) != 0) {
            shader_defs.push_back(shader::ShaderDefVal::from_bool("DEBAND_DITHER"));
        }
    }
    if ((bits & Mesh2dPipelineKey::MAY_DISCARD) != 0) {
        shader_defs.push_back(shader::ShaderDefVal::from_bool("MAY_DISCARD"));
    }

    auto specialized_layout = layout.value->get_layout(vertex_attributes);
    if (!specialized_layout) return std::unexpected(specialized_layout.error());

    const wgpu::TextureFormat format = ((bits & Mesh2dPipelineKey::HDR) != 0)
                                           ? render::view::ViewTarget::TEXTURE_FORMAT_HDR
                                           : image::bevy_default<wgpu::TextureFormat>();
    const bool blend_alpha           = (bits & Mesh2dPipelineKey::BLEND_ALPHA) != 0;

    std::vector<mesh::VertexBufferLayout> vertex_buffers;
    if (!specialized_layout->attributes.empty()) vertex_buffers.push_back(*specialized_layout);

    render::VertexState vertex_state{.shader = shader};
    vertex_state.set_buffers(vertex_buffers);
    vertex_state.set_shader_defs(shader_defs);
    // The embedded mesh2d shader names its entry points `vertex` and `fragment`
    // (its Bevy WGSL equivalent uses `vs_main`/`fs_main`), and wgpu does not
    // infer the name: leaving it unset sends the engine's "main" default, which
    // `vkCreateGraphicsPipelines` rejects because the module declares no such
    // entry point (VUID-VkPipelineShaderStageCreateInfo-pName-00707). The
    // headless mesh2d shader test always set these explicitly, which is why it
    // passed while every live mesh2d pipeline failed.
    vertex_state.set_entry_point("vertex");

    auto color_target = wgpu::ColorTargetState().setFormat(format).setWriteMask(wgpu::ColorWriteMask::eAll);
    if (blend_alpha) {
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

    render::FragmentState fragment_state{.shader = shader};
    fragment_state.add_target(color_target);
    fragment_state.set_shader_defs(shader_defs);
    fragment_state.set_entry_point("fragment");

    return render::RenderPipelineDescriptor{
        .label     = blend_alpha ? "transparent_mesh2d_pipeline" : "opaque_mesh2d_pipeline",
        .layouts   = std::vector<render::BindGroupLayoutDescriptor>{view_layout, mesh_layout},
        .vertex    = std::move(vertex_state),
        .primitive = wgpu::PrimitiveState()
                         .setTopology(key.primitive_topology())
                         .setFrontFace(wgpu::FrontFace::eCCW)
                         .setCullMode(wgpu::CullMode::eNone),
        // Core2D clears the reverse-Z depth buffer to 0, so greater-equal.
        .depth_stencil = wgpu::DepthStencilState()
                             .setFormat(core_graph::core_2d::CORE_2D_DEPTH_FORMAT)
                             .setDepthWriteEnabled(blend_alpha ? wgpu::OptionalBool::eFalse : wgpu::OptionalBool::eTrue)
                             .setDepthCompare(wgpu::CompareFunction::eGreaterEqual)
                             // Bevy's `StencilState::IGNORE`. Written explicitly
                             // because this wrapper zero-initializes these fields
                             // and 0 is `Undefined` for both `CompareFunction`
                             // and `StencilOperation` — not a value wgpu-native
                             // accepts.
                             .setStencilFront(wgpu::StencilFaceState()
                                                  .setCompare(wgpu::CompareFunction::eAlways)
                                                  .setFailOp(wgpu::StencilOperation::eKeep)
                                                  .setDepthFailOp(wgpu::StencilOperation::eKeep)
                                                  .setPassOp(wgpu::StencilOperation::eKeep))
                             .setStencilBack(wgpu::StencilFaceState()
                                                 .setCompare(wgpu::CompareFunction::eAlways)
                                                 .setFailOp(wgpu::StencilOperation::eKeep)
                                                 .setDepthFailOp(wgpu::StencilOperation::eKeep)
                                                 .setPassOp(wgpu::StencilOperation::eKeep))
                             .setStencilReadMask(0)
                             .setStencilWriteMask(0),
        .multisample =
            wgpu::MultisampleState().setCount(key.msaa_samples()).setMask(~0u).setAlphaToCoverageEnabled(false),
        .fragment = std::move(fragment_state),
    };
}
void check_views_need_specialization(ecs::ResMut<ViewKeyCache> view_key_cache,
                                     ecs::ResMut<ViewSpecializationTicks> view_specialization_ticks,
                                     ecs::Query<ecs::Item<const render::sync_world::MainEntity&,
                                                          const render::view::ExtractedView&,
                                                          const render::view::Msaa&,
                                                          ecs::Opt<const core_graph::Tonemapping&>,
                                                          ecs::Opt<const core_graph::DebandDither&>>> views,
                                     ecs::SystemChangeTick ticks) {
    // Bevy `check_views_need_specialization` (mesh.rs:122-156).
    for (auto&& [view_entity, view, msaa, tonemapping, dither] : views.iter()) {
        std::uint32_t bits =
            Mesh2dPipelineKey::from_msaa_samples(samples(msaa)).bits() | Mesh2dPipelineKey::from_hdr(view.hdr).bits();

        if (!view.hdr) {
            if (tonemapping) {
                bits |= Mesh2dPipelineKey::TONEMAP_IN_SHADER;
                bits |= tonemapping_pipeline_key(tonemapping->get()).bits();
            }
            if (dither && dither->get() == core_graph::DebandDither::Enabled) {
                bits |= Mesh2dPipelineKey::DEBAND_DITHER;
            }
        }

        const auto view_key = Mesh2dPipelineKey{bits};
        const auto it       = view_key_cache->keys.find(view_entity);
        if (it == view_key_cache->keys.end() || it->second.bits() != view_key.bits()) {
            view_key_cache->keys.insert_or_assign(view_entity, view_key);
            view_specialization_ticks->ticks.insert_or_assign(view_entity, ticks.this_run());
        }
    }
}

void init_batched_instance_buffer(ecs::Commands commands, ecs::Res<wgpu::Device> device) {
    // Bevy `init_batched_instance_buffer` (mesh.rs:158-161): the shared
    // per-buffer-data instance buffer the mesh2d bind group binds.
    wgpu::Limits limits{};
    device.get().getLimits(&limits);
    commands.insert_resource(render::batching::BatchedInstanceBuffer<Mesh2dUniform>{limits});
}

void Mesh2dRenderPlugin::attach_mesh2d_pipeline(app::App& app) {
    // Bevy's `Mesh2dRenderPlugin::build` publishes the embedded shader and adds
    // `init_mesh_2d_pipeline` to the render app's RenderStartup.
    register_mesh2d_shader(app.world_mut());
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        auto& render = render_app->get();
        // Bevy's `Mesh2dRenderPlugin::build` (mesh.rs:73-111). Bindings are
        // registered as embedded bytes above, then loaded at startup when the
        // device-dependent uniform batch size is available.
        render.world_mut().init_resource<ViewKeyCache>();
        render.world_mut().init_resource<RenderMesh2dInstances>();
        render.world_mut().init_resource<render::SpecializedMeshPipelines<Mesh2dPipeline>>();
        render.world_mut().init_resource<ViewSpecializationTicks>();
        // The batching systems read this, and Epix validates a system's params when
        // it is first dispatched. Bevy's plugin gets it from the batching plugin;
        // this plugin registers the batch systems directly, so it must create the
        // buffers itself.
        render.world_mut()
            .init_resource<epix::render::batching::BatchedInstanceBuffers<Mesh2dUniform, std::monostate>>();

        render.add_systems(render::RenderStartup,
                           into(init_mesh_2d_pipeline, init_batched_instance_buffer, load_mesh2d_bindings)
                               .set_names(std::array{"init mesh 2d pipeline", "init batched instance buffer",
                                                     "load mesh2d bindings"}));
        render.add_systems(render::ExtractSchedule, into(extract_mesh2d).set_name("extract mesh2d"));
        render.add_systems(render::Render, into(check_views_need_specialization)
                                               .in_set(render::RenderSystems::PrepareAssets)
                                               .set_name("check views need specialization"));
        render.add_systems(render::Render, into(render::phase::sweep_old_entities<core_graph::core_2d::Opaque2D>,
                                                render::phase::sweep_old_entities<core_graph::core_2d::AlphaMask2D>)
                                               .in_set(render::RenderSystems::QueueSweep)
                                               .set_name("sweep old mesh2d entities"));
        render.add_systems(
            render::Render,
            into(render::batching::batch_and_prepare_binned_render_phase<core_graph::core_2d::Opaque2D, Mesh2dPipeline>)
                .in_set(render::RenderSystems::PrepareResources)
                .set_name("batch opaque mesh2d"));
        render.add_systems(render::Render, into(render::batching::batch_and_prepare_binned_render_phase<
                                                    core_graph::core_2d::AlphaMask2D, Mesh2dPipeline>)
                                               .in_set(render::RenderSystems::PrepareResources)
                                               .set_name("batch alpha-mask mesh2d"));
        render.add_systems(render::Render, into(render::batching::batch_and_prepare_sorted_render_phase<
                                                    core_graph::core_2d::Transparent2D, Mesh2dPipeline>)
                                               .in_set(render::RenderSystems::PrepareResources)
                                               .set_name("batch transparent mesh2d"));
        // Bevy `Mesh2dRenderPlugin::build` (mesh.rs:103-104) registers
        // `no_gpu_preprocessing::write_batched_instance_buffer::<Mesh2dPipeline>`
        // here, once for every phase that shares `Mesh2dUniform`. mesh2d uses
        // `GpuPreprocessingMode::None` (see the phase preparation in
        // `core2d.cpp`), so the *CPU* instance buffer is the one
        // `prepare_mesh2d_bind_group` binds, and Bevy does not register the
        // GPU-preprocessing writer for this pipeline at all.
        render.add_systems(render::Render, into(render::batching::write_batched_cpu_instance_buffer<Mesh2dPipeline>)
                                               .in_set(render::RenderSystems::PrepareResourcesFlush)
                                               .set_name("write batched instance buffer"));
        render.add_systems(render::Render, into(prepare_mesh2d_bind_group, prepare_mesh2d_view_bind_groups)
                                               .in_set(render::RenderSystems::PrepareBindGroups)
                                               .set_name("prepare mesh2d bind groups"));
        // Bevy mesh.rs:107-109: the shared CPU instance buffer is cleared once
        // per frame, after the render graph has consumed it.
        render.add_systems(render::Render, into(render::batching::clear_batched_cpu_instance_buffers<Mesh2dPipeline>)
                                               .in_set(render::RenderSystems::Cleanup)
                                               .after(render::RenderSystems::Render)
                                               .set_name("clear batched cpu instance buffers"));
    }
}

}  // namespace epix::sprite_render
