#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <concepts>
#include <cstdint>
#include <epix/ecs.hpp>
#include <epix/meta.hpp>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <epix/render/binned_phase.hpp>
#include <epix/render/gpu_preprocessing_mode.hpp>
#include <epix/render/gpu_preprocessing_support.hpp>
#include <epix/render/occlusion_culling.hpp>
#include <epix/render/render_phase.hpp>
#include <epix/render/render_resource.hpp>
#include <epix/render/sync_world.hpp>

namespace epix::render::batching {

/** @brief Indexed draw parameters consumed by `drawIndexedIndirect` (Bevy
 * `IndirectParametersIndexed`). */
EPIX_EXPORT struct IndirectParametersIndexed {
    std::uint32_t index_count    = 0;
    std::uint32_t instance_count = 0;
    std::uint32_t first_index    = 0;
    std::int32_t base_vertex     = 0;
    std::uint32_t first_instance = 0;
};
static_assert(std::is_standard_layout_v<IndirectParametersIndexed> && sizeof(IndirectParametersIndexed) == 20);

/** @brief Non-indexed draw parameters consumed by `drawIndirect` (Bevy
 * `IndirectParametersNonIndexed`). */
EPIX_EXPORT struct IndirectParametersNonIndexed {
    std::uint32_t vertex_count   = 0;
    std::uint32_t instance_count = 0;
    std::uint32_t first_vertex   = 0;
    std::uint32_t first_instance = 0;
};
static_assert(std::is_standard_layout_v<IndirectParametersNonIndexed> && sizeof(IndirectParametersNonIndexed) == 16);

/** @brief CPU metadata supplied for an indirect batch (Bevy
 * `IndirectParametersCpuMetadata`). */
EPIX_EXPORT struct IndirectParametersCpuMetadata {
    std::uint32_t base_output_index = 0;
    std::uint32_t batch_set_index   = 0;
};

/** @brief GPU-visible batch metadata (Bevy
 * `IndirectParametersGpuMetadata`). */
EPIX_EXPORT struct IndirectParametersGpuMetadata {
    std::uint32_t mesh_index           = 0;
    std::uint32_t early_instance_count = 0;
    std::uint32_t late_instance_count  = 0;
};

/** @brief One preprocessing-shader invocation for an instance in one view
 * (Bevy `PreprocessWorkItem`). */
EPIX_EXPORT struct PreprocessWorkItem {
    std::uint32_t input_index                         = 0;
    std::uint32_t output_or_indirect_parameters_index = 0;
};
static_assert(std::is_standard_layout_v<PreprocessWorkItem> && sizeof(PreprocessWorkItem) == 8);

/** @brief Indirect compute dispatch parameters for the late occlusion pass
 * (Bevy `LatePreprocessWorkItemIndirectParameters`). */
EPIX_EXPORT struct LatePreprocessWorkItemIndirectParameters {
    std::uint32_t dispatch_x      = 0;
    std::uint32_t dispatch_y      = 1;
    std::uint32_t dispatch_z      = 1;
    std::uint32_t work_item_count = 0;
    std::uint32_t pad[4]{};
};
static_assert(std::is_standard_layout_v<LatePreprocessWorkItemIndirectParameters> &&
              sizeof(LatePreprocessWorkItemIndirectParameters) == 32);

/** @brief A contiguous multi-draw batch range (Bevy `IndirectBatchSet`). */
EPIX_EXPORT struct IndirectBatchSet {
    std::uint32_t indirect_parameters_count = 0;
    std::uint32_t indirect_parameters_base  = 0;
};

}  // namespace epix::render::batching

namespace epix::render::render_resource {

template <>
struct RawBufferElementInfo<::epix::render::batching::IndirectParametersIndexed> {
    static constexpr bool has_no_uninit = true;
};
template <>
struct RawBufferElementInfo<::epix::render::batching::IndirectParametersNonIndexed> {
    static constexpr bool has_no_uninit = true;
};
template <>
struct RawBufferElementInfo<::epix::render::batching::IndirectParametersCpuMetadata> {
    static constexpr bool has_no_uninit = true;
};
template <>
struct RawBufferElementInfo<::epix::render::batching::IndirectParametersGpuMetadata> {
    static constexpr bool has_no_uninit = true;
};
template <>
struct RawBufferElementInfo<::epix::render::batching::PreprocessWorkItem> {
    static constexpr bool has_no_uninit = true;
};
template <>
struct RawBufferElementInfo<::epix::render::batching::LatePreprocessWorkItemIndirectParameters> {
    static constexpr bool has_no_uninit = true;
};
template <>
struct RawBufferElementInfo<::epix::render::batching::IndirectBatchSet> {
    static constexpr bool has_no_uninit = true;
};
template <>
struct ShaderTypeInfo<::epix::render::batching::PreprocessWorkItem>
    : RawShaderType<::epix::render::batching::PreprocessWorkItem> {};

}  // namespace epix::render::render_resource

namespace epix::render::batching {

/** @brief Per-view late-work-item storage used when GPU occlusion culling is
 * enabled (Bevy `GpuOcclusionCullingWorkItemBuffers`). */
EPIX_EXPORT struct GpuOcclusionCullingWorkItemBuffers {
    render_resource::UninitBufferVec<PreprocessWorkItem> late_indexed{wgpu::BufferUsage::eStorage |
                                                                      wgpu::BufferUsage::eCopyDst};
    render_resource::UninitBufferVec<PreprocessWorkItem> late_non_indexed{wgpu::BufferUsage::eStorage |
                                                                          wgpu::BufferUsage::eCopyDst};
    std::uint32_t late_indirect_parameters_indexed_offset     = 0;
    std::uint32_t late_indirect_parameters_non_indexed_offset = 0;
};

/** @brief Per-view preprocessing work-item buffers (Bevy
 * `PreprocessWorkItemBuffers`). Direct mode has one stream; indirect mode
 * separates indexed and non-indexed commands. */
EPIX_EXPORT struct PreprocessWorkItemBuffers {
    struct Direct {
        render_resource::RawBufferVec<PreprocessWorkItem> items{wgpu::BufferUsage::eStorage |
                                                                wgpu::BufferUsage::eCopyDst};
    };
    struct Indirect {
        render_resource::RawBufferVec<PreprocessWorkItem> indexed{wgpu::BufferUsage::eStorage |
                                                                  wgpu::BufferUsage::eCopyDst};
        render_resource::RawBufferVec<PreprocessWorkItem> non_indexed{wgpu::BufferUsage::eStorage |
                                                                      wgpu::BufferUsage::eCopyDst};
        std::optional<GpuOcclusionCullingWorkItemBuffers> gpu_occlusion_culling;
    };

    std::variant<Direct, Indirect> storage;

    explicit PreprocessWorkItemBuffers(bool no_indirect_drawing = false)
        : storage(no_indirect_drawing ? std::variant<Direct, Indirect>{std::in_place_type<Direct>}
                                      : std::variant<Direct, Indirect>{std::in_place_type<Indirect>}) {}

    /** @brief Add one item to the stream appropriate for its mesh class. */
    void push(bool indexed, const PreprocessWorkItem& item) {
        std::visit(
            [&](auto& buffers) {
                using Buffers = std::decay_t<decltype(buffers)>;
                if constexpr (std::same_as<Buffers, Direct>) {
                    buffers.items.push(item);
                } else {
                    auto& target = indexed ? buffers.indexed : buffers.non_indexed;
                    target.push(item);
                    if (buffers.gpu_occlusion_culling) {
                        auto& late = indexed ? buffers.gpu_occlusion_culling->late_indexed
                                             : buffers.gpu_occlusion_culling->late_non_indexed;
                        late.add();
                    }
                }
            },
            storage);
    }

    /** @brief Clear work for a new frame but retain GPU allocations. */
    void clear() {
        std::visit(
            [](auto& buffers) {
                using Buffers = std::decay_t<decltype(buffers)>;
                if constexpr (std::same_as<Buffers, Direct>) {
                    buffers.items.clear();
                } else {
                    buffers.indexed.clear();
                    buffers.non_indexed.clear();
                    if (buffers.gpu_occlusion_culling) {
                        buffers.gpu_occlusion_culling->late_indexed.clear();
                        buffers.gpu_occlusion_culling->late_non_indexed.clear();
                        buffers.gpu_occlusion_culling->late_indirect_parameters_indexed_offset     = 0;
                        buffers.gpu_occlusion_culling->late_indirect_parameters_non_indexed_offset = 0;
                    }
                }
            },
            storage);
    }
};

/** @brief Initialize or retrieve a view's work-item buffers, including the
 * optional late culling streams (Bevy `get_or_create_work_item_buffer`). */
inline PreprocessWorkItemBuffers& get_or_create_work_item_buffer(
    std::unordered_map<view::RetainedViewEntity, PreprocessWorkItemBuffers>& work_item_buffers,
    view::RetainedViewEntity view,
    bool no_indirect_drawing,
    bool enable_gpu_occlusion_culling) {
    auto [it, inserted] = work_item_buffers.try_emplace(view, no_indirect_drawing);
    (void)inserted;
    if (auto* indirect = std::get_if<PreprocessWorkItemBuffers::Indirect>(&it->second.storage)) {
        if (enable_gpu_occlusion_culling && !indirect->gpu_occlusion_culling) {
            indirect->gpu_occlusion_culling.emplace();
        } else if (!enable_gpu_occlusion_culling) {
            indirect->gpu_occlusion_culling.reset();
        }
    }
    return it->second;
}

/** @brief Allocate late-dispatch metadata for a view's indirect work streams
 * (Bevy `init_work_item_buffers`). */
inline void init_work_item_buffers(
    PreprocessWorkItemBuffers& work_item_buffers,
    render_resource::RawBufferVec<LatePreprocessWorkItemIndirectParameters>& late_indexed_indirect_parameters,
    render_resource::RawBufferVec<LatePreprocessWorkItemIndirectParameters>& late_non_indexed_indirect_parameters) {
    if (auto* indirect = std::get_if<PreprocessWorkItemBuffers::Indirect>(&work_item_buffers.storage);
        indirect && indirect->gpu_occlusion_culling) {
        auto& culling = *indirect->gpu_occlusion_culling;
        culling.late_indirect_parameters_indexed_offset =
            static_cast<std::uint32_t>(late_indexed_indirect_parameters.push({}));
        culling.late_indirect_parameters_non_indexed_offset =
            static_cast<std::uint32_t>(late_non_indexed_indirect_parameters.push({}));
    }
}

/** @brief CPU-owned input-buffer allocator used by a GPU preprocessing
 * pipeline (Bevy `InstanceInputUniformBuffer`). */
template <render_resource::RawBufferElement InputData>
    requires std::default_initializable<InputData>
struct InstanceInputUniformBuffer {
    render_resource::RawBufferVec<InputData> buffer{wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst};
    std::vector<std::uint32_t> free_uniform_indices;

    void clear() noexcept {
        buffer.clear();
        free_uniform_indices.clear();
    }
    std::uint32_t add(const InputData& value) {
        if (!free_uniform_indices.empty()) {
            const auto index = free_uniform_indices.back();
            free_uniform_indices.pop_back();
            buffer.set(index, value);
            return index;
        }
        return static_cast<std::uint32_t>(buffer.push(value));
    }
    void remove(std::uint32_t index) { free_uniform_indices.push_back(index); }
    std::optional<InputData> get(std::uint32_t index) const {
        if (index >= buffer.len() ||
            std::ranges::find(free_uniform_indices, index) != free_uniform_indices.end())
            return std::nullopt;
        return *buffer.get(index);
    }
    InputData get_unchecked(std::uint32_t index) const { return *buffer.get(index); }
    void set(std::uint32_t index, const InputData& value) { buffer.set(index, value); }
    void ensure_nonempty() {
        if (buffer.is_empty()) buffer.push(InputData{});
    }
    std::size_t len() const noexcept { return buffer.len(); }
    bool is_empty() const noexcept { return buffer.is_empty(); }
};

/** @brief GPU preprocessing buffers for one phase, without the phase type
 * (Bevy `UntypedPhaseBatchedInstanceBuffers`). */
template <render_resource::GpuArrayBufferable BufferData>
struct UntypedPhaseBatchedInstanceBuffers {
    render_resource::UninitBufferVec<BufferData> data_buffer{wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst};
    std::unordered_map<view::RetainedViewEntity, PreprocessWorkItemBuffers> work_item_buffers;
    render_resource::RawBufferVec<LatePreprocessWorkItemIndirectParameters> late_indexed_indirect_parameters{
        wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eIndirect | wgpu::BufferUsage::eCopyDst};
    render_resource::RawBufferVec<LatePreprocessWorkItemIndirectParameters> late_non_indexed_indirect_parameters{
        wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eIndirect | wgpu::BufferUsage::eCopyDst};

    std::optional<std::reference_wrapper<const wgpu::Buffer>> instance_data_binding() const noexcept {
        if (const auto* buffer = data_buffer.buffer()) return std::cref(*buffer);
        return std::nullopt;
    }
    /** @brief Reset the frame's storage while preserving per-view allocations. */
    void clear() {
        data_buffer.clear();
        late_indexed_indirect_parameters.clear();
        late_non_indexed_indirect_parameters.clear();
        for (auto& [view, work_items] : work_item_buffers) {
            (void)view;
            work_items.clear();
        }
    }
    void write_buffers(const wgpu::Device& device, const wgpu::Queue& queue) {
        data_buffer.write_buffer(device);
        late_indexed_indirect_parameters.write_buffer(device, queue);
        late_non_indexed_indirect_parameters.write_buffer(device, queue);
        for (auto& [view, work_items] : work_item_buffers) {
            (void)view;
            std::visit(
                [&](auto& streams) {
                    using Streams = std::decay_t<decltype(streams)>;
                    if constexpr (std::same_as<Streams, PreprocessWorkItemBuffers::Direct>) {
                        streams.items.write_buffer(device, queue);
                    } else {
                        streams.indexed.write_buffer(device, queue);
                        streams.non_indexed.write_buffer(device, queue);
                        if (streams.gpu_occlusion_culling) {
                            streams.gpu_occlusion_culling->late_indexed.write_buffer(device);
                            streams.gpu_occlusion_culling->late_non_indexed.write_buffer(device);
                        }
                    }
                },
                work_items.storage);
        }
    }
};

/** @brief Typed resource wrapper for preprocessing buffers of one render phase
 * (Bevy `PhaseBatchedInstanceBuffers<PI, BD>`). */
template <phase::PhaseItem PI, render_resource::GpuArrayBufferable BufferData>
struct PhaseBatchedInstanceBuffers {
    UntypedPhaseBatchedInstanceBuffers<BufferData> buffers;
};

/** @brief Input and output buffers shared by phases that use the same GPU
 * preprocessing adapter data (Bevy `BatchedInstanceBuffers`). Phase-local
 * buffers are moved into this table after parallel preparation so concrete
 * preprocessing passes can look them up by phase type. */
template <render_resource::GpuArrayBufferable BufferData, render_resource::RawBufferElement BufferInputData>
    requires std::default_initializable<BufferInputData>
struct BatchedInstanceBuffers {
    InstanceInputUniformBuffer<BufferInputData> current_input_buffer;
    InstanceInputUniformBuffer<BufferInputData> previous_input_buffer;
    std::unordered_map<std::type_index, UntypedPhaseBatchedInstanceBuffers<BufferData>> phase_instance_buffers;

    void clear() {
        for (auto& [phase_type, buffers] : phase_instance_buffers) {
            (void)phase_type;
            buffers.clear();
        }
    }
};

/** @brief Remove cached work-item buffers for views that no longer exist
 * (Bevy `delete_old_work_item_buffers`). */
template <render_resource::GpuArrayBufferable BufferData, std::ranges::input_range Views>
    requires std::convertible_to<std::ranges::range_value_t<Views>, view::RetainedViewEntity>
void delete_old_work_item_buffers(UntypedPhaseBatchedInstanceBuffers<BufferData>& buffers, const Views& views) {
    std::unordered_set<view::RetainedViewEntity> retained_views;
    for (const auto& view : views) retained_views.insert(static_cast<view::RetainedViewEntity>(view));
    std::erase_if(buffers.work_item_buffers, [&](const auto& entry) { return !retained_views.contains(entry.first); });
}

/** @brief GPU buffers used by one render phase for indirect drawing (Bevy
 * `UntypedPhaseIndirectParametersBuffers`). */
EPIX_EXPORT struct UntypedPhaseIndirectParametersBuffers {
    render_resource::RawBufferVec<IndirectParametersIndexed> indexed_data;
    render_resource::RawBufferVec<IndirectParametersNonIndexed> non_indexed_data;
    render_resource::RawBufferVec<IndirectParametersCpuMetadata> indexed_cpu_metadata;
    render_resource::RawBufferVec<IndirectParametersCpuMetadata> non_indexed_cpu_metadata;
    render_resource::RawBufferVec<IndirectParametersGpuMetadata> indexed_gpu_metadata;
    render_resource::RawBufferVec<IndirectParametersGpuMetadata> non_indexed_gpu_metadata;
    render_resource::RawBufferVec<IndirectBatchSet> indexed_batch_sets;
    render_resource::RawBufferVec<IndirectBatchSet> non_indexed_batch_sets;

    explicit UntypedPhaseIndirectParametersBuffers(bool allow_copy_src = false)
        : indexed_data(indirect_usage(allow_copy_src)),
          non_indexed_data(indirect_usage(allow_copy_src)),
          indexed_cpu_metadata(storage_usage()),
          non_indexed_cpu_metadata(storage_usage()),
          indexed_gpu_metadata(storage_usage()),
          non_indexed_gpu_metadata(storage_usage()),
          indexed_batch_sets(storage_usage()),
          non_indexed_batch_sets(storage_usage()) {}

    static wgpu::BufferUsage indirect_usage(bool allow_copy_src) noexcept {
        auto usage = wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eIndirect | wgpu::BufferUsage::eCopyDst;
        return allow_copy_src ? usage | wgpu::BufferUsage::eCopySrc : usage;
    }
    static wgpu::BufferUsage storage_usage() noexcept {
        return wgpu::BufferUsage::eStorage | wgpu::BufferUsage::eCopyDst;
    }
    /** @brief Reserve matching CPU/GPU metadata and command slots, returning
     * the first allocated indirect-command index (Bevy
     * `UntypedPhaseIndirectParametersBuffers::allocate`). */
    std::uint32_t allocate(bool indexed, std::uint32_t count) {
        if (indexed) {
            const auto first = static_cast<std::uint32_t>(indexed_data.len());
            indexed_data.resize(indexed_data.len() + count);
            indexed_cpu_metadata.resize(indexed_cpu_metadata.len() + count);
            indexed_gpu_metadata.resize(indexed_gpu_metadata.len() + count);
            return first;
        }
        const auto first = static_cast<std::uint32_t>(non_indexed_data.len());
        non_indexed_data.resize(non_indexed_data.len() + count);
        non_indexed_cpu_metadata.resize(non_indexed_cpu_metadata.len() + count);
        non_indexed_gpu_metadata.resize(non_indexed_gpu_metadata.len() + count);
        return first;
    }
    /** @brief Number of allocated indirect commands for one mesh class. */
    std::size_t batch_count(bool indexed) const noexcept {
        return indexed ? indexed_data.len() : non_indexed_data.len();
    }
    /** @brief Number of multi-draw batch sets for one mesh class. */
    std::size_t batch_set_count(bool indexed) const noexcept {
        return indexed ? indexed_batch_sets.len() : non_indexed_batch_sets.len();
    }
    /** @brief Append a batch-set counter with the command-buffer base offset
     * (Bevy `add_batch_set`). */
    void add_batch_set(bool indexed, std::uint32_t indirect_parameters_base) {
        auto& batch_sets = indexed ? indexed_batch_sets : non_indexed_batch_sets;
        batch_sets.push({.indirect_parameters_count = 0, .indirect_parameters_base = indirect_parameters_base});
    }
    /** @brief The index that the next batch set will receive, or no value when
     * it would collide with Bevy's `NonMaxU32` sentinel. */
    std::optional<std::uint32_t> next_batch_set_index(bool indexed) const noexcept {
        const auto count = batch_set_count(indexed);
        if (count >= std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
        return static_cast<std::uint32_t>(count);
    }
    /** @brief Store metadata generated while a phase is batched. */
    void set_cpu_metadata(bool indexed, std::uint32_t index, IndirectParametersCpuMetadata value) {
        auto& metadata            = indexed ? indexed_cpu_metadata : non_indexed_cpu_metadata;
        metadata.set(index, value);
    }
    std::optional<std::reference_wrapper<const wgpu::Buffer>> data_buffer(bool indexed) const noexcept {
        const auto* data = indexed ? indexed_data.buffer() : non_indexed_data.buffer();
        if (!data) return std::nullopt;
        return std::cref(*data);
    }
    std::optional<std::reference_wrapper<const wgpu::Buffer>> cpu_metadata_buffer(bool indexed) const noexcept {
        const auto* metadata = indexed ? indexed_cpu_metadata.buffer() : non_indexed_cpu_metadata.buffer();
        if (!metadata) return std::nullopt;
        return std::cref(*metadata);
    }
    std::optional<std::reference_wrapper<const wgpu::Buffer>> gpu_metadata_buffer(bool indexed) const noexcept {
        const auto* metadata = indexed ? indexed_gpu_metadata.buffer() : non_indexed_gpu_metadata.buffer();
        if (!metadata) return std::nullopt;
        return std::cref(*metadata);
    }
    std::optional<std::reference_wrapper<const wgpu::Buffer>> batch_sets_buffer(bool indexed) const noexcept {
        const auto* batch_sets = indexed ? indexed_batch_sets.buffer() : non_indexed_batch_sets.buffer();
        if (!batch_sets) return std::nullopt;
        return std::cref(*batch_sets);
    }
    void clear() noexcept {
        indexed_data.clear();
        non_indexed_data.clear();
        indexed_cpu_metadata.clear();
        non_indexed_cpu_metadata.clear();
        indexed_gpu_metadata.clear();
        non_indexed_gpu_metadata.clear();
        indexed_batch_sets.clear();
        non_indexed_batch_sets.clear();
    }
    void write_buffers(const wgpu::Device& device, const wgpu::Queue& queue) {
        indexed_data.write_buffer(device, queue);
        non_indexed_data.write_buffer(device, queue);
        indexed_cpu_metadata.write_buffer(device, queue);
        non_indexed_cpu_metadata.write_buffer(device, queue);
        indexed_gpu_metadata.write_buffer(device, queue);
        non_indexed_gpu_metadata.write_buffer(device, queue);
        indexed_batch_sets.write_buffer(device, queue);
        non_indexed_batch_sets.write_buffer(device, queue);
    }
};

/** @brief Indirect buffers owned by one concrete render phase (Bevy
 * `PhaseIndirectParametersBuffers<PI>`). The later GPU collection pass moves
 * these phase-local buffers into its cross-phase working set, which permits
 * phase preparation to remain parallel. */
template <phase::PhaseItem PI>
struct PhaseIndirectParametersBuffers {
    UntypedPhaseIndirectParametersBuffers buffers;

    explicit PhaseIndirectParametersBuffers(bool allow_copy_src = false) : buffers(allow_copy_src) {}
};

/** @brief Indirect command buffers gathered from all phase-local preparation
 * resources (Bevy `IndirectParametersBuffers`). */
struct IndirectParametersBuffers {
    std::unordered_map<std::type_index, UntypedPhaseIndirectParametersBuffers> buffers;
    bool allow_copies_from_indirect_parameter_buffers = false;

    explicit IndirectParametersBuffers(bool allow_copy_src = false)
        : allow_copies_from_indirect_parameter_buffers(allow_copy_src) {}

    void clear() {
        for (auto& [phase_type, parameters] : buffers) {
            (void)phase_type;
            parameters.clear();
        }
    }
    void write_buffers(const wgpu::Device& device, const wgpu::Queue& queue) {
        for (auto& [phase_type, parameters] : buffers) {
            (void)phase_type;
            parameters.write_buffers(device, queue);
        }
    }
};

inline void clear_indirect_parameters_buffers(ecs::ResMut<IndirectParametersBuffers> buffers) { buffers->clear(); }

inline void write_indirect_parameters_buffers(ecs::ResMut<IndirectParametersBuffers> buffers,
                                              ecs::Res<wgpu::Device> device,
                                              ecs::Res<wgpu::Queue> queue) {
    buffers->write_buffers(device.get(), queue.get());
}

template <phase::PhaseItem PI>
void clear_phase_indirect_parameters_buffers(ecs::ResMut<PhaseIndirectParametersBuffers<PI>> buffers) {
    buffers->buffers.clear();
}

template <phase::PhaseItem PI>
void write_phase_indirect_parameters_buffers(ecs::ResMut<PhaseIndirectParametersBuffers<PI>> buffers,
                                             ecs::Res<wgpu::Device> device,
                                             ecs::Res<wgpu::Queue> queue) {
    buffers->buffers.write_buffers(device.get(), queue.get());
}

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
    } -> std::same_as<std::optional<
        std::pair<typename GetBatchData<T>::BufferData, std::optional<typename GetBatchData<T>::CompareData>>>>;
};

/**
 * @brief Trait to specialize for binning + GPU preprocessing batching support
 * (Bevy `GetFullBatchData`, batching/mod.rs:106-178).
 * @tparam T The adapter type identifying this batching specialization.
 */
template <typename T>
struct GetFullBatchData;

/** @brief Concept satisfied by valid GetFullBatchData specializations. */
template <typename T>
concept GetFullBatchDataImpl = GetBatchDataImpl<T> && requires(GetFullBatchData<T> batch) {
    requires std::constructible_from<GetFullBatchData<T>>;
    requires std::is_empty_v<GetFullBatchData<T>>;
    typename GetFullBatchData<T>::BufferInputData;
    requires render_resource::ShaderWritable<typename GetFullBatchData<T>::BufferInputData>;
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
        batch.get_binned_index(std::declval<typename GetBatchData<T>::Param&>(), std::declval<sync_world::MainEntity>())
    } -> std::same_as<std::optional<std::uint32_t>>;
    {
        batch.write_batch_indirect_parameters_metadata(
            std::declval<bool>(), std::declval<std::uint32_t>(), std::declval<std::optional<std::uint32_t>>(),
            std::declval<UntypedPhaseIndirectParametersBuffers&>(), std::declval<std::uint32_t>())
    } -> std::same_as<void>;
};

/**
 * @brief Build the per-view work items, output slots, indirect metadata, and
 * prepared batch sets for a binned GPU-preprocessed phase (Bevy
 * `gpu_preprocessing::batch_and_prepare_binned_render_phase`).
 *
 * A renderer-specific compute pass consumes the written work items and fills
 * `data_buffer`/indirect commands. Keeping that pass separate is deliberate:
 * Bevy's generic render crate owns this preparation, while the concrete mesh
 * renderer owns the preprocessing shader and input-uniform layout.
 */
template <phase::BinnedPhaseItem BPI, typename Adapter>
    requires(GetFullBatchDataImpl<Adapter>)
void batch_and_prepare_gpu_binned_phase(
    phase::BinnedRenderPhase<BPI>& render_phase,
    UntypedPhaseBatchedInstanceBuffers<typename GetBatchData<Adapter>::BufferData>& phase_buffers,
    UntypedPhaseIndirectParametersBuffers& indirect_parameters,
    const view::RetainedViewEntity& retained_view,
    bool no_indirect_drawing,
    bool enable_gpu_occlusion_culling,
    typename GetBatchData<Adapter>::Param& batch_param) {
    using BufferData  = typename GetBatchData<Adapter>::BufferData;
    auto& output_data = phase_buffers.data_buffer;
    auto& work_items  = get_or_create_work_item_buffer(phase_buffers.work_item_buffers, retained_view,
                                                       no_indirect_drawing, enable_gpu_occlusion_culling);
    work_items.clear();
    init_work_item_buffers(work_items, phase_buffers.late_indexed_indirect_parameters,
                           phase_buffers.late_non_indexed_indirect_parameters);

    const auto reserve_output = [&](std::uint32_t count) {
        return static_cast<std::uint32_t>(output_data.add_multiple(count));
    };
    const auto append_direct_batch = [&](const phase::BinnedRenderPhaseBatch& batch) {
        if (auto* direct = std::get_if<1>(&render_phase.batch_sets)) {
            direct->push_back(batch);
        } else if (auto* dynamic = std::get_if<0>(&render_phase.batch_sets)) {
            dynamic->push_back({batch});
        }
    };

    // In culling mode each multidrawable batch set owns one contiguous range
    // of indirect commands. The compute consumer fills the command contents.
    if (!no_indirect_drawing) {
        if (auto* multidraw_sets = std::get_if<2>(&render_phase.batch_sets)) {
            for (const auto& [batch_set_key, bins] : render_phase.multidrawable_meshes.iter()) {
                if (bins.empty()) continue;
                const bool indexed = batch_set_key.indexed();
                const std::uint32_t command_base =
                    indirect_parameters.allocate(indexed, static_cast<std::uint32_t>(bins.size()));
                const std::uint32_t batch_set_index = indirect_parameters.next_batch_set_index(indexed).value_or(0);
                const std::uint32_t output_base     = static_cast<std::uint32_t>(output_data.len());
                const auto first_bin                = bins.iter().begin();
                const auto first_entity             = first_bin->second.entities().iter().begin()->first;
                const std::uint32_t first_count     = static_cast<std::uint32_t>(first_bin->second.entities().size());
                std::uint32_t command_index         = command_base;

                for (const auto& [bin_key, bin] : bins.iter()) {
                    (void)bin_key;
                    const std::uint32_t first_output = reserve_output(static_cast<std::uint32_t>(bin.entities().size()));
                    GetFullBatchData<Adapter>{}.write_batch_indirect_parameters_metadata(
                        indexed, first_output, batch_set_index, indirect_parameters, command_index);
                    for (const auto& [main_entity, input_index] : bin.entities().iter()) {
                        (void)main_entity;
                        work_items.push(indexed, {.input_index                         = input_index.index,
                                                  .output_or_indirect_parameters_index = command_index});
                    }
                    ++command_index;
                }
                indirect_parameters.add_batch_set(indexed, command_base);
                multidraw_sets->push_back({
                    .first_batch = {.representative_entity = {ecs::Entity::PLACEHOLDER, first_entity},
                                    .instance_range        = {output_base, output_base + first_count},
                                    .extra_index           = phase::PhaseItemExtraIndex::indirect_parameters_range(
                                        command_base, command_index, batch_set_index)},
                    .bin_key     = first_bin->first,
                    .batch_count = command_index - command_base,
                    .index       = batch_set_index,
                });
            }
        }
    }

    // Prepare regular mesh bins. Direct preprocessing uses output indices;
    // indirect preprocessing uses one command per bin.
    for (const auto& [key, bin] : render_phase.batchable_meshes.iter()) {
        if (bin.is_empty()) continue;
        const bool indexed              = key.first.indexed();
        const std::uint32_t output_base = reserve_output(static_cast<std::uint32_t>(bin.entities().size()));
        const std::optional<std::uint32_t> indirect_index =
            no_indirect_drawing ? std::nullopt : std::optional{indirect_parameters.allocate(indexed, 1)};
        const std::optional<std::uint32_t> batch_set_index =
            indirect_index &&
                    std::holds_alternative<std::vector<phase::BinnedRenderPhaseBatchSet<typename BPI::BinKey>>>(
                        render_phase.batch_sets)
                ? indirect_parameters.next_batch_set_index(indexed)
                : std::nullopt;
        if (indirect_index) {
            GetFullBatchData<Adapter>{}.write_batch_indirect_parameters_metadata(indexed, output_base, batch_set_index,
                                                                                 indirect_parameters, *indirect_index);
        }
        const auto representative = bin.entities().iter().begin()->first;
        for (std::uint32_t offset = 0; const auto& entry : bin.entities().iter()) {
            const auto& input_index = entry.second;
            work_items.push(indexed,
                            {.input_index                         = input_index.index,
                             .output_or_indirect_parameters_index = indirect_index.value_or(output_base + offset)});
            ++offset;
        }
        phase::BinnedRenderPhaseBatch batch{
            .representative_entity = {ecs::Entity::PLACEHOLDER, representative},
            .instance_range        = {output_base, output_base + static_cast<std::uint32_t>(bin.entities().size())},
            .extra_index           = indirect_index ? phase::PhaseItemExtraIndex::indirect_parameters_range(
                                                          *indirect_index, *indirect_index + 1, batch_set_index)
                                                    : phase::PhaseItemExtraIndex::None,
        };
        if (auto* multidraw_sets = std::get_if<2>(&render_phase.batch_sets)) {
            const std::uint32_t index = batch_set_index.value_or(0);
            if (indirect_index) indirect_parameters.add_batch_set(indexed, *indirect_index);
            multidraw_sets->push_back({.first_batch = batch, .bin_key = key.second, .batch_count = 1, .index = index});
        } else {
            append_direct_batch(batch);
        }
    }

    // Unbatchable meshes are still copied by preprocessing, but each entity
    // retains an individual draw/indirect-command range.
    for (auto& [key, unbatchable] : render_phase.unbatchable_meshes.iter()) {
        const bool indexed = key.first.indexed();
        unbatchable.batches.clear();
        for (const auto& [main_entity, render_entity] : unbatchable.entities) {
            const auto input_index =
                GetFullBatchData<Adapter>{}.get_binned_index(batch_param, main_entity);
            if (!input_index) continue;
            const std::uint32_t output = reserve_output(1);
            const std::optional<std::uint32_t> indirect_index =
                no_indirect_drawing ? std::nullopt : std::optional{indirect_parameters.allocate(indexed, 1)};
            if (indirect_index) {
                GetFullBatchData<Adapter>{}.write_batch_indirect_parameters_metadata(
                    indexed, output, std::nullopt, indirect_parameters, *indirect_index);
                indirect_parameters.add_batch_set(indexed, *indirect_index);
            }
            work_items.push(indexed, {.input_index                         = *input_index,
                                      .output_or_indirect_parameters_index = indirect_index.value_or(output)});
            unbatchable.batches.emplace(
                main_entity, phase::BinnedRenderPhaseBatch{
                                 .representative_entity = {render_entity, main_entity},
                                 .instance_range        = {output, output + 1},
                                 .extra_index = indirect_index ? phase::PhaseItemExtraIndex::indirect_parameters_range(
                                                                     *indirect_index, *indirect_index + 1)
                                                               : phase::PhaseItemExtraIndex::None,
                             });
        }
    }
}

/** @brief ECS entry point for GPU binned-phase preparation (Bevy
 * `gpu_preprocessing::batch_and_prepare_binned_render_phase`). It is only
 * scheduled when the concrete renderer has installed the matching shared
 * `BatchedInstanceBuffers` resource. */
template <phase::BinnedPhaseItem BPI, typename Adapter>
    requires GetFullBatchDataImpl<Adapter>
void batch_and_prepare_gpu_binned_render_phase(
    ecs::ResMut<PhaseBatchedInstanceBuffers<BPI, typename GetBatchData<Adapter>::BufferData>> phase_buffers,
    ecs::ResMut<PhaseIndirectParametersBuffers<BPI>> indirect_parameters,
    ecs::ResMut<phase::ViewBinnedRenderPhases<BPI>> phases,
    ecs::Query<ecs::Item<const view::ExtractedView&,
                         ecs::Has<view::NoIndirectDrawing>,
                         ecs::Has<experimental::OcclusionCulling>>,
               ecs::With<view::ExtractedView>> views,
    typename GetBatchData<Adapter>::Param batch_param) {
    for (auto&& [extracted_view, no_indirect_drawing, occlusion_culling] : views.iter()) {
        const auto phase = phases->phases.find(extracted_view.retained_view_entity);
        if (phase == phases->phases.end()) continue;
        batch_and_prepare_gpu_binned_phase<BPI, Adapter>(
            phase->second, phase_buffers->buffers, indirect_parameters->buffers, extracted_view.retained_view_entity,
            no_indirect_drawing, occlusion_culling, batch_param);
    }
}

/** @brief Moves one phase's prepared GPU buffers into the shared lookup
 * tables, retaining the previous allocation for the next frame (Bevy
 * `gpu_preprocessing::collect_buffers_for_phase`). */
template <phase::PhaseItem PI, typename Adapter>
    requires GetFullBatchDataImpl<Adapter>
void collect_buffers_for_phase(
    ecs::ResMut<PhaseBatchedInstanceBuffers<PI, typename GetBatchData<Adapter>::BufferData>> phase_buffers,
    ecs::ResMut<PhaseIndirectParametersBuffers<PI>> phase_indirect_parameters,
    ecs::ResMut<BatchedInstanceBuffers<typename GetBatchData<Adapter>::BufferData,
                                       typename GetFullBatchData<Adapter>::BufferInputData>> batched_instance_buffers,
    ecs::ResMut<IndirectParametersBuffers> indirect_parameters) {
    const auto phase_type = std::type_index(typeid(PI));

    auto prepared_buffers = std::exchange(phase_buffers->buffers, {});
    if (auto it = batched_instance_buffers->phase_instance_buffers.find(phase_type);
        it == batched_instance_buffers->phase_instance_buffers.end()) {
        batched_instance_buffers->phase_instance_buffers.emplace(phase_type, std::move(prepared_buffers));
    } else {
        std::swap(it->second, prepared_buffers);
        prepared_buffers.clear();
        phase_buffers->buffers = std::move(prepared_buffers);
    }

    auto prepared_indirect = std::exchange(
        phase_indirect_parameters->buffers,
        UntypedPhaseIndirectParametersBuffers{indirect_parameters->allow_copies_from_indirect_parameter_buffers});
    if (auto it = indirect_parameters->buffers.find(phase_type); it == indirect_parameters->buffers.end()) {
        indirect_parameters->buffers.emplace(phase_type, std::move(prepared_indirect));
    } else {
        std::swap(it->second, prepared_indirect);
        prepared_indirect.clear();
        phase_indirect_parameters->buffers = std::move(prepared_indirect);
    }
}

/** @brief Uploads the shared input and per-phase GPU-preprocessing buffers
 * (Bevy `gpu_preprocessing::write_batched_instance_buffers`). Concrete
 * renderers register this for their `GetFullBatchData` adapter. */
template <typename Adapter>
    requires GetFullBatchDataImpl<Adapter>
void write_batched_instance_buffers(
    ecs::ResMut<BatchedInstanceBuffers<typename GetBatchData<Adapter>::BufferData,
                                       typename GetFullBatchData<Adapter>::BufferInputData>> buffers,
    ecs::Res<wgpu::Device> device,
    ecs::Res<wgpu::Queue> queue) {
    buffers->current_input_buffer.buffer.write_buffer(device.get(), queue.get());
    buffers->previous_input_buffer.buffer.write_buffer(device.get(), queue.get());
    for (auto& [phase_type, phase_buffers] : buffers->phase_instance_buffers) {
        (void)phase_type;
        phase_buffers.write_buffers(device.get(), queue.get());
    }
}

/** @brief GPU-preprocessing version of sorted-phase batching (Bevy
 * `gpu_preprocessing::batch_and_prepare_sorted_render_phase`). The first
 * item in each compatible run receives the output/indirect range; rendering
 * skips the remaining items in that range, exactly like Bevy's sorted phase.
 */
template <phase::CachedRenderPipelinePhaseItem P, typename Adapter>
    requires(GetFullBatchDataImpl<Adapter> && phase::SortedPhaseItem<P>)
void batch_and_prepare_gpu_sorted_phase(
    phase::SortedRenderPhase<P>& render_phase,
    UntypedPhaseBatchedInstanceBuffers<typename GetBatchData<Adapter>::BufferData>& phase_buffers,
    UntypedPhaseIndirectParametersBuffers& indirect_parameters,
    const view::RetainedViewEntity& retained_view,
    bool no_indirect_drawing,
    bool enable_gpu_occlusion_culling,
    typename GetBatchData<Adapter>::Param& batch_param) {
    using CompareData = typename GetBatchData<Adapter>::CompareData;
    using BatchMeta   = std::tuple<CachedPipelineId, phase::DrawFunctionId, CompareData>;
    struct ActiveBatch {
        std::size_t phase_item_start = 0;
        std::uint32_t instance_start = 0;
        bool indexed                 = false;
        std::optional<std::uint32_t> indirect_index;
        std::optional<BatchMeta> meta;
    };

    auto& output_data = phase_buffers.data_buffer;
    auto& work_items  = get_or_create_work_item_buffer(phase_buffers.work_item_buffers, retained_view,
                                                       no_indirect_drawing, enable_gpu_occlusion_culling);
    work_items.clear();
    init_work_item_buffers(work_items, phase_buffers.late_indexed_indirect_parameters,
                           phase_buffers.late_non_indexed_indirect_parameters);

    std::optional<ActiveBatch> active;
    const auto flush = [&](std::optional<std::uint32_t> instance_end = std::nullopt) {
        if (!active) return;
        auto& item        = render_phase.items[active->phase_item_start];
        auto& batch_range = item.batch_range();
        auto& extra_index = item.extra_index();
        batch_range = {active->instance_start, instance_end.value_or(static_cast<std::uint32_t>(output_data.len()))};
        extra_index = active->indirect_index ? phase::PhaseItemExtraIndex::indirect_parameters_range(
                                                  *active->indirect_index, *active->indirect_index + 1)
                                            : phase::PhaseItemExtraIndex::None;
        if (active->indirect_index) indirect_parameters.add_batch_set(active->indexed, *active->indirect_index);
        active.reset();
    };

    for (std::size_t current_index = 0; current_index < render_phase.items.size(); ++current_index) {
        auto& item = render_phase.items[current_index];
        const auto input_and_compare =
            GetFullBatchData<Adapter>{}.get_index_and_compare_data(batch_param, item.main_entity());
        if (!input_and_compare) {
            flush();
            continue;
        }
        const auto& [input_index, compare_data] = *input_and_compare;
        const std::optional<BatchMeta> current_meta =
            compare_data ? std::optional{BatchMeta{item.cached_pipeline(), item.draw_function(), *compare_data}}
                         : std::nullopt;
        const bool can_batch    = active && current_meta && active->meta && *current_meta == *active->meta;
        const auto output_index = static_cast<std::uint32_t>(output_data.add());
        if (!can_batch) {
            flush(output_index);
            const bool indexed = item.indexed();
            const std::optional<std::uint32_t> indirect_index =
                no_indirect_drawing ? std::nullopt : std::optional{indirect_parameters.allocate(indexed, 1)};
            if (indirect_index) {
                GetFullBatchData<Adapter>{}.write_batch_indirect_parameters_metadata(
                    indexed, output_index, std::nullopt, indirect_parameters, *indirect_index);
            }
            active = ActiveBatch{.phase_item_start = current_index,
                                 .instance_start   = output_index,
                                 .indexed          = indexed,
                                 .indirect_index   = indirect_index,
                                 .meta             = current_meta};
        }
        work_items.push(item.indexed(),
                        {.input_index                         = input_index,
                         .output_or_indirect_parameters_index = active->indirect_index.value_or(output_index)});
    }
    flush();
}

/** @brief ECS wrapper for GPU sorted-phase batching. */
template <phase::CachedRenderPipelinePhaseItem P, typename Adapter>
    requires(GetFullBatchDataImpl<Adapter> && phase::SortedPhaseItem<P>)
void batch_and_prepare_gpu_sorted_render_phase(
    ecs::ResMut<PhaseBatchedInstanceBuffers<P, typename GetBatchData<Adapter>::BufferData>> phase_buffers,
    ecs::ResMut<PhaseIndirectParametersBuffers<P>> indirect_parameters,
    ecs::ResMut<phase::ViewSortedRenderPhases<P>> sorted_render_phases,
    ecs::Query<ecs::Item<const view::ExtractedView&,
                         ecs::Has<view::NoIndirectDrawing>,
                         ecs::Has<experimental::OcclusionCulling>>,
               ecs::With<view::ExtractedView>> views,
    typename GetBatchData<Adapter>::Param batch_param) {
    for (auto&& [extracted_view, no_indirect_drawing, occlusion_culling] : views.iter()) {
        auto it = sorted_render_phases->find(extracted_view.retained_view_entity);
        if (it == sorted_render_phases->end()) continue;
        batch_and_prepare_gpu_sorted_phase<P, Adapter>(
            it->second, phase_buffers->buffers, indirect_parameters->buffers, extracted_view.retained_view_entity,
            no_indirect_drawing, occlusion_culling, batch_param);
    }
}

/** @brief Metadata used to decide whether consecutive sorted items can share
 * one draw (Bevy `BatchMeta`). */
template <typename CompareData>
struct CpuBatchMeta {
    CachedPipelineId pipeline;
    phase::DrawFunctionId draw_function;
    std::optional<std::uint32_t> dynamic_offset;
    CompareData compare_data;

    bool operator==(const CpuBatchMeta&) const = default;
};

/** @brief CPU-built instance buffer shared by every phase with the same
 * `BufferData` (Bevy `no_gpu_preprocessing::BatchedInstanceBuffer`). */
template <render_resource::GpuArrayBufferable BufferData>
struct BatchedInstanceBuffer {
    render_resource::GpuArrayBuffer<BufferData> buffer;

    explicit BatchedInstanceBuffer(const wgpu::Limits& limits) : buffer(limits) {}
};

/** @brief Whether a phase item opts in to Bevy-style automatic batching.
 * Items that do not expose the optional static flag retain Bevy's default of
 * participating in automatic batching. */
template <typename P>
inline constexpr bool automatic_batching_enabled = [] {
    if constexpr (requires {
                      { P::AUTOMATIC_BATCHING } -> std::convertible_to<bool>;
                  }) {
        return static_cast<bool>(P::AUTOMATIC_BATCHING);
    }
    return true;
}();

/** @brief CPU fallback for Bevy
 * `no_gpu_preprocessing::batch_and_prepare_sorted_render_phase`.
 *
 * It writes per-instance data, assigns the actual dynamic offset where the
 * uniform fallback requires one, and joins adjacent items only when their
 * pipeline, draw function, dynamic offset, and adapter comparison data all
 * agree. */
template <phase::CachedRenderPipelinePhaseItem P, typename Adapter>
    requires GetBatchDataImpl<Adapter>
void batch_and_prepare_sorted_phase(
    phase::SortedRenderPhase<P>& render_phase,
    render_resource::GpuArrayBuffer<typename GetBatchData<Adapter>::BufferData>& instance_buffer,
    typename GetBatchData<Adapter>::Param& batch_param) {
    using compare_data = typename GetBatchData<Adapter>::CompareData;
    std::optional<CpuBatchMeta<compare_data>> previous_meta;
    P* batch_head = nullptr;

    for (auto& item : render_phase.items) {
        auto batch_data = GetBatchData<Adapter>{}.get_batch_data(batch_param, {item.entity(), item.main_entity()});
        if (!batch_data) {
            auto& batch_range = item.batch_range();
            auto& extra_index = item.extra_index();
            batch_range = {0, 0};
            extra_index = phase::PhaseItemExtraIndex::None;
            previous_meta.reset();
            batch_head = nullptr;
            continue;
        }

        auto&& [buffer_data, optional_compare_data] = *batch_data;
        const auto buffer_index                     = instance_buffer.push(buffer_data);
        auto& batch_range = item.batch_range();
        auto& extra_index = item.extra_index();
        batch_range = {buffer_index.index, buffer_index.index + 1};
        extra_index = buffer_index.dynamic_offset ? phase::PhaseItemExtraIndex::dynamic_offset(*buffer_index.dynamic_offset)
                                                  : phase::PhaseItemExtraIndex::None;

        const std::optional<CpuBatchMeta<typename GetBatchData<Adapter>::CompareData>> current_meta =
            automatic_batching_enabled<P> && optional_compare_data
                ? std::optional{CpuBatchMeta<typename GetBatchData<Adapter>::CompareData>{
                      .pipeline       = item.cached_pipeline(),
                      .draw_function  = item.draw_function(),
                      .dynamic_offset = buffer_index.dynamic_offset,
                      .compare_data   = std::move(*optional_compare_data),
                  }}
                : std::nullopt;

        if (current_meta && previous_meta && *current_meta == *previous_meta) {
            batch_head->batch_range().second = item.batch_range().second;
        } else {
            batch_head = &item;
        }
        previous_meta = current_meta;
    }
}

/** @brief ECS wrapper for CPU sorted-phase batching. The buffer is shared by
 * all views of the phase, exactly as Bevy's `BatchedInstanceBuffer`. */
template <phase::CachedRenderPipelinePhaseItem P, typename Adapter>
    requires GetBatchDataImpl<Adapter>
void batch_and_prepare_sorted_render_phase(
    ecs::ResMut<BatchedInstanceBuffer<typename GetBatchData<Adapter>::BufferData>> instance_buffer,
    ecs::ResMut<phase::ViewSortedRenderPhases<P>> phases,
    typename GetBatchData<Adapter>::Param batch_param) {
    for (auto& [retained_view_entity, render_phase] : *phases) {
        (void)retained_view_entity;
        batch_and_prepare_sorted_phase<P, Adapter>(render_phase, instance_buffer->buffer, batch_param);
    }
}

/** @brief Clears the shared CPU instance buffer once per frame (Bevy
 * `clear_batched_cpu_instance_buffers`). Renderers that provide a CPU
 * fallback register this before their phase batch systems. */
template <typename Adapter>
    requires GetBatchDataImpl<Adapter>
void clear_batched_cpu_instance_buffers(
    ecs::ResMut<BatchedInstanceBuffer<typename GetBatchData<Adapter>::BufferData>> instance_buffer) {
    instance_buffer->buffer.clear();
}

/** @brief Uploads a CPU-batched sorted-phase instance buffer in the same
 * render set as Bevy's `write_batched_instance_buffer`. */
template <typename P, typename Adapter>
    requires GetBatchDataImpl<Adapter>
void write_batched_cpu_instance_buffer(
    ecs::ResMut<BatchedInstanceBuffer<typename GetBatchData<Adapter>::BufferData>> instance_buffer,
    ecs::Res<wgpu::Device> device,
    ecs::Res<wgpu::Queue> queue) {
    instance_buffer->buffer.write_buffer(device.get(), queue.get());
}

/** @brief Installs the CPU fallback for a sorted render phase. This is the
 * C++ counterpart of Bevy's sorted-phase batching path when GPU
 * preprocessing is unavailable. */
template <phase::CachedRenderPipelinePhaseItem P, typename Adapter>
    requires GetFullBatchDataImpl<Adapter>
struct CpuSortedRenderPhasePlugin {
    void attach(app::App& app) const {
        auto render_app = app.get_sub_app_mut(Render);
        if (!render_app) return;
        render_app->get().add_systems(
            Render, ecs::into(batch_and_prepare_sorted_render_phase<P, Adapter>)
                        .in_set(RenderSystems::PrepareResources)
                        .run_if([](const ecs::World& world) {
                            return world
                                .get_resource<BatchedInstanceBuffer<typename GetBatchData<Adapter>::BufferData>>()
                                .has_value();
                        })
                        .set_name(std::format("batch sorted render phase '{}'", meta::type_id<P>().short_name())));
        render_app->get().add_systems(
            Render,
            ecs::into(write_batched_cpu_instance_buffer<P, Adapter>)
                .in_set(RenderSystems::PrepareResourcesFlush)
                .run_if([](const ecs::World& world) {
                    return world.get_resource<BatchedInstanceBuffer<typename GetBatchData<Adapter>::BufferData>>()
                        .has_value();
                })
                .set_name(std::format("write sorted-phase instance buffer '{}'", meta::type_id<P>().short_name())));
    }
};

/** @brief CPU fallback for Bevy
 * `no_gpu_preprocessing::batch_and_prepare_binned_render_phase`. It builds
 * explicit batches for each render bin and preserves the dynamic-offset split
 * required by the uniform-buffer fallback. */
template <phase::BinnedPhaseItem BPI, typename Adapter>
    requires GetFullBatchDataImpl<Adapter>
void batch_and_prepare_binned_phase(
    phase::BinnedRenderPhase<BPI>& render_phase,
    render_resource::GpuArrayBuffer<typename GetBatchData<Adapter>::BufferData>& instance_buffer,
    typename GetBatchData<Adapter>::Param& batch_param) {
    auto* dynamic_batch_sets = std::get_if<0>(&render_phase.batch_sets);
    if (!dynamic_batch_sets) {
        spdlog::error("[render] Dynamic uniform batch sets are required when GPU preprocessing is disabled");
    }
    for (auto&& [key, bin] : render_phase.batchable_meshes.iter()) {
        (void)key;
        bin.clear_batches();
        for (const auto& [main_entity, input_uniform_index] : bin.entities().iter()) {
            (void)input_uniform_index;
            auto buffer_data =
                GetFullBatchData<Adapter>{}.get_binned_batch_data(batch_param, main_entity);
            if (!buffer_data) continue;
            const auto index = instance_buffer.push(*buffer_data);
            const auto extra = index.dynamic_offset ? phase::PhaseItemExtraIndex::dynamic_offset(*index.dynamic_offset)
                                                    : phase::PhaseItemExtraIndex::None;
            if (bin.batches.empty() || bin.batches.back().instance_range.second != index.index ||
                bin.batches.back().extra_index != extra) {
                bin.batches.push_back(phase::BinnedRenderPhaseBatch{
                    .representative_entity = {ecs::Entity::PLACEHOLDER, main_entity},
                    .instance_range        = {index.index, index.index},
                    .extra_index           = extra,
                });
            }
            bin.batches.back().instance_range.second = index.index + 1;
        }
        if (dynamic_batch_sets) dynamic_batch_sets->push_back(bin.batches);
    }

    for (auto&& [key, unbatchable] : render_phase.unbatchable_meshes.iter()) {
        (void)key;
        unbatchable.batches.clear();
        for (const auto& [main_entity, render_entity] : unbatchable.entities) {
            auto buffer_data = GetFullBatchData<Adapter>{}.get_binned_batch_data(batch_param, main_entity);
            if (!buffer_data) continue;
            const auto index = instance_buffer.push(*buffer_data);
            unbatchable.batches.emplace(
                main_entity, phase::BinnedRenderPhaseBatch{
                                 .representative_entity = {render_entity, main_entity},
                                 .instance_range        = {index.index, index.index + 1},
                                 .extra_index = index.dynamic_offset
                                                    ? phase::PhaseItemExtraIndex::dynamic_offset(*index.dynamic_offset)
                                                    : phase::PhaseItemExtraIndex::None,
                             });
        }
    }
}

/** @brief ECS wrapper for CPU binned-phase batching. */
template <phase::BinnedPhaseItem BPI, typename Adapter>
    requires GetFullBatchDataImpl<Adapter>
void batch_and_prepare_binned_render_phase(
    ecs::ResMut<BatchedInstanceBuffer<typename GetBatchData<Adapter>::BufferData>> instance_buffer,
    ecs::ResMut<phase::ViewBinnedRenderPhases<BPI>> phases,
    typename GetBatchData<Adapter>::Param batch_param) {
    for (auto& [view, render_phase] : phases->phases) {
        (void)view;
        batch_and_prepare_binned_phase<BPI, Adapter>(render_phase, instance_buffer->buffer, batch_param);
    }
}

/** @brief Installs the CPU fallback for a binned render phase. */
template <phase::BinnedPhaseItem BPI, typename Adapter>
    requires(GetFullBatchDataImpl<Adapter> && std::totally_ordered<typename BPI::BatchSetKey> &&
             std::totally_ordered<typename BPI::BinKey>)
struct CpuBinnedRenderPhasePlugin {
    void attach(app::App& app) const {
        auto render_app = app.get_sub_app_mut(Render);
        if (!render_app) return;
        auto& render_world = render_app->get().world_mut();
        render_world.init_resource<phase::ViewBinnedRenderPhases<BPI>>();
        render_app->get().add_systems(Render,
                                      ecs::into(phase::sweep_old_entities<BPI>).in_set(RenderSystems::QueueSweep));
        render_app->get().add_systems(Render,
                                      ecs::into(phase::sort_binned_render_phase<BPI>).in_set(RenderSystems::PhaseSort));
        render_app->get().add_systems(
            Render, ecs::into(batch_and_prepare_binned_render_phase<BPI, Adapter>)
                        .in_set(RenderSystems::PrepareResources)
                        .run_if([](const ecs::World& world) {
                            return world
                                .get_resource<BatchedInstanceBuffer<typename GetBatchData<Adapter>::BufferData>>()
                                .has_value();
                        })
                        .set_name(std::format("batch binned render phase '{}'", meta::type_id<BPI>().short_name())));
        render_app->get().add_systems(
            Render,
            ecs::into(write_batched_cpu_instance_buffer<BPI, Adapter>)
                .in_set(RenderSystems::PrepareResourcesFlush)
                .run_if([](const ecs::World& world) {
                    return world.get_resource<BatchedInstanceBuffer<typename GetBatchData<Adapter>::BufferData>>()
                        .has_value();
                })
                .set_name(std::format("write binned-phase instance buffer '{}'", meta::type_id<BPI>().short_name())));
    }
};

}  // namespace epix::render::batching

namespace epix::render::phase {

/** @brief Bevy-compatible automatic binned-phase plugin. The standard phase
 * plugin always owns its `GetFullBatchData` adapter; bespoke phase-only
 * registration is deliberately not an alternate public API here. */
template <BinnedPhaseItem BPI, typename Adapter>
void BinnedRenderPhasePlugin<BPI, Adapter>::attach(app::App& app) {
    static_assert(batching::GetFullBatchDataImpl<Adapter>,
                  "BinnedRenderPhasePlugin adapter must specialize GetFullBatchData");
    static_assert(std::totally_ordered<typename BPI::BatchSetKey> && std::totally_ordered<typename BPI::BinKey>,
                  "BinnedRenderPhasePlugin keys must be orderable for Bevy-compatible bin sorting");
    app.add_plugins(batching::CpuBinnedRenderPhasePlugin<BPI, Adapter>{});
    if (auto render_app = app.get_sub_app_mut(epix::render::Render)) {
        auto& world = render_app->get().world_mut();
        world.init_resource<
            batching::PhaseBatchedInstanceBuffers<BPI, typename batching::GetBatchData<Adapter>::BufferData>>();
        world.insert_resource(
            batching::PhaseIndirectParametersBuffers<BPI>{debug_flags.allow_copies_from_indirect_parameters()});
        render_app->get().add_systems(
            Render,
            ecs::into(batching::batch_and_prepare_gpu_binned_render_phase<BPI, Adapter>)
                .in_set(RenderSystems::PrepareResources)
                .run_if([](const ecs::World& world) {
                    return world
                        .get_resource<batching::BatchedInstanceBuffers<
                            typename batching::GetBatchData<Adapter>::BufferData,
                            typename batching::GetFullBatchData<Adapter>::BufferInputData>>()
                        .has_value();
                })
                .set_name(std::format("GPU batch binned render phase '{}'", meta::type_id<BPI>().short_name())));
        render_app->get().add_systems(
            Render,
            ecs::into(batching::collect_buffers_for_phase<BPI, Adapter>)
                .in_set(RenderSystems::PrepareResourcesCollectPhaseBuffers)
                .run_if([](const ecs::World& world) {
                    return world
                        .get_resource<batching::BatchedInstanceBuffers<
                            typename batching::GetBatchData<Adapter>::BufferData,
                            typename batching::GetFullBatchData<Adapter>::BufferInputData>>()
                        .has_value();
                })
                .set_name(std::format("collect binned phase GPU buffers '{}'", meta::type_id<BPI>().short_name())));
    }
}

/** @brief Bevy-compatible automatic sorted-phase plugin. */
template <CachedRenderPipelinePhaseItem P, typename Adapter>
void SortedRenderPhasePlugin<P, Adapter>::attach(app::App& app) {
    static_assert(batching::GetFullBatchDataImpl<Adapter>,
                  "SortedRenderPhasePlugin adapter must specialize GetFullBatchData");
    static_assert(SortedPhaseItem<P>,
                  "SortedRenderPhasePlugin phase items must provide indexed() for GPU indirect batching");
    app.add_plugins(batching::CpuSortedRenderPhasePlugin<P, Adapter>{});
    if (auto render_app = app.get_sub_app_mut(epix::render::Render)) {
        auto& world = render_app->get().world_mut();
        world.init_resource<ViewSortedRenderPhases<P>>();
        world.init_resource<
            batching::PhaseBatchedInstanceBuffers<P, typename batching::GetBatchData<Adapter>::BufferData>>();
        world.insert_resource(
            batching::PhaseIndirectParametersBuffers<P>{debug_flags.allow_copies_from_indirect_parameters()});
        render_app->get().add_systems(
            Render, ecs::into(batching::batch_and_prepare_gpu_sorted_render_phase<P, Adapter>)
                        .in_set(RenderSystems::PrepareResources)
                        .run_if([](const ecs::World& world) {
                            return world
                                .get_resource<batching::BatchedInstanceBuffers<
                                    typename batching::GetBatchData<Adapter>::BufferData,
                                    typename batching::GetFullBatchData<Adapter>::BufferInputData>>()
                                .has_value();
                        })
                        .set_name(std::format("GPU batch sorted render phase '{}'", meta::type_id<P>().short_name())));
        render_app->get().add_systems(
            Render,
            ecs::into(batching::collect_buffers_for_phase<P, Adapter>)
                .in_set(RenderSystems::PrepareResourcesCollectPhaseBuffers)
                .run_if([](const ecs::World& world) {
                    return world
                        .get_resource<batching::BatchedInstanceBuffers<
                            typename batching::GetBatchData<Adapter>::BufferData,
                            typename batching::GetFullBatchData<Adapter>::BufferInputData>>()
                        .has_value();
                })
                .set_name(std::format("collect sorted phase GPU buffers '{}'", meta::type_id<P>().short_name())));
    }
}

}  // namespace epix::render::phase

namespace epix::render::batching {

/**
 * @brief Plugin enabling automatic batching (Bevy `BatchingPlugin`,
 * batching/gpu_preprocessing.rs:45-79). Capability detection is installed in
 * the render world; phase-specific GPU work is registered by the phase that
 * owns its input/output buffers.
 */
EPIX_EXPORT struct BatchingPlugin {
    /** @brief Debugging flags (Bevy BatchingPlugin::debug_flags). */
    RenderDebugFlags debug_flags{};

    void attach(app::App& app) const noexcept {
        if (auto render_app = app.get_sub_app_mut(Render)) {
            auto device = render_app->get().world().get_resource<wgpu::Device>();
            std::optional<wgpu::BackendType> backend_type;
            if (auto adapter = render_app->get().world().get_resource<wgpu::Adapter>()) {
                wgpu::AdapterInfo info;
                adapter->get().getInfo(&info);
                backend_type = info.backendType;
            }
            auto& world = render_app->get().world_mut();
            world.insert_resource(IndirectParametersBuffers{debug_flags.allow_copies_from_indirect_parameters()});
            world.insert_resource(device ? GpuPreprocessingSupport::from_device(device->get(), backend_type)
                                         : GpuPreprocessingSupport{});
            render_app->get().add_systems(Render, ecs::into(clear_indirect_parameters_buffers)
                                                      .in_set(RenderSystems::ManageViews)
                                                      .set_name("clear indirect parameter buffers"));
            render_app->get().add_systems(Render, ecs::into(write_indirect_parameters_buffers)
                                                      .in_set(RenderSystems::PrepareResourcesFlush)
                                                      .set_name("write indirect parameter buffers"));
        }
    }
};

}  // namespace epix::render::batching
