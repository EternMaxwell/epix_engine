#include <epix/pbr/prepass_shaders.hpp>

#include <span>
#include <string>

namespace epix::pbr {
namespace {
// Pinned prepass/prepass_io.wgsl: locations differ from forward_io.
constexpr std::string_view io_shader = R"slang(
module epix.prepass_io;
namespace epix::prepass_io {
public struct Vertex {
    public uint instance_index : SV_VulkanInstanceID;
    public [[vk::location(0)]] float3 position;
#ifdef VERTEX_UVS_A
    public [[vk::location(1)]] float2 uv;
#endif
#ifdef VERTEX_UVS_B
    public [[vk::location(2)]] float2 uv_b;
#endif
#ifdef NORMAL_PREPASS_OR_DEFERRED_PREPASS
#ifdef VERTEX_NORMALS
    public [[vk::location(3)]] float3 normal;
#endif
#ifdef VERTEX_TANGENTS
    public [[vk::location(4)]] float4 tangent;
#endif
#endif
#ifdef SKINNED
    public [[vk::location(5)]] uint4 joint_indices;
    public [[vk::location(6)]] float4 joint_weights;
#endif
#ifdef VERTEX_COLORS
    public [[vk::location(7)]] float4 color;
#endif
#ifdef MORPH_TARGETS
    public uint index : SV_VulkanVertexID;
#endif
};
public struct VertexOutput {
    public float4 position : SV_Position;
#ifdef VERTEX_UVS_A
    public [[vk::location(0)]] float2 uv;
#endif
#ifdef VERTEX_UVS_B
    public [[vk::location(1)]] float2 uv_b;
#endif
#ifdef NORMAL_PREPASS_OR_DEFERRED_PREPASS
    public [[vk::location(2)]] float3 world_normal;
#ifdef VERTEX_TANGENTS
    public [[vk::location(3)]] float4 world_tangent;
#endif
#endif
    public [[vk::location(4)]] float4 world_position;
#ifdef MOTION_VECTOR_PREPASS
    public [[vk::location(5)]] float4 previous_world_position;
#endif
#ifdef UNCLIPPED_DEPTH_ORTHO_EMULATION
    public [[vk::location(6)]] float unclipped_depth;
#endif
#ifdef VERTEX_OUTPUT_INSTANCE_INDEX
    public nointerpolation [[vk::location(7)]] uint instance_index;
#endif
#ifdef VERTEX_COLORS
    public [[vk::location(8)]] float4 color;
#endif
#ifdef VISIBILITY_RANGE_DITHER
    public nointerpolation [[vk::location(9)]] int visibility_range_dither;
#endif
};
#ifdef PREPASS_FRAGMENT
public struct FragmentOutput {
#ifdef NORMAL_PREPASS
    public [[vk::location(0)]] float4 normal;
#endif
#ifdef MOTION_VECTOR_PREPASS
    public [[vk::location(1)]] float2 motion_vector;
#endif
#ifdef DEFERRED_PREPASS
    public [[vk::location(2)]] uint4 deferred;
    public [[vk::location(3)]] uint deferred_lighting_pass_id;
#endif
#ifdef UNCLIPPED_DEPTH_ORTHO_EMULATION
    public float frag_depth : SV_Depth;
#endif
};
#endif
}
)slang";

// Pinned render/rgb9e5.wgsl, including the general decoder (not a magenta constant).
constexpr std::string_view rgb_shader = R"slang(
module epix.rgb9e5;
namespace epix::rgb9e5 {
public static const uint RGB9E5_EXPONENT_BITS = 5u;
public static const int RGB9E5_MANTISSA_BITS = 9;
public static const uint RGB9E5_MANTISSA_BITSU = 9u;
public static const int RGB9E5_EXP_BIAS = 15;
public static const uint RGB9E5_MAX_VALID_BIASED_EXP = 31u;
public static const uint MAX_RGB9E5_EXP = 16u;
public static const int RGB9E5_MANTISSA_VALUES = 512;
public static const int MAX_RGB9E5_MANTISSA = 511;
public static const uint MAX_RGB9E5_MANTISSAU = 511u;
public static const float MAX_RGB9E5_ = 65408.0;
public static const float EPSILON_RGB9E5_ = 0.000000059604645;
public int floor_log2_(float x) {
    uint f = asuint(x);
    uint biasedexponent = (f & 0x7f800000u) >> 23u;
    return int(biasedexponent) - 127;
}
public uint vec3_to_rgb9e5_(float3 rgb_in) {
    float3 rgb = clamp(rgb_in, float3(0.0), float3(MAX_RGB9E5_));
    float maxrgb = max(rgb.r, max(rgb.g, rgb.b));
    int exp_shared = max(-RGB9E5_EXP_BIAS - 1, floor_log2_(maxrgb)) + 1 + RGB9E5_EXP_BIAS;
    float denom = exp2(float(exp_shared - RGB9E5_EXP_BIAS - RGB9E5_MANTISSA_BITS));
    int maxm = int(floor(maxrgb / denom + 0.5));
    if (maxm == RGB9E5_MANTISSA_VALUES) {
        denom *= 2.0;
        exp_shared += 1;
    }
    uint3 n = uint3(floor(rgb / denom + 0.5));
    return (uint(exp_shared) << 27u) | (n.b << 18u) | (n.g << 9u) | n.r;
}
public uint extract_bits(uint value, uint offset, uint bits) {
    uint mask = (1u << bits) - 1u;
    return (value >> offset) & mask;
}
public float3 rgb9e5_to_vec3_(uint v) {
    int exponent = int(extract_bits(v, 27u, RGB9E5_EXPONENT_BITS)) - RGB9E5_EXP_BIAS - RGB9E5_MANTISSA_BITS;
    float scale = exp2(float(exponent));
    return float3(float(extract_bits(v, 0u, RGB9E5_MANTISSA_BITSU)),
                  float(extract_bits(v, 9u, RGB9E5_MANTISSA_BITSU)),
                  float(extract_bits(v, 18u, RGB9E5_MANTISSA_BITSU))) * scale;
}
}
)slang";

constexpr std::string_view utils_shader = R"slang(
module epix.prepass_utils;
import epix.mesh_view_bindings;
namespace epix::prepass_utils {
#ifdef DEPTH_PREPASS
public float prepass_depth(float4 frag_coord, uint sample_index) {
#ifdef MULTISAMPLED
    return depth_prepass_texture.Load(int2(frag_coord.xy), int(sample_index));
#else
    return depth_prepass_texture.Load(int3(int2(frag_coord.xy), 0));
#endif
}
#endif
#ifdef NORMAL_PREPASS
public float3 prepass_normal(float4 frag_coord, uint sample_index) {
#ifdef MULTISAMPLED
    float4 normal_sample = normal_prepass_texture.Load(int2(frag_coord.xy), int(sample_index));
#else
    float4 normal_sample = normal_prepass_texture.Load(int3(int2(frag_coord.xy), 0));
#endif
    return normalize(normal_sample.xyz * 2.0 - float3(1.0));
}
#endif
#ifdef MOTION_VECTOR_PREPASS
public float2 prepass_motion_vector(float4 frag_coord, uint sample_index) {
#ifdef MULTISAMPLED
    float4 motion_vector_sample = motion_vector_prepass_texture.Load(int2(frag_coord.xy), int(sample_index));
#else
    float4 motion_vector_sample = motion_vector_prepass_texture.Load(int3(int2(frag_coord.xy), 0));
#endif
    return motion_vector_sample.rg;
}
#endif
}
)slang";

constexpr std::string_view prepass_shader = R"slang(
import epix.prepass_bindings;
import epix.mesh_bindings;
import epix.mesh_functions;
import epix.prepass_io;
import epix.skinning;
import epix.morph;
import epix.mesh_view_bindings;
import epix.view_transformations;
#ifdef DEFERRED_PREPASS
import epix.rgb9e5;
#endif

#ifdef MORPH_TARGETS
epix::prepass_io::Vertex morph_vertex(epix::prepass_io::Vertex vertex_in) {
    epix::prepass_io::Vertex v = vertex_in;
    uint first_vertex = mesh_instance(v.instance_index).first_vertex_index;
    uint vertex_index = v.index - first_vertex;
    uint weight_count = layer_count();
    for (uint i = 0u; i < weight_count; ++i) {
        float weight = weight_at(i);
        if (weight == 0.0) continue;
        v.position += weight * morph(vertex_index, position_offset, i);
#ifdef VERTEX_NORMALS
        v.normal += weight * morph(vertex_index, normal_offset, i);
#endif
#ifdef VERTEX_TANGENTS
        v.tangent += float4(weight * morph(vertex_index, tangent_offset, i), 0.0);
#endif
    }
    return v;
}
epix::prepass_io::Vertex morph_prev_vertex(epix::prepass_io::Vertex vertex_in) {
    epix::prepass_io::Vertex v = vertex_in;
    uint weight_count = layer_count();
    for (uint i = 0u; i < weight_count; ++i) {
        float weight = prev_weight_at(i);
        if (weight == 0.0) continue;
        // Pinned source intentionally uses raw index here, unlike current morph.
        v.position += weight * morph(v.index, position_offset, i);
    }
    return v;
}
#endif

[shader("vertex")]
epix::prepass_io::VertexOutput vertex(epix::prepass_io::Vertex vertex_no_morph) {
    epix::prepass_io::VertexOutput out = {};
#ifdef MORPH_TARGETS
    epix::prepass_io::Vertex v = morph_vertex(vertex_no_morph);
#else
    epix::prepass_io::Vertex v = vertex_no_morph;
#endif
    float4x4 mesh_world_from_local = epix::mesh_functions::get_world_from_local(vertex_no_morph.instance_index);
#ifdef SKINNED
    float4x4 world_from_local = skin_model(v.joint_indices, v.joint_weights, vertex_no_morph.instance_index);
#else
    float4x4 world_from_local = mesh_world_from_local;
#endif
    out.world_position = epix::mesh_functions::mesh_position_local_to_world(world_from_local, float4(v.position, 1.0));
    out.position = epix::view_transformations::position_world_to_clip(out.world_position.xyz);
#ifdef UNCLIPPED_DEPTH_ORTHO_EMULATION
    out.unclipped_depth = out.position.z;
    out.position.z = min(out.position.z, 1.0);
#endif
#ifdef VERTEX_UVS_A
    out.uv = v.uv;
#endif
#ifdef VERTEX_UVS_B
    out.uv_b = v.uv_b;
#endif
#ifdef NORMAL_PREPASS_OR_DEFERRED_PREPASS
#ifdef VERTEX_NORMALS
#ifdef SKINNED
    out.world_normal = skin_normals(world_from_local, v.normal);
#else
    out.world_normal = epix::mesh_functions::mesh_normal_local_to_world(v.normal, vertex_no_morph.instance_index);
#endif
#endif
#ifdef VERTEX_TANGENTS
    out.world_tangent = epix::mesh_functions::mesh_tangent_local_to_world(world_from_local, v.tangent,
                                                                       vertex_no_morph.instance_index);
#endif
#endif
#ifdef VERTEX_COLORS
    out.color = v.color;
#endif
#ifdef MOTION_VECTOR_PREPASS
#if defined(MORPH_TARGETS) && defined(HAS_PREVIOUS_MORPH)
    epix::prepass_io::Vertex prev_vertex = morph_prev_vertex(vertex_no_morph);
#else
    epix::prepass_io::Vertex prev_vertex = vertex_no_morph;
#endif
#if defined(SKINNED) && defined(HAS_PREVIOUS_SKIN)
    float4x4 prev_model = skin_prev_model(prev_vertex.joint_indices, prev_vertex.joint_weights,
                                         vertex_no_morph.instance_index);
#else
    float4x4 prev_model = epix::mesh_functions::get_previous_world_from_local(prev_vertex.instance_index);
#endif
    out.previous_world_position = epix::mesh_functions::mesh_position_local_to_world(prev_model,
                                                                                   float4(prev_vertex.position, 1.0));
#endif
#ifdef VERTEX_OUTPUT_INSTANCE_INDEX
    out.instance_index = vertex_no_morph.instance_index;
#endif
#ifdef VISIBILITY_RANGE_DITHER
    out.visibility_range_dither = epix::mesh_functions::get_visibility_range_dither_level(
        vertex_no_morph.instance_index, transpose(mesh_world_from_local)[3]);
#endif
    return out;
}

#ifdef PREPASS_FRAGMENT
[shader("fragment")]
epix::prepass_io::FragmentOutput fragment(epix::prepass_io::VertexOutput input) {
    epix::prepass_io::FragmentOutput out = {};
#ifdef NORMAL_PREPASS
    out.normal = float4(input.world_normal * 0.5 + float3(0.5), 1.0);
#endif
#ifdef UNCLIPPED_DEPTH_ORTHO_EMULATION
    out.frag_depth = input.unclipped_depth;
#endif
#ifdef MOTION_VECTOR_PREPASS
    float4 clip_position_t = mul(view.unjittered_clip_from_world, input.world_position);
    float2 clip_position = clip_position_t.xy / clip_position_t.w;
    float4 previous_clip_position_t = mul(previous_view_uniforms.clip_from_world,
                                          input.previous_world_position);
    float2 previous_clip_position = previous_clip_position_t.xy / previous_clip_position_t.w;
    out.motion_vector = (clip_position - previous_clip_position) * float2(0.5, -0.5);
#endif
#ifdef DEFERRED_PREPASS
    out.deferred = uint4(0u, epix::rgb9e5::vec3_to_rgb9e5_(float3(1.0, 0.0, 1.0)), 0u, 0u);
    out.deferred_lighting_pass_id = 1u;
#endif
    return out;
}
#endif
)slang";

std::span<const std::byte> bytes(std::string_view source) {
    return {reinterpret_cast<const std::byte*>(source.data()), source.size()};
}
}  // namespace

void register_prepass_shaders(ecs::World& world) {
    auto registry = world.get_resource_mut<assets::EmbeddedAssetRegistry>();
    auto server = world.get_resource<assets::AssetServer>();
    if (!registry || !server) return;
    registry->get().insert_asset_static(PREPASS_IO_SHADER_PATH, bytes(io_shader));
    registry->get().insert_asset_static(PREPASS_UTILS_SHADER_PATH, bytes(utils_shader));
    registry->get().insert_asset_static(RGB9E5_SHADER_PATH, bytes(rgb_shader));
    registry->get().insert_asset_static(PREPASS_SHADER_PATH, bytes(prepass_shader));
    const auto load = [&](std::string_view path) {
        return server->get().load<shader::Shader>(std::string("embedded://") + std::string(path));
    };
    world.insert_resource(PrepassShaderHandles{load(PREPASS_IO_SHADER_PATH), load(PREPASS_UTILS_SHADER_PATH),
                                             load(RGB9E5_SHADER_PATH), load(PREPASS_SHADER_PATH)});
}
}  // namespace epix::pbr
