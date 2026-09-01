#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstdint>
#include <epix/ecs.hpp>
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
    CachedPipelineId specialize(const PipelineServer& server,
                                const S& specialized_pipeline,
                                typename S::Key key) {
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

    CachedPipelineId specialize(const PipelineServer& server,
                                const S& specialized_pipeline,
                                typename S::Key key) {
        if (auto it = cache.find(key); it != cache.end()) {
            return it->second;
        }
        CachedPipelineId id = server.queue_compute_pipeline(specialized_pipeline.specialize(key));
        cache.emplace(std::move(key), id);
        return id;
    }
};

}  // namespace epix::render
