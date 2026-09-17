#include <epix/core_graph.hpp>
#include <epix/sprite_render/mesh2d.hpp>
#include <epix/sprite_render/mesh2d_pipeline.hpp>

#include <spdlog/spdlog.h>

namespace epix::sprite_render {
namespace {

/** @brief Embedded path of the mesh2d shader (Bevy `mesh2d.wgsl`). */
constexpr std::string_view kMesh2dShaderPath = "sprite/mesh2d.slang";

/**
 * @brief Slang representation of pinned Bevy 0.18 `mesh2d.wgsl`,
 * `mesh2d_functions.wgsl`, `mesh2d_types.wgsl`, `mesh2d_bindings.wgsl` and
 * `mesh2d_view_bindings.wgsl`.
 *
 * Bevy splits these over five naga_oil modules and imports
 * `bevy_render::maths::{affine3_to_square, mat2x4_f32_to_mat3x3_unpack}`. Epix
 * has no `maths` module, so those two helpers are inlined here, and the five
 * modules are one file because every one of them is private to mesh2d.
 *
 * The one representation change: Bevy's `Mesh2d.world_from_local` is a WGSL
 * `mat3x4` (three columns of `vec4`) and its `local_from_world_transpose_a` a
 * `mat2x4`. Slang matrix orientation is the opposite of WGSL's, so both are
 * declared as the explicit `float4` arrays the C++ `Mesh2dUniform` stores; the
 * byte layout is identical, and the accessors below spell the transform out
 * with dot products instead of a matrix constructor.
 */
constexpr std::string_view kMesh2dShader = R"slang(
import epix.view;
import epix.globals;
#ifdef TONEMAP_IN_SHADER
import epix.tonemapping;
#endif

// Group 0: the mesh2d view bindings. Bevy's mesh2d_view_bindings.wgsl binds the
// view and globals uniforms plus the tonemapping LUT.
[[vk::binding(0, 0)]] ConstantBuffer<epix::view::View> view;
[[vk::binding(1, 0)]] ConstantBuffer<epix::globals::Globals> globals;
[[vk::binding(2, 0)]] Texture3D<float4> dt_lut_texture;
[[vk::binding(3, 0)]] SamplerState dt_lut_sampler;

struct Mesh2d {
    // WGSL mat3x4, three columns of vec4; column_major keeps the same 48 bytes.
    column_major float4x3 world_from_local;
    // WGSL mat2x4, two columns of vec4 holding the packed inverse transpose.
    column_major float4x2 local_from_world_transpose_a;
    float local_from_world_transpose_b;
    // 'flags' is a bit field; the type is 32 bits, so up to 32 options.
    uint flags;
    uint tag;
    // Bevy's WGSL struct is padded to the 16-byte array stride (92 -> 96).
    float _padding;
};

// Group 1: the per-instance mesh2d uniforms (mesh2d_bindings.wgsl): a storage
// buffer where the adapter has them, otherwise Bevy's uniform array read through
// a dynamic offset.
#ifdef PER_OBJECT_BUFFER_BATCH_SIZE
struct Mesh2dBatch {
    Mesh2d values[PER_OBJECT_BUFFER_BATCH_SIZE];
};
[[vk::binding(0, 1)]] ConstantBuffer<Mesh2dBatch> mesh;
#else
[[vk::binding(0, 1)]] StructuredBuffer<Mesh2d> mesh;
#endif

Mesh2d mesh2d_instance(uint instance_index) {
#ifdef PER_OBJECT_BUFFER_BATCH_SIZE
    return mesh.values[instance_index];
#else
    return mesh[instance_index];
#endif
}

struct VertexInput {
    uint instance_index : SV_InstanceID;
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

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 world_position;
    [[vk::location(1)]] float3 world_normal;
    [[vk::location(2)]] float2 uv;
#ifdef VERTEX_TANGENTS
    [[vk::location(3)]] float4 world_tangent;
#endif
#ifdef VERTEX_COLORS
    [[vk::location(4)]] float4 color;
#endif
};

// mesh2d_functions.wgsl's helpers. The stored columns are Bevy's transposed
// affine, so element [row][col] is axis col's row component, and row 3 holds the
// translation.
float3 mesh2d_position_local_to_world(float4x3 world_from_local, float3 position) {
    return float3(world_from_local[0][0] * position.x + world_from_local[0][1] * position.y +
                      world_from_local[0][2] * position.z + world_from_local[3][0],
                  world_from_local[1][0] * position.x + world_from_local[1][1] * position.y +
                      world_from_local[1][2] * position.z + world_from_local[3][1],
                  world_from_local[2][0] * position.x + world_from_local[2][1] * position.y +
                      world_from_local[2][2] * position.z + world_from_local[3][2]);
}

float3 mesh2d_normal_local_to_world(float4x2 packed_a, float packed_b, float3 normal) {
    // mat2x4_f32_to_mat3x3_unpack: the columns of the inverse transpose, where
    // column 0 is (a[0].xyz, a[0].w), column 1 is (a[1].x, a[1].y, a[1].z, a[1].w)
    // and column 2 is (b, ...) respectively.
    float3 c0 = float3(packed_a[0][0], packed_a[1][0], packed_a[2][0]);
    float3 c1 = float3(packed_a[3][0], packed_a[0][1], packed_a[1][1]);
    float3 c2 = float3(packed_a[2][1], packed_a[3][1], packed_b);
    return c0 * normal.x + c1 * normal.y + c2 * normal.z;
}

float4 mesh2d_tangent_local_to_world(float4x3 world_from_local, float4 tangent) {
    return float4(world_from_local[0][0] * tangent.x + world_from_local[0][1] * tangent.y +
                      world_from_local[0][2] * tangent.z,
                  world_from_local[1][0] * tangent.x + world_from_local[1][1] * tangent.y +
                      world_from_local[1][2] * tangent.z,
                  world_from_local[2][0] * tangent.x + world_from_local[2][1] * tangent.y +
                      world_from_local[2][2] * tangent.z,
                  tangent.w);
}

[shader("vertex")]
VertexOutput vertex(VertexInput input) {
    VertexOutput output;
    Mesh2d instance = mesh2d_instance(input.instance_index);
#ifdef VERTEX_UVS
    output.uv = input.uv;
#endif

#ifdef VERTEX_POSITIONS
    float4 world_position =
        float4(mesh2d_position_local_to_world(instance.world_from_local, input.position), 1.0);
    output.world_position = world_position;
    // The intermediate world_position assignment matters for precision when the
    // depth comparison uses the 'equals' function.
    output.position = mul(view.clip_from_world, world_position);
#endif

#ifdef VERTEX_NORMALS
    output.world_normal = mesh2d_normal_local_to_world(instance.local_from_world_transpose_a,
                                                       instance.local_from_world_transpose_b,
                                                       input.normal);
#endif

#ifdef VERTEX_TANGENTS
    output.world_tangent = mesh2d_tangent_local_to_world(instance.world_from_local, input.tangent);
#endif

#ifdef VERTEX_COLORS
    output.color = input.color;
#endif
    return output;
}

[shader("fragment")]
float4 fragment(VertexOutput input) : SV_Target {
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
void register_mesh2d_shader(ecs::World& world) {
    auto registry = world.get_resource_mut<assets::EmbeddedAssetRegistry>();
    if (!registry) {
        spdlog::warn("[sprite.mesh2d] EmbeddedAssetRegistry not available. Mesh2d shader not registered.");
        return;
    }
    registry->get().insert_asset_static(kMesh2dShaderPath, shader_bytes(kMesh2dShader));
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
        uniform_buffer(true, sizeof(render::view::ViewUniform)),
        uniform_buffer(false, sizeof(render::GlobalsUniform)),
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
                           ecs::Res<render::PipelineServer> pipeline_server,
                           ecs::Res<wgpu::Device> device,
                           ecs::Res<assets::AssetServer> asset_server) {
    wgpu::Limits limits{};
    device.get().getLimits(&limits);
    commands.insert_resource(Mesh2dPipeline{
        .view_layout = pipeline_server->get_bind_group_layout(mesh2d_view_layout_descriptor()),
        .mesh_layout = pipeline_server->get_bind_group_layout(mesh2d_mesh_layout_descriptor(limits)),
        .shader      = asset_server.get().load<shader::Shader>("embedded://sprite/mesh2d.slang"),
        .per_object_buffer_batch_size = mesh2d_per_object_buffer_batch_size(limits),
    });
}

void Mesh2dRenderPlugin::attach_mesh2d_pipeline(app::App& app) {
    // Bevy's `Mesh2dRenderPlugin::build` publishes the embedded shader and adds
    // `init_mesh_2d_pipeline` to the render app's RenderStartup.
    register_mesh2d_shader(app.world_mut());
    if (auto render_app = app.get_sub_app_mut(render::Render)) {
        render_app->get().add_systems(render::RenderStartup,
                                      into(init_mesh_2d_pipeline).set_name("init mesh 2d pipeline"));
    }
}

}  // namespace epix::sprite_render
