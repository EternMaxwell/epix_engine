#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::core_graph::core_3d {

EPIX_EXPORT inline constexpr wgpu::TextureFormat CORE_3D_DEPTH_FORMAT = wgpu::TextureFormat::eDepth32Float;

/** @brief Names exposed by Bevy's `core_3d::graph::input` module. */
namespace input {
inline constexpr std::string_view ViewEntity = "view_entity";
}

/** @brief Node labels for the 3D render graph passes (Bevy `Node3d`,
 * `core_3d/mod.rs:16-47`). Labels whose owning subsystem does not exist yet
 * are still declared and installed so optional plugins can extend the graph
 * at the same points Bevy exposes. */
EPIX_EXPORT enum class Core3dNodes {
    /** @brief Optional MSAA resolve pass supplied by the MSAA plugin. */
    MsaaWriteback,
    /** @brief Early prepass node (Bevy `Node3d::EarlyPrepass`). */
    EarlyPrepass,
    /** @brief Optional early depth downsampling pass. */
    EarlyDownsampleDepth,
    /** @brief Late prepass node (Bevy `Node3d::LatePrepass`). */
    LatePrepass,
    /** @brief Early deferred G-buffer prepass node. */
    EarlyDeferredPrepass,
    /** @brief Late deferred G-buffer prepass node. */
    LateDeferredPrepass,
    /** @brief Deferred lighting-id copy node. */
    CopyDeferredLightingId,
    /** @brief End of the prepass segment. */
    EndPrepasses,
    /** @brief Node that begins the main render pass. */
    StartMainPass,
    /** @brief Node for rendering opaque 3D items. */
    MainOpaquePass,
    /** @brief Node for rendering transmissive 3D items. */
    MainTransmissivePass,
    /** @brief Node for rendering transparent 3D items. */
    MainTransparentPass,
    /** @brief Node that ends the main render pass. */
    EndMainPass,
    /** @brief Optional wireframe pass supplied by the wireframe plugin. */
    Wireframe,
    /** @brief Start of the extensible post-processing segment. */
    StartMainPassPostProcessing,
    /** @brief Optional late depth downsampling pass. */
    LateDownsampleDepth,
    /** @brief Optional motion blur pass supplied by the motion-blur plugin. */
    MotionBlur,
    /** @brief Optional temporal anti-aliasing pass. */
    Taa,
    /** @brief Optional DLSS super-resolution pass. */
    DlssSuperResolution,
    /** @brief Optional DLSS ray-reconstruction pass. */
    DlssRayReconstruction,
    /** @brief Optional bloom pass supplied by the bloom plugin. */
    Bloom,
    /** @brief Optional auto-exposure pass. */
    AutoExposure,
    /** @brief Optional depth-of-field pass. */
    DepthOfField,
    /** @brief Optional general post-processing pass. */
    PostProcessing,
    /** @brief Tone-mapping pass. */
    Tonemapping,
    /** @brief Optional FXAA pass supplied by the FXAA plugin. */
    Fxaa,
    /** @brief Optional SMAA pass supplied by the SMAA plugin. */
    Smaa,
    /** @brief Final output pass. */
    Upscaling,
    /** @brief Optional contrast-adaptive-sharpening pass. */
    ContrastAdaptiveSharpening,
    /** @brief End of the extensible post-processing segment. */
    EndMainPassPostProcessing,
};

/** @brief Information that must be identical to place opaque meshes in the
 * same *batch set* without a lightmap (Bevy
 * `OpaqueNoLightmap3dBatchSetKey`, `prepass/mod.rs:197`).
 *
 * Bevy declares this in its prepass module because the prepass and deferred
 * passes consume it first; Epix declares it here, next to its first consumer
 * (`AlphaMask3d`), so the prepass work reuses one definition rather than
 * duplicating it. */
EPIX_EXPORT struct OpaqueNoLightmap3dBatchSetKey {
    /** @brief The identifier of the render pipeline. */
    render::CachedPipelineId pipeline_id;
    /** @brief The function used to draw. */
    render::phase::DrawFunctionId draw_func;
    /** @brief The ID of a bind group specific to the material instance. */
    std::optional<std::uint32_t> material_bind_group_index;
    /** @brief The ID of the slab of GPU memory that contains vertex data. For
     * non-mesh items, fill this with 0 if your items can be multi-drawn, or
     * with a unique value if they cannot. */
    render::mesh::SlabId vertex_slab{};
    /** @brief The ID of the slab of GPU memory that contains index data, if
     * present. For non-mesh items this is empty. */
    std::optional<render::mesh::SlabId> index_slab;
    bool indexed() const noexcept { return index_slab.has_value(); }
    auto operator<=>(const OpaqueNoLightmap3dBatchSetKey&) const = default;
};

/** @brief Data that must be identical to *batch* opaque non-lightmapped 3D
 * items together (Bevy `OpaqueNoLightmap3dBinKey`, `prepass/mod.rs:230`). */
EPIX_EXPORT struct OpaqueNoLightmap3dBinKey {
    /** @brief The asset that this phase item is associated with. Normally the
     * mesh ID, but for non-mesh items another asset type. */
    assets::UntypedAssetId asset_id;
    auto operator<=>(const OpaqueNoLightmap3dBinKey&) const = default;
};

/** @brief Information that must be identical to place opaque meshes with a
 * lightmap in the same *batch set* (Bevy `Opaque3dBatchSetKey`,
 * `core_3d/mod.rs:272`). */
EPIX_EXPORT struct Opaque3dBatchSetKey {
    /** @brief The identifier of the render pipeline. */
    render::CachedPipelineId pipeline_id;
    /** @brief The function used to draw. */
    render::phase::DrawFunctionId draw_func;
    /** @brief The ID of a bind group specific to the material instance. */
    std::optional<std::uint32_t> material_bind_group_index;
    /** @brief The ID of the slab of GPU memory that contains vertex data. For
     * non-mesh items, fill this with 0 if your items can be multi-drawn, or
     * with a unique value if they cannot. */
    render::mesh::SlabId vertex_slab{};
    /** @brief The ID of the slab of GPU memory that contains index data, if
     * present. For non-mesh items this is empty. */
    std::optional<render::mesh::SlabId> index_slab;
    /** @brief Index of the slab that the lightmap resides in, if a lightmap is
     * present (Bevy `Option<NonMaxU32>`). */
    std::optional<std::uint32_t> lightmap_slab;
    bool indexed() const noexcept { return index_slab.has_value(); }
    auto operator<=>(const Opaque3dBatchSetKey&) const = default;
};

/** @brief Data that must be identical to *batch* opaque 3D items together
 * (Bevy `Opaque3dBinKey`, `core_3d/mod.rs:310`). */
EPIX_EXPORT struct Opaque3dBinKey {
    /** @brief The asset that this phase item is associated with. Normally the
     * mesh ID, but for non-mesh items another asset type. */
    assets::UntypedAssetId asset_id;
    auto operator<=>(const Opaque3dBinKey&) const = default;
};

/** @brief A binned opaque 3D render phase item (Bevy `Opaque3d`). */
EPIX_EXPORT struct Opaque3d {
    std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity;
    Opaque3dBatchSetKey batch_set_key_value;
    Opaque3dBinKey bin_key_value;
    std::pair<std::uint32_t, std::uint32_t> batch_range_value;
    render::phase::PhaseItemExtraIndex extra_index_value{};

    ecs::Entity entity() const noexcept { return representative_entity.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return representative_entity.second; }
    render::phase::DrawFunctionId draw_function() const noexcept { return batch_set_key_value.draw_func; }
    render::CachedPipelineId cached_pipeline() const noexcept { return batch_set_key_value.pipeline_id; }
    const std::pair<std::uint32_t, std::uint32_t>& batch_range() const noexcept { return batch_range_value; }
    std::pair<std::uint32_t, std::uint32_t>& batch_range() noexcept { return batch_range_value; }
    const render::phase::PhaseItemExtraIndex& extra_index() const noexcept { return extra_index_value; }
    render::phase::PhaseItemExtraIndex& extra_index() noexcept { return extra_index_value; }
    using BinKey      = Opaque3dBinKey;
    using BatchSetKey = Opaque3dBatchSetKey;
    static Opaque3d create(BatchSetKey batch_set_key,
                           BinKey bin_key,
                           std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity,
                           std::pair<std::uint32_t, std::uint32_t> batch_range,
                           render::phase::PhaseItemExtraIndex extra_index) {
        return {std::move(representative_entity), std::move(batch_set_key), std::move(bin_key), batch_range,
                extra_index};
    }
};
static_assert(render::phase::CachedRenderPipelinePhaseItem<Opaque3d>);
static_assert(render::phase::BinnedPhaseItem<Opaque3d>);

/** @brief A binned alpha-masked 3D render phase item (Bevy `AlphaMask3d`).
 * Uses the lightmap-free key pair, exactly as Bevy does. */
EPIX_EXPORT struct AlphaMask3d {
    std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity;
    OpaqueNoLightmap3dBatchSetKey batch_set_key_value;
    OpaqueNoLightmap3dBinKey bin_key_value;
    std::pair<std::uint32_t, std::uint32_t> batch_range_value;
    render::phase::PhaseItemExtraIndex extra_index_value{};

    ecs::Entity entity() const noexcept { return representative_entity.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return representative_entity.second; }
    render::phase::DrawFunctionId draw_function() const noexcept { return batch_set_key_value.draw_func; }
    render::CachedPipelineId cached_pipeline() const noexcept { return batch_set_key_value.pipeline_id; }
    const std::pair<std::uint32_t, std::uint32_t>& batch_range() const noexcept { return batch_range_value; }
    std::pair<std::uint32_t, std::uint32_t>& batch_range() noexcept { return batch_range_value; }
    const render::phase::PhaseItemExtraIndex& extra_index() const noexcept { return extra_index_value; }
    render::phase::PhaseItemExtraIndex& extra_index() noexcept { return extra_index_value; }
    using BinKey      = OpaqueNoLightmap3dBinKey;
    using BatchSetKey = OpaqueNoLightmap3dBatchSetKey;
    static AlphaMask3d create(BatchSetKey batch_set_key,
                              BinKey bin_key,
                              std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity,
                              std::pair<std::uint32_t, std::uint32_t> batch_range,
                              render::phase::PhaseItemExtraIndex extra_index) {
        return {std::move(representative_entity), std::move(batch_set_key), std::move(bin_key), batch_range,
                extra_index};
    }
};
static_assert(render::phase::CachedRenderPipelinePhaseItem<AlphaMask3d>);
static_assert(render::phase::BinnedPhaseItem<AlphaMask3d>);

/** @brief A sorted transmissive 3D render phase item (Bevy `Transmissive3d`).
 *
 * Sorted by ascending view-space distance for back-to-front rendering.
 * Automatic batching is disabled, exactly as in Bevy: the item is drawn
 * multiple times across the configured transmission steps, and batching
 * cannot currently split a batch range at a step boundary. */
EPIX_EXPORT struct Transmissive3d {
    /** @brief Disables automatic batching (Bevy
     * `Transmissive3d::AUTOMATIC_BATCHING`). */
    static constexpr bool AUTOMATIC_BATCHING = false;

    /** @brief View-space distance from the camera. */
    float distance;
    /** @brief Cached render pipeline ID. */
    render::CachedPipelineId pipeline_id;
    /** @brief The render entity and its main-world entity. */
    std::pair<ecs::Entity, render::sync_world::MainEntity> entity_value;
    /** @brief Draw function ID for rendering this item. */
    render::phase::DrawFunctionId draw_func;
    /** @brief Instance range covered by this item's batch (Bevy batch_range:
     * Range<u32>). */
    std::pair<std::uint32_t, std::uint32_t> batch_range_value;
    /** @brief Dynamic-offset or indirect-parameter index assigned while
     * batching. */
    render::phase::PhaseItemExtraIndex extra_index_value{};
    /** @brief Whether this item draws with a mesh index buffer. */
    bool indexed_value = false;

    ecs::Entity entity() const noexcept { return entity_value.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return entity_value.second; }
    float sort_key() const noexcept { return distance; }
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    render::CachedPipelineId cached_pipeline() const noexcept { return pipeline_id; }
    bool indexed() const noexcept { return indexed_value; }
    const std::pair<std::uint32_t, std::uint32_t>& batch_range() const noexcept { return batch_range_value; }
    std::pair<std::uint32_t, std::uint32_t>& batch_range() noexcept { return batch_range_value; }
    const render::phase::PhaseItemExtraIndex& extra_index() const noexcept { return extra_index_value; }
    render::phase::PhaseItemExtraIndex& extra_index() noexcept { return extra_index_value; }
};
static_assert(render::phase::CachedRenderPipelinePhaseItem<Transmissive3d>);
static_assert(render::phase::SortedPhaseItem<Transmissive3d>);

/** @brief A sorted transparent 3D render phase item (Bevy `Transparent3d`).
 *
 * Sorted by ascending view-space distance for back-to-front rendering. */
EPIX_EXPORT struct Transparent3d {
    /** @brief View-space distance from the camera. */
    float distance;
    /** @brief Cached render pipeline ID. */
    render::CachedPipelineId pipeline_id;
    /** @brief The render entity and its main-world entity. */
    std::pair<ecs::Entity, render::sync_world::MainEntity> entity_value;
    /** @brief Draw function ID for rendering this item. */
    render::phase::DrawFunctionId draw_func;
    /** @brief Instance range covered by this item's batch (Bevy batch_range:
     * Range<u32>). */
    std::pair<std::uint32_t, std::uint32_t> batch_range_value;
    /** @brief Dynamic-offset or indirect-parameter index assigned while
     * batching. */
    render::phase::PhaseItemExtraIndex extra_index_value{};
    /** @brief Whether this item draws with a mesh index buffer. */
    bool indexed_value = false;

    ecs::Entity entity() const noexcept { return entity_value.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return entity_value.second; }
    float sort_key() const noexcept { return distance; }
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    render::CachedPipelineId cached_pipeline() const noexcept { return pipeline_id; }
    bool indexed() const noexcept { return indexed_value; }
    const std::pair<std::uint32_t, std::uint32_t>& batch_range() const noexcept { return batch_range_value; }
    std::pair<std::uint32_t, std::uint32_t>& batch_range() noexcept { return batch_range_value; }
    const render::phase::PhaseItemExtraIndex& extra_index() const noexcept { return extra_index_value; }
    render::phase::PhaseItemExtraIndex& extra_index() noexcept { return extra_index_value; }
};
static_assert(render::phase::CachedRenderPipelinePhaseItem<Transparent3d>);
static_assert(render::phase::SortedPhaseItem<Transparent3d>);

/** @brief A screen-space transmission copy of the current main texture (Bevy
 * `ViewTransmissionTexture`). */
EPIX_EXPORT struct ViewTransmissionTexture {
    wgpu::Texture texture;
    wgpu::TextureView view;
    wgpu::Sampler sampler;
};

/** @brief Bevy `MainOpaquePass3dNode`: renders the opaque and alpha-mask
 * binned phases into one deferred command buffer against the shared depth
 * target. Bevy's optional skybox and `MainPassResolutionOverride` inputs have
 * no Epix counterpart yet. */
EPIX_EXPORT struct MainOpaquePass3dNode {
    using ViewQuery = ecs::Item<const render::camera::ExtractedCamera&,
                                const render::view::ExtractedView&,
                                const render::view::ViewTarget&,
                                const render::view::ViewDepthTexture&>;
    void update(ecs::World&) {}
    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext&,
                                                         render::graph::RenderContext& render_context,
                                                         typename ecs::QueryData<ViewQuery>::Item view,
                                                         const ecs::World& world) const;
};

/** @brief Bevy `MainTransmissivePass3dNode`: renders the retained sorted
 * transmissive phase, split into the camera's configured number of
 * back-to-front ranges with a source copy before each range. */
EPIX_EXPORT struct MainTransmissivePass3dNode {
    using ViewQuery = ecs::Item<const render::camera::ExtractedCamera&,
                                const render::view::ExtractedView&,
                                const ::epix::camera::Camera3d&,
                                const render::view::ViewTarget&,
                                ecs::Opt<const ViewTransmissionTexture&>,
                                const render::view::ViewDepthTexture&>;
    void update(ecs::World&) {}
    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext&,
                                                         render::graph::RenderContext& render_context,
                                                         typename ecs::QueryData<ViewQuery>::Item view,
                                                         const ecs::World& world) const;
};

/** @brief Bevy `MainTransparentPass3dNode`: renders the retained sorted
 * transparent phase in an independent deferred command buffer. */
EPIX_EXPORT struct MainTransparentPass3dNode {
    using ViewQuery = ecs::Item<const render::camera::ExtractedCamera&,
                                const render::view::ExtractedView&,
                                const render::view::ViewTarget&,
                                const render::view::ViewDepthTexture&>;
    void update(ecs::World&) {}
    std::expected<void, render::graph::NodeRunError> run(render::graph::GraphContext&,
                                                         render::graph::RenderContext& render_context,
                                                         typename ecs::QueryData<ViewQuery>::Item view,
                                                         const ecs::World& world) const;
};

/** @brief Splits `[start, end)` into at most `max_num_splits` non-overlapping
 * sub-ranges, giving the remainder to the initial sub-ranges (Bevy
 * `main_transmissive_pass_3d_node.rs::split_range`). */
EPIX_EXPORT std::vector<std::pair<std::size_t, std::size_t>> split_transmissive_range(std::size_t start,
                                                                                      std::size_t end,
                                                                                      std::size_t max_num_splits);

/** @brief Singleton struct for initializing the core 3D render graph. */
EPIX_EXPORT inline struct Core3dGraph {
    /** @brief Add this graph as a sub-graph to the given render graph. */
    void add_to(render::graph::RenderGraph& g, ecs::World& world);
} Core3d;

/** @brief Plugin that sets up the core 3D render graph and camera
 * projection. */
EPIX_EXPORT struct Core3dPlugin {
    void attach(app::App& app);
};

/** @brief Maintain the four phase collections for active Camera3d entities
 * (Bevy `extract_core_3d_camera_phases`). */
EPIX_EXPORT void extract_core_3d_camera_phases(
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<Opaque3d>> opaque_phases,
    ecs::ResMut<render::phase::ViewBinnedRenderPhases<AlphaMask3d>> alpha_mask_phases,
    ecs::ResMut<render::phase::ViewSortedRenderPhases<Transmissive3d>> transmissive_phases,
    ecs::ResMut<render::phase::ViewSortedRenderPhases<Transparent3d>> transparent_phases,
    app::Extract<
        ecs::Query<ecs::Item<ecs::Entity, const ::epix::camera::Camera&, ecs::Has<render::view::NoIndirectDrawing>>,
                   ecs::With<::epix::camera::Camera3d>>> cameras,
    ecs::Local<std::unordered_set<render::view::RetainedViewEntity>> live_views,
    ecs::Res<render::batching::GpuPreprocessingSupport> gpu_preprocessing_support);

/** @brief Prepare depth attachments for live Core3D views (Bevy
 * `prepare_core_3d_depth_textures`). The general view plugin deliberately
 * does not allocate depth for arbitrary cameras. */
EPIX_EXPORT void prepare_core_3d_depth_textures(
    ecs::Commands cmd,
    ecs::ResMut<render::render_resource::TextureCache> texture_cache,
    ecs::Res<wgpu::Device> device,
    ecs::Res<render::phase::ViewBinnedRenderPhases<Opaque3d>> opaque_phases,
    ecs::Res<render::phase::ViewBinnedRenderPhases<AlphaMask3d>> alpha_mask_phases,
    ecs::Res<render::phase::ViewSortedRenderPhases<Transmissive3d>> transmissive_phases,
    ecs::Res<render::phase::ViewSortedRenderPhases<Transparent3d>> transparent_phases,
    ecs::Query<ecs::Item<ecs::Entity,
                         const render::camera::ExtractedCamera&,
                         const render::view::ExtractedView&,
                         const ::epix::camera::Camera3d&,
                         const render::view::Msaa&>> views);

/** @brief Prepare per-view transmission copies for views with transmissive
 * work (Bevy `prepare_core_3d_transmission_textures`). A view is skipped when
 * the step count is zero, the transmissive phase is empty, or the camera has
 * no physical target size. */
EPIX_EXPORT void prepare_core_3d_transmission_textures(
    ecs::Commands cmd,
    ecs::ResMut<render::render_resource::TextureCache> texture_cache,
    ecs::Res<wgpu::Device> device,
    ecs::Res<render::phase::ViewBinnedRenderPhases<Opaque3d>> opaque_phases,
    ecs::Res<render::phase::ViewBinnedRenderPhases<AlphaMask3d>> alpha_mask_phases,
    ecs::Res<render::phase::ViewSortedRenderPhases<Transmissive3d>> transmissive_phases,
    ecs::Res<render::phase::ViewSortedRenderPhases<Transparent3d>> transparent_phases,
    ecs::Query<ecs::Item<ecs::Entity,
                         const render::camera::ExtractedCamera&,
                         const ::epix::camera::Camera3d&,
                         const render::view::ExtractedView&>> views);

}  // namespace epix::core_graph::core_3d

template <>
struct std::hash<epix::core_graph::core_3d::OpaqueNoLightmap3dBatchSetKey> {
    std::size_t operator()(const epix::core_graph::core_3d::OpaqueNoLightmap3dBatchSetKey& key) const noexcept {
        std::size_t hash   = std::hash<epix::render::CachedPipelineId>{}(key.pipeline_id);
        const auto combine = [&hash](std::size_t value) { hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2); };
        combine(std::hash<epix::render::phase::DrawFunctionId>{}(key.draw_func));
        if (key.material_bind_group_index) combine(std::hash<std::uint32_t>{}(*key.material_bind_group_index));
        combine(std::hash<epix::render::mesh::SlabId>{}(key.vertex_slab));
        if (key.index_slab) combine(std::hash<epix::render::mesh::SlabId>{}(*key.index_slab));
        return hash;
    }
};

template <>
struct std::hash<epix::core_graph::core_3d::OpaqueNoLightmap3dBinKey> {
    std::size_t operator()(const epix::core_graph::core_3d::OpaqueNoLightmap3dBinKey& key) const noexcept {
        return std::hash<epix::assets::UntypedAssetId>{}(key.asset_id);
    }
};

template <>
struct std::hash<epix::core_graph::core_3d::Opaque3dBatchSetKey> {
    std::size_t operator()(const epix::core_graph::core_3d::Opaque3dBatchSetKey& key) const noexcept {
        std::size_t hash   = std::hash<epix::render::CachedPipelineId>{}(key.pipeline_id);
        const auto combine = [&hash](std::size_t value) { hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2); };
        combine(std::hash<epix::render::phase::DrawFunctionId>{}(key.draw_func));
        if (key.material_bind_group_index) combine(std::hash<std::uint32_t>{}(*key.material_bind_group_index));
        combine(std::hash<epix::render::mesh::SlabId>{}(key.vertex_slab));
        if (key.index_slab) combine(std::hash<epix::render::mesh::SlabId>{}(*key.index_slab));
        if (key.lightmap_slab) combine(std::hash<std::uint32_t>{}(*key.lightmap_slab));
        return hash;
    }
};

template <>
struct std::hash<epix::core_graph::core_3d::Opaque3dBinKey> {
    std::size_t operator()(const epix::core_graph::core_3d::Opaque3dBinKey& key) const noexcept {
        return std::hash<epix::assets::UntypedAssetId>{}(key.asset_id);
    }
};
