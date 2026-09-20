#include <epix/core_graph/mip_generation.hpp>

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <bit>
#include <format>
#include <spdlog/spdlog.h>
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
        .label = std::format("downsample depth{}{} pipeline", multisampled ? " multisample" : "",
                             second_phase ? " second phase" : " first phase"),
        .layouts = std::vector<render::BindGroupLayoutDescriptor>{bind_group_layout},
        .push_constant_ranges =
            std::vector<render::PushConstantRange>{render::PushConstantRange{
                .stages = wgpu::ShaderStage::eCompute, .start = 0, .end = 4}},
        .shader      = shader,
        .shader_defs = std::move(shader_defs),
        .entry_point = std::string(second_phase ? "downsample_depth_second" : "downsample_depth_first"),
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

    pipelines.first.pipeline_id = specialized_compute_pipelines->specialize(
        *pipeline_server, pipelines.first, DownsampleDepthPipelineKey{});
    pipelines.second.pipeline_id = specialized_compute_pipelines->specialize(
        *pipeline_server, pipelines.second,
        DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::SECOND_PHASE});
    pipelines.first_multisample.pipeline_id = specialized_compute_pipelines->specialize(
        *pipeline_server, pipelines.first_multisample,
        DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::MULTISAMPLE});
    pipelines.second_multisample.pipeline_id = specialized_compute_pipelines->specialize(
        *pipeline_server, pipelines.second_multisample,
        DownsampleDepthPipelineKey{DownsampleDepthPipelineKey::SECOND_PHASE |
                                   DownsampleDepthPipelineKey::MULTISAMPLE});

    commands.insert_resource(std::move(pipelines));
}

void MipGenerationPlugin::attach(app::App& app) const {
    // Bevy adds `MipGenerationPlugin` from `CorePipelinePlugin`
    // (`bevy_core_pipeline/src/lib.rs:44-51`).
    std::optional<assets::Handle<shader::Shader>> shader;
    if (auto registry = app.world_mut().get_resource_mut<assets::EmbeddedAssetRegistry>();
        auto server   = app.world_mut().get_resource<assets::AssetServer>()) {
        registry->get().insert_asset_static(kDownsampleDepthPath,
                                            std::as_bytes(std::span(kDownsampleDepthSlang)));
        shader = server->get().load<shader::Shader>("embedded://core_pipeline/downsample_depth.slang");
    } else {
        spdlog::warn("[core_graph.mip_generation] EmbeddedAssetRegistry or AssetServer not available; depth "
                     "pyramid disabled.");
    }

    auto sub_app = app.get_sub_app_mut(render::Render);
    if (!sub_app || !shader) return;

    sub_app->get().world_mut().insert_resource(DownsampleDepthShader{.handle = std::move(*shader)});
    sub_app->get().world_mut().init_resource<render::SpecializedComputePipelines<DownsampleDepthPipeline>>();
    sub_app->get().add_systems(render::RenderStartup,
                               ecs::into(create_downsample_depth_pipelines)
                                   .set_name("create downsample depth pipelines"));
}

}  // namespace epix::core_graph::mip_generation
