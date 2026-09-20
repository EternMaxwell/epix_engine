#pragma once

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/app.hpp>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <optional>
#include <string_view>
#include <webgpu/webgpu.hpp>
#include <epix/common.hpp>
#endif
#include <epix/core_graph/core3d.hpp>

namespace epix::core_graph::mip_generation {

/** @brief The maximum number of mip levels the depth pyramid can produce (Bevy
 *  `DEPTH_PYRAMID_MIP_COUNT`, `experimental/mip_generation/mod.rs:64`). 2^12 is
 *  4096, so that is the largest depth buffer the pyramid supports. */
inline constexpr std::size_t DEPTH_PYRAMID_MIP_COUNT = 12;

/** @brief The number of bind group entries one downsample depth layout has: the
 *  depth buffer, one storage texture per pyramid mip, and the sampler. */
inline constexpr std::size_t DOWNSAMPLE_DEPTH_BIND_GROUP_ENTRIES = DEPTH_PYRAMID_MIP_COUNT + 2;

/** @brief The size of a depth pyramid built from a depth buffer of `size`
 *  (Bevy `ViewDepthPyramid::new`'s `depth_pyramid_size`): half the depth
 *  buffer, rounded up. */
EPIX_EXPORT glm::uvec2 depth_pyramid_size(glm::uvec2 size) noexcept;

/** @brief The number of mip levels a `D2` texture of `size` can hold (Bevy
 *  `Extent3d::max_mips(TextureDimension::D2)`): the base-2 logarithm of the
 *  greatest dimension, rounded up, and never less than one. */
EPIX_EXPORT std::uint32_t max_mips_for_size(glm::uvec2 size) noexcept;

/** @brief The number of mip levels the pyramid of a depth buffer of `size`
 *  needs (Bevy `ViewDepthPyramid::new`'s `depth_pyramid_mip_count`). */
EPIX_EXPORT std::uint32_t depth_pyramid_mip_count(glm::uvec2 size) noexcept;

/** @brief Identifies the configuration of the downsample depth shader (Bevy
 *  `DownsampleDepthPipelineKey`). Meshlets keep their own downsample shaders,
 *  so there is no flag for them here even though the shader has defines for
 *  that path. */
EPIX_EXPORT struct DownsampleDepthPipelineKey {
    /** @brief True when the source depth buffer is multisampled. */
    static constexpr std::uint8_t MULTISAMPLE = 1u;
    /** @brief True for the second phase of the downsample process. */
    static constexpr std::uint8_t SECOND_PHASE = 2u;

    std::uint8_t bits = 0;

    [[nodiscard]] constexpr bool contains(std::uint8_t flag) const noexcept { return (bits & flag) != 0; }

    friend constexpr bool operator==(const DownsampleDepthPipelineKey&,
                                     const DownsampleDepthPipelineKey&) noexcept = default;
};

/** @brief One depth downsample compute pipeline (Bevy
 *  `DownsampleDepthPipeline`). */
EPIX_EXPORT struct DownsampleDepthPipeline {
    using Key = DownsampleDepthPipelineKey;

    /** @brief The bind group layout shared by both phases. */
    render::BindGroupLayoutDescriptor bind_group_layout;
    /** @brief The compiled pipeline, once `PipelineServer` has built it. */
    std::optional<render::CachedPipelineId> pipeline_id;
    /** @brief The shader the phases are specialized from. */
    assets::Handle<shader::Shader> shader;

    /** @brief Builds the compute descriptor for `key` (Bevy
     *  `SpecializedComputePipeline::specialize`): the first phase's entry point
     *  unless `SECOND_PHASE` is set, the `MULTISAMPLE` shader def when the depth
     *  buffer is multisampled, and the mip-count push constant. */
    [[nodiscard]] render::ComputePipelineDescriptor specialize(Key key) const;
};

/** @brief Creates one downsample depth bind group layout (Bevy
 *  `create_downsample_depth_bind_group_layout`): the depth buffer at binding 0,
 *  one storage texture per pyramid mip at bindings 1 to
 *  `DEPTH_PYRAMID_MIP_COUNT`, and a non-filtering sampler at the last binding.
 *
 *  Only the first mip's multisample status needs a distinct layout; once that
 *  level is sampled the pyramid drops to a single sample. Every storage entry
 *  uses read-write access because Slang and HLSL have no write-only
 *  storage-texture declaration, unlike Bevy's WGSL
 *  (`mip_generation/mod.rs:420-455`). */
EPIX_EXPORT render::BindGroupLayoutDescriptor create_downsample_depth_bind_group_layout(bool multisampled);

/** @brief A placeholder storage texture bound to depth-pyramid slots that no
 *  mip level uses (Bevy `DepthPyramidDummyTexture`). */
EPIX_EXPORT struct DepthPyramidDummyTexture {
    wgpu::TextureView view;
};

/** @brief Creates a one-texel `R32Float` storage texture used for unused
 *  pyramid mip bindings (Bevy `create_depth_pyramid_dummy_texture`). */
EPIX_EXPORT wgpu::TextureView create_depth_pyramid_dummy_texture(const wgpu::Device& device,
                                                                const char* texture_label,
                                                                const char* texture_view_label);

/** @brief Identifies the `downsample_depth` shader (Bevy `DownsampleDepthShader`). */
EPIX_EXPORT struct DownsampleDepthShader {
    assets::Handle<shader::Shader> handle;
};

/** @brief Stores all depth buffer downsampling pipelines (Bevy
 *  `DownsampleDepthPipelines`). Two phases, each with a non-multisampled and a
 *  multisampled variant, plus one sampler shared by all of them. */
EPIX_EXPORT struct DownsampleDepthPipelines {
    /** @brief The first phase, for a non-multisampled depth buffer. */
    DownsampleDepthPipeline first;
    /** @brief The second phase, for a non-multisampled depth buffer. */
    DownsampleDepthPipeline second;
    /** @brief The first phase, for a multisampled depth buffer. */
    DownsampleDepthPipeline first_multisample;
    /** @brief The second phase, for a multisampled depth buffer. */
    DownsampleDepthPipeline second_multisample;
    /** @brief The sampler the downsample shader samples the depth buffer with. */
    wgpu::Sampler sampler;
};

/** @brief `RenderStartup` system that creates the four downsample pipelines
 *  (Bevy `create_downsample_depth_pipelines`). It runs once and only when the
 *  device supports GPU preprocessing culling, which is the subsystem the
 *  pyramid feeds. */
EPIX_EXPORT void create_downsample_depth_pipelines(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> render_device,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::ResMut<render::SpecializedComputePipelines<DownsampleDepthPipeline>> specialized_compute_pipelines,
    ecs::Res<render::batching::GpuPreprocessingSupport> gpu_preprocessing_support,
    ecs::Res<DownsampleDepthShader> downsample_depth_shader,
    ecs::Local<bool> has_run);

/** @brief Installs the depth-pyramid machinery (Bevy `MipGenerationPlugin`).
 *
 *  The pyramid resource, the two `prepare_*` systems and the node that
 *  dispatches the passes are the remaining step of the port. */
EPIX_EXPORT struct MipGenerationPlugin {
    void attach(app::App& app) const;
};

}  // namespace epix::core_graph::mip_generation

template <>
struct std::hash<epix::core_graph::mip_generation::DownsampleDepthPipelineKey> {
    std::size_t operator()(
        const epix::core_graph::mip_generation::DownsampleDepthPipelineKey& key) const noexcept {
        return std::hash<std::uint8_t>{}(key.bits);
    }
};
