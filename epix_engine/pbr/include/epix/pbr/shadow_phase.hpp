#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <optional>
#include <utility>
#include <epix/common.hpp>
#endif

namespace epix::pbr {

/** @brief Information that must be identical to place items in the same batch
 *  set in the shadow map phase (Bevy `ShadowBatchSetKey`,
 *  `bevy_pbr/src/render/light.rs:2100-2123`).
 *
 *  The same fields as the opaque 3D key minus the lightmap: shadow-casting
 *  geometry is not lit, so there is nothing lightmap-specific to separate. */
EPIX_EXPORT struct ShadowBatchSetKey {
    /** @brief The identifier of the render pipeline. */
    render::CachedPipelineId pipeline_id;
    /** @brief The function used to draw. */
    render::phase::DrawFunctionId draw_func;
    /** @brief The ID of a bind group specific to the material instance. */
    std::optional<std::uint32_t> material_bind_group_index;
    /** @brief The ID of the slab of GPU memory that contains vertex data. For
     *  non-mesh items, fill this with 0 if your items can be multi-drawn, or
     *  with a unique value if they cannot. */
    render::mesh::SlabId vertex_slab{};
    /** @brief The ID of the slab of GPU memory that contains index data, if
     *  present. For non-mesh items this is empty. */
    std::optional<render::mesh::SlabId> index_slab;

    /** @brief Whether this batch set is indexed, which selects the indexed or
     *  non-indexed indirect-command layout. */
    bool indexed() const noexcept { return index_slab.has_value(); }
    auto operator<=>(const ShadowBatchSetKey&) const = default;
};

/** @brief Data that must be identical to *bin* shadow map items together (Bevy
 *  `ShadowBinKey`, `render/light.rs:2131-2136`): the asset, normally a mesh. */
EPIX_EXPORT struct ShadowBinKey {
    /** @brief The asset that this phase item is associated with. Normally the
     *  mesh ID, but for non-mesh items another asset type. */
    assets::UntypedAssetId asset_id;
    auto operator<=>(const ShadowBinKey&) const = default;
};

/** @brief A binned shadow map phase item (Bevy `Shadow`,
 *  `render/light.rs:2082-2093`).
 *
 *  One item per shadow-casting object per shadow view, where a view is a
 *  directional light's cascade or one face of a point light's cubemap. Its keys
 *  are what group those items into bins and batch sets. */
EPIX_EXPORT struct Shadow {
    std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity;
    ShadowBatchSetKey batch_set_key_value;
    ShadowBinKey bin_key_value;
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

    using BinKey      = ShadowBinKey;
    using BatchSetKey = ShadowBatchSetKey;

    static Shadow create(BatchSetKey batch_set_key, BinKey bin_key,
                         std::pair<ecs::Entity, render::sync_world::MainEntity> representative_entity,
                         std::pair<std::uint32_t, std::uint32_t> batch_range,
                         render::phase::PhaseItemExtraIndex extra_index) {
        return {std::move(representative_entity), std::move(batch_set_key), std::move(bin_key), batch_range,
                extra_index};
    }
};
static_assert(render::phase::PhaseItem<Shadow>);
static_assert(render::phase::CachedRenderPipelinePhaseItem<Shadow>);
static_assert(render::phase::PhaseItemBatchSetKey<ShadowBatchSetKey>);

}  // namespace epix::pbr
