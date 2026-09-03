#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string_view>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <epix/transform.hpp>
#include <utility>
#include <webgpu/webgpu.hpp>
#endif

namespace epix::core_graph::core_2d {

/** @brief Names exposed by Bevy's `core_2d::graph::input` module. */
namespace input {
inline constexpr std::string_view ViewEntity = "view_entity";
}

/** @brief Node labels for the 2D render graph passes. */
EPIX_EXPORT enum class Core2dNodes {
    /** @brief Optional MSAA resolve pass supplied by the MSAA plugin. */
    MsaaWriteback,
    /** @brief Node that begins the main render pass. */
    StartMainPass,
    /** @brief Node for rendering opaque 2D items. */
    MainOpaquePass,
    /** @brief Node for rendering transparent 2D items. */
    MainTransparentPass,
    /** @brief Node that ends the main render pass. */
    EndMainPass,
    /** @brief Optional wireframe pass supplied by the wireframe plugin. */
    Wireframe,
    /** @brief Start of the extensible post-processing segment. */
    StartMainPassPostProcessing,
    /** @brief Optional bloom pass supplied by the bloom plugin. */
    Bloom,
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

/**
 * @brief A transparent 2D render phase item.
 *
 * Sorted by inverse depth for back-to-front rendering. Supports instanced
 * batching.
 */
EPIX_EXPORT struct Transparent2D {
    /** @brief The render entity and its main-world entity (Bevy
     * representative_entity: (Entity, MainEntity)). */
    std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity;
    /** @brief Depth value for sorting (inverted for back-to-front). */
    float depth;
    /** @brief Cached render pipeline ID. */
    render::CachedPipelineId pipeline_id;
    /** @brief Draw function ID for rendering this item. */
    render::phase::DrawFunctionId draw_func;
    /** @brief Instance range covered by this item's batch (Bevy batch_range:
     * Range<u32>). */
    std::pair<std::uint32_t, std::uint32_t> batch_range_value;
    /** @brief Dynamic-offset or indirect-parameter index assigned while batching. */
    render::phase::PhaseItemExtraIndex extra_index_value{};
    /** @brief Whether this item draws with a mesh index buffer (Bevy
     * `Transparent2d::indexed`). */
    bool indexed_value = false;

    ecs::Entity entity() const noexcept { return representative_entity.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return representative_entity.second; }
    float sort_key() const noexcept { return -depth; }  // inverse depth for back-to-front rendering
    render::phase::DrawFunctionId draw_function() const noexcept { return draw_func; }
    render::CachedPipelineId cached_pipeline() const noexcept { return pipeline_id; }
    bool indexed() const noexcept { return indexed_value; }
    const std::pair<std::uint32_t, std::uint32_t>& batch_range() const noexcept { return batch_range_value; }
    std::pair<std::uint32_t, std::uint32_t>& batch_range() noexcept { return batch_range_value; }
    const render::phase::PhaseItemExtraIndex& extra_index() const noexcept { return extra_index_value; }
    render::phase::PhaseItemExtraIndex& extra_index() noexcept { return extra_index_value; }
};
static_assert(render::phase::CachedRenderPipelinePhaseItem<Transparent2D>);
static_assert(render::phase::SortedPhaseItem<Transparent2D>);

/** @brief Batch-set key for Bevy Core2D binned meshes. Core2D currently
 * does not multi-draw meshes, but the indexed discriminator is still part of
 * its public phase contract. */
EPIX_EXPORT struct BatchSetKey2D {
    bool indexed_value = false;
    bool indexed() const noexcept { return indexed_value; }
    auto operator<=>(const BatchSetKey2D&) const noexcept = default;
};

/** @brief Fields shared by Bevy's opaque and alpha-mask Core2D bin keys.
 * Direct wgpu has no `BindGroupId`; image/material asset identity is the
 * stable Epix equivalent used for the material-bind-group key. */
EPIX_EXPORT struct Opaque2DBinKey {
    render::CachedPipelineId pipeline_id;
    render::phase::DrawFunctionId draw_func;
    assets::UntypedAssetId asset_id;
    std::optional<assets::UntypedAssetId> material_bind_group_id;
    auto operator<=>(const Opaque2DBinKey&) const = default;
};

/** @brief Bin key for alpha-masked Core2D meshes. Kept distinct from
 * Opaque2DBinKey exactly as Bevy keeps the two phase types distinct. */
EPIX_EXPORT struct AlphaMask2DBinKey {
    render::CachedPipelineId pipeline_id;
    render::phase::DrawFunctionId draw_func;
    assets::UntypedAssetId asset_id;
    std::optional<assets::UntypedAssetId> material_bind_group_id;
    auto operator<=>(const AlphaMask2DBinKey&) const = default;
};

/** @brief A binned opaque 2D render phase item (Bevy `Opaque2d`). */
EPIX_EXPORT struct Opaque2D {
    std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity;
    BatchSetKey2D batch_set_key_value;
    Opaque2DBinKey bin_key_value;
    std::pair<std::uint32_t, std::uint32_t> batch_range_value;
    render::phase::PhaseItemExtraIndex extra_index_value{};

    ecs::Entity entity() const noexcept { return representative_entity.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return representative_entity.second; }
    render::phase::DrawFunctionId draw_function() const noexcept { return bin_key_value.draw_func; }
    render::CachedPipelineId cached_pipeline() const noexcept { return bin_key_value.pipeline_id; }
    const std::pair<std::uint32_t, std::uint32_t>& batch_range() const noexcept { return batch_range_value; }
    std::pair<std::uint32_t, std::uint32_t>& batch_range() noexcept { return batch_range_value; }
    const render::phase::PhaseItemExtraIndex& extra_index() const noexcept { return extra_index_value; }
    render::phase::PhaseItemExtraIndex& extra_index() noexcept { return extra_index_value; }
    using BinKey      = Opaque2DBinKey;
    using BatchSetKey = BatchSetKey2D;
    static Opaque2D create(BatchSetKey batch_set_key,
                           BinKey bin_key,
                           std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity,
                           std::pair<std::uint32_t, std::uint32_t> batch_range,
                           render::phase::PhaseItemExtraIndex extra_index) {
        return {std::move(representative_entity), std::move(batch_set_key), std::move(bin_key),
                batch_range, extra_index};
    }
};
static_assert(render::phase::CachedRenderPipelinePhaseItem<Opaque2D>);
static_assert(render::phase::BinnedPhaseItem<Opaque2D>);

/** @brief A binned alpha-mask 2D render phase item (Bevy `AlphaMask2d`). */
EPIX_EXPORT struct AlphaMask2D {
    std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity;
    BatchSetKey2D batch_set_key_value;
    AlphaMask2DBinKey bin_key_value;
    std::pair<std::uint32_t, std::uint32_t> batch_range_value;
    render::phase::PhaseItemExtraIndex extra_index_value{};

    ecs::Entity entity() const noexcept { return representative_entity.first; }
    render::sync_world::MainEntity main_entity() const noexcept { return representative_entity.second; }
    render::phase::DrawFunctionId draw_function() const noexcept { return bin_key_value.draw_func; }
    render::CachedPipelineId cached_pipeline() const noexcept { return bin_key_value.pipeline_id; }
    const std::pair<std::uint32_t, std::uint32_t>& batch_range() const noexcept { return batch_range_value; }
    std::pair<std::uint32_t, std::uint32_t>& batch_range() noexcept { return batch_range_value; }
    const render::phase::PhaseItemExtraIndex& extra_index() const noexcept { return extra_index_value; }
    render::phase::PhaseItemExtraIndex& extra_index() noexcept { return extra_index_value; }
    using BinKey      = AlphaMask2DBinKey;
    using BatchSetKey = BatchSetKey2D;
    static AlphaMask2D create(BatchSetKey batch_set_key,
                              BinKey bin_key,
                              std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity,
                              std::pair<std::uint32_t, std::uint32_t> batch_range,
                              render::phase::PhaseItemExtraIndex extra_index) {
        return {std::move(representative_entity), std::move(batch_set_key), std::move(bin_key),
                batch_range, extra_index};
    }
};
static_assert(render::phase::CachedRenderPipelinePhaseItem<AlphaMask2D>);
static_assert(render::phase::BinnedPhaseItem<AlphaMask2D>);

/** @brief Bevy `MainOpaquePass2dNode`: renders opaque and alpha-mask binned
 * phases together in one deferred command buffer. */
EPIX_EXPORT struct MainOpaquePass2DNode {
    using ViewQuery = ecs::Item<const render::camera::ExtractedCamera&,
                                const render::view::ExtractedView&,
                                const render::view::ViewTarget&,
                                const render::view::ViewDepthTexture&>;
    void update(ecs::World&) {}
    std::expected<void, render::graph::NodeRunError> run(
        render::graph::GraphContext&,
        render::graph::RenderContext& render_context,
        typename ecs::QueryData<ViewQuery>::Item view,
        const ecs::World& world) const;
};

/** @brief Bevy `MainTransparentPass2dNode`: renders the retained sorted
 * transparent phase in an independent deferred command buffer. */
EPIX_EXPORT struct MainTransparentPass2DNode {
    using ViewQuery = MainOpaquePass2DNode::ViewQuery;
    void update(ecs::World&) {}
    std::expected<void, render::graph::NodeRunError> run(
        render::graph::GraphContext&,
        render::graph::RenderContext& render_context,
        typename ecs::QueryData<ViewQuery>::Item view,
        const ecs::World& world) const;
};

/** @brief Singleton struct for initializing the core 2D render graph. */
EPIX_EXPORT inline struct Core2dGraph {
    /** @brief Add this graph as a sub-graph to the given render graph. */
    void add_to(render::graph::RenderGraph& g, ecs::World& world);
} Core2d;

/** @brief Plugin that sets up the core 2D render graph and camera
 * projection. */
EPIX_EXPORT struct Core2dPlugin {
    void attach(app::App& app);
};

/** @brief Prepare depth attachments for live Core2D views (Bevy
 * `prepare_core_2d_depth_textures`). The general view plugin deliberately
 * does not allocate depth for arbitrary cameras. */
void prepare_core_2d_depth_textures(
    ecs::Commands cmd,
    ecs::ResMut<render::render_resource::TextureCache> texture_cache,
    ecs::Res<wgpu::Device> device,
    ecs::Res<render::phase::ViewSortedRenderPhases<Transparent2D>> transparent_phases,
    ecs::Res<render::phase::ViewBinnedRenderPhases<Opaque2D>> opaque_phases,
    ecs::Query<ecs::Item<ecs::Entity,
                         const render::camera::ExtractedCamera&,
                         const render::view::ExtractedView&,
                         const render::view::Msaa&>> views);

}  // namespace epix::core_graph::core_2d

template <>
struct std::hash<epix::core_graph::core_2d::BatchSetKey2D> {
    std::size_t operator()(const epix::core_graph::core_2d::BatchSetKey2D& key) const noexcept {
        return std::hash<bool>{}(key.indexed_value);
    }
};

template <typename Key>
inline std::size_t epix_core2d_bin_key_hash(const Key& key) noexcept {
    std::size_t hash = std::hash<epix::render::CachedPipelineId>{}(key.pipeline_id);
    const auto combine = [&hash](std::size_t value) { hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2); };
    combine(std::hash<epix::render::phase::DrawFunctionId>{}(key.draw_func));
    combine(std::hash<epix::assets::UntypedAssetId>{}(key.asset_id));
    if (key.material_bind_group_id) combine(std::hash<epix::assets::UntypedAssetId>{}(*key.material_bind_group_id));
    return hash;
}

template <>
struct std::hash<epix::core_graph::core_2d::Opaque2DBinKey> {
    std::size_t operator()(const epix::core_graph::core_2d::Opaque2DBinKey& key) const noexcept {
        return epix_core2d_bin_key_hash(key);
    }
};

template <>
struct std::hash<epix::core_graph::core_2d::AlphaMask2DBinKey> {
    std::size_t operator()(const epix::core_graph::core_2d::AlphaMask2DBinKey& key) const noexcept {
        return epix_core2d_bin_key_hash(key);
    }
};
