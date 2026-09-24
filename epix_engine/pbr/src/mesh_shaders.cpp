#include <epix/pbr/mesh_shaders.hpp>

#include <spdlog/spdlog.h>

#include <string>

namespace epix::pbr {

namespace {

/**
 * @brief Slang representation of pinned Bevy 0.18 `mesh_types.wgsl`.
 *
 * The one representation change: Bevy's two `mat3x4<f32>` transforms and its
 * `mat2x4<f32>` inverse transpose are declared as the explicit `float4` arrays
 * the C++ `MeshUniform` stores, because a Slang `float3x4` is a matrix whose
 * columns are `vec3`, which this layout is not. The 176-byte element with its
 * three-word tail padding is the same struct `mesh_uniform.hpp` asserts.
 */
constexpr std::string_view kMeshTypesShader = R"slang(
module epix.mesh_types;

namespace epix::mesh_types {

public struct Mesh {
    // Affine 4x3 matrix transposed to 3x4: three columns of `vec4`, the
    // translation in each one's fourth component.
    public float4 world_from_local[3];
    public float4 previous_world_from_local[3];
    // The 3x3 inverse transpose packed as two `vec4`s and a trailing float:
    // [0].xyz, [1].x then [1].yz, [2].xy then [2].z.
    public float4 local_from_world_transpose_a[2];
    public float local_from_world_transpose_b;
    // A bit field indicating various options; 32 bits.
    public uint flags;
    public uint2 lightmap_uv_rect;
    // The index of the mesh's first vertex in the vertex buffer.
    public uint first_vertex_index;
    public uint current_skin_index;
    // Low 16 bits: index of the material inside the bind group data.
    // High 16 bits: index of the lightmap in the binding array.
    public uint material_and_lightmap_bind_group_slot;
    // User supplied index to identify the mesh instance.
    public uint tag;
    public uint pad;
    // The element is 16-byte aligned; glm's members are not, so the CPU struct
    // spells this padding out too.
    public uint tail_padding[3];
};

#ifdef SKINNED
public struct SkinnedMesh {
    public float4x4 data[256];
};
#endif

#ifdef MORPH_TARGETS
public struct MorphWeights {
    // 64 = 256 / 4 (256 = MAX_MORPH_WEIGHTS).
    public float4 weights[64];
};
#endif

// [2^0, 2^16)
public static const uint MESH_FLAGS_VISIBILITY_RANGE_INDEX_BITS = (1u << 16u) - 1u;
public static const uint MESH_FLAGS_NO_FRUSTUM_CULLING_BIT = 1u << 28u;
public static const uint MESH_FLAGS_SHADOW_RECEIVER_BIT = 1u << 29u;
public static const uint MESH_FLAGS_TRANSMITTED_SHADOW_RECEIVER_BIT = 1u << 30u;
// If the flag is set the sign is positive, else it is negative.
public static const uint MESH_FLAGS_SIGN_DETERMINANT_MODEL_3X3_BIT = 1u << 31u;

}
)slang";

/**
 * @brief Slang representation of pinned Bevy 0.18 `mesh_bindings.wgsl`.
 *
 * The binding is group 2, not 1: Bevy's layout list is the view layout, the
 * binding-array layout and then this one (`render/mesh.rs:2433-2451`).
 */
constexpr std::string_view kMeshBindingsShader = R"slang(
module epix.mesh_bindings;

import epix.mesh_types;

#ifndef MESHLET_MESH_MATERIAL_PASS
#ifdef PER_OBJECT_BUFFER_BATCH_SIZE
public struct MeshBatch {
    epix::mesh_types::Mesh values[PER_OBJECT_BUFFER_BATCH_SIZE];
};
public [[vk::binding(0, 2)]] ConstantBuffer<MeshBatch> mesh;
#else
public [[vk::binding(0, 2)]] StructuredBuffer<epix::mesh_types::Mesh> mesh;
#endif
#endif
)slang";

/** @brief Slang representation of pinned Bevy 0.18 `forward_io.wgsl`. */
constexpr std::string_view kForwardIoShader = R"slang(
module epix.forward_io;

namespace epix::forward_io {

public struct Vertex {
    public uint instance_index : SV_InstanceID;
#ifdef VERTEX_POSITIONS
    public [[vk::location(0)]] float3 position;
#endif
#ifdef VERTEX_NORMALS
    public [[vk::location(1)]] float3 normal;
#endif
#ifdef VERTEX_UVS_A
    public [[vk::location(2)]] float2 uv;
#endif
#ifdef VERTEX_UVS_B
    public [[vk::location(3)]] float2 uv_b;
#endif
#ifdef VERTEX_TANGENTS
    public [[vk::location(4)]] float4 tangent;
#endif
#ifdef VERTEX_COLORS
    public [[vk::location(5)]] float4 color;
#endif
#ifdef SKINNED
    public [[vk::location(6)]] uint4 joint_indices;
    public [[vk::location(7)]] float4 joint_weights;
#endif
#ifdef MORPH_TARGETS
    public uint index : SV_VertexID;
#endif
};

public struct VertexOutput {
    // Clip position as a vertex stage output, fragment coordinates as a
    // fragment stage input.
    public float4 position : SV_Position;
    public [[vk::location(0)]] float4 world_position;
    public [[vk::location(1)]] float3 world_normal;
#ifdef VERTEX_UVS_A
    public [[vk::location(2)]] float2 uv;
#endif
#ifdef VERTEX_UVS_B
    public [[vk::location(3)]] float2 uv_b;
#endif
#ifdef VERTEX_TANGENTS
    public [[vk::location(4)]] float4 world_tangent;
#endif
#ifdef VERTEX_COLORS
    public [[vk::location(5)]] float4 color;
#endif
#ifdef VERTEX_OUTPUT_INSTANCE_INDEX
    public nointerpolation [[vk::location(6)]] uint instance_index;
#endif
#ifdef VISIBILITY_RANGE_DITHER
    public nointerpolation [[vk::location(7)]] int visibility_range_dither;
#endif
};

public struct FragmentOutput {
    public [[vk::location(0)]] float4 color;
};

}
)slang";

std::span<const std::byte> shader_bytes(std::string_view source) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(source.data()), source.size());
}

}  // namespace

void register_mesh_shaders(ecs::World& world) {
    auto registry = world.get_resource_mut<assets::EmbeddedAssetRegistry>();
    auto server   = world.get_resource<assets::AssetServer>();
    if (!registry || !server) {
        spdlog::warn("[pbr.mesh] EmbeddedAssetRegistry or AssetServer not available. Mesh shaders not registered.");
        return;
    }
    registry->get().insert_asset_static(MESH_TYPES_SHADER_PATH, shader_bytes(kMeshTypesShader));
    registry->get().insert_asset_static(MESH_BINDINGS_SHADER_PATH, shader_bytes(kMeshBindingsShader));
    registry->get().insert_asset_static(FORWARD_IO_SHADER_PATH, shader_bytes(kForwardIoShader));
    world.insert_resource(MeshShaderHandles{
        .mesh_types = server->get().load<shader::Shader>(std::string("embedded://") + std::string(MESH_TYPES_SHADER_PATH)),
        .mesh_bindings =
            server->get().load<shader::Shader>(std::string("embedded://") + std::string(MESH_BINDINGS_SHADER_PATH)),
        .forward_io =
            server->get().load<shader::Shader>(std::string("embedded://") + std::string(FORWARD_IO_SHADER_PATH)),
    });
}

}  // namespace epix::pbr
