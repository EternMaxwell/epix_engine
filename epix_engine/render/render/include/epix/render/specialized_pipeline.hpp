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
 * specialized by key (Bevy `SpecializedRenderPipeline`). Specialize
 * `SpecializedRenderPipeline<S>` with `Key` and the static `specialize`
 * function returning a `RenderPipelineDescriptor`.
 */
template <typename S>
struct SpecializedRenderPipeline;

template <typename S>
concept SpecializedRenderPipelineImpl = requires(typename SpecializedRenderPipeline<S>::Key key) {
    { SpecializedRenderPipeline<S>::specialize(key) } -> std::same_as<RenderPipelineDescriptor>;
};

/**
 * @brief Cache of specialized render pipelines keyed by `Key` (Bevy
 * `SpecializedRenderPipelines<S>`).
 */
template <typename S>
    requires SpecializedRenderPipelineImpl<S>
struct SpecializedRenderPipelines {
    /** @brief Cached pipeline ids keyed by specialization key. */
    std::unordered_map<typename SpecializedRenderPipeline<S>::Key, CachedPipelineId> cache;

    /** @brief Get the cached pipeline for `key`, queueing a new pipeline if absent. */
    CachedPipelineId specialize(const PipelineServer& server, typename SpecializedRenderPipeline<S>::Key key) {
        if (auto it = cache.find(key); it != cache.end()) {
            return it->second;
        }
        CachedPipelineId id = server.queue_render_pipeline(SpecializedRenderPipeline<S>::specialize(key));
        cache.emplace(std::move(key), id);
        return id;
    }
};

/**
 * @brief Trait specialization pattern for compute pipelines (Bevy
 * `SpecializedComputePipeline`).
 */
template <typename S>
struct SpecializedComputePipeline;

template <typename S>
concept SpecializedComputePipelineImpl = requires(typename SpecializedComputePipeline<S>::Key key) {
    { SpecializedComputePipeline<S>::specialize(key) } -> std::same_as<ComputePipelineDescriptor>;
};

/**
 * @brief Cache of specialized compute pipelines keyed by `Key` (Bevy
 * `SpecializedComputePipelines<S>`).
 */
template <typename S>
    requires SpecializedComputePipelineImpl<S>
struct SpecializedComputePipelines {
    /** @brief Cached pipeline ids keyed by specialization key. */
    std::unordered_map<typename SpecializedComputePipeline<S>::Key, CachedPipelineId> cache;

    CachedPipelineId specialize(const PipelineServer& server, typename SpecializedComputePipeline<S>::Key key) {
        if (auto it = cache.find(key); it != cache.end()) {
            return it->second;
        }
        CachedPipelineId id = server.queue_compute_pipeline(SpecializedComputePipeline<S>::specialize(key));
        cache.emplace(std::move(key), id);
        return id;
    }
};

}  // namespace epix::render
