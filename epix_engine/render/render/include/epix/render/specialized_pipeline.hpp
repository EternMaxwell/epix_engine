#pragma once

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <epix/common.hpp>
#include <epix/ecs.hpp>
#include <epix/mesh/vertex_buffer_layout.hpp>
#include <expected>
#include <functional>
#include <unordered_map>
#include <utility>
#include <variant>
#endif

#include <epix/render/pipeline_server.hpp>

namespace epix::render {
/**
 * @brief Trait specialization pattern for render pipelines that can be
 * specialized by key (Bevy `SpecializedRenderPipeline`). A pipeline exposes
 * `Key` and an instance `specialize` function returning a
 * `RenderPipelineDescriptor`.
 */
template <typename S>
concept SpecializedRenderPipeline = requires(const S& pipeline, typename S::Key key) {
    { pipeline.specialize(key) } -> std::same_as<RenderPipelineDescriptor>;
};

/**
 * @brief Cache of specialized render pipelines keyed by `Key` (Bevy
 * `SpecializedRenderPipelines<S>`).
 */
template <typename S>
    requires SpecializedRenderPipeline<S>
struct SpecializedRenderPipelines {
    /** @brief Cached pipeline ids keyed by specialization key. */
    std::unordered_map<typename S::Key, CachedPipelineId> cache;

    /** @brief Get the cached pipeline for `key`, queueing a new pipeline if absent. */
    CachedPipelineId specialize(const PipelineServer& server, const S& specialized_pipeline, typename S::Key key) {
        if (auto it = cache.find(key); it != cache.end()) {
            return it->second;
        }
        CachedPipelineId id = server.queue_render_pipeline(specialized_pipeline.specialize(key));
        cache.emplace(std::move(key), id);
        return id;
    }
};

/**
 * @brief Trait specialization pattern for compute pipelines (Bevy
 * `SpecializedComputePipeline`). A pipeline exposes `Key` and an instance
 * `specialize` function returning a `ComputePipelineDescriptor`.
 */
template <typename S>
concept SpecializedComputePipeline = requires(const S& pipeline, typename S::Key key) {
    { pipeline.specialize(key) } -> std::same_as<ComputePipelineDescriptor>;
};

/**
 * @brief Cache of specialized compute pipelines keyed by `Key` (Bevy
 * `SpecializedComputePipelines<S>`).
 */
template <typename S>
    requires SpecializedComputePipeline<S>
struct SpecializedComputePipelines {
    /** @brief Cached pipeline ids keyed by specialization key. */
    std::unordered_map<typename S::Key, CachedPipelineId> cache;

    CachedPipelineId specialize(const PipelineServer& server, const S& specialized_pipeline, typename S::Key key) {
        if (auto it = cache.find(key); it != cache.end()) {
            return it->second;
        }
        CachedPipelineId id = server.queue_compute_pipeline(specialized_pipeline.specialize(key));
        cache.emplace(std::move(key), id);
        return id;
    }
};

/**
 * @brief Error returned by a mesh-pipeline specialization (Bevy
 * `SpecializedMeshPipelineError`, `pipeline_specializer.rs:256`). Bevy's enum
 * has one variant, `MissingVertexAttribute`.
 */
EPIX_EXPORT using SpecializedMeshPipelineError = std::variant<::epix::mesh::MissingVertexAttributeError>;

/**
 * @brief Trait specialization pattern for render pipelines that depend on the
 * particular mesh's vertex buffer layout (Bevy `SpecializedMeshPipeline`,
 * `pipeline_specializer.rs:133`). A pipeline exposes `Key` and an instance
 * `specialize` function returning a `RenderPipelineDescriptor` whose single
 * vertex buffer is derived from `layout`.
 */
template <typename S>
concept SpecializedMeshPipeline =
    requires(const S& pipeline, typename S::Key key, const ::epix::mesh::MeshVertexBufferLayoutRef& layout) {
        {
            pipeline.specialize(key, layout)
        } -> std::same_as<std::expected<RenderPipelineDescriptor, SpecializedMeshPipelineError>>;
    };

/**
 * @brief Cache of specialized mesh pipelines keyed by the mesh's vertex buffer
 * layout and the specialization key (Bevy `SpecializedMeshPipelines<S>`,
 * `pipeline_specializer.rs:151`).
 *
 * Two caches are kept, as in Bevy: the fast path keyed by the interned mesh
 * layout reference, and a second one keyed by the *resulting*
 * `VertexBufferLayout` so that different mesh layouts that specialize to the
 * same final layout share one compiled pipeline.
 */
template <typename S>
    requires SpecializedMeshPipeline<S>
struct SpecializedMeshPipelines {
    /** @brief Key combining the interned mesh layout and the specialization key
     * (Bevy's `(MeshVertexBufferLayoutRef, S::Key)` tuple). */
    struct MeshLayoutKey {
        ::epix::mesh::MeshVertexBufferLayoutRef layout;
        typename S::Key key;

        // Infer the guarantee from the key: user comparisons may throw.
        bool operator==(const MeshLayoutKey&) const = default;
    };

    /** @brief Hasher for `MeshLayoutKey`. */
    struct MeshLayoutKeyHash {
        std::size_t operator()(const MeshLayoutKey& value) const
            noexcept(noexcept(std::hash<typename S::Key>{}(value.key))) {
            std::size_t hash = std::hash<::epix::mesh::MeshVertexBufferLayoutRef>{}(value.layout);
            hash ^= std::hash<typename S::Key>{}(value.key) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    /** @brief Fast path: interned mesh layout plus key to pipeline id. */
    std::unordered_map<MeshLayoutKey, CachedPipelineId, MeshLayoutKeyHash> mesh_layout_cache;
    /** @brief Deduplicating path: final vertex layout plus key to pipeline id. */
    std::unordered_map<::epix::mesh::VertexBufferLayout, std::unordered_map<typename S::Key, CachedPipelineId>>
        vertex_layout_cache;

    /**
     * @brief Get or create the specialized pipeline for `key` and `layout`.
     *
     * On failure the returned error's `pipeline_type` names `S`, matching Bevy's
     * `type_name::<S>()` annotation (`pipeline_specializer.rs:209-217`).
     */
    std::expected<CachedRenderPipelineId, SpecializedMeshPipelineError> specialize(
        const PipelineServer& server,
        const S& specialized_pipeline,
        typename S::Key key,
        const ::epix::mesh::MeshVertexBufferLayoutRef& layout) {
        const MeshLayoutKey cache_key{.layout = layout, .key = key};
        if (auto it = mesh_layout_cache.find(cache_key); it != mesh_layout_cache.end()) {
            return CachedRenderPipelineId{it->second.get()};
        }

        auto descriptor = specialized_pipeline.specialize(std::move(key), layout);
        if (!descriptor) {
            std::get<::epix::mesh::MissingVertexAttributeError>(descriptor.error()).pipeline_type =
                meta::type_id<S>().short_name();
            return std::unexpected(std::move(descriptor.error()));
        }

        // Different `MeshVertexBufferLayout`s can produce the same final
        // `VertexBufferLayout`; those share one pipeline. Bevy indexes
        // `descriptor.vertex.buffers[0]` and therefore panics on an empty list;
        // Epix raises the project's typed exception for the same condition.
        if (descriptor->vertex.buffers.empty()) {
            throw std::logic_error("A specialized mesh pipeline must produce one vertex buffer.");
        }
        auto [layout_map, _]   = vertex_layout_cache.try_emplace(descriptor->vertex.buffers.front());
        auto [entry, inserted] = layout_map->second.try_emplace(cache_key.key, CachedPipelineId{});
        if (!inserted) {
            mesh_layout_cache.emplace(std::move(cache_key), entry->second);
            return CachedRenderPipelineId{entry->second.get()};
        }
        const CachedPipelineId id = server.queue_render_pipeline(std::move(*descriptor));
        entry->second             = id;
        mesh_layout_cache.emplace(std::move(cache_key), id);
        return CachedRenderPipelineId{id.get()};
    }
};

}  // namespace epix::render
