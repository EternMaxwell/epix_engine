#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <cstdint>
#include <epix/app.hpp>
#include <epix/ecs.hpp>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <webgpu/webgpu.hpp>
#endif

#include <epix/render/render_resource.hpp>
#include <epix/render/sync_world.hpp>

namespace epix::render::view {
/** @brief Storage buffer slot reserved for visibility ranges (Bevy const). */
EPIX_EXPORT inline constexpr std::uint32_t VISIBILITY_RANGES_STORAGE_BUFFER_COUNT = 4;
/** @brief Size in elements of the visibility-ranges buffer when fewer than
 * VISIBILITY_RANGES_STORAGE_BUFFER_COUNT storage buffers are available and a
 * uniform buffer must be used instead (WebGL2; Bevy const). */
EPIX_EXPORT inline constexpr std::size_t VISIBILITY_RANGE_UNIFORM_BUFFER_SIZE = 64;

/**
 * @brief Component defining a distance range in which an entity is visible
 * (cross-crate: bevy_camera::visibility::VisibilityRange; epix keeps the data
 * here in the render module).
 */
EPIX_EXPORT struct VisibilityRange {
    /** @brief Start of the crossfade margin into the visible range. */
    float start_margin_start = 0.0f;
    /** @brief End of the start crossfade margin. */
    float start_margin_end = 0.0f;
    /** @brief Start of the end crossfade margin. */
    float end_margin_start = 0.0f;
    /** @brief End of the visible range. */
    float end_margin_end                          = 0.0f;
    bool operator==(const VisibilityRange&) const = default;
    /** @brief Whether the start margin is abrupt (no crossfade). */
    bool abrupt_start_margin = false;
    /** @brief Whether the end margin is abrupt (no crossfade). */
    bool abrupt_end_margin = false;

    /** @brief True if both transitions are abrupt, i.e. no crossfade (Bevy
     * VisibilityRange::is_abrupt). */
    bool is_abrupt() const noexcept {
        return start_margin_start == start_margin_end && end_margin_start == end_margin_end;
    }
};

}  // namespace epix::render::view

template <>
struct std::hash<epix::render::view::VisibilityRange> {
    std::size_t operator()(const epix::render::view::VisibilityRange& r) const noexcept {
        std::size_t h = std::hash<float>{}(r.start_margin_start);
        h ^= std::hash<float>{}(r.start_margin_end) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(r.end_margin_start) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(r.end_margin_end) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(r.abrupt_start_margin) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(r.abrupt_end_margin) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

namespace epix::render::view {
namespace detail {
struct RenderVisibilityEntityInfo {
    std::uint16_t buffer_index = 0;
    bool is_abrupt             = false;
};
}  // namespace detail

/**
 * @brief Stores per-entity visibility-range info and the GPU buffer holding
 * the ranges (Bevy RenderVisibilityRanges). Each entry is a vec4 of (start
 * margin start, start margin end, end margin start, end margin end).
 */
EPIX_EXPORT struct RenderVisibilityRanges {
    /** @brief Per-entity range info. */
    sync_world::MainEntityHashMap<detail::RenderVisibilityEntityInfo> entities;
    /** @brief Range-to-index dedup map. */
    std::unordered_map<VisibilityRange, std::uint16_t> range_to_index;
    /** @brief GPU buffer of range vec4s (Bevy usages: STORAGE|UNIFORM|VERTEX). */
    render_resource::BufferVec<glm::vec4> buffer{wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eUniform |
                                                 wgpu::BufferUsage::eVertex | wgpu::BufferUsage::eCopyDst};
    /** @brief True when the buffer needs re-upload (Bevy default: true). */
    bool buffer_dirty = true;

    /** @brief Clears the per-entity ranges in preparation for a new frame
     * (Bevy RenderVisibilityRanges::clear). The range index map and GPU buffer
     * are KEPT: range indices must stay stable for the app lifetime because
     * GPU-driven consumers bake them into per-entity data at extraction
     * (bevy range.rs:60-70, 106-146). */
    void clear() noexcept { entities.clear(); }

    /** @brief Inserts an entity's range, deduplicating identical ranges into
     * one GPU slot (Bevy RenderVisibilityRanges::insert). */
    void insert(sync_world::MainEntity entity, const VisibilityRange& visibility_range) {
        std::uint16_t buffer_index = 0;
        if (auto it = range_to_index.find(visibility_range); it != range_to_index.end()) {
            buffer_index = it->second;
        } else {
            // Indices are assigned from range_to_index.size() and never reused
            // (Bevy range.rs:115-146); the buffer slot at that index holds the
            // range vec4 and is stable for the app lifetime.
            if (range_to_index.size() >= std::numeric_limits<std::uint16_t>::max()) {
                spdlog::warn("[render] Too many distinct visibility ranges; index truncated.");
            }
            buffer_index = static_cast<std::uint16_t>(range_to_index.size());
            buffer.push(glm::vec4(visibility_range.start_margin_start, visibility_range.start_margin_end,
                                  visibility_range.end_margin_start, visibility_range.end_margin_end));
            range_to_index.emplace(visibility_range, buffer_index);
            buffer_dirty = true;
        }
        entities.emplace(entity.entity, detail::RenderVisibilityEntityInfo{buffer_index, visibility_range.is_abrupt()});
    }

    /** @brief GPU buffer index of the entity's visible range, if any (Bevy
     * lod_index_for_entity). */
    std::optional<std::uint16_t> lod_index_for_entity(sync_world::MainEntity entity) const {
        if (auto it = entities.find(entity.entity); it != entities.end()) return it->second.buffer_index;
        return std::nullopt;
    }

    /** @brief True if the entity has a visibility range and it isn't abrupt,
     * i.e. it has a crossfade (Bevy entity_has_crossfading_visibility_ranges). */
    bool entity_has_crossfading_visibility_ranges(sync_world::MainEntity entity) const {
        if (auto it = entities.find(entity.entity); it != entities.end()) return !it->second.is_abrupt;
        return false;
    }

    /** @brief The GPU buffer of range vec4s (Bevy RenderVisibilityRanges::buffer). */
    const render_resource::BufferVec<glm::vec4>& buffer_ref() const noexcept { return buffer; }
};

/** @brief Extracts all VisibilityRange components from the main world into
 * RenderVisibilityRanges (Bevy extract_visibility_ranges). Early-outs when no
 * range changed and nothing was removed. */
inline void extract_visibility_ranges(
    ecs::ResMut<RenderVisibilityRanges> render_visibility_ranges,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity, const VisibilityRange&>>> visibility_ranges_query,
    app::Extract<ecs::Query<ecs::Item<ecs::Entity>,
                            ecs::Or<ecs::Added<VisibilityRange>, ecs::Modified<VisibilityRange>>>> changed_ranges_query,
    app::Extract<ecs::RemovedComponents<VisibilityRange>> removed_visibility_ranges) {
    auto changed        = changed_ranges_query.iter();
    auto removed_reader = removed_visibility_ranges.read();
    if (changed.begin() == changed.end() && removed_reader.begin() == removed_reader.end()) {
        return;
    }
    render_visibility_ranges->clear();
    for (auto&& [entity, visibility_range] : visibility_ranges_query.iter()) {
        render_visibility_ranges->insert(sync_world::MainEntity{entity}, visibility_range);
    }
}

namespace detail {
/**
 * @brief Writes visibility ranges into the GPU buffer (Bevy
 * write_render_visibility_ranges).
 */
inline void write_render_visibility_ranges(ecs::ResMut<RenderVisibilityRanges> ranges,
                                           ecs::Res<wgpu::Device> device,
                                           ecs::Res<wgpu::Queue> queue) {
    if (!ranges->buffer_dirty) return;
    // Bevy range.rs:184-227: a uniform-buffer fallback (WebGL2) must have
    // exactly VISIBILITY_RANGE_UNIFORM_BUFFER_SIZE elements; the storage-buffer
    // path (our backend) just ensures the buffer is non-empty so it allocates.
    if (ranges->buffer.is_empty()) {
        ranges->buffer.push(glm::vec4(0.0f));
    }
    ranges->buffer.write_buffer(device.get(), queue.get());
    ranges->buffer_dirty = false;
}
}  // namespace detail

/**
 * @brief Plugin that enables RenderVisibilityRanges (Bevy
 * RenderVisibilityRangePlugin).
 */
EPIX_EXPORT struct RenderVisibilityRangePlugin {
    void attach(app::App& app);
};

}  // namespace epix::render::view
