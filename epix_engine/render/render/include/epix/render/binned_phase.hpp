#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <algorithm>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/meta.hpp>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#endif

#include <epix/render/gpu_preprocessing_mode.hpp>
#include <epix/render/render_debug.hpp>
#include <epix/render/render_phase.hpp>
#include <epix/render/schedule.hpp>
#include <epix/render/sync_world.hpp>
#include <epix/render/view.hpp>

namespace epix::render::phase {

/** @brief Reconstruct the extra index passed to a multidraw item (Bevy
 * `BinnedRenderPhase::render`). A GPU-generated
 * command count is meaningful
 * only when the device exposes multi-draw-indirect-count; wgpu's DX12 backend
 * is
 * excluded to mirror Bevy's driver workaround. */
EPIX_EXPORT constexpr PhaseItemExtraIndex multidraw_extra_index(PhaseItemExtraIndex extra_index,
                                                                std::uint32_t batch_count,
                                                                bool multi_draw_indirect_count_supported,
                                                                std::uint32_t batch_set_index) noexcept {
    if (extra_index.type != PhaseItemExtraIndex::Type::IndirectParametersIndex) return extra_index;
    extra_index.indirect_range.second = extra_index.indirect_range.first + batch_count;
    extra_index.batch_set_index = multi_draw_indirect_count_supported ? std::optional{batch_set_index} : std::nullopt;
    return extra_index;
}

/**
 * @brief Map with insertion-ordered iteration and O(1) lookup (Bevy
 * `indexmap::IndexMap`). Used by the binned phase machinery so that bin and
 * instance ordering is deterministic.
 * @tparam K Key type (must be hashable and equality-comparable).
 * @tparam V Value type.
 */
EPIX_EXPORT template <typename K, typename V>
class IndexMap {
   public:
    using value_type = std::pair<K, V>;

    V& operator[](const K& key) {
        if (auto it = m_indices.find(key); it != m_indices.end()) {
            return m_entries[it->second].second;
        }
        const std::size_t idx = m_entries.size();
        m_indices.emplace(key, idx);
        m_entries.emplace_back(key, V{});
        return m_entries.back().second;
    }
    V* get(const K& key) {
        if (auto it = m_indices.find(key); it != m_indices.end()) {
            return &m_entries[it->second].second;
        }
        return nullptr;
    }
    const V* get(const K& key) const {
        if (auto it = m_indices.find(key); it != m_indices.end()) {
            return &m_entries[it->second].second;
        }
        return nullptr;
    }
    bool contains(const K& key) const { return m_indices.contains(key); }
    bool empty() const noexcept { return m_entries.empty(); }
    std::size_t size() const noexcept { return m_entries.size(); }
    void clear() noexcept {
        m_entries.clear();
        m_indices.clear();
    }
    /** @brief Remove the entry with the given key, preserving order of the
     * remaining entries. Returns true if an entry was removed. */
    bool remove(const K& key) {
        auto it = m_indices.find(key);
        if (it == m_indices.end()) {
            return false;
        }
        const std::size_t idx = it->second;
        m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(idx));
        m_indices.erase(it);
        // fix up indices after the removed position
        for (auto& [k, i] : m_indices) {
            (void)k;
            if (i > idx) {
                --i;
            }
        }
        return true;
    }
    /** @brief Position of the key in the insertion order, if present. */
    std::optional<std::size_t> index_of(const K& key) const {
        if (auto it = m_indices.find(key); it != m_indices.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    /** @brief Sort entries by key (Bevy `IndexMap::sort_unstable_keys`). */
    void sort_unstable_keys()
        requires std::totally_ordered<K>
    {
        std::sort(m_entries.begin(), m_entries.end(),
                  [](const value_type& lhs, const value_type& rhs) { return lhs.first < rhs.first; });
        m_indices.clear();
        for (std::size_t index = 0; index < m_entries.size(); ++index) {
            m_indices.emplace(m_entries[index].first, index);
        }
    }
    /** @brief Remove the entry at the given insertion position, preserving the
     * order of the rest. Returns the removed pair. */
    value_type remove_at(std::size_t idx) {
        value_type removed = std::move(m_entries[idx]);
        m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(idx));
        m_indices.erase(removed.first);
        for (auto& [k, i] : m_indices) {
            (void)k;
            if (i > idx) {
                --i;
            }
        }
        return removed;
    }
    /** @brief Iterate entries in insertion order. */
    auto begin() { return m_entries.begin(); }
    auto end() { return m_entries.end(); }
    auto begin() const { return m_entries.begin(); }
    auto end() const { return m_entries.end(); }
    auto iter() { return std::views::all(m_entries); }
    auto iter() const { return std::views::all(m_entries); }

   private:
    std::vector<value_type> m_entries;
    std::unordered_map<K, std::size_t> m_indices;
};

/**
 * @brief The index of the uniform describing an object in the GPU buffer
 * (Bevy `InputUniformIndex`).
 */
EPIX_EXPORT struct InputUniformIndex {
    /** @brief The buffer index. */
    std::uint32_t index                             = 0;
    bool operator==(const InputUniformIndex&) const = default;
};

/** @brief One CPU-prepared draw within a binned render bin (Bevy
 * `BinnedRenderPhaseBatch`). A bin can split into
 * several draws when the
 * dynamic-uniform fallback changes offset. */
EPIX_EXPORT struct BinnedRenderPhaseBatch {
    sync_world::MainEntity representative_entity;
    std::pair<std::uint32_t, std::uint32_t> instance_range{0, 0};
    PhaseItemExtraIndex extra_index{};
};

/** @brief A group of binned batches submitted through one multi-draw indirect
 * command (Bevy
 * `BinnedRenderPhaseBatchSet`). */
template <typename BK>
struct BinnedRenderPhaseBatchSet {
    BinnedRenderPhaseBatch first_batch;
    BK bin_key;
    std::uint32_t batch_count = 0;
    std::uint32_t index       = 0;
};

/**
 * @brief The draw-batch layout chosen for a binned phase (Bevy
 * `BinnedRenderPhaseBatchSets`).
 *
 * `None` uses
 * dynamic-uniform batches, `PreprocessingOnly` emits direct
 * batches, and `Culling` groups them for indirect
 * multi-draw.  Concrete GPU
 * preprocessing consumers populate these containers during preparation.
 */
template <typename BK>
using BinnedRenderPhaseBatchSets = std::variant<std::vector<std::vector<BinnedRenderPhaseBatch>>,
                                                std::vector<BinnedRenderPhaseBatch>,
                                                std::vector<BinnedRenderPhaseBatchSet<BK>>>;

/**
 * @brief All entities that share a mesh and a material and can be batched as
 * part of a `BinnedRenderPhase` (Bevy `RenderBin`).
 */
EPIX_EXPORT class RenderBin {
   public:
    /** @brief CPU-prepared draw batches, rebuilt every frame. */
    std::vector<BinnedRenderPhaseBatch> batches;

    /** @brief Insert an entity (main-world id) with its input uniform index.
     * Replaces any existing entry for the same entity. */
    void insert(epix::ecs::Entity main_entity, InputUniformIndex uniform_index) {
        if (auto existing = m_indices.find(main_entity); existing != m_indices.end()) {
            m_entries[existing->second].second = uniform_index;
            return;
        }
        const std::size_t idx = m_entries.size();
        m_indices.emplace(main_entity, idx);
        m_entries.emplace_back(main_entity, uniform_index);
    }
    /** @brief Remove an entity, preserving order of the rest. */
    bool remove(epix::ecs::Entity main_entity) {
        auto it = m_indices.find(main_entity);
        if (it == m_indices.end()) {
            return false;
        }
        const std::size_t idx = it->second;
        m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(idx));
        m_indices.erase(it);
        for (auto& [k, i] : m_indices) {
            (void)k;
            if (i > idx) {
                --i;
            }
        }
        return true;
    }
    /** @brief Whether the entity is in this bin. */
    bool contains(epix::ecs::Entity main_entity) const { return m_indices.contains(main_entity); }
    /** @brief Get the input uniform index of an entity. */
    InputUniformIndex* get(epix::ecs::Entity main_entity) {
        if (auto it = m_indices.find(main_entity); it != m_indices.end()) {
            return &m_entries[it->second].second;
        }
        return nullptr;
    }
    /** @brief Number of entities in the bin. */
    std::size_t size() const noexcept { return m_entries.size(); }
    bool empty() const noexcept { return m_entries.empty(); }
    /** @brief Iterate (main entity, uniform index) pairs in insertion order
     * (this order determines instance indices). */
    auto iter() { return std::views::all(m_entries); }
    auto iter() const { return std::views::all(m_entries); }
    void clear_batches() noexcept { batches.clear(); }

   private:
    std::vector<std::pair<epix::ecs::Entity, InputUniformIndex>> m_entries;
    std::unordered_map<epix::ecs::Entity, std::size_t> m_indices;
};

/** @brief The unbatchable entities in a bin (Bevy `UnbatchableBinnedEntities`);
 * storage-buffer path: entities plus a contiguous instance range. */
EPIX_EXPORT struct UnbatchableBinnedEntities {
    /** @brief main entity -> render entity. */
    sync_world::MainEntityHashMap<epix::ecs::Entity> entities;
    /** @brief Instance index range [start, end) of this bin's entities. */
    std::optional<std::pair<std::uint32_t, std::uint32_t>> instance_range;
    /** @brief Per-entity CPU-prepared range and dynamic/indirect index. */
    std::unordered_map<epix::ecs::Entity, BinnedRenderPhaseBatch> batches;
    bool empty() const noexcept { return entities.empty(); }
};

/** @brief Non-mesh items in a bin (Bevy `NonMeshEntities`). */
EPIX_EXPORT struct NonMeshEntities {
    /** @brief main entity -> render entity. */
    sync_world::MainEntityHashMap<epix::ecs::Entity> entities;
    bool empty() const noexcept { return entities.empty(); }
};
/**
 * @brief Hashable pair used as the composite (batch set, bin) key (Bevy
 * IndexMap<(BatchSetKey, BinKey)>; std::pair is not hashable).
 */
template <typename A, typename B>
struct BinKeyPair {
    /** @brief First component (batch set key). */
    A first;
    /** @brief Second component (bin key). */
    B second;
    bool operator==(const BinKeyPair&) const  = default;
    auto operator<=>(const BinKeyPair&) const = default;
};

template <typename A, typename B>
struct std::hash<::epix::render::phase::BinKeyPair<A, B>> {
    std::size_t operator()(const ::epix::render::phase::BinKeyPair<A, B>& p) const noexcept {
        std::size_t h = std::hash<A>{}(p.first);
        h ^= std::hash<B>{}(p.second) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

/**
 * @brief Identifies the list within `BinnedRenderPhase` that a phase item is
 * placed in (Bevy `BinnedRenderPhaseType`).
 */
EPIX_EXPORT enum class BinnedRenderPhaseType {
    /** @brief A mesh eligible for multi-draw indirect; batched with other
     * meshes of the same type. */
    MultidrawableMesh,
    /** @brief A mesh batchable with other meshes of the same type in a single
     * draw call. */
    BatchableMesh,
    /** @brief A mesh rendered in a separate draw call. */
    UnbatchableMesh,
    /** @brief Not a mesh; draw commands run one after another. */
    NonMesh,
};

/** @brief The batch-set key and bin key of an entity within a phase (Bevy
 * `CachedBinKey<BPI>`). */
template <BinnedPhaseItem BPI>
struct CachedBinKey {
    /** @brief Key of the batch set containing the entity. */
    typename BPI::BatchSetKey batch_set_key;
    /** @brief Key of the bin containing the entity. */
    typename BPI::BinKey bin_key;
    /** @brief How the entity is rendered. */
    BinnedRenderPhaseType phase_type           = BinnedRenderPhaseType::BatchableMesh;
    bool operator==(const CachedBinKey&) const = default;
};

/** @brief Information kept about an entity currently within a bin (Bevy
 * `CachedBinnedEntity<BPI>`). */
template <BinnedPhaseItem BPI>
struct CachedBinnedEntity {
    /** @brief The entity's bin keys, if it is binned. */
    std::optional<CachedBinKey<BPI>> cached_bin_key;
    /** @brief Last modified tick of the entity; used to detect invalidation. */
    ecs::Tick change_tick{};
};

/** @brief An entity that was in one bin last frame and a different bin this
 * frame (Bevy `EntityThatChangedBins<BPI>`). */
template <BinnedPhaseItem BPI>
struct EntityThatChangedBins {
    /** @brief The entity (main world id). */
    sync_world::MainEntity main_entity;
    /** @brief The bin the entity used to be in. */
    CachedBinnedEntity<BPI> old_cached_binned_entity;
};

/**
 * @brief A collection of all rendering instructions for a single binned
 * render phase for a single view (Bevy `BinnedRenderPhase<BPI>`, 0.18).
 *
 * Entities are binned by `(batch_set_key, bin_key)`; bin membership is
 * cached per entity with a change tick so that
 * `sweep_old_entities` can cheaply remove entities that disappeared or changed bins.
 * @tparam BPI The binned phase item type (see `BinnedPhaseItem`).
 */
EPIX_EXPORT template <BinnedPhaseItem BPI>
class BinnedRenderPhase {
   public:
    using BatchSetKey = typename BPI::BatchSetKey;
    using BinKey      = typename BPI::BinKey;

    /** @brief Construct a phase for the selected GPU preprocessing mode. */
    explicit BinnedRenderPhase(
        batching::GpuPreprocessingMode gpu_preprocessing_mode = batching::GpuPreprocessingMode::None)
        : batch_sets(make_batch_sets(gpu_preprocessing_mode)), gpu_preprocessing_mode(gpu_preprocessing_mode) {}

    /** @brief Batch set -> (bin -> entities) for multidrawable meshes. */
    IndexMap<BatchSetKey, IndexMap<BinKey, RenderBin>> multidrawable_meshes;
    /** @brief (batch set, bin) -> entities for batchable non-multidrawable meshes. */
    IndexMap<BinKeyPair<BatchSetKey, BinKey>, RenderBin> batchable_meshes;
    /** @brief (batch set, bin) -> entities for unbatchable meshes. */
    IndexMap<BinKeyPair<BatchSetKey, BinKey>, UnbatchableBinnedEntities> unbatchable_meshes;
    /** @brief (batch set, bin) -> entities for non-mesh items. */
    IndexMap<BinKeyPair<BatchSetKey, BinKey>, NonMeshEntities> non_mesh_items;
    /** @brief Per-frame prepared batches. Its alternative is fixed by
     * `gpu_preprocessing_mode`. */
    BinnedRenderPhaseBatchSets<BinKey> batch_sets;
    /** @brief GPU preprocessing mode selected when this phase was created. */
    batching::GpuPreprocessingMode gpu_preprocessing_mode;

    /** @brief True when no entities are binned in any list. */
    bool is_empty() const noexcept {
        return multidrawable_meshes.empty() && batchable_meshes.empty() && unbatchable_meshes.empty() &&
               non_mesh_items.empty();
    }

    /**
     * @brief Bins a new entity (Bevy `BinnedRenderPhase::add`).
     * @param batch_set_key Key of the batch set containing the entity.
     * @param bin_key Key of the bin containing the entity.
     * @param render_entity The render-world entity.
     * @param main_entity The main-world entity.
     * @param input_uniform_index Index of the entity's input uniform.
     * @param phase_type How the entity is rendered.
     * @param change_tick The current change tick of the world.
     */
    void add(BatchSetKey batch_set_key,
             BinKey bin_key,
             epix::ecs::Entity render_entity,
             sync_world::MainEntity main_entity,
             InputUniformIndex input_uniform_index,
             BinnedRenderPhaseType phase_type,
             ecs::Tick change_tick) {
        // Match Bevy: only the direct preprocessing path overrides indirect
        // drawing. Culling keeps multidrawable bins separate.
        if (gpu_preprocessing_mode == batching::GpuPreprocessingMode::PreprocessingOnly &&
            phase_type == BinnedRenderPhaseType::MultidrawableMesh) {
            phase_type = BinnedRenderPhaseType::BatchableMesh;
        }
        switch (phase_type) {
            case BinnedRenderPhaseType::MultidrawableMesh: {
                auto& batch_set = multidrawable_meshes[batch_set_key];
                auto& bin       = batch_set[bin_key];
                bin.insert(main_entity.entity, input_uniform_index);
                break;
            }
            case BinnedRenderPhaseType::BatchableMesh: {
                auto& bin = batchable_meshes[{batch_set_key, bin_key}];
                bin.insert(main_entity.entity, input_uniform_index);
                break;
            }
            case BinnedRenderPhaseType::UnbatchableMesh: {
                auto& unbatchable                        = unbatchable_meshes[{batch_set_key, bin_key}];
                unbatchable.entities[main_entity.entity] = render_entity;
                break;
            }
            case BinnedRenderPhaseType::NonMesh: {
                auto& non_mesh                        = non_mesh_items[{batch_set_key, bin_key}];
                non_mesh.entities[main_entity.entity] = render_entity;
                break;
            }
        }
        update_cache(main_entity, CachedBinKey<BPI>{batch_set_key, bin_key, phase_type}, change_tick);
    }

    /**
     * @brief Record an entity's current bin keys (Bevy `update_cache`). If
     * the entity changed bins, its old bin is recorded for removal by
     * `sweep_old_entities`; the entity is also marked valid.
     */
    void update_cache(sync_world::MainEntity main_entity,
                      std::optional<CachedBinKey<BPI>> cached_bin_key,
                      ecs::Tick change_tick) {
        CachedBinnedEntity<BPI> new_entry{cached_bin_key, change_tick};
        std::optional<CachedBinnedEntity<BPI>> old_entry;
        if (auto existing = cached_entity_bin_keys.get(main_entity.entity)) {
            old_entry                = *existing;
            existing->cached_bin_key = cached_bin_key;
            existing->change_tick    = change_tick;
        } else {
            cached_entity_bin_keys[main_entity.entity] = new_entry;
        }
        // If the entity changed bins, record its old bin so that we can
        // remove the entity from it during sweep.
        if (old_entry && old_entry->cached_bin_key != new_entry.cached_bin_key) {
            entities_that_changed_bins.push_back(EntityThatChangedBins<BPI>{main_entity, *old_entry});
        }
        // Mark the entity as valid (validity flags align with insertion order).
        if (auto idx = cached_entity_bin_keys.index_of(main_entity.entity)) {
            if (valid_cached_entity_bin_keys.size() <= *idx) {
                valid_cached_entity_bin_keys.resize(*idx + 1, false);
            }
            valid_cached_entity_bin_keys[*idx] = true;
        }
    }

    /**
     * @brief Reset per-frame state: all cached entities become invalid until
     * re-validated by `validate_cached_entity` or re-added via `add` (Bevy
     * `prepare_for_new_frame`).
     */
    void prepare_for_new_frame() {
        std::visit([](auto& batches) { batches.clear(); }, batch_sets);
        valid_cached_entity_bin_keys.assign(cached_entity_bin_keys.size(), false);
        entities_that_changed_bins.clear();
        for (auto& [key, unbatchable] : unbatchable_meshes.iter()) {
            (void)key;
            unbatchable.instance_range.reset();
            unbatchable.batches.clear();
        }
        for (auto& [key, bin] : batchable_meshes.iter()) {
            (void)key;
            bin.clear_batches();
        }
    }

    /**
     * @brief If the entity is cached and its change tick matches, mark it
     * valid and return true (Bevy `validate_cached_entity`).
     */
    bool validate_cached_entity(sync_world::MainEntity visible_entity, ecs::Tick current_change_tick) {
        if (auto idx = cached_entity_bin_keys.index_of(visible_entity.entity)) {
            auto& entry = *cached_entity_bin_keys.get(visible_entity.entity);
            if (entry.change_tick == current_change_tick) {
                if (valid_cached_entity_bin_keys.size() <= *idx) {
                    valid_cached_entity_bin_keys.resize(*idx + 1, false);
                }
                valid_cached_entity_bin_keys[*idx] = true;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Encodes the GPU commands needed to render all entities in this
     * phase (Bevy BinnedRenderPhase::render; storage-buffer fast path). The
     * item factory `BPI::create(batch_set_key, bin_key, representative_entity,
     * instance_range)` must exist (Bevy names it `new`, a C++ keyword); phases
     * whose item type lacks it render nothing.
     */
    void render(const wgpu::RenderPassEncoder& render_pass,
                const epix::ecs::World& world,
                epix::ecs::Entity view) const {
        {
            auto&& draw_functions = world.resource<DrawFunctions<BPI>>();
            draw_functions.prepare(world);
        }
        // Preparation writes the mode-specific batch-set representation. As in
        // Bevy, this render path consumes only that representation: an empty
        // prepared set means no batchable mesh was prepared this frame.
        if (std::holds_alternative<std::vector<std::vector<BinnedRenderPhaseBatch>>>(batch_sets)) {
            const auto& dynamic = std::get<0>(batch_sets);
            render_dynamic_uniform_batches(render_pass, world, view, dynamic);
        } else if (std::holds_alternative<std::vector<BinnedRenderPhaseBatch>>(batch_sets)) {
            const auto& direct = std::get<1>(batch_sets);
            render_direct_batches(render_pass, world, view, direct);
        } else {
            const auto& multidraw = std::get<2>(batch_sets);
            render_multidraw_batches(render_pass, world, view, multidraw);
        }
        render_unbatchable_meshes(render_pass, world, view);
        render_non_meshes(render_pass, world, view);
    }

    /**
     * @brief Removes entities not marked valid (they were not re-queued this
     * frame) and entities that changed bins, from their old bins (Bevy
     * `sweep_old_entities`).
     */
    void sweep_old_entities() {
        // Remove entities not marked as valid; iterate in reverse so that
        // index fixups do not disturb positions we have not visited yet.
        for (std::size_t i = cached_entity_bin_keys.size(); i-- > 0;) {
            if (!valid_cached_entity_bin_keys[i]) {
                auto [entity, cached] = cached_entity_bin_keys.remove_at(i);
                if (cached.cached_bin_key) {
                    remove_entity_from_bin(entity, *cached.cached_bin_key);
                }
                if (i < valid_cached_entity_bin_keys.size()) {
                    valid_cached_entity_bin_keys.erase(valid_cached_entity_bin_keys.begin() +
                                                       static_cast<std::ptrdiff_t>(i));
                }
            }
        }
        // If an entity changed bins, remove it from its old bin.
        for (auto& changed : entities_that_changed_bins) {
            if (changed.old_cached_binned_entity.cached_bin_key) {
                remove_entity_from_bin(changed.main_entity.entity, *changed.old_cached_binned_entity.cached_bin_key);
            }
        }
        entities_that_changed_bins.clear();
    }

   private:
    static BinnedRenderPhaseBatchSets<BinKey> make_batch_sets(batching::GpuPreprocessingMode mode) {
        switch (mode) {
            case batching::GpuPreprocessingMode::None:
                return std::vector<std::vector<BinnedRenderPhaseBatch>>{};
            case batching::GpuPreprocessingMode::PreprocessingOnly:
                return std::vector<BinnedRenderPhaseBatch>{};
            case batching::GpuPreprocessingMode::Culling:
                return std::vector<BinnedRenderPhaseBatchSet<BinKey>>{};
        }
        std::unreachable();
    }

    void remove_entity_from_bin(epix::ecs::Entity main_entity, const CachedBinKey<BPI>& key) {
        switch (key.phase_type) {
            case BinnedRenderPhaseType::MultidrawableMesh: {
                if (auto* batch_set = multidrawable_meshes.get(key.batch_set_key)) {
                    if (auto* bin = batch_set->get(key.bin_key)) {
                        bin->remove(main_entity);
                        if (bin->empty()) {
                            batch_set->remove(key.bin_key);
                        }
                    }
                    if (batch_set->empty()) {
                        multidrawable_meshes.remove(key.batch_set_key);
                    }
                }
                break;
            }
            case BinnedRenderPhaseType::BatchableMesh: {
                BinKeyPair<BatchSetKey, BinKey> key_pair{key.batch_set_key, key.bin_key};
                if (auto* bin = batchable_meshes.get(key_pair)) {
                    bin->remove(main_entity);
                    if (bin->empty()) {
                        batchable_meshes.remove(key_pair);
                    }
                }
                break;
            }
            case BinnedRenderPhaseType::UnbatchableMesh: {
                BinKeyPair<BatchSetKey, BinKey> key_pair{key.batch_set_key, key.bin_key};
                if (auto* unbatchable = unbatchable_meshes.get(key_pair)) {
                    unbatchable->entities.erase(main_entity);
                    if (unbatchable->empty()) {
                        unbatchable_meshes.remove(key_pair);
                    }
                }
                break;
            }
            case BinnedRenderPhaseType::NonMesh: {
                BinKeyPair<BatchSetKey, BinKey> key_pair{key.batch_set_key, key.bin_key};
                if (auto* non_mesh = non_mesh_items.get(key_pair)) {
                    non_mesh->entities.erase(main_entity);
                    if (non_mesh->empty()) {
                        non_mesh_items.remove(key_pair);
                    }
                }
                break;
            }
        }
    }

    /** @brief True when BPI provides the bin->item factory (Bevy BPI::new;
     * named `create` because `new` is a C++ keyword). */
    static constexpr bool has_item_factory = requires(typename BPI::BatchSetKey batch_set_key,
                                                      typename BPI::BinKey bin_key,
                                                      epix::ecs::Entity representative_entity,
                                                      std::uint32_t instance_start,
                                                      std::uint32_t instance_end) {
        BPI::create(batch_set_key, bin_key, representative_entity, instance_start, instance_end);
    };

    static BPI make_item(const BatchSetKey& batch_set_key,
                         const BinKey& bin_key,
                         sync_world::MainEntity representative_entity,
                         std::pair<std::uint32_t, std::uint32_t> instance_range,
                         PhaseItemExtraIndex extra_index) {
        auto item = BPI::create(batch_set_key, bin_key, representative_entity.entity, instance_range.first,
                                instance_range.second);
        if constexpr (MutablePhaseItemExtraIndex<BPI>) {
            item.set_extra_index(extra_index);
        }
        return item;
    }

    /** @brief Run one item's draw function, logging failures (Bevy draw
     * functions return Result<(), DrawError>; Skip is ignored). */
    void draw_item(const wgpu::RenderPassEncoder& render_pass,
                   const epix::ecs::World& world,
                   epix::ecs::Entity view,
                   const BPI& item) const {
        auto&& draw_functions = world.resource<DrawFunctions<BPI>>();
        auto draw_function    = draw_functions.get(item.draw_function());
        if (!draw_function) {
            spdlog::error("[render] Draw function {} not found for binned item {:#x}.",
                          static_cast<std::uint32_t>(item.draw_function()), item.entity().index);
            return;
        }
        auto result = draw_function->get().draw(world, render_pass, view, item);
        if (!result && result.error().type != DrawError::ErrorType::Skip) {
            spdlog::error("[render] Draw function {} failed for binned item {:#x}: {}",
                          static_cast<std::uint32_t>(item.draw_function()), item.entity().index,
                          result.error().message);
        }
    }

    void render_dynamic_uniform_batches(const wgpu::RenderPassEncoder& render_pass,
                                        const epix::ecs::World& world,
                                        epix::ecs::Entity view,
                                        const std::vector<std::vector<BinnedRenderPhaseBatch>>& batches) const {
        if constexpr (!has_item_factory) return;
        auto key = batchable_meshes.iter().begin();
        for (const auto& bin_batches : batches) {
            if (key == batchable_meshes.iter().end()) break;
            for (const auto& batch : bin_batches) {
                draw_item(render_pass, world, view,
                          make_item(key->first.first, key->first.second, batch.representative_entity,
                                    batch.instance_range, batch.extra_index));
            }
            ++key;
        }
    }

    void render_direct_batches(const wgpu::RenderPassEncoder& render_pass,
                               const epix::ecs::World& world,
                               epix::ecs::Entity view,
                               const std::vector<BinnedRenderPhaseBatch>& batches) const {
        if constexpr (!has_item_factory) return;
        auto key = batchable_meshes.iter().begin();
        for (const auto& batch : batches) {
            if (key == batchable_meshes.iter().end()) break;
            draw_item(render_pass, world, view,
                      make_item(key->first.first, key->first.second, batch.representative_entity, batch.instance_range,
                                batch.extra_index));
            ++key;
        }
    }

    void render_multidraw_batches(const wgpu::RenderPassEncoder& render_pass,
                                  const epix::ecs::World& world,
                                  epix::ecs::Entity view,
                                  const std::vector<BinnedRenderPhaseBatchSet<BinKey>>& batches) const {
        if constexpr (!has_item_factory) return;
        const bool multi_draw_indirect_count_supported = [&world] {
            const auto device = world.get_resource<wgpu::Device>();
            if (!device || !device->get().hasFeature(wgpu::FeatureName(wgpu::NativeFeature::eMultiDrawIndirectCount))) {
                return false;
            }
            const auto adapter = world.get_resource<wgpu::Adapter>();
            if (!adapter) return true;
            wgpu::AdapterInfo info;
            adapter->get().getInfo(&info);
            // Same temporary DX12 exclusion as Bevy (#7974).
            return info.backendType != wgpu::BackendType::eD3D12;
        }();
        auto multidraw_key = multidrawable_meshes.iter().begin();
        auto batchable_key = batchable_meshes.iter().begin();
        for (const auto& batch_set : batches) {
            const auto extra_index = multidraw_extra_index(batch_set.first_batch.extra_index, batch_set.batch_count,
                                                           multi_draw_indirect_count_supported, batch_set.index);
            if (multidraw_key != multidrawable_meshes.iter().end()) {
                draw_item(
                    render_pass, world, view,
                    make_item(multidraw_key->first, batch_set.bin_key, batch_set.first_batch.representative_entity,
                              batch_set.first_batch.instance_range, extra_index));
                ++multidraw_key;
            } else if (batchable_key != batchable_meshes.iter().end()) {
                draw_item(render_pass, world, view,
                          make_item(batchable_key->first.first, batch_set.bin_key,
                                    batch_set.first_batch.representative_entity, batch_set.first_batch.instance_range,
                                    extra_index));
                ++batchable_key;
            } else {
                break;
            }
        }
    }

    /** @brief Render each unbatchable mesh entity with a separate draw call
     * (Bevy render_unbatchable_meshes). */
    void render_unbatchable_meshes(const wgpu::RenderPassEncoder& render_pass,
                                   const epix::ecs::World& world,
                                   epix::ecs::Entity view) const {
        if constexpr (!has_item_factory) return;
        for (auto&& [key, unbatchable] : unbatchable_meshes.iter()) {
            for (auto&& [main_entity, render_entity] : unbatchable.entities) {
                (void)render_entity;
                const auto prepared = unbatchable.batches.find(main_entity);
                if (prepared == unbatchable.batches.end()) continue;
                draw_item(render_pass, world, view,
                          make_item(key.first, key.second, sync_world::MainEntity{main_entity},
                                    prepared->second.instance_range, prepared->second.extra_index));
            }
        }
    }

    /** @brief Render each non-mesh entity with a separate draw call (Bevy
     * render_non_meshes). */
    void render_non_meshes(const wgpu::RenderPassEncoder& render_pass,
                           const epix::ecs::World& world,
                           epix::ecs::Entity view) const {
        if constexpr (!has_item_factory) return;
        for (auto&& [key, non_mesh] : non_mesh_items.iter()) {
            for (auto&& [main_entity, render_entity] : non_mesh.entities) {
                (void)render_entity;
                BPI item = BPI::create(key.first, key.second, main_entity, 0u, 1u);
                draw_item(render_pass, world, view, item);
            }
        }
    }

    /** @brief main entity -> cached bin keys + change tick. */
    IndexMap<epix::ecs::Entity, CachedBinnedEntity<BPI>> cached_entity_bin_keys;
    /** @brief Validity flag per cached entry (aligned with insertion order). */
    std::vector<bool> valid_cached_entity_bin_keys;
    /** @brief Entities that changed bins this frame. */
    std::vector<EntityThatChangedBins<BPI>> entities_that_changed_bins;
};

/**
 * @brief Stores the binned render phases for all views (Bevy
 * `ViewBinnedRenderPhases<BPI>`). Retained across frames to reuse
 * allocations.
 */
EPIX_EXPORT template <BinnedPhaseItem BPI>
struct ViewBinnedRenderPhases {
    /** @brief View -> binned phase. */
    std::unordered_map<view::RetainedViewEntity, BinnedRenderPhase<BPI>> phases;

    /** @brief Reset the phase for the view, creating it if needed (Bevy
     * `prepare_for_new_frame`). */
    void prepare_for_new_frame(const view::RetainedViewEntity& retained_view_entity,
                               batching::GpuPreprocessingMode gpu_preprocessing_mode) {
        if (auto it = phases.find(retained_view_entity); it != phases.end()) {
            it->second.prepare_for_new_frame();
        } else {
            phases.emplace(retained_view_entity, BinnedRenderPhase<BPI>{gpu_preprocessing_mode});
        }
    }
};

/**
 * @brief System that removes entities no longer queued in any view's binned
 * phase (Bevy `sweep_old_entities`).
 */
EPIX_EXPORT template <BinnedPhaseItem BPI>
void sweep_old_entities(ecs::ResMut<ViewBinnedRenderPhases<BPI>> render_phases) {
    for (auto& [view, phase] : render_phases->phases) {
        (void)view;
        phase.sweep_old_entities();
    }
}

/** @brief Sort each binned phase's keys (Bevy
 * `batching::sort_binned_render_phase`). */
EPIX_EXPORT template <BinnedPhaseItem BPI>
    requires(std::totally_ordered<typename BPI::BatchSetKey> && std::totally_ordered<typename BPI::BinKey>)
void sort_binned_render_phase(ecs::ResMut<ViewBinnedRenderPhases<BPI>> render_phases) {
    for (auto& [view, phase] : render_phases->phases) {
        (void)view;
        phase.multidrawable_meshes.sort_unstable_keys();
        for (auto& [batch_set_key, bins] : phase.multidrawable_meshes.iter()) {
            (void)batch_set_key;
            bins.sort_unstable_keys();
        }
        phase.batchable_meshes.sort_unstable_keys();
        phase.unbatchable_meshes.sort_unstable_keys();
        phase.non_mesh_items.sort_unstable_keys();
    }
}

/**
 * @brief Plugin that sets up a binned render phase (Bevy
 * `BinnedRenderPhasePlugin`). Like Bevy's plugin, its batch-data adapter is
 * mandatory: phase-only registration
 * belongs to the subsystem that owns a
 * bespoke phase, not to this automatic-batching plugin.
 */
EPIX_EXPORT template <BinnedPhaseItem BPI, typename Adapter>
struct BinnedRenderPhasePlugin {
    RenderDebugFlags debug_flags{};
    explicit BinnedRenderPhasePlugin(RenderDebugFlags flags = {}) noexcept : debug_flags(flags) {}
    void attach(app::App& app);
};

/**
 * @brief Plugin that sets up a sorted render phase (Bevy
 * `SortedRenderPhasePlugin`): registers the phase sort system in
 * `RenderSystems::PhaseSort`. As in Bevy, automatic sorted-phase batching
 * always has a `GetFullBatchData` adapter.

 */
EPIX_EXPORT template <CachedRenderPipelinePhaseItem P, typename Adapter>
struct SortedRenderPhasePlugin {
    RenderDebugFlags debug_flags{};
    explicit SortedRenderPhasePlugin(RenderDebugFlags flags = {}) noexcept : debug_flags(flags) {}
    void attach(app::App& app);
};

}  // namespace epix::render::phase

// Keep this public header self-contained: the phase-plugin member templates
// are defined by batching.hpp after all phase types above are complete.
#include <epix/render/batching.hpp>
