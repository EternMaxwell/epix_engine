#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <epix/ecs.hpp>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#endif

#include <epix/render/pipeline_server.hpp>

namespace epix::render {

/**
 * @brief Trait to specialize for resource types that can be specialized by
 * key (Bevy `Specializable`, specializer.rs:24-29): describes the pipeline
 * descriptor type, the cached-id type, and how descriptors are queued/read
 * through the pipeline server.
 * @tparam T The pipeline/resource type (e.g. RenderPipeline).
 */
template <typename T>
struct Specializable;

/** @brief Concept satisfied by valid Specializable specializations. */
template <typename T>
concept SpecializableImpl = requires(Specializable<T> s, PipelineServer& server) {
    typename Specializable<T>::Descriptor;
    typename Specializable<T>::CachedId;
    {
        s.queue(server, std::declval<typename Specializable<T>::Descriptor>())
    } -> std::same_as<typename Specializable<T>::CachedId>;
    {
        s.get_descriptor(server, std::declval<const typename Specializable<T>::CachedId&>())
    } -> std::same_as<const typename Specializable<T>::Descriptor&>;
};

/** @brief Concept for a type usable as a specializer key (Bevy
 * `SpecializerKey`, specializer.rs:212-219: Clone + Hash + Eq). C++ requires
 * equality-comparable + std::hash-able. */
template <typename K>
concept SpecializerKey = requires(const K& key) {
    { std::hash<K>{}(key) } -> std::convertible_to<std::size_t>;
    requires std::equality_comparable<K>;
};

/**
 * @brief Concept for a type capable of specializing values of T (Bevy
 * `Specializer<T>`, specializer.rs:174-181). The specializer struct defines a
 * hashable `Key` and a `specialize(key, descriptor)` method that mutates the
 * base descriptor and returns the canonical key used for duplicate detection.
 */
template <typename T, typename S>
concept SpecializerImpl =
    SpecializableImpl<T> && requires(S specializer, typename Specializable<T>::Descriptor& descriptor) {
        typename S::Key;
        requires SpecializerKey<typename S::Key>;
        { specializer.specialize(std::declval<const typename S::Key&>(), descriptor) } -> std::same_as<typename S::Key>;
    };

/**
 * @brief Cache for variants of a resource type created by a specializer
 * (Bevy `Variants<T, S>`, specializer.rs:267-272). At most one pipeline is
 * queued per key; a secondary canonical-key cache deduplicates pipelines
 * whose keys canonicalize to the same descriptor.
 * @tparam T The Specializable resource type.
 * @tparam S The specializer struct.
 */
template <typename T, typename S>
    requires SpecializableImpl<T> && SpecializerImpl<T, S>
struct Variants {
    using Descriptor = typename Specializable<T>::Descriptor;
    using Key        = typename S::Key;
    using CachedId   = typename Specializable<T>::CachedId;

    /** @brief The specializer. */
    S specializer;
    /** @brief The base (un-specialized) descriptor, cloned per specialization. */
    Descriptor base_descriptor;
    /** @brief Key -> cached id (Bevy primary_cache). */
    std::unordered_map<Key, CachedId> primary_cache;
    /** @brief Canonical key -> cached id (Bevy secondary_cache). */
    std::unordered_map<Key, CachedId> secondary_cache;

    /** @brief Create a Variants cache from a specializer and base descriptor
     * (Bevy Variants::new, specializer.rs:277-284). */
    Variants(S specializer, Descriptor base_descriptor)
        : specializer(std::move(specializer)), base_descriptor(std::move(base_descriptor)) {}

    /**
     * @brief Specialize a resource for the given key, queueing a new pipeline
     * only when the key (or its canonical form) has not been seen before (Bevy
     * Variants::specialize, specializer.rs:288-299 + specialize_slow).
     */
    CachedId specialize(PipelineServer& server, Key key) {
        if (auto it = primary_cache.find(key); it != primary_cache.end()) {
            return it->second;
        }
        // Slow path: clone the base descriptor, run the specializer, then
        // deduplicate against the canonical key of the produced descriptor.
        Descriptor descriptor = base_descriptor;
        Key canonical         = specializer.specialize(key, descriptor);
        if (auto it = secondary_cache.find(canonical); it != secondary_cache.end()) {
            CachedId id = it->second;
            primary_cache.emplace(std::move(key), id);
            return id;
        }
        CachedId id = Specializable<T>{}.queue(server, std::move(descriptor));
        secondary_cache.emplace(canonical, id);
        primary_cache.emplace(std::move(key), id);
        return id;
    }
};

/** @brief Specializable for render pipelines (Bevy impl, specializer.rs:31-45). */
template <>
struct Specializable<RenderPipeline> {
    using Descriptor = RenderPipelineDescriptor;
    using CachedId   = CachedPipelineId;

    CachedPipelineId queue(PipelineServer& server, RenderPipelineDescriptor descriptor) const {
        return server.queue_render_pipeline(std::move(descriptor));
    }
    const RenderPipelineDescriptor& get_descriptor(PipelineServer& server, CachedPipelineId id) const {
        auto descriptor = server.get_render_pipeline_descriptor(id);
        if (!descriptor) {
            throw std::runtime_error("Render pipeline descriptor not available");
        }
        return descriptor->get();
    }
};

/** @brief Specializable for compute pipelines (Bevy impl, specializer.rs:47-62). */
template <>
struct Specializable<ComputePipeline> {
    using Descriptor = ComputePipelineDescriptor;
    using CachedId   = CachedPipelineId;

    CachedPipelineId queue(PipelineServer& server, ComputePipelineDescriptor descriptor) const {
        return server.queue_compute_pipeline(std::move(descriptor));
    }
    const ComputePipelineDescriptor& get_descriptor(PipelineServer& server, CachedPipelineId id) const {
        auto descriptor = server.get_compute_pipeline_descriptor(id);
        if (!descriptor) {
            throw std::runtime_error("Compute pipeline descriptor not available");
        }
        return descriptor->get();
    }
};

static_assert(SpecializableImpl<RenderPipeline>);
static_assert(SpecializableImpl<ComputePipeline>);

}  // namespace epix::render
