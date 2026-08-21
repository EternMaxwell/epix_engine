#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <epix/ecs.hpp>
#endif
#include <epix/render/render_resource.hpp>
#include <epix/render/sync_world.hpp>

namespace epix::render::batching {

/**
 * @brief Trait to specialize for CPU batching support (Bevy `GetBatchData`,
 * batching/mod.rs:76-100). If the pipeline id, draw function id, per-instance
 * data buffer dynamic offset and CompareData match, consecutive draws can be
 * batched.
 * @tparam T The adapter type identifying this batching specialization.
 */
template <typename T>
struct GetBatchData;

/** @brief Concept satisfied by valid GetBatchData specializations. */
template <typename T>
concept GetBatchDataImpl = requires(GetBatchData<T> batch) {
    requires std::constructible_from<GetBatchData<T>>;
    requires std::is_empty_v<GetBatchData<T>>;
    typename GetBatchData<T>::Param;
    requires ecs::system_param<typename GetBatchData<T>::Param>;
    typename GetBatchData<T>::CompareData;
    requires std::equality_comparable<typename GetBatchData<T>::CompareData>;
    typename GetBatchData<T>::BufferData;
    requires render_resource::GpuArrayBufferable<typename GetBatchData<T>::BufferData>;
    {
        batch.get_batch_data(std::declval<typename GetBatchData<T>::Param&>(),
                             std::declval<std::pair<epix::ecs::Entity, sync_world::MainEntity>>())
    } -> std::same_as<std::optional<std::pair<typename GetBatchData<T>::BufferData,
                                            std::optional<typename GetBatchData<T>::CompareData>>>>;
};

/**
 * @brief Trait to specialize for binning + GPU preprocessing batching support
 * (Bevy `GetFullBatchData`, batching/mod.rs:106-178). The GPU-culling
 * `write_batch_indirect_parameters_metadata` method is documented as N/A: GPU
 * preprocessing is not ported (the bundled wgpu-native lacks the
 * binding-array descriptor APIs required for the preprocessing shaders).
 * @tparam T The adapter type identifying this batching specialization.
 */
template <typename T>
struct GetFullBatchData;

/** @brief Concept satisfied by valid GetFullBatchData specializations (the
 * CPU-usable subset; the GPU-culling indirect-metadata method is N/A). */
template <typename T>
concept GetFullBatchDataImpl = GetBatchDataImpl<T> && requires(GetFullBatchData<T> batch) {
    requires std::constructible_from<GetFullBatchData<T>>;
    requires std::is_empty_v<GetFullBatchData<T>>;
    typename GetFullBatchData<T>::BufferInputData;
    requires render_resource::ShaderType<typename GetFullBatchData<T>::BufferInputData>;
    requires std::default_initializable<typename GetFullBatchData<T>::BufferInputData>;
    {
        batch.get_binned_batch_data(std::declval<typename GetBatchData<T>::Param&>(),
                                    std::declval<sync_world::MainEntity>())
    } -> std::same_as<std::optional<typename GetBatchData<T>::BufferData>>;
    {
        batch.get_index_and_compare_data(std::declval<typename GetBatchData<T>::Param&>(),
                                         std::declval<sync_world::MainEntity>())
    } -> std::same_as<std::optional<std::pair<std::uint32_t, std::optional<typename GetBatchData<T>::CompareData>>>>;
    {
        batch.get_binned_index(std::declval<typename GetBatchData<T>::Param&>(),
                               std::declval<sync_world::MainEntity>())
    } -> std::same_as<std::optional<std::uint32_t>>;
};

/**
 * @brief Plugin enabling automatic batching (Bevy `BatchingPlugin`,
 * batching/gpu_preprocessing.rs:45-79). Bevy registers the GPU-preprocessing
 * indirect-parameters buffers here; that path is not ported (the bundled
 * wgpu-native lacks the binding-array descriptor APIs), so epix batches via
 * PhaseItem `batch_range` merging at queue sites and this plugin is attached
 * for structural parity (Bevy lib.rs:371-373).
 */
EPIX_EXPORT struct BatchingPlugin {
    /** @brief Debugging flags (Bevy BatchingPlugin::debug_flags). */
    RenderDebugFlags debug_flags{};

    void attach(app::App&) const noexcept {
        // GPU preprocessing is not ported (wgpu-native FFI limitation);
        // nothing to register.
    }
};

}  // namespace epix::render::batching

