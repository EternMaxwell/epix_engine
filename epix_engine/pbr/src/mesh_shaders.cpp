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

/**
 * @brief Slang representation of pinned Bevy 0.18 `mesh_view_types.wgsl`.
 *
 * These are the group-0 types the view layout binds, each one mirroring a C++
 * struct the port already asserts the size of: `ClusterableObject` is
 * `GpuClusterableObject`, `Lights`/`DirectionalLight` are `gpu_lights.hpp`,
 * `Fog` is `GpuFog`, the three cluster structs are `cluster.hpp`'s buffer
 * elements, `LightProbe(s)` are `light_probe.hpp`, the SSR settings are
 * `ssr.hpp`'s uniform and `ClusteredDecal(s)` are `clustered_decal.hpp`'s.
 *
 * `MAX_CASCADES_PER_LIGHT`, `MAX_DIRECTIONAL_LIGHTS` and
 * `AVAILABLE_STORAGE_BUFFER_BINDINGS` are definitions the pipeline supplies, as
 * Bevy's `load_shader_library!` closure attaches the first two.
 *
 * The core matrices are `float4x4`, which is layout-identical to WGSL's
 * `mat4x4`; the two `mat3x4` values are the explicit `float4` columns the C++
 * structs store. `OrderIndependentTransparencySettings.layers_count` keeps the
 * shader's spelling, not the Rust component's `layer_count`.
 */
constexpr std::string_view kMeshViewTypesShader = R"slang(
module epix.mesh_view_types;

namespace epix::mesh_view_types {

public struct ClusterableObject {
    public float4 light_custom_data;
    public float4 color_inverse_square_range;
    public float4 position_radius;
    public uint flags;
    public float shadow_depth_bias;
    public float shadow_normal_bias;
    public float spot_light_tan_angle;
    public float soft_shadow_size;
    public float shadow_map_near_z;
    public uint decal_index;
    public float pad;
};

public static const uint POINT_LIGHT_FLAGS_SHADOWS_ENABLED_BIT = 1u << 0u;
public static const uint POINT_LIGHT_FLAGS_SPOT_LIGHT_Y_NEGATIVE = 1u << 1u;
public static const uint POINT_LIGHT_FLAGS_VOLUMETRIC_BIT = 1u << 2u;
public static const uint POINT_LIGHT_FLAGS_AFFECTS_LIGHTMAPPED_MESH_DIFFUSE_BIT = 1u << 3u;

public struct DirectionalCascade {
    public float4x4 clip_from_world;
    public float texel_size;
    public float far_bound;
};

public struct DirectionalLight {
    public DirectionalCascade cascades[MAX_CASCADES_PER_LIGHT];
    public float4 color;
    public float3 direction_to_light;
    public uint flags;
    public float soft_shadow_size;
    public float shadow_depth_bias;
    public float shadow_normal_bias;
    public uint num_cascades;
    public float cascades_overlap_proportion;
    public uint depth_texture_base_index;
    public uint decal_index;
    public float sun_disk_angular_size;
    public float sun_disk_intensity;
};

public static const uint DIRECTIONAL_LIGHT_FLAGS_SHADOWS_ENABLED_BIT = 1u << 0u;
public static const uint DIRECTIONAL_LIGHT_FLAGS_VOLUMETRIC_BIT = 1u << 1u;
public static const uint DIRECTIONAL_LIGHT_FLAGS_AFFECTS_LIGHTMAPPED_MESH_DIFFUSE_BIT = 1u << 2u;

public struct Lights {
    public DirectionalLight directional_lights[MAX_DIRECTIONAL_LIGHTS];
    public float4 ambient_color;
    // x/y/z dimensions and n_clusters in w.
    public uint4 cluster_dimensions;
    // xy are the cluster dimensions over the view size; z and w are the
    // perspective or orthographic cluster factors.
    public float4 cluster_factors;
    public uint n_directional_lights;
    public int spot_light_shadowmap_offset;
    public uint ambient_light_affects_lightmapped_meshes;
};

public struct Fog {
    public float4 base_color;
    public float4 directional_light_color;
    // `be` and `bi` are allocated differently per fog mode.
    public float3 be;
    public float directional_light_exponent;
    public float3 bi;
    public uint mode;
};

// These must be kept in sync with the fog module's modes.
public static const uint FOG_MODE_OFF = 0u;
public static const uint FOG_MODE_LINEAR = 1u;
public static const uint FOG_MODE_EXPONENTIAL = 2u;
public static const uint FOG_MODE_EXPONENTIAL_SQUARED = 3u;
public static const uint FOG_MODE_ATMOSPHERIC = 4u;

#if AVAILABLE_STORAGE_BUFFER_BINDINGS >= 3
public struct ClusterableObjects {
    public ClusterableObject data[];
};
public struct ClusterLightIndexLists {
    public uint data[];
};
public struct ClusterOffsetsAndCounts {
    public uint4 data[][2];
};
#else
public struct ClusterableObjects {
    public ClusterableObject data[204];
};
public struct ClusterLightIndexLists {
    // Each u32 contains four u8 indices into the ClusterableObjects array.
    public uint4 data[1024];
};
public struct ClusterOffsetsAndCounts {
    // Each u32 contains a 24-bit index into ClusterLightIndexLists in the high
    // 24 bits and an 8-bit count of the lights in the low 8 bits.
    public uint4 data[1024];
};
#endif

public struct LightProbe {
    // Stored transposed to save space; the accessor transposes it back.
    public float4 light_from_world_transposed[3];
    public int cubemap_index;
    public float intensity;
    public uint affects_lightmapped_mesh_diffuse;
};

public struct LightProbes {
    // This must match the reflection-probe cap on the Rust side.
    public LightProbe reflection_probes[8];
    public LightProbe irradiance_volumes[8];
    public int reflection_probe_count;
    public int irradiance_volume_count;
    // The index of the view environment map cubemap binding, or -1.
    public int view_cubemap_index;
    public uint smallest_specular_mip_level_for_view;
    public float intensity_for_view;
    public uint view_environment_map_affects_lightmapped_mesh_diffuse;
};

public struct ScreenSpaceReflectionsSettings {
    public float perceptual_roughness_threshold;
    public float thickness;
    public uint linear_steps;
    public float linear_march_exponent;
    public uint bisection_steps;
    public uint use_secant;
};

public struct EnvironmentMapUniform {
    // Transformation matrix for the environment cubemaps in world space.
    public float4x4 transform;
};

public struct OrderIndependentTransparencySettings {
    public int layers_count;
    public float alpha_threshold;
};

public struct ClusteredDecal {
    public float4x4 local_from_world;
    public int base_color_texture_index;
    public int normal_map_texture_index;
    public int metallic_roughness_texture_index;
    public int emissive_texture_index;
    public uint tag;
    public uint pad_a;
    public uint pad_b;
    public uint pad_c;
};

public struct ClusteredDecals {
    public ClusteredDecal decals[];
};

}
)slang";

/**
 * @brief Slang representation of pinned Bevy 0.18 `atmosphere/types.wgsl`.
 *
 * The explicit padding is what WGSL's alignment forces and Slang's packing does
 * not: `vec3` is sixteen-byte aligned in the shader (so `Atmosphere` is 32 bytes
 * and `AtmosphereSettings` 80), while glm's members on the CPU side and Slang's
 * default packing would make them 20 and 72. The C++ structs carry the same
 * padding, and their `static_assert`s pin the sizes both sides agree on.
 */
constexpr std::string_view kAtmosphereTypesShader = R"slang(
module epix.atmosphere_types;

namespace epix::atmosphere_types {

public struct Atmosphere {
    public float3 ground_albedo;
    public float bottom_radius;
    public float top_radius;
    // Padding: the shader's vec3<f32> is sixteen-byte aligned.
    public float pad[3];
};

public struct AtmosphereSettings {
    public uint2 transmittance_lut_size;
    public uint2 multiscattering_lut_size;
    public uint2 sky_view_lut_size;
    // Padding: the shader's vec3<u32> is sixteen-byte aligned.
    public uint pad[2];
    public uint3 aerial_view_lut_size;
    public uint transmittance_lut_samples;
    public uint multiscattering_lut_dirs;
    public uint multiscattering_lut_samples;
    public uint sky_view_lut_samples;
    public uint aerial_view_lut_samples;
    public float aerial_view_lut_max_distance;
    public float scene_units_to_m;
    public uint sky_max_samples;
    public uint rendering_method;
};

// "Atmosphere space" is the view position with y = 0 and oriented horizontally,
// so the horizon stays a horizontal line in the LUTs.
public struct AtmosphereTransforms {
    public float4x4 world_from_atmosphere;
};

public struct AtmosphereData {
    public Atmosphere atmosphere;
    public AtmosphereSettings settings;
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
    registry->get().insert_asset_static(MESH_VIEW_TYPES_SHADER_PATH, shader_bytes(kMeshViewTypesShader));
    registry->get().insert_asset_static(ATMOSPHERE_TYPES_SHADER_PATH, shader_bytes(kAtmosphereTypesShader));
    world.insert_resource(MeshShaderHandles{
        .mesh_types = server->get().load<shader::Shader>(std::string("embedded://") + std::string(MESH_TYPES_SHADER_PATH)),
        .mesh_bindings =
            server->get().load<shader::Shader>(std::string("embedded://") + std::string(MESH_BINDINGS_SHADER_PATH)),
        .forward_io =
            server->get().load<shader::Shader>(std::string("embedded://") + std::string(FORWARD_IO_SHADER_PATH)),
        .mesh_view_types = server->get().load<shader::Shader>(std::string("embedded://") +
                                                              std::string(MESH_VIEW_TYPES_SHADER_PATH)),
        .atmosphere_types = server->get().load<shader::Shader>(std::string("embedded://") +
                                                               std::string(ATMOSPHERE_TYPES_SHADER_PATH)),
    });
}

}  // namespace epix::pbr
