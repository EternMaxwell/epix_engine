#include <epix/core_graph/mip_generation.hpp>

#ifndef EPIX_CXX_MODULE
#include <spdlog/spdlog.h>

#include <algorithm>
#include <bit>
#include <format>
#endif

using namespace epix::ecs;

namespace epix::core_graph::mip_generation {
namespace {
constexpr std::string_view kDownsampleDepthPath  = "core_pipeline/downsample_depth.slang";
constexpr std::string_view kDownsampleDepthSlang = R"slang(
// Bevy's `downsample_depth.wgsl` (FidelityFX SPD v2.1 single-pass
// downsampling). The meshlet paths of the original shader have no counterpart
// in Epix and are omitted. Two Slang specifics are required by the bindings
// (see the tests and gfx-rs/wgpu#7332): the depth buffer uses the built-in
// `DepthTexture2D`/`DepthTexture2DMS` types, which is what makes the SPIR-V
// backend emit a depth image, and every storage image carries an explicit
// `[[vk::image_format("r32f")]]`.
struct Constants { uint max_mip_level; };
[[vk::push_constant]] ConstantBuffer<Constants> constants;

#ifdef MULTISAMPLE
// `DepthTexture2DMS` takes only the storage format: like WGSL's
// `texture_depth_multisampled_2d`, the sample count is not part of the type and
// is read back with `GetDimensions`.
[[vk::binding(0, 0)]] DepthTexture2DMS<0> mip_0;
#else
[[vk::binding(0, 0)]] DepthTexture2D mip_0;
#endif
[[vk::binding(1, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> mip_1;
[[vk::binding(2, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> mip_2;
[[vk::binding(3, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> mip_3;
[[vk::binding(4, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> mip_4;
[[vk::binding(5, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> mip_5;
[[vk::binding(6, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> mip_6;
[[vk::binding(7, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> mip_7;
[[vk::binding(8, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> mip_8;
[[vk::binding(9, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> mip_9;
[[vk::binding(10, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> mip_10;
[[vk::binding(11, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> mip_11;
[[vk::binding(12, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> mip_12;
[[vk::binding(13, 0)]] SamplerState samplr;

groupshared float intermediate_memory[16][16];

uint extract_bits(uint value, uint offset, uint count) { return (value >> offset) & ((1u << count) - 1u); }

uint insert_bits(uint base, uint inserted, uint offset, uint count) {
    const uint mask = ((1u << count) - 1u) << offset;
    return (base & ~mask) | ((inserted << offset) & mask);
}

uint2 remap_for_wave_reduction(uint a) {
    return uint2(insert_bits(extract_bits(a, 2u, 3u), a, 0u, 1u),
                 insert_bits(extract_bits(a, 3u, 3u), extract_bits(a, 1u, 2u), 0u, 2u));
}

float reduce_4(float4 v) { return min(min(v.x, v.y), min(v.z, v.w)); }

float load_mip_0(uint x, uint y) {
#ifdef MULTISAMPLE
    // The top level is multisampled, so every sample is reduced to one.
    float result = mip_0.Load(int2(int(x), int(y)), 0);
    uint width, height, samples;
    mip_0.GetDimensions(width, height, samples);
    for (uint sample = 1u; sample < samples; ++sample) {
        result = min(result, mip_0.Load(int2(int(x), int(y)), int(sample)));
    }
    return result;
#else
    return mip_0.Load(int3(int(x), int(y), 0));
#endif
}

float reduce_load_mip_0(uint2 tex) {
    return reduce_4(float4(load_mip_0(tex.x, tex.y), load_mip_0(tex.x + 1u, tex.y),
                           load_mip_0(tex.x, tex.y + 1u), load_mip_0(tex.x + 1u, tex.y + 1u)));
}

float reduce_load_mip_6(uint2 tex) {
    return reduce_4(float4(mip_6.Load(int2(int(tex.x), int(tex.y))),
                           mip_6.Load(int2(int(tex.x), int(tex.y + 1u))),
                           mip_6.Load(int2(int(tex.x + 1u), int(tex.y))),
                           mip_6.Load(int2(int(tex.x + 1u), int(tex.y + 1u)))));
}

void downsample_mip_2(uint x, uint y, uint2 workgroup_id, uint local_invocation_index) {
    if (local_invocation_index < 64u) {
        const float v = reduce_4(float4(intermediate_memory[x * 2u + 0u][y * 2u + 0u],
                                        intermediate_memory[x * 2u + 1u][y * 2u + 0u],
                                        intermediate_memory[x * 2u + 0u][y * 2u + 1u],
                                        intermediate_memory[x * 2u + 1u][y * 2u + 1u]));
        mip_3[workgroup_id * 8u + uint2(x, y)] = v;
        intermediate_memory[x * 2u + y % 2u][y * 2u] = v;
    }
}

void downsample_mip_3(uint x, uint y, uint2 workgroup_id, uint local_invocation_index) {
    if (local_invocation_index < 16u) {
        const float v = reduce_4(float4(intermediate_memory[x * 4u + 0u + 0u][y * 4u + 0u],
                                        intermediate_memory[x * 4u + 2u + 0u][y * 4u + 0u],
                                        intermediate_memory[x * 4u + 0u + 1u][y * 4u + 2u],
                                        intermediate_memory[x * 4u + 2u + 1u][y * 4u + 2u]));
        mip_4[workgroup_id * 4u + uint2(x, y)] = v;
        intermediate_memory[x * 4u + y][y * 4u] = v;
    }
}

void downsample_mip_4(uint x, uint y, uint2 workgroup_id, uint local_invocation_index) {
    if (local_invocation_index < 4u) {
        const float v = reduce_4(float4(intermediate_memory[x * 8u + 0u + 0u + y * 2u][y * 8u + 0u],
                                        intermediate_memory[x * 8u + 4u + 0u + y * 2u][y * 8u + 0u],
                                        intermediate_memory[x * 8u + 0u + 1u + y * 2u][y * 8u + 4u],
                                        intermediate_memory[x * 8u + 4u + 1u + y * 2u][y * 8u + 4u]));
        mip_5[workgroup_id * 2u + uint2(x, y)] = v;
        intermediate_memory[x + y * 2u][0u] = v;
    }
}

void downsample_mip_5(uint2 workgroup_id, uint local_invocation_index) {
    if (local_invocation_index < 1u) {
        const float v = reduce_4(float4(intermediate_memory[0u][0u], intermediate_memory[1u][0u],
                                        intermediate_memory[2u][0u], intermediate_memory[3u][0u]));
        mip_6[workgroup_id] = v;
    }
}

void downsample_mips_0_and_1(uint x, uint y, uint2 workgroup_id, uint local_invocation_index) {
    float4 v;

    uint2 tex = workgroup_id * 64u + uint2(x * 2u, y * 2u);
    uint2 pix = workgroup_id * 32u + uint2(x, y);
    v[0] = reduce_load_mip_0(tex);
    mip_1[pix] = v[0];

    tex = workgroup_id * 64u + uint2(x * 2u + 32u, y * 2u);
    pix = workgroup_id * 32u + uint2(x + 16u, y);
    v[1] = reduce_load_mip_0(tex);
    mip_1[pix] = v[1];

    tex = workgroup_id * 64u + uint2(x * 2u, y * 2u + 32u);
    pix = workgroup_id * 32u + uint2(x, y + 16u);
    v[2] = reduce_load_mip_0(tex);
    mip_1[pix] = v[2];

    tex = workgroup_id * 64u + uint2(x * 2u + 32u, y * 2u + 32u);
    pix = workgroup_id * 32u + uint2(x + 16u, y + 16u);
    v[3] = reduce_load_mip_0(tex);
    mip_1[pix] = v[3];

    if (constants.max_mip_level <= 1u) return;

    for (uint i = 0u; i < 4u; ++i) {
        intermediate_memory[x][y] = v[i];
        GroupMemoryBarrierWithGroupSync();
        if (local_invocation_index < 64u) {
            v[i] = reduce_4(float4(intermediate_memory[x * 2u + 0u][y * 2u + 0u],
                                   intermediate_memory[x * 2u + 1u][y * 2u + 0u],
                                   intermediate_memory[x * 2u + 0u][y * 2u + 1u],
                                   intermediate_memory[x * 2u + 1u][y * 2u + 1u]));
            pix = workgroup_id * 16u + uint2(x + (i % 2u) * 8u, y + (i / 2u) * 8u);
            mip_2[pix] = v[i];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (local_invocation_index < 64u) {
        intermediate_memory[x + 0u][y + 0u] = v[0];
        intermediate_memory[x + 8u][y + 0u] = v[1];
        intermediate_memory[x + 0u][y + 8u] = v[2];
        intermediate_memory[x + 8u][y + 8u] = v[3];
    }
}

void downsample_mips_2_to_5(uint x, uint y, uint2 workgroup_id, uint local_invocation_index) {
    if (constants.max_mip_level <= 2u) return;
    GroupMemoryBarrierWithGroupSync();
    downsample_mip_2(x, y, workgroup_id, local_invocation_index);

    if (constants.max_mip_level <= 3u) return;
    GroupMemoryBarrierWithGroupSync();
    downsample_mip_3(x, y, workgroup_id, local_invocation_index);

    if (constants.max_mip_level <= 4u) return;
    GroupMemoryBarrierWithGroupSync();
    downsample_mip_4(x, y, workgroup_id, local_invocation_index);

    if (constants.max_mip_level <= 5u) return;
    GroupMemoryBarrierWithGroupSync();
    downsample_mip_5(workgroup_id, local_invocation_index);
}

void downsample_mips_6_and_7(uint x, uint y) {
    float4 v;

    uint2 tex = uint2(x * 4u + 0u, y * 4u + 0u);
    uint2 pix = uint2(x * 2u + 0u, y * 2u + 0u);
    v[0] = reduce_load_mip_6(tex);
    mip_7[pix] = v[0];

    tex = uint2(x * 4u + 2u, y * 4u + 0u);
    pix = uint2(x * 2u + 1u, y * 2u + 0u);
    v[1] = reduce_load_mip_6(tex);
    mip_7[pix] = v[1];

    tex = uint2(x * 4u + 0u, y * 4u + 2u);
    pix = uint2(x * 2u + 0u, y * 2u + 1u);
    v[2] = reduce_load_mip_6(tex);
    mip_7[pix] = v[2];

    tex = uint2(x * 4u + 2u, y * 4u + 2u);
    pix = uint2(x * 2u + 1u, y * 2u + 1u);
    v[3] = reduce_load_mip_6(tex);
    mip_7[pix] = v[3];

    if (constants.max_mip_level <= 7u) return;

    const float vr = reduce_4(v);
    mip_8[uint2(x, y)] = vr;
    intermediate_memory[x][y] = vr;
}

void downsample_mip_8(uint x, uint y, uint local_invocation_index) {
    if (local_invocation_index < 64u) {
        const float v = reduce_4(float4(intermediate_memory[x * 2u + 0u][y * 2u + 0u],
                                        intermediate_memory[x * 2u + 1u][y * 2u + 0u],
                                        intermediate_memory[x * 2u + 0u][y * 2u + 1u],
                                        intermediate_memory[x * 2u + 1u][y * 2u + 1u]));
        mip_9[uint2(x, y)] = v;
        intermediate_memory[x * 2u + y % 2u][y * 2u] = v;
    }
}

void downsample_mip_9(uint x, uint y, uint local_invocation_index) {
    if (local_invocation_index < 16u) {
        const float v = reduce_4(float4(intermediate_memory[x * 4u + 0u + 0u][y * 4u + 0u],
                                        intermediate_memory[x * 4u + 2u + 0u][y * 4u + 0u],
                                        intermediate_memory[x * 4u + 0u + 1u][y * 4u + 2u],
                                        intermediate_memory[x * 4u + 2u + 1u][y * 4u + 2u]));
        mip_10[uint2(x, y)] = v;
        intermediate_memory[x * 4u + y][y * 4u] = v;
    }
}

void downsample_mip_10(uint x, uint y, uint local_invocation_index) {
    if (local_invocation_index < 4u) {
        const float v = reduce_4(float4(intermediate_memory[x * 8u + 0u + 0u + y * 2u][y * 8u + 0u],
                                        intermediate_memory[x * 8u + 4u + 0u + y * 2u][y * 8u + 0u],
                                        intermediate_memory[x * 8u + 0u + 1u + y * 2u][y * 8u + 4u],
                                        intermediate_memory[x * 8u + 4u + 1u + y * 2u][y * 8u + 4u]));
        mip_11[uint2(x, y)] = v;
        intermediate_memory[x + y * 2u][0u] = v;
    }
}

void downsample_mip_11(uint local_invocation_index) {
    if (local_invocation_index < 1u) {
        const float v = reduce_4(float4(intermediate_memory[0u][0u], intermediate_memory[1u][0u],
                                        intermediate_memory[2u][0u], intermediate_memory[3u][0u]));
        mip_12[uint2(0u, 0u)] = v;
    }
}

void downsample_mips_8_to_11(uint x, uint y, uint local_invocation_index) {
    if (constants.max_mip_level <= 8u) return;
    GroupMemoryBarrierWithGroupSync();
    downsample_mip_8(x, y, local_invocation_index);

    if (constants.max_mip_level <= 9u) return;
    GroupMemoryBarrierWithGroupSync();
    downsample_mip_9(x, y, local_invocation_index);

    if (constants.max_mip_level <= 10u) return;
    GroupMemoryBarrierWithGroupSync();
    downsample_mip_10(x, y, local_invocation_index);

    if (constants.max_mip_level <= 11u) return;
    GroupMemoryBarrierWithGroupSync();
    downsample_mip_11(local_invocation_index);
}

[shader("compute")]
[numthreads(256, 1, 1)]
void downsample_depth_first(uint3 workgroup_id : SV_GroupID, uint local_invocation_index : SV_GroupIndex) {
    const uint2 sub_xy = remap_for_wave_reduction(local_invocation_index % 64u);
    const uint x = sub_xy.x + 8u * ((local_invocation_index >> 6u) % 2u);
    const uint y = sub_xy.y + 8u * (local_invocation_index >> 7u);

    downsample_mips_0_and_1(x, y, uint2(workgroup_id.xy), local_invocation_index);
    downsample_mips_2_to_5(x, y, uint2(workgroup_id.xy), local_invocation_index);
}

[shader("compute")]
[numthreads(256, 1, 1)]
void downsample_depth_second(uint local_invocation_index : SV_GroupIndex) {
    const uint2 sub_xy = remap_for_wave_reduction(local_invocation_index % 64u);
    const uint x = sub_xy.x + 8u * ((local_invocation_index >> 6u) % 2u);
    const uint y = sub_xy.y + 8u * (local_invocation_index >> 7u);

    downsample_mips_6_and_7(x, y);
    downsample_mips_8_to_11(x, y, local_invocation_index);
}
)slang";
}  // namespace

glm::uvec2 depth_pyramid_size(glm::uvec2 size) noexcept {
    // Bevy `ViewDepthPyramid::new` (`experimental/mip_generation/mod.rs:590-594`):
    // the pyramid is half the depth buffer, rounded up, so that every source
    // texel is covered by one level-0 pyramid texel.
    return glm::uvec2((size.x + 1u) / 2u, (size.y + 1u) / 2u);
}

std::uint32_t max_mips_for_size(glm::uvec2 size) noexcept {
    // Bevy `Extent3d::max_mips(TextureDimension::D2)`: the base-2 logarithm of
    // the greatest dimension, rounded up, and never less than one.
    const std::uint32_t greatest = std::max(size.x, size.y);
    return std::max(1u, 32u - static_cast<std::uint32_t>(std::countl_zero(greatest)));
}

std::uint32_t depth_pyramid_mip_count(glm::uvec2 size) noexcept {
    // Bevy `ViewDepthPyramid::new` (`:597`): `depth_pyramid_size.max_mips(D2)`.
    return max_mips_for_size(depth_pyramid_size(size));
}

render::BindGroupLayoutDescriptor create_downsample_depth_bind_group_layout(bool multisampled) {
    // Bevy `create_downsample_depth_bind_group_layout` (`:420-455`). Bevy's
    // storage entries are write-only except binding 6, which the second phase
    // reads while writing mip 7; Slang and HLSL have no write-only
    // storage-texture declaration, so every storage entry is read-write here.
    std::vector<wgpu::BindGroupLayoutEntry> entries;
    entries.reserve(DOWNSAMPLE_DEPTH_BIND_GROUP_ENTRIES);

    entries.push_back(wgpu::BindGroupLayoutEntry()
                          .setBinding(0)
                          .setVisibility(wgpu::ShaderStage::eCompute)
                          .setTexture(wgpu::TextureBindingLayout()
                                          .setSampleType(wgpu::TextureSampleType::eDepth)
                                          .setViewDimension(wgpu::TextureViewDimension::e2D)
                                          .setMultisampled(multisampled)));
    for (std::size_t level = 0; level < DEPTH_PYRAMID_MIP_COUNT; ++level) {
        entries.push_back(wgpu::BindGroupLayoutEntry()
                              .setBinding(static_cast<std::uint32_t>(level + 1))
                              .setVisibility(wgpu::ShaderStage::eCompute)
                              .setStorageTexture(wgpu::StorageTextureBindingLayout()
                                                     .setAccess(wgpu::StorageTextureAccess::eReadWrite)
                                                     .setFormat(wgpu::TextureFormat::eR32Float)
                                                     .setViewDimension(wgpu::TextureViewDimension::e2D)));
    }
    entries.push_back(wgpu::BindGroupLayoutEntry()
                          .setBinding(static_cast<std::uint32_t>(DEPTH_PYRAMID_MIP_COUNT + 1))
                          .setVisibility(wgpu::ShaderStage::eCompute)
                          .setSampler(wgpu::SamplerBindingLayout().setType(wgpu::SamplerBindingType::eNonFiltering)));

    return render::BindGroupLayoutDescriptor()
        .set_label(multisampled ? "downsample multisample depth bind group layout"
                                : "downsample depth bind group layout")
        .set_entries(entries);
}

render::ComputePipelineDescriptor DownsampleDepthPipeline::specialize(Key key) const {
    // Bevy `SpecializedComputePipeline for DownsampleDepthPipeline` (`:473-513`).
    const bool multisampled = key.contains(DownsampleDepthPipelineKey::MULTISAMPLE);
    const bool second_phase = key.contains(DownsampleDepthPipelineKey::SECOND_PHASE);

    std::vector<shader::ShaderDefVal> shader_defs;
    if (multisampled) {
        shader_defs.emplace_back("MULTISAMPLE");
    }

    return render::ComputePipelineDescriptor{
        .label                = std::format("downsample depth{}{} pipeline", multisampled ? " multisample" : "",
                                            second_phase ? " second phase" : " first phase"),
        .layouts              = std::vector<render::BindGroupLayoutDescriptor>{bind_group_layout},
        .push_constant_ranges = std::vector<render::PushConstantRange>{render::PushConstantRange{
            .stages = wgpu::ShaderStage::eCompute, .start = 0, .end = 4}},
        .shader               = shader,
        .shader_defs          = std::move(shader_defs),
        .entry_point          = std::string(second_phase ? "downsample_depth_second" : "downsample_depth_first"),
    };
}

wgpu::TextureView create_depth_pyramid_dummy_texture(const wgpu::Device& device,
                                                     const char* texture_label,
                                                     const char* texture_view_label) {
    // Bevy `create_depth_pyramid_dummy_texture` (`:533-560`): a one-texel
    // R32Float storage texture that fills the pyramid bindings no mip uses.
    const wgpu::Texture texture = device.createTexture(wgpu::TextureDescriptor()
                                                           .setLabel(texture_label)
                                                           .setSize({1, 1, 1})
                                                           .setMipLevelCount(1)
                                                           .setSampleCount(1)
                                                           .setDimension(wgpu::TextureDimension::e2D)
                                                           .setFormat(wgpu::TextureFormat::eR32Float)
                                                           .setUsage(wgpu::TextureUsage::eStorageBinding));
    return texture.createView(wgpu::TextureViewDescriptor()
                                  .setLabel(texture_view_label)
                                  .setFormat(wgpu::TextureFormat::eR32Float)
                                  .setDimension(wgpu::TextureViewDimension::e2D)
                                  .setBaseMipLevel(0)
                                  .setMipLevelCount(1)
                                  .setBaseArrayLayer(0)
                                  .setArrayLayerCount(1));
}

void create_downsample_depth_pipelines(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> render_device,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::ResMut<render::SpecializedComputePipelines<DownsampleDepthPipeline>> specialized_compute_pipelines,
    ecs::Res<render::batching::GpuPreprocessingSupport> gpu_preprocessing_support,
    ecs::Res<DownsampleDepthShader> downsample_depth_shader,
    ecs::Local<bool> has_run) {
    // Bevy `create_downsample_depth_pipelines` (`mip_generation/mod.rs:334-417`).
    // Only run once; the platform check cannot be a run condition because a
    // device without compute simply never produces the resource.
    if (has_run.get()) return;
    has_run.get() = true;

    if (!gpu_preprocessing_support->is_culling_supported()) {
        spdlog::debug("[core_graph.mip_generation] Downsample depth is not supported on this platform.");
        return;
    }

    // The two layouts differ only in the multisample status of the first mip.
    const auto standard_layout     = create_downsample_depth_bind_group_layout(false);
    const auto multisampled_layout = create_downsample_depth_bind_group_layout(true);

    const wgpu::Sampler sampler =
        render_device->createSampler(wgpu::SamplerDescriptor().setLabel("depth pyramid sampler"));

    const auto make_pipeline = [&](const render::BindGroupLayoutDescriptor& layout) {
        return DownsampleDepthPipeline{
            .bind_group_layout = layout,
            .pipeline_id       = std::nullopt,
            .shader            = downsample_depth_shader->handle,
        };
    };

    DownsampleDepthPipelines pipelines{
        .first              = make_pipeline(standard_layout),
        .second             = make_pipeline(standard_layout),
        .first_multisample  = make_pipeline(multisampled_layout),
        .second_multisample = make_pipeline(multisampled_layout),
        .sampler            = sampler,
    };

    pipelines.first.pipeline_id =
        specialized_compute_pipelines->specialize(*pipeline_server, pipelines.first, DownsampleDepthPipelineKey{});
    pipelines.second.pipeline_id = specialized_compute_pipelines->specialize(
        *pipeline_server, pipelines.second, DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::SECOND_PHASE});
    pipelines.first_multisample.pipeline_id =
        specialized_compute_pipelines->specialize(*pipeline_server, pipelines.first_multisample,
                                                  DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::MULTISAMPLE});
    pipelines.second_multisample.pipeline_id = specialized_compute_pipelines->specialize(
        *pipeline_server, pipelines.second_multisample,
        DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::SECOND_PHASE | DownsampleDepthPipelineKey::MULTISAMPLE});

    commands.insert_resource(std::move(pipelines));
}

void init_depth_pyramid_dummy_texture(ecs::Commands commands, ecs::Res<wgpu::Device> render_device) {
    // Bevy `init_depth_pyramid_dummy_texture` (`mip_generation/mod.rs:521-529`).
    commands.insert_resource(DepthPyramidDummyTexture{create_depth_pyramid_dummy_texture(
        *render_device, "depth pyramid dummy texture", "depth pyramid dummy texture view")});
}

ViewDepthPyramid ViewDepthPyramid::create(const wgpu::Device& device,
                                          render::render_resource::TextureCache& texture_cache,
                                          const wgpu::TextureView& dummy_texture,
                                          glm::uvec2 size,
                                          const char* texture_label,
                                          const char* texture_view_label) {
    // Bevy `ViewDepthPyramid::new` (`:581-641`).
    const glm::uvec2 pyramid_size = depth_pyramid_size(size);
    const std::uint32_t mip_count = max_mips_for_size(pyramid_size);
    const auto usage              = wgpu::TextureUsage::eStorageBinding | wgpu::TextureUsage::eTextureBinding;

    // Bevy keys its texture cache on the descriptor; Epix's key mirrors the
    // fields the descriptor sets, so both are built from the same values.
    const render::render_resource::TextureCacheKey key{
        .format                = wgpu::TextureFormat::eR32Float,
        .dimension             = wgpu::TextureDimension::e2D,
        .width                 = pyramid_size.x,
        .height                = pyramid_size.y,
        .depth_or_array_layers = 1,
        .mip_level_count       = mip_count,
        .sample_count          = 1,
        .usage                 = usage,
        .label                 = std::string(texture_label),
    };
    const auto cached = texture_cache.get(device, key,
                                          wgpu::TextureDescriptor()
                                              .setLabel(texture_label)
                                              .setSize({pyramid_size.x, pyramid_size.y, 1})
                                              .setMipLevelCount(mip_count)
                                              .setSampleCount(1)
                                              .setDimension(wgpu::TextureDimension::e2D)
                                              .setFormat(wgpu::TextureFormat::eR32Float)
                                              .setUsage(usage));

    ViewDepthPyramid pyramid;
    pyramid.all_mips  = cached.default_view;
    pyramid.mip_count = mip_count;
    for (std::size_t level = 0; level < DEPTH_PYRAMID_MIP_COUNT; ++level) {
        // Bevy binds the dummy texture for the mips this depth buffer does not
        // need, so every binding in the layout is always populated.
        pyramid.mips[level] = level < mip_count
                                  ? cached.texture.createView(wgpu::TextureViewDescriptor()
                                                                  .setLabel(texture_view_label)
                                                                  .setFormat(wgpu::TextureFormat::eR32Float)
                                                                  .setDimension(wgpu::TextureViewDimension::e2D)
                                                                  .setBaseMipLevel(static_cast<std::uint32_t>(level))
                                                                  .setMipLevelCount(1)
                                                                  .setBaseArrayLayer(0)
                                                                  .setArrayLayerCount(1))
                                  : dummy_texture;
    }
    return pyramid;
}

wgpu::BindGroup ViewDepthPyramid::create_bind_group(const wgpu::Device& device,
                                                    const char* label,
                                                    const wgpu::BindGroupLayout& layout,
                                                    const wgpu::TextureView& source,
                                                    const wgpu::Sampler& sampler) const {
    // Bevy `ViewDepthPyramid::create_bind_group` (`:645-676`): the source depth
    // buffer at binding 0, one storage view per pyramid mip after it, and the
    // sampler last - the order `create_downsample_depth_bind_group_layout` uses.
    std::vector<wgpu::BindGroupEntry> entries;
    entries.reserve(DOWNSAMPLE_DEPTH_BIND_GROUP_ENTRIES);
    entries.push_back(wgpu::BindGroupEntry().setBinding(0).setTextureView(source));
    for (std::size_t level = 0; level < DEPTH_PYRAMID_MIP_COUNT; ++level) {
        entries.push_back(
            wgpu::BindGroupEntry().setBinding(static_cast<std::uint32_t>(level + 1)).setTextureView(mips[level]));
    }
    entries.push_back(
        wgpu::BindGroupEntry().setBinding(static_cast<std::uint32_t>(DEPTH_PYRAMID_MIP_COUNT + 1)).setSampler(sampler));

    return device.createBindGroup(wgpu::BindGroupDescriptor().setLabel(label).setLayout(layout).setEntries(entries));
}

void ViewDepthPyramid::downsample_depth(const char* label,
                                        render::graph::RenderContext& render_context,
                                        glm::uvec2 view_size,
                                        const wgpu::BindGroup& bind_group,
                                        const render::ComputePipeline& first_phase,
                                        const render::ComputePipeline& second_phase) const {
    // Bevy `ViewDepthPyramid::downsample_depth` (`:681-705`). Two dispatches are
    // needed because storage images are not coherent across a single pass; the
    // second only runs when the pyramid is deep enough to have mips 6 and up.
    auto pass = render_context.command_encoder().beginComputePass(wgpu::ComputePassDescriptor().setLabel(label));
    pass.setPipeline(first_phase.pipeline());
    pass.setImmediates(0, &mip_count, sizeof(mip_count));
    pass.setBindGroup(0, bind_group, std::span<const std::uint32_t>{});
    pass.dispatchWorkgroups((view_size.x + 63u) / 64u, (view_size.y + 63u) / 64u, 1);

    if (mip_count >= 7u) {
        pass.setPipeline(second_phase.pipeline());
        pass.dispatchWorkgroups(1, 1, 1);
    }
}

void prepare_view_depth_pyramids(ecs::Commands commands,
                                 ecs::Res<wgpu::Device> render_device,
                                 ecs::ResMut<render::render_resource::TextureCache> texture_cache,
                                 std::optional<ecs::Res<DepthPyramidDummyTexture>> depth_pyramid_dummy_texture,
                                 ecs::Query<ecs::Item<ecs::Entity, const render::view::ExtractedView&>,
                                            ecs::Filter<ecs::With<render::experimental::OcclusionCulling>,
                                                        ecs::Without<render::view::NoIndirectDrawing>>> views) {
    // Bevy `prepare_view_depth_pyramids` (`:709-726`).
    if (!depth_pyramid_dummy_texture.has_value()) return;
    for (auto&& [entity, view] : views.iter()) {
        commands.entity(entity).insert(ViewDepthPyramid::create(
            *render_device, *texture_cache, depth_pyramid_dummy_texture->get().view,
            glm::uvec2(static_cast<std::uint32_t>(view.viewport.z), static_cast<std::uint32_t>(view.viewport.w)),
            "view depth pyramid texture", "view depth pyramid texture view"));
    }
}

void prepare_downsample_depth_view_bind_groups(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> render_device,
    std::optional<ecs::Res<DownsampleDepthPipelines>> downsample_depth_pipelines,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::Query<ecs::Item<ecs::Entity,
                         const ViewDepthPyramid&,
                         ecs::Opt<const render::view::ViewDepthTexture&>,
                         ecs::Opt<const render::experimental::OcclusionCullingSubview&>>,
               ecs::Filter<ecs::Or<ecs::With<render::view::ViewDepthTexture>,
                                   ecs::With<render::experimental::OcclusionCullingSubview>>>> view_depth_textures) {
    // Bevy `prepare_downsample_depth_view_bind_groups` (`:737-785`).
    if (!downsample_depth_pipelines.has_value()) return;
    const auto& pipelines = downsample_depth_pipelines->get();
    for (auto&& [entity, view_depth_pyramid, view_depth_texture, shadow_occlusion_culling] :
         view_depth_textures.iter()) {
        const bool is_multisampled =
            view_depth_texture.has_value() && view_depth_texture->get().texture.getSampleCount() > 1;
        const auto& layout =
            is_multisampled ? pipelines.first_multisample.bind_group_layout : pipelines.first.bind_group_layout;

        const wgpu::TextureView* source = nullptr;
        if (view_depth_texture.has_value()) {
            source = &view_depth_texture->get().attachment.view;
        } else if (shadow_occlusion_culling.has_value()) {
            source = &shadow_occlusion_culling->get().depth_texture_view;
        }
        if (source == nullptr) continue;

        commands.entity(entity).insert(ViewDownsampleDepthBindGroup{view_depth_pyramid.create_bind_group(
            *render_device, is_multisampled ? "downsample multisample depth bind group" : "downsample depth bind group",
            pipeline_server->get_bind_group_layout(layout), *source, pipelines.sampler)});
    }
}

std::expected<void, render::graph::NodeRunError> DownsampleDepthNode::run(render::graph::GraphContext& graph,
                                                                          render::graph::RenderContext& render_context,
                                                                          typename ecs::QueryData<ViewQuery>::Item view,
                                                                          const ecs::World& world) const {
    // Bevy `DownsampleDepthNode` (`:167-227`) runs two instances of itself, one
    // per graph label, and does nothing unless a pyramid was prepared - which
    // only happens for views that enable occlusion culling.
    static_cast<void>(graph);
    auto&& [view_depth_pyramid, view_downsample_depth_bind_group, view_depth_texture] = view;

    const auto pipelines       = world.get_resource<DownsampleDepthPipelines>();
    const auto pipeline_server = world.get_resource<render::PipelineServer>();
    if (!pipelines.has_value() || !pipeline_server.has_value()) return {};

    const bool is_multisampled = view_depth_texture.texture.getSampleCount() > 1;
    const auto& first          = is_multisampled ? pipelines->get().first_multisample : pipelines->get().first;
    const auto& second         = is_multisampled ? pipelines->get().second_multisample : pipelines->get().second;
    if (!first.pipeline_id.has_value() || !second.pipeline_id.has_value()) return {};

    const auto first_pipeline  = pipeline_server->get().get_compute_pipeline(*first.pipeline_id);
    const auto second_pipeline = pipeline_server->get().get_compute_pipeline(*second.pipeline_id);
    if (!first_pipeline.has_value() || !second_pipeline.has_value()) return {};

    view_depth_pyramid.downsample_depth("downsample depth", render_context,
                                        glm::uvec2(static_cast<std::uint32_t>(view_depth_texture.texture.getWidth()),
                                                   static_cast<std::uint32_t>(view_depth_texture.texture.getHeight())),
                                        view_downsample_depth_bind_group.bind_group, first_pipeline->get(),
                                        second_pipeline->get());
    return {};
}

void MipGenerationPlugin::attach(app::App& app) const {
    // Bevy adds `MipGenerationPlugin` from `CorePipelinePlugin`
    // (`bevy_core_pipeline/src/lib.rs:44-51`).
    std::optional<assets::Handle<shader::Shader>> shader;
    if (auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>();
        auto server   = app.world_mut().get_resource<assets::AssetServer>()) {
        registry->get().insert_asset_static(kDownsampleDepthPath, std::as_bytes(std::span(kDownsampleDepthSlang)));
        shader = server->get().load<shader::Shader>("embedded://core_pipeline/downsample_depth.slang");
    } else {
        spdlog::warn(
            "[core_graph.mip_generation] EmbeddedAssetRegistry or AssetServer not available; depth "
            "pyramid disabled.");
    }

    auto sub_app = app.get_sub_app_mut(render::Render);
    if (!sub_app || !shader) return;

    auto& render_world = sub_app->get().world_mut();
    render_world.insert_resource(DownsampleDepthShader{.handle = std::move(*shader)});
    render_world.init_resource<render::SpecializedComputePipelines<DownsampleDepthPipeline>>();

    // Bevy `MipGenerationPlugin::build` (`:73-122`): both downsample nodes, the
    // two edge chains that place them around the prepasses and the end of the
    // main phase, the dummy texture in RenderStartup, and the two prepare
    // systems after the 3D depth textures are prepared.
    render::graph::add_render_graph_node<render::graph::ViewNodeRunner<DownsampleDepthNode>>(
        render_world, core_3d::Core3d, core_3d::Node3d::EarlyDownsampleDepth,
        render::graph::ViewNodeRunner{DownsampleDepthNode{}, render_world});
    render::graph::add_render_graph_node<render::graph::ViewNodeRunner<DownsampleDepthNode>>(
        render_world, core_3d::Core3d, core_3d::Node3d::LateDownsampleDepth,
        render::graph::ViewNodeRunner{DownsampleDepthNode{}, render_world});
    render::graph::add_render_graph_edges(render_world, core_3d::Core3d, core_3d::Node3d::EarlyPrepass,
                                          core_3d::Node3d::EarlyDeferredPrepass, core_3d::Node3d::EarlyDownsampleDepth,
                                          core_3d::Node3d::LatePrepass, core_3d::Node3d::LateDeferredPrepass);
    render::graph::add_render_graph_edges(render_world, core_3d::Core3d, core_3d::Node3d::StartMainPassPostProcessing,
                                          core_3d::Node3d::LateDownsampleDepth,
                                          core_3d::Node3d::EndMainPassPostProcessing);

    sub_app->get().add_systems(
        render::RenderStartup,
        ecs::into(init_depth_pyramid_dummy_texture).set_name("init depth pyramid dummy texture"));
    sub_app->get().add_systems(render::Render, ecs::into(create_downsample_depth_pipelines)
                                                   .in_set(render::RenderSystems::Prepare)
                                                   .set_name("create downsample depth pipelines"));
    sub_app->get().add_systems(
        render::Render,
        ecs::into(prepare_view_depth_pyramids, prepare_downsample_depth_view_bind_groups)
            .chain()
            .in_set(render::RenderSystems::PrepareResources)
            .after(core_3d::prepare_core_3d_depth_textures)
            .set_names(std::array{"prepare view depth pyramids", "prepare downsample depth view bind groups"}));
}

}  // namespace epix::core_graph::mip_generation
