#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/core_graph/fullscreen.hpp>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <epix/utils/async.hpp>
#include <optional>
#include <tuple>
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
 * Like Bevy's default feature set, Epix bundles the Tony McMapface, AgX, and
 * Blender Filmic KTX2 LUTs. The source KTX2 payloads are Zstandard compressed
 * and decoded into filterable 3D float images during plugin setup. */
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
    None                  = 0x00,
    HueRotate             = 0x01,
    WhiteBalance          = 0x02,
    SectionalColorGrading = 0x04,
};

constexpr TonemappingPipelineKeyFlags operator|(TonemappingPipelineKeyFlags lhs,
                                                 TonemappingPipelineKeyFlags rhs) noexcept {
    return static_cast<TonemappingPipelineKeyFlags>(static_cast<std::uint8_t>(lhs) |
                                                     static_cast<std::uint8_t>(rhs));
}
constexpr TonemappingPipelineKeyFlags& operator|=(TonemappingPipelineKeyFlags& lhs,
                                                   TonemappingPipelineKeyFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}
constexpr bool contains(TonemappingPipelineKeyFlags flags, TonemappingPipelineKeyFlags value) noexcept {
    return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(value)) != 0;
}

/** @brief Specialization key for `TonemappingPipeline` (Bevy
 * `TonemappingPipelineKey`). */
EPIX_EXPORT struct TonemappingPipelineKey {
    DebandDither deband_dither        = DebandDither::Disabled;
    Tonemapping tonemapping           = Tonemapping::None;
    TonemappingPipelineKeyFlags flags = TonemappingPipelineKeyFlags::None;

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

/** @brief Resolve the method's LUT texture and sampler, falling back to the
 * render module's 3D fallback image while the selected LUT is unavailable
 * (Bevy `get_lut_bindings`). */
EPIX_EXPORT std::tuple<const wgpu::TextureView&, const wgpu::Sampler&> get_lut_bindings(
    const render::RenderAssets<image::Image>& images,
    const TonemappingLuts& tonemapping_luts,
    Tonemapping tonemapping,
    const render::texture::FallbackImage& fallback_image);

/** @brief Return the filterable 3D texture and sampler layout builders used
 * for LUT bindings 3 and 4 (Bevy `get_lut_bind_group_layout_entries`). */
EPIX_EXPORT std::array<render::render_resource::BindGroupLayoutEntryBuilder, 2>
get_lut_bind_group_layout_entries();

/** @brief Create Bevy's magenta 1x1x1 render-world-only fallback LUT. */
EPIX_EXPORT image::Image lut_placeholder();

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

   private:
    using CachedBindGroup =
        std::tuple<wgpu::Buffer, wgpu::TextureView, wgpu::TextureView, wgpu::BindGroup>;
    mutable utils::Mutex<std::optional<CachedBindGroup>> cached_bind_group{};
    mutable utils::Mutex<std::optional<Tonemapping>> last_tonemapping{};
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
