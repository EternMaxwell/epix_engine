#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/core_graph/fullscreen.hpp>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::core_graph {

/** @brief Per-camera tone-mapping method (Bevy `Tonemapping`).
 *
 * `TonyMcMapface` is the value-initialized default, matching Bevy's `Default`
 * implementation (Bevy 0.18 `#[default] TonyMcMapface`). Core2D explicitly
 * requires `None`. */
EPIX_EXPORT enum class Tonemapping {
    TonyMcMapface,
    None,
    Reinhard,
    ReinhardLuminance,
    AcesFitted,
    AgX,
    SomewhatBoringDisplayTransform,
    BlenderFilmic,
};

/** @brief Bevy `Tonemapping::is_enabled`. */
constexpr bool is_enabled(Tonemapping value) noexcept { return value != Tonemapping::None; }

/** @brief Per-camera debanding switch (Bevy `DebandDither`). */
EPIX_EXPORT enum class DebandDither { Disabled, Enabled };

/** @brief 3D LUT textures used for tonemapping (Bevy 0.18
 * `TonemappingLuts`).
 *
 * Bevy bundles three KTX2 LUTs (tony_mc_mapface, AgX, BlenderFilmic) behind
 * the optional `tonemapping_luts` cargo feature; without it (Bevy's default
 * build), all three handles point at a single magenta placeholder. Epix
 * follows the default-build semantics until a KTX2/BasisLZ decoder lands. */
EPIX_EXPORT struct TonemappingLuts {
    /** @brief Blender filmic LUT (Bevy `blender_filmic`). */
    assets::Handle<image::Image> blender_filmic;
    /** @brief AgX LUT (Bevy `agx`). */
    assets::Handle<image::Image> agx;
    /** @brief Tony McMapface LUT (Bevy `tony_mc_mapface`). */
    assets::Handle<image::Image> tony_mc_mapface;
};

/** @brief Flags describing which color-grading steps the tonemapping shader
 * must perform (Bevy `TonemappingPipelineKeyFlags`). */
enum class TonemappingPipelineKeyFlags : std::uint8_t {
    HueRotate             = 0x01,
    WhiteBalance          = 0x02,
    SectionalColorGrading = 0x04,
};

/** @brief Specialization key for `TonemappingPipeline` (Bevy
 * `TonemappingPipelineKey`). */
EPIX_EXPORT struct TonemappingPipelineKey {
    wgpu::TextureFormat target_format = wgpu::TextureFormat::eUndefined;
    DebandDither deband_dither        = DebandDither::Disabled;
    Tonemapping tonemapping           = Tonemapping::None;
    std::uint8_t flags                = 0;

    bool operator==(const TonemappingPipelineKey& other) const noexcept;
};

/** @brief Reusable HDR->display tonemapping pipeline (Bevy
 * `TonemappingPipeline`). */
EPIX_EXPORT struct TonemappingPipeline {
    using Key = TonemappingPipelineKey;

    wgpu::BindGroupLayout layout;
    wgpu::Sampler sampler;              // non-filtering source sampler
    FullscreenShader fullscreen_shader;
    assets::Handle<shader::Shader> fragment_shader;

    /** @brief Creates the 5-entry tonemapping bind group (view uniform buffer,
     * source texture, source sampler, LUT texture, LUT sampler). */
    wgpu::BindGroup create_bind_group(const wgpu::Device& device, const wgpu::Buffer& view_uniforms,
                                      const wgpu::TextureView& source, const wgpu::TextureView& lut_view,
                                      const wgpu::Sampler& lut_sampler) const;

    /** @brief Builds a format/deband/method/flags-specialized render descriptor. */
    render::RenderPipelineDescriptor specialize(Key key) const;
};

/** @brief Per-view selected tonemapping pipeline (Bevy
 * `ViewTonemappingPipeline`). */
EPIX_EXPORT struct ViewTonemappingPipeline {
    render::CachedPipelineId pipeline_id;
};

/** @brief Typed HDR->display tone-mapping render-graph node (Bevy
 * `TonemappingNode`). */
EPIX_EXPORT struct TonemappingNode {
    using ViewQuery = ecs::Item<const render::view::ViewUniformOffset&,
                                const render::view::ViewTarget&,
                                const ViewTonemappingPipeline&,
                                const Tonemapping&>;

    void update(ecs::World&) {}
    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext& graph,
                                                         render::graph::RenderContext& render_context,
                                                         typename ecs::QueryData<ViewQuery>::Item view,
                                                         const ecs::World& world) const;
};

/** @brief Installs tonemapping resources, per-view pipeline preparation, and
 * the `Tonemapping` graph node (Bevy `TonemappingPlugin`). */
EPIX_EXPORT struct TonemappingPlugin {
    void attach(app::App& app);
};

}  // namespace epix::core_graph

template <>
struct std::hash<epix::core_graph::TonemappingPipelineKey> {
    std::size_t operator()(const epix::core_graph::TonemappingPipelineKey& key) const noexcept;
};
