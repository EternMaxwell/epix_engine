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
EPIX_EXPORT EPIX_API glm::uvec2 depth_pyramid_size(glm::uvec2 size) noexcept;

/** @brief The number of mip levels a `D2` texture of `size` can hold (Bevy
 *  `Extent3d::max_mips(TextureDimension::D2)`): the base-2 logarithm of the
 *  greatest dimension, rounded up, and never less than one. */
EPIX_EXPORT EPIX_API std::uint32_t max_mips_for_size(glm::uvec2 size) noexcept;

/** @brief The number of mip levels the pyramid of a depth buffer of `size`
 *  needs (Bevy `ViewDepthPyramid::new`'s `depth_pyramid_mip_count`). */
EPIX_EXPORT EPIX_API std::uint32_t depth_pyramid_mip_count(glm::uvec2 size) noexcept;

/** @brief Identifies the configuration of the downsample depth shader (Bevy
 *  `DownsampleDepthPipelineKey`). Meshlets keep their own downsample shaders,
 *  so there is no flag for them here even though the shader has defines for
 *  that path. */
EPIX_EXPORT struct EPIX_API DownsampleDepthPipelineKey {
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
EPIX_EXPORT struct EPIX_API DownsampleDepthPipeline {
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
EPIX_EXPORT EPIX_API render::BindGroupLayoutDescriptor create_downsample_depth_bind_group_layout(bool multisampled);

/** @brief A placeholder storage texture bound to depth-pyramid slots that no
 *  mip level uses (Bevy `DepthPyramidDummyTexture`). */
EPIX_EXPORT struct DepthPyramidDummyTexture {
    wgpu::TextureView view;
};

/** @brief Creates a one-texel `R32Float` storage texture used for unused
 *  pyramid mip bindings (Bevy `create_depth_pyramid_dummy_texture`). */
EPIX_EXPORT EPIX_API wgpu::TextureView create_depth_pyramid_dummy_texture(const wgpu::Device& device,
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
EPIX_EXPORT EPIX_API void create_downsample_depth_pipelines(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> render_device,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::ResMut<render::SpecializedComputePipelines<DownsampleDepthPipeline>> specialized_compute_pipelines,
    ecs::Res<render::batching::GpuPreprocessingSupport> gpu_preprocessing_support,
    ecs::Res<DownsampleDepthShader> downsample_depth_shader,
    ecs::Local<bool> has_run);

/** @brief `RenderStartup` system that creates the placeholder pyramid texture
 *  (Bevy `init_depth_pyramid_dummy_texture`). */
EPIX_EXPORT EPIX_API void init_depth_pyramid_dummy_texture(ecs::Commands commands, ecs::Res<wgpu::Device> render_device);

/** @brief A hierarchical Z-buffer for one view: a series of mipmaps of the depth
 *  buffer, bound to the downsample shader as storage images (Bevy
 *  `ViewDepthPyramid`). */
EPIX_EXPORT struct EPIX_API ViewDepthPyramid {
    /** @brief A texture view containing the entire depth pyramid. */
    wgpu::TextureView all_mips;
    /** @brief One view per pyramid mip, or the dummy texture for unused mips. */
    std::array<wgpu::TextureView, DEPTH_PYRAMID_MIP_COUNT> mips;
    /** @brief The total number of mipmap levels. */
    std::uint32_t mip_count = 0;

    /** @brief Allocates a depth pyramid for a depth buffer of `size` (Bevy
     *  `ViewDepthPyramid::new`). */
    static ViewDepthPyramid create(const wgpu::Device& device,
                                   render::render_resource::TextureCache& texture_cache,
                                   const wgpu::TextureView& dummy_texture,
                                   glm::uvec2 size,
                                   const char* texture_label,
                                   const char* texture_view_label);

    /** @brief Creates the bind group that attaches `source` and this pyramid to
     *  the downsample shader (Bevy `ViewDepthPyramid::create_bind_group`). */
    [[nodiscard]] wgpu::BindGroup create_bind_group(const wgpu::Device& device,
                                                    const char* label,
                                                    const wgpu::BindGroupLayout& layout,
                                                    const wgpu::TextureView& source,
                                                    const wgpu::Sampler& sampler) const;

    /** @brief Runs the two downsample passes (Bevy
     *  `ViewDepthPyramid::downsample_depth`). */
    void downsample_depth(const char* label,
                          render::graph::RenderContext& render_context,
                          glm::uvec2 view_size,
                          const wgpu::BindGroup& bind_group,
                          const render::ComputePipeline& first_phase,
                          const render::ComputePipeline& second_phase) const;
};

/** @brief The bind group attaching one view's depth buffer and pyramid to the
 *  downsample shader (Bevy `ViewDownsampleDepthBindGroup`). */
EPIX_EXPORT struct ViewDownsampleDepthBindGroup {
    wgpu::BindGroup bind_group;
};

/** @brief Creates depth pyramids for the views that have occlusion culling
 *  enabled (Bevy `prepare_view_depth_pyramids`). The dummy texture is optional
 *  so the system runs and does nothing when the platform has no pyramid
 *  support, which is Bevy's `resource_exists` gate. */
EPIX_EXPORT EPIX_API void prepare_view_depth_pyramids(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> render_device,
    ecs::ResMut<render::render_resource::TextureCache> texture_cache,
    std::optional<ecs::Res<DepthPyramidDummyTexture>> depth_pyramid_dummy_texture,
    ecs::Query<ecs::Item<ecs::Entity, const render::view::ExtractedView&>,
               ecs::Filter<ecs::With<render::experimental::OcclusionCulling>,
                           ecs::Without<render::view::NoIndirectDrawing>>>
        views);

/** @brief Creates the downsample bind groups for the views with a depth
 *  pyramid (Bevy `prepare_downsample_depth_view_bind_groups`). The pipelines are
 *  optional for the same reason as above. */
EPIX_EXPORT EPIX_API void prepare_downsample_depth_view_bind_groups(
    ecs::Commands commands,
    ecs::Res<wgpu::Device> render_device,
    std::optional<ecs::Res<DownsampleDepthPipelines>> downsample_depth_pipelines,
    ecs::Res<render::PipelineServer> pipeline_server,
    ecs::Query<ecs::Item<ecs::Entity,
                         const ViewDepthPyramid&,
                         ecs::Opt<const render::view::ViewDepthTexture&>,
                         ecs::Opt<const render::experimental::OcclusionCullingSubview&>>,
               ecs::Filter<ecs::Or<ecs::With<render::view::ViewDepthTexture>,
                                   ecs::With<render::experimental::OcclusionCullingSubview>>>>
        view_depth_textures);

/** @brief Produces the hierarchical Z-buffer for a view (Bevy
 *  `DownsampleDepthNode`), installed twice: before the late prepass and at the
 *  end of the main phase. */
EPIX_EXPORT struct EPIX_API DownsampleDepthNode {
    using ViewQuery = ecs::Item<const ViewDepthPyramid&,
                                const ViewDownsampleDepthBindGroup&,
                                const render::view::ViewDepthTexture&>;
    void update(ecs::World&) {}
    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext&,
                                                        render::graph::RenderContext& render_context,
                                                        typename ecs::QueryData<ViewQuery>::Item view,
                                                        const ecs::World& world) const;
};

/** @brief Installs the depth-pyramid machinery (Bevy `MipGenerationPlugin`):
 *  the shader and its four pipelines, the two prepare systems that give every
 *  occlusion-culling view a pyramid and a bind group, and the node that runs
 *  the downsample passes at Bevy's two graph labels. */
EPIX_EXPORT struct EPIX_API MipGenerationPlugin {
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
