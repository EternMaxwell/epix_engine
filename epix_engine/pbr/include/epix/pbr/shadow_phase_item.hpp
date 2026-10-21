#pragma once

#ifndef EPIX_CXX_MODULE
#include <cstddef>
#include <cstdint>
#include <epix/assets.hpp>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/render.hpp>
#include <functional>
#include <optional>
#include <utility>
#endif

namespace epix::pbr {

/** @brief Batch-set identity for shadow casters (Bevy ShadowBatchSetKey,
 * render/light.rs:2100-2123). Unlike opaque batches this has no lightmap. */
EPIX_EXPORT struct ShadowBatchSetKey {
    render::CachedPipelineId pipeline_id;
    render::phase::DrawFunctionId draw_func;
    std::optional<std::uint32_t> material_bind_group_index;
    render::mesh::SlabId vertex_slab{};
    std::optional<render::mesh::SlabId> index_slab;

    bool indexed() const noexcept { return index_slab.has_value(); }
    auto operator<=>(const ShadowBatchSetKey&) const = default;
};

/** @brief Asset identity within a shadow batch set (Bevy ShadowBinKey). */
EPIX_EXPORT struct ShadowBinKey {
    assets::UntypedAssetId asset_id;
    auto operator<=>(const ShadowBinKey&) const = default;
};

/** @brief A binned shadow-casting object (Bevy Shadow, render/light.rs:2082-2093).
 * Kept independent of light preparation so its phase resources can appear in
 * system declarations without a light-meta/shadow-helper include cycle. */
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

    using BinKey = ShadowBinKey;
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

template <>
struct std::hash<epix::pbr::ShadowBatchSetKey> {
    std::size_t operator()(const epix::pbr::ShadowBatchSetKey& key) const noexcept {
        std::size_t hash = std::hash<epix::render::CachedPipelineId>{}(key.pipeline_id);
        const auto combine = [&hash](std::size_t value) {
            hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        };
        combine(std::hash<epix::render::phase::DrawFunctionId>{}(key.draw_func));
        if (key.material_bind_group_index) combine(std::hash<std::uint32_t>{}(*key.material_bind_group_index));
        combine(std::hash<epix::render::mesh::SlabId>{}(key.vertex_slab));
        if (key.index_slab) combine(std::hash<epix::render::mesh::SlabId>{}(*key.index_slab));
        return hash;
    }
};

template <>
struct std::hash<epix::pbr::ShadowBinKey> {
    std::size_t operator()(const epix::pbr::ShadowBinKey& key) const noexcept {
        return std::hash<epix::assets::UntypedAssetId>{}(key.asset_id);
    }
};
