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

/**
 * @brief Slang representation of pinned Bevy 0.18 `mesh_view_bindings.wgsl`.
 *
 * Every binding is the entry the view layout declares, at the same group and
 * binding number, with Bevy's definitions gating exactly what they gate.
 *
 * Two representation notes. Bevy's `var<uniform> visibility_ranges:
 * array<vec4<f32>, 64u>` becomes a one-member block, because Slang has no bare
 * uniform array; the block's layout is the same and consumers read `values[i]`.
 * The binding arrays (`binding_array<texture_cube<f32>, 8u>` and the two others)
 * are descriptor arrays, which is how Slang and SPIR-V spell a binding array.
 */
constexpr std::string_view kMeshViewBindingsShader = R"slang(
module epix.mesh_view_bindings;

import epix.mesh_view_types;
import epix.atmosphere_types;
import epix.view;
import epix.globals;

public [[vk::binding(0, 0)]] ConstantBuffer<epix::view::View> view;
public [[vk::binding(1, 0)]] ConstantBuffer<epix::mesh_view_types::Lights> lights;
#ifdef NO_CUBE_ARRAY_TEXTURES_SUPPORT
public [[vk::binding(2, 0)]] DepthTextureCube point_shadow_textures;
#else
public [[vk::binding(2, 0)]] DepthTextureCubeArray point_shadow_textures;
#endif
public [[vk::binding(3, 0)]] SamplerComparisonState point_shadow_textures_comparison_sampler;
#ifdef PCSS_SAMPLERS_AVAILABLE
public [[vk::binding(4, 0)]] SamplerState point_shadow_textures_linear_sampler;
#endif
#ifdef NO_ARRAY_TEXTURES_SUPPORT
public [[vk::binding(5, 0)]] DepthTexture2D directional_shadow_textures;
#else
public [[vk::binding(5, 0)]] DepthTexture2DArray directional_shadow_textures;
#endif
public [[vk::binding(6, 0)]] SamplerComparisonState directional_shadow_textures_comparison_sampler;
#ifdef PCSS_SAMPLERS_AVAILABLE
public [[vk::binding(7, 0)]] SamplerState directional_shadow_textures_linear_sampler;
#endif

#if AVAILABLE_STORAGE_BUFFER_BINDINGS >= 3
// A WGSL storage buffer whose whole block is a runtime-sized array is a Slang
// `StructuredBuffer` over the *element*; the runtime array is the buffer's own.
// The offsets element is a pair of `uvec4`s, so the buffer is declared as the
// flat `uint4` array the pair packs into: same bytes, and a reader scales the
// index by two.
public [[vk::binding(8, 0)]] StructuredBuffer<epix::mesh_view_types::ClusterableObject> clusterable_objects;
public [[vk::binding(9, 0)]] StructuredBuffer<uint> clusterable_object_index_lists;
public [[vk::binding(10, 0)]] StructuredBuffer<uint4> cluster_offsets_and_counts;
#else
public [[vk::binding(8, 0)]] ConstantBuffer<epix::mesh_view_types::ClusterableObjects> clusterable_objects;
public [[vk::binding(9, 0)]] ConstantBuffer<epix::mesh_view_types::ClusterLightIndexLists> clusterable_object_index_lists;
public [[vk::binding(10, 0)]] ConstantBuffer<epix::mesh_view_types::ClusterOffsetsAndCounts> cluster_offsets_and_counts;
#endif

public [[vk::binding(11, 0)]] ConstantBuffer<epix::globals::Globals> globals;
public [[vk::binding(12, 0)]] ConstantBuffer<epix::mesh_view_types::Fog> fog;
public [[vk::binding(13, 0)]] ConstantBuffer<epix::mesh_view_types::LightProbes> light_probes;

public static const uint VISIBILITY_RANGE_UNIFORM_BUFFER_SIZE = 64u;
#if AVAILABLE_STORAGE_BUFFER_BINDINGS >= 6
public [[vk::binding(14, 0)]] StructuredBuffer<float4> visibility_ranges;
#else
public struct VisibilityRanges {
    public float4 values[VISIBILITY_RANGE_UNIFORM_BUFFER_SIZE];
};
public [[vk::binding(14, 0)]] ConstantBuffer<VisibilityRanges> visibility_ranges;
#endif

public [[vk::binding(15, 0)]] ConstantBuffer<epix::mesh_view_types::ScreenSpaceReflectionsSettings> ssr_settings;
public [[vk::binding(16, 0)]] Texture2D<float4> screen_space_ambient_occlusion_texture;
public [[vk::binding(17, 0)]] ConstantBuffer<epix::mesh_view_types::EnvironmentMapUniform> environment_map_uniform;

// Tonemapping LUTs; keep in sync with the tonemapping module.
public [[vk::binding(18, 0)]] Texture3D<float4> dt_lut_texture;
public [[vk::binding(19, 0)]] SamplerState dt_lut_sampler;

#ifdef MULTISAMPLED
#ifdef DEPTH_PREPASS
public [[vk::binding(20, 0)]] DepthTexture2DMS<0> depth_prepass_texture;
#endif
#ifdef NORMAL_PREPASS
public [[vk::binding(21, 0)]] Texture2DMS<float4> normal_prepass_texture;
#endif
#ifdef MOTION_VECTOR_PREPASS
public [[vk::binding(22, 0)]] Texture2DMS<float4> motion_vector_prepass_texture;
#endif
#else
#ifdef DEPTH_PREPASS
public [[vk::binding(20, 0)]] DepthTexture2D depth_prepass_texture;
#endif
#ifdef NORMAL_PREPASS
public [[vk::binding(21, 0)]] Texture2D<float4> normal_prepass_texture;
#endif
#ifdef MOTION_VECTOR_PREPASS
public [[vk::binding(22, 0)]] Texture2D<float4> motion_vector_prepass_texture;
#endif
#endif

#ifdef DEFERRED_PREPASS
public [[vk::binding(23, 0)]] Texture2D<uint> deferred_prepass_texture;
#endif

public [[vk::binding(24, 0)]] Texture2D<float4> view_transmission_texture;
public [[vk::binding(25, 0)]] SamplerState view_transmission_sampler;

#ifdef OIT_ENABLED
public [[vk::binding(26, 0)]] RWStructuredBuffer<uint2> oit_layers;
public [[vk::binding(27, 0)]] RWStructuredBuffer<int> oit_layer_ids;
public [[vk::binding(28, 0)]] ConstantBuffer<epix::mesh_view_types::OrderIndependentTransparencySettings> oit_settings;
#endif

#ifdef ATMOSPHERE
public [[vk::binding(29, 0)]] Texture2D<float4> atmosphere_transmittance_texture;
public [[vk::binding(30, 0)]] SamplerState atmosphere_transmittance_sampler;
public [[vk::binding(31, 0)]] StructuredBuffer<epix::atmosphere_types::AtmosphereData> atmosphere_data;
#endif

#ifdef MULTIPLE_LIGHT_PROBES_IN_ARRAY
public [[vk::binding(0, 1)]] TextureCube<float4> diffuse_environment_maps[8];
public [[vk::binding(1, 1)]] TextureCube<float4> specular_environment_maps[8];
#else
public [[vk::binding(0, 1)]] TextureCube<float4> diffuse_environment_map;
public [[vk::binding(1, 1)]] TextureCube<float4> specular_environment_map;
#endif
public [[vk::binding(2, 1)]] SamplerState environment_map_sampler;

#ifdef IRRADIANCE_VOLUMES_ARE_USABLE
#ifdef MULTIPLE_LIGHT_PROBES_IN_ARRAY
public [[vk::binding(3, 1)]] Texture3D<float4> irradiance_volumes[8];
#else
public [[vk::binding(3, 1)]] Texture3D<float4> irradiance_volume;
#endif
public [[vk::binding(4, 1)]] SamplerState irradiance_volume_sampler;
#endif

#ifdef CLUSTERED_DECALS_ARE_USABLE
public [[vk::binding(5, 1)]] StructuredBuffer<epix::mesh_view_types::ClusteredDecal> clustered_decals;
public [[vk::binding(6, 1)]] Texture2D<float4> clustered_decal_textures[8];
public [[vk::binding(7, 1)]] SamplerState clustered_decal_sampler;
#endif
)slang";

/** @brief Slang representation of pinned Bevy 0.18 `occlusion_culling.wgsl`.
 *
 * The two functions take the depth pyramid as a parameter, so in Slang they take
 * a `Texture2D`; `textureDimensions` and `textureLoad` become `GetDimensions`
 * and `Load`. */
constexpr std::string_view kOcclusionCullingShader = R"slang(
module epix.occlusion_culling;

namespace epix::occlusion_culling {

public float2 get_aabb_size_in_pixels(float4 aabb, Texture2D<float4> depth_pyramid) {
    uint width, height;
    depth_pyramid.GetDimensions(width, height);
    float2 depth_pyramid_size_mip_0 = float2(width, height);
    float aabb_width_pixels = (aabb.z - aabb.x) * depth_pyramid_size_mip_0.x;
    float aabb_height_pixels = (aabb.w - aabb.y) * depth_pyramid_size_mip_0.y;
    return float2(aabb_width_pixels, aabb_height_pixels);
}

public float get_occluder_depth(float4 aabb, float2 aabb_pixel_size, Texture2D<float4> depth_pyramid) {
    float aabb_width_pixels = aabb_pixel_size.x;
    float aabb_height_pixels = aabb_pixel_size.y;

    uint width, height;
    depth_pyramid.GetDimensions(width, height);
    float2 depth_pyramid_size_mip_0 = float2(width, height);
    // The mip level the AABB fits in; Bevy notes that naga wants this signed.
    int depth_level = max(0, int(ceil(log2(max(aabb_width_pixels, aabb_height_pixels)))));
    uint level_width, level_height, level_count;
    // The mip-level overload also reports the level count, and takes the level
    // unsigned where `Load` takes it signed.
    depth_pyramid.GetDimensions(uint(depth_level), level_width, level_height, level_count);
    float2 depth_pyramid_size = float2(level_width, level_height);
    uint2 aabb_top_left = uint2(aabb.xy * depth_pyramid_size);

    float depth_quad_a = depth_pyramid.Load(int3(aabb_top_left, depth_level)).x;
    float depth_quad_b = depth_pyramid.Load(int3(aabb_top_left + uint2(1u, 0u), depth_level)).x;
    float depth_quad_c = depth_pyramid.Load(int3(aabb_top_left + uint2(0u, 1u), depth_level)).x;
    float depth_quad_d = depth_pyramid.Load(int3(aabb_top_left + uint2(1u, 1u), depth_level)).x;
    return min(min(depth_quad_a, depth_quad_b), min(depth_quad_c, depth_quad_d));
}

}
)slang";

/** @brief Slang representation of pinned Bevy 0.18 `morph.wgsl`.
 *
 * The morph weights and the target texture are the mesh layout's group-2
 * bindings 2, 3 and 7, which the module declares itself. The component offsets
 * are the hardcoded ones Bevy's `MorphAttributes` produces. */
constexpr std::string_view kMorphShader = R"slang(
module epix.morph;

import epix.mesh_types;

#ifdef MORPH_TARGETS

public [[vk::binding(2, 2)]] ConstantBuffer<epix::mesh_types::MorphWeights> morph_weights;
public [[vk::binding(3, 2)]] Texture3D<float4> morph_targets;
public [[vk::binding(7, 2)]] ConstantBuffer<epix::mesh_types::MorphWeights> prev_morph_weights;

public static const uint position_offset = 0u;
public static const uint normal_offset = 3u;
public static const uint tangent_offset = 6u;
public static const uint total_component_count = 9u;

public uint layer_count() {
    uint width, height, depth;
    morph_targets.GetDimensions(width, height, depth);
    return depth;
}

public uint2 component_texture_coord(uint vertex_index, uint component_offset) {
    uint width, height, depth;
    morph_targets.GetDimensions(width, height, depth);
    uint component_index = total_component_count * vertex_index + component_offset;
    return uint2(component_index % width, component_index / width);
}

public float weight_at(uint weight_index) {
    return morph_weights.weights[weight_index / 4u][weight_index % 4u];
}

public float prev_weight_at(uint weight_index) {
    return prev_morph_weights.weights[weight_index / 4u][weight_index % 4u];
}

public float morph_pixel(uint vertex, uint component, uint weight) {
    uint2 coord = component_texture_coord(vertex, component);
    // The texture stores an f32, but a load returns the vector with only its
    // first component set.
    return morph_targets.Load(int4(coord, weight, 0)).r;
}

public float3 morph(uint vertex_index, uint component_offset, uint weight_index) {
    return float3(morph_pixel(vertex_index, component_offset, weight_index),
                  morph_pixel(vertex_index, component_offset + 1u, weight_index),
                  morph_pixel(vertex_index, component_offset + 2u, weight_index));
}

#endif
)slang";

/** @brief Slang representation of pinned Bevy 0.18 `skinning.wgsl`.
 *
 * Slang matrices are row-major where WGSL's are column-major, so the port keeps
 * Bevy's matrices *transposed*: WGSL's `M[i]` (a column) is Slang's `M[i]` (a
 * row), a WGSL `mat3x3(c0, c1, c2)` is Slang's `float3x3` whose rows are those
 * columns, and every product flips sides - WGSL's `M * v` is Slang's
 * `mul(v, M)`. With that rule the two functions below are Bevy's bodies
 * unchanged, and the scalar-times-matrix accumulation is orientation-free. */
constexpr std::string_view kSkinningShader = R"slang(
module epix.skinning;

import epix.mesh_types;
import epix.mesh_bindings;

#ifdef SKINNED

#ifdef SKINS_USE_UNIFORM_BUFFERS
public [[vk::binding(1, 2)]] ConstantBuffer<epix::mesh_types::SkinnedMesh> joint_matrices;
#else
public [[vk::binding(1, 2)]] StructuredBuffer<float4x4> joint_matrices;
#endif

// The previous frame's joint matrices, for motion vectors; on the first frame
// they are the current ones.
#ifdef SKINS_USE_UNIFORM_BUFFERS
public [[vk::binding(6, 2)]] ConstantBuffer<epix::mesh_types::SkinnedMesh> prev_joint_matrices;
#else
public [[vk::binding(6, 2)]] StructuredBuffer<float4x4> prev_joint_matrices;
#endif

public float4x4 skin_model(uint4 indexes, float4 weights, uint instance_index) {
#ifdef SKINS_USE_UNIFORM_BUFFERS
    return weights.x * joint_matrices.data[indexes.x] + weights.y * joint_matrices.data[indexes.y] +
           weights.z * joint_matrices.data[indexes.z] + weights.w * joint_matrices.data[indexes.w];
#else
    uint skin_index = mesh[instance_index].current_skin_index;
    return weights.x * joint_matrices[skin_index + indexes.x] +
           weights.y * joint_matrices[skin_index + indexes.y] +
           weights.z * joint_matrices[skin_index + indexes.z] +
           weights.w * joint_matrices[skin_index + indexes.w];
#endif
}

public float4x4 skin_prev_model(uint4 indexes, float4 weights, uint instance_index) {
#ifdef SKINS_USE_UNIFORM_BUFFERS
    return weights.x * prev_joint_matrices.data[indexes.x] + weights.y * prev_joint_matrices.data[indexes.y] +
           weights.z * prev_joint_matrices.data[indexes.z] + weights.w * prev_joint_matrices.data[indexes.w];
#else
    uint skin_index = mesh[instance_index].current_skin_index;
    return weights.x * prev_joint_matrices[skin_index + indexes.x] +
           weights.y * prev_joint_matrices[skin_index + indexes.y] +
           weights.z * prev_joint_matrices[skin_index + indexes.z] +
           weights.w * prev_joint_matrices[skin_index + indexes.w];
#endif
}

public float3x3 inverse_transpose_3x3m(float3x3 input_matrix) {
    float3 x = cross(input_matrix[1], input_matrix[2]);
    float3 y = cross(input_matrix[2], input_matrix[0]);
    float3 z = cross(input_matrix[0], input_matrix[1]);
    float det = dot(input_matrix[2], z);
    return float3x3(x / det, y / det, z / det);
}

public float3 skin_normals(float4x4 world_from_local, float3 normal) {
    return normalize(mul(normal, inverse_transpose_3x3m(
                                       float3x3(world_from_local[0].xyz, world_from_local[1].xyz,
                                                world_from_local[2].xyz))));
}

#endif
)slang";

/** @brief Slang representation of pinned Bevy 0.18
 *  `prepass/prepass_bindings.wgsl`: the previous frame's view matrices.
 *
 * Unlike the other view bindings this one lives in the prepass pipeline's own
 * layout, where binding 2 is the previous view uniforms. */
constexpr std::string_view kPrepassBindingsShader = R"slang(
module epix.prepass_bindings;

namespace epix::prepass_bindings {

public struct PreviousViewUniforms {
    public float4x4 view_from_world;
    public float4x4 clip_from_world;
    public float4x4 clip_from_view;
    public float4x4 world_from_clip;
    public float4x4 view_from_clip;
};

}

// Material bindings are in group 2.
public [[vk::binding(2, 0)]] ConstantBuffer<epix::prepass_bindings::PreviousViewUniforms> previous_view_uniforms;
)slang";

/**
 * @brief Slang representation of pinned Bevy 0.18 `view_transformations.wgsl`.
 *
 * The view uniform and the previous-view uniforms come from the two modules
 * imported at the top; Slang's `import` brings their module-scope bindings into
 * scope, so they are spelled bare where Bevy qualifies them with
 * `view_bindings::`/`prepass_bindings::`.
 *
 * Every matrix product flips sides for the reason the skinning module states:
 * the port keeps Bevy's matrices transposed, so `M * v` is `mul(v, M)`.
 * Two-dimensional indexing is unchanged (`M[3][2]` is the same element either
 * way), and `#else ifdef X` becomes Slang's `#elif defined(X)`.
 */
constexpr std::string_view kViewTransformationsShader = R"slang(
module epix.view_transformations;

import epix.mesh_view_bindings;
import epix.prepass_bindings;

namespace epix::view_transformations {

// World space: +y is up. View space: -z is forward. NDC: (-1,-1) is bottom-left,
// z is 1 at the near plane and 0 at the far plane. UV: (0,0) is top-left.

public float3 position_view_to_world(float3 view_pos) {
    float4 world_pos = mul(float4(view_pos, 1.0), view.world_from_view);
    return world_pos.xyz;
}

public float3 position_clip_to_world(float4 clip_pos) {
    float4 world_pos = mul(clip_pos, view.world_from_clip);
    return world_pos.xyz;
}

public float3 position_ndc_to_world(float3 ndc_pos) {
    float4 world_pos = mul(float4(ndc_pos, 1.0), view.world_from_clip);
    return world_pos.xyz / world_pos.w;
}

public float3 direction_view_to_world(float3 view_dir) {
    float4 world_dir = mul(float4(view_dir, 0.0), view.world_from_view);
    return world_dir.xyz;
}

public float3 direction_clip_to_world(float4 clip_dir) {
    float4 world_dir = mul(clip_dir, view.world_from_clip);
    return world_dir.xyz;
}

public float3 position_world_to_view(float3 world_pos) {
    float4 view_pos = mul(float4(world_pos, 1.0), view.view_from_world);
    return view_pos.xyz;
}

public float3 position_clip_to_view(float4 clip_pos) {
    float4 view_pos = mul(clip_pos, view.view_from_clip);
    return view_pos.xyz;
}

public float3 position_ndc_to_view(float3 ndc_pos) {
    float4 view_pos = mul(float4(ndc_pos, 1.0), view.view_from_clip);
    return view_pos.xyz / view_pos.w;
}

public float3 direction_world_to_view(float3 world_dir) {
    float4 view_dir = mul(float4(world_dir, 0.0), view.view_from_world);
    return view_dir.xyz;
}

public float3 direction_clip_to_view(float4 clip_dir) {
    float4 view_dir = mul(clip_dir, view.view_from_clip);
    return view_dir.xyz;
}

public float3 position_world_to_prev_view(float3 world_pos) {
    float4 view_pos = mul(float4(world_pos, 1.0), previous_view_uniforms.view_from_world);
    return view_pos.xyz;
}

public float3 position_world_to_prev_ndc(float3 world_pos) {
    float4 ndc_pos = mul(float4(world_pos, 1.0), previous_view_uniforms.clip_from_world);
    return ndc_pos.xyz / ndc_pos.w;
}

public float4 position_world_to_clip(float3 world_pos) {
    return mul(float4(world_pos, 1.0), view.clip_from_world);
}

public float4 position_view_to_clip(float3 view_pos) {
    return mul(float4(view_pos, 1.0), view.clip_from_view);
}

public float4 direction_world_to_clip(float3 world_dir) {
    return mul(float4(world_dir, 0.0), view.clip_from_world);
}

public float4 direction_view_to_clip(float3 view_dir) {
    return mul(float4(view_dir, 0.0), view.clip_from_view);
}

public float3 position_world_to_ndc(float3 world_pos) {
    float4 ndc_pos = mul(float4(world_pos, 1.0), view.clip_from_world);
    return ndc_pos.xyz / ndc_pos.w;
}

public float3 position_view_to_ndc(float3 view_pos) {
    float4 ndc_pos = mul(float4(view_pos, 1.0), view.clip_from_view);
    return ndc_pos.xyz / ndc_pos.w;
}

// The perspective camera's near clipping plane.
public float perspective_camera_near() {
    return view.clip_from_view[3][2];
}

// Depth values in front of the camera are negative, because -z is forward.
public float depth_ndc_to_view_z(float ndc_depth) {
#if defined(VIEW_PROJECTION_PERSPECTIVE)
    return -perspective_camera_near() / ndc_depth;
#elif defined(VIEW_PROJECTION_ORTHOGRAPHIC)
    return -(view.clip_from_view[3][2] - ndc_depth) / view.clip_from_view[2][2];
#else
    float4 view_pos = mul(float4(0.0, 0.0, ndc_depth, 1.0), view.view_from_clip);
    return view_pos.z / view_pos.w;
#endif
}

public float view_z_to_depth_ndc(float view_z) {
#if defined(VIEW_PROJECTION_PERSPECTIVE)
    return -perspective_camera_near() / view_z;
#elif defined(VIEW_PROJECTION_ORTHOGRAPHIC)
    return view.clip_from_view[3][2] + view_z * view.clip_from_view[2][2];
#else
    float4 ndc_pos = mul(float4(0.0, 0.0, view_z, 1.0), view.clip_from_view);
    return ndc_pos.z / ndc_pos.w;
#endif
}

public float prev_view_z_to_depth_ndc(float view_z) {
#if defined(VIEW_PROJECTION_PERSPECTIVE)
    return -perspective_camera_near() / view_z;
#elif defined(VIEW_PROJECTION_ORTHOGRAPHIC)
    return previous_view_uniforms.clip_from_view[3][2] +
           view_z * previous_view_uniforms.clip_from_view[2][2];
#else
    float4 ndc_pos = mul(float4(0.0, 0.0, view_z, 1.0), previous_view_uniforms.clip_from_view);
    return ndc_pos.z / ndc_pos.w;
#endif
}

// NDC xy [-1, 1] to UV [0, 1] and back.
public float2 ndc_to_uv(float2 ndc) {
    return ndc * float2(0.5, -0.5) + float2(0.5, 0.5);
}

public float2 uv_to_ndc(float2 uv) {
    return uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
}

// The [0, 1] position within the viewport and back.
public float2 frag_coord_to_uv(float2 frag_coord) {
    return (frag_coord - view.viewport.xy) / view.viewport.zw;
}

public float3 frag_coord_to_ndc(float4 frag_coord) {
    return float3(uv_to_ndc(frag_coord_to_uv(frag_coord.xy)), frag_coord.z);
}

public float2 ndc_to_frag_coord(float2 ndc) {
    return ndc_to_uv(ndc) * view.viewport.zw;
}

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
    registry->get().insert_asset_static(MESH_VIEW_BINDINGS_SHADER_PATH, shader_bytes(kMeshViewBindingsShader));
    registry->get().insert_asset_static(OCCLUSION_CULLING_SHADER_PATH, shader_bytes(kOcclusionCullingShader));
    registry->get().insert_asset_static(MORPH_SHADER_PATH, shader_bytes(kMorphShader));
    registry->get().insert_asset_static(SKINNING_SHADER_PATH, shader_bytes(kSkinningShader));
    registry->get().insert_asset_static(PREPASS_BINDINGS_SHADER_PATH, shader_bytes(kPrepassBindingsShader));
    registry->get().insert_asset_static(VIEW_TRANSFORMATIONS_SHADER_PATH,
                                        shader_bytes(kViewTransformationsShader));
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
        .mesh_view_bindings = server->get().load<shader::Shader>(std::string("embedded://") +
                                                                 std::string(MESH_VIEW_BINDINGS_SHADER_PATH)),
        .occlusion_culling = server->get().load<shader::Shader>(std::string("embedded://") +
                                                                std::string(OCCLUSION_CULLING_SHADER_PATH)),
        .morph =
            server->get().load<shader::Shader>(std::string("embedded://") + std::string(MORPH_SHADER_PATH)),
        .skinning =
            server->get().load<shader::Shader>(std::string("embedded://") + std::string(SKINNING_SHADER_PATH)),
        .prepass_bindings = server->get().load<shader::Shader>(std::string("embedded://") +
                                                               std::string(PREPASS_BINDINGS_SHADER_PATH)),
        .view_transformations = server->get().load<shader::Shader>(std::string("embedded://") +
                                                                   std::string(VIEW_TRANSFORMATIONS_SHADER_PATH)),
    });
}

}  // namespace epix::pbr
