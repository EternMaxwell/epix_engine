#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <BS_thread_pool.hpp>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <epix/ecs.hpp>
#include <epix/shader.hpp>
#include <epix/utils.hpp>
#include <expected>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#endif

#include <epix/render/pipeline.hpp>

namespace epix::render {
/** @brief Key type used to cache pipeline layouts by their bind group
 * layout IDs plus the full contents of every push constant range.
 *
 * Keying only the push-constant COUNT would let two pipelines with different
 * (stage, offset, size) ranges collide and reuse the wrong PipelineLayout
 * (wgpu validates the declared ranges against the shader's usage). Bevy keys
 * on the immediate-data size; epix uses wgpu push constants, so the ranges
 * themselves must be part of the key. */
struct LayoutCacheKey {
    std::vector<wgpu::BindGroupLayoutId> bind_group_layouts;
    /** @brief Push constant ranges flattened to {stages, start, end} per range. */
    std::vector<std::array<std::uint32_t, 3>> push_constant_ranges;
    bool operator==(const LayoutCacheKey&) const noexcept = default;
};
EPIX_EXPORT using CachedPipelineId = shader::CachedPipelineId;
/** @brief Typed id for cached render pipelines (Bevy 'CachedRenderPipelineId'). */
EPIX_EXPORT struct CachedRenderPipelineId : CachedPipelineId {
    using CachedPipelineId::CachedPipelineId;
};
/** @brief Typed id for cached compute pipelines (Bevy 'CachedComputePipelineId'). */
EPIX_EXPORT struct CachedComputePipelineId : CachedPipelineId {
    using CachedPipelineId::CachedPipelineId;
};
/** @brief Sentinel id for an invalid render pipeline (Bevy
 * CachedRenderPipelineId::INVALID = usize::MAX). */
EPIX_EXPORT inline constexpr CachedRenderPipelineId INVALID_RENDER_PIPELINE_ID{
    std::numeric_limits<std::uint64_t>::max()};
/** @brief Sentinel id for an invalid compute pipeline (Bevy
 * CachedComputePipelineId::INVALID = usize::MAX). */
EPIX_EXPORT inline constexpr CachedComputePipelineId INVALID_COMPUTE_PIPELINE_ID{
    std::numeric_limits<std::uint64_t>::max()};
struct LayoutKeyHash {
    std::size_t operator()(const LayoutCacheKey& key) const noexcept {
        std::size_t hash = 0;
        auto combine    = [&](std::size_t value) {
            hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        };
        for (const auto& id : key.bind_group_layouts) {
            combine(static_cast<std::size_t>(id));
        }
        for (const auto& range : key.push_constant_ranges) {
            for (const auto value : range) {
                combine(static_cast<std::size_t>(value));
            }
        }
        return hash;
    }
};
/** @brief Cache of pipeline layouts to avoid redundant creation.
 *
 * Deduplicates layouts by their constituent bind group layout IDs. */
EPIX_EXPORT struct LayoutCache {
   public:
    LayoutCache()                              = default;
    LayoutCache(const LayoutCache&)            = delete;
    LayoutCache& operator=(const LayoutCache&) = delete;

    /** @brief Get or create a pipeline layout for the given bind group layouts
     * and optional push constant ranges (Bevy PipelineCache layout caching
     * incl. push_constant_ranges, pipeline_cache.rs:210-228). */
    wgpu::PipelineLayout get(const wgpu::Device& device,
                             std::ranges::range auto&& layouts,
                             std::span<const wgpu::PushConstantRange> push_constant_ranges = {})
        requires std::convertible_to<std::ranges::range_value_t<decltype(layouts)>, wgpu::BindGroupLayout>
    {
        LayoutCacheKey key;
        key.bind_group_layouts = std::ranges::to<std::vector>(
            std::views::transform(layouts, [](const auto& layout) { return layout.id(); }));
        key.push_constant_ranges.reserve(push_constant_ranges.size());
        for (const auto& range : push_constant_ranges) {
            key.push_constant_ranges.push_back(
                {static_cast<std::uint32_t>(range.stages), range.start, range.end});
        }
        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second;
        }
        wgpu::PipelineLayoutDescriptor descriptor;
        descriptor.setBindGroupLayouts(layouts);
        if (!push_constant_ranges.empty()) {
            // wgpu-native exposes push constants via the PipelineLayoutExtras
            // chained struct on the layout descriptor.
            wgpu::PipelineLayoutExtras extras;
            extras.setPushConstantRanges(push_constant_ranges);
            descriptor.setNextInChain(extras);
        }
        wgpu::PipelineLayout layout = device.createPipelineLayout(descriptor);
        cache[key] = layout;
        return layout;
    }

   private:
    std::unordered_map<LayoutCacheKey, wgpu::PipelineLayout, LayoutKeyHash> cache;
};

std::expected<wgpu::ShaderModule, shader::ShaderCacheError> load_module(const wgpu::Device& device,
                                                                        const shader::ShaderCacheSource& source,
                                                                        shader::ValidateShader);

/** @brief Variant describing either a render or compute pipeline
 * descriptor. */
EPIX_EXPORT using PipelineDescriptor = std::variant<RenderPipelineDescriptor, ComputePipelineDescriptor>;
/** @brief Variant holding a created render or compute pipeline. */
EPIX_EXPORT using Pipeline = std::variant<RenderPipeline, ComputePipeline>;
/** @brief Error code for pipeline creation failure. */
EPIX_EXPORT enum PipelineError {
    CreationFailure,
};
/** @brief Variant of errors that can occur during pipeline server
 * operations. */
EPIX_EXPORT using PipelineServerError = std::variant<PipelineError, shader::ShaderCacheError>;
/** @brief Returned when a queried pipeline is still queued or being
 * compiled. */
EPIX_EXPORT struct GetPipelineNotReady {};
/** @brief Returned when a pipeline ID is out of range. */
EPIX_EXPORT struct GetPipelineInvalidId {};
/** @brief Error variant returned by pipeline retrieval methods. */
EPIX_EXPORT using GetPipelineError = std::variant<GetPipelineNotReady, GetPipelineInvalidId, PipelineServerError>;
EPIX_EXPORT struct PipelineStateQueued {};
EPIX_EXPORT using PipelineStateCreating = std::future<std::expected<Pipeline, PipelineServerError>>;
EPIX_EXPORT struct PipelineStateRecoverableShaderError {
    shader::ShaderCacheError error;
    std::string signature;
    std::chrono::steady_clock::time_point first_seen;
    std::size_t repeat_count = 0;
    bool logged              = false;
};
/** @brief Current state of a cached pipeline in its lifecycle. */
EPIX_EXPORT using CachedPipelineState = std::variant<PipelineStateQueued,
                                                     PipelineStateCreating,
                                                     PipelineStateRecoverableShaderError,
                                                     Pipeline,
                                                     PipelineServerError>;
struct CachedPipeline {
    PipelineDescriptor descriptor;
    CachedPipelineState state;
    std::optional<std::reference_wrapper<const Pipeline>> get_pipeline() const noexcept {
        if (std::holds_alternative<Pipeline>(state)) {
            return std::get<Pipeline>(state);
        }
        return std::nullopt;
    }
};
/** @brief Internal data shared between all copies of a PipelineServer. */
struct PipelineServerData {
    PipelineServerData(const PipelineServerData&)            = delete;
    PipelineServerData& operator=(const PipelineServerData&) = delete;

    std::shared_ptr<utils::Mutex<LayoutCache>> layout_cache;
    std::shared_ptr<utils::Mutex<shader::ShaderCache>> shader_cache;
    wgpu::Device device;
    std::vector<CachedPipeline> pipelines;
    std::unordered_set<CachedPipelineId> waiting_pipelines;
    utils::Mutex<std::vector<CachedPipeline>> new_pipelines;
    std::unique_ptr<BS::thread_pool<BS::tp::none>> pipeline_create_task_pool;

    PipelineServerData(wgpu::Device device);
};
/** @brief Central server that manages pipeline creation, caching, and
 * shader dependency tracking.
 *
 * Pipelines are queued via `queue_render_pipeline()` /
 * `queue_compute_pipeline()` and compiled asynchronously in a thread
 * pool. The underlying data is shared across copies via a shared_ptr,
 * allowing PipelineServer to exist in both the main app and render app.
 * Mutation is private and driven by the render schedule. The shared state
 * deliberately lets the main and render worlds observe the same server, but
 * render-graph nodes are read-only consumers: they must never synchronously
 * advance pipeline creation.
 */
EPIX_EXPORT struct PipelineServer {
   public:
    PipelineServer(const PipelineServer&)            = default;
    PipelineServer& operator=(const PipelineServer&) = default;
    PipelineServer(PipelineServer&&)                 = default;
    PipelineServer& operator=(PipelineServer&&)      = default;

    PipelineServer(wgpu::Device device);

    /** @brief Get the current state of a cached pipeline by id. */
    auto get_pipeline_state(CachedPipelineId id) const noexcept
        -> std::optional<std::reference_wrapper<const CachedPipelineState>>;
    /** @brief Number of cached pipelines (Bevy pipelines().count()). */
    std::size_t pipeline_count() const noexcept { return m_data->pipelines.size(); }
    /** @brief Number of pipelines currently waiting to be processed (Bevy
     * waiting_pipelines().count()). */
    std::size_t waiting_pipeline_count() const noexcept { return m_data->waiting_pipelines.size(); }
    /** @brief The set of pipeline ids currently waiting to be processed (Bevy
     * waiting_pipelines()). */
    const std::unordered_set<CachedPipelineId>& waiting_pipelines() const noexcept {
        return m_data->waiting_pipelines;
    }
    /** @brief Get the render pipeline descriptor for a cached pipeline. */
    auto get_render_pipeline_descriptor(CachedPipelineId id) const noexcept
        -> std::optional<std::reference_wrapper<const RenderPipelineDescriptor>>;
    /** @brief Get the compute pipeline descriptor for a cached pipeline. */
    auto get_compute_pipeline_descriptor(CachedPipelineId id) const noexcept
        -> std::optional<std::reference_wrapper<const ComputePipelineDescriptor>>;
    /** @brief Get the compiled render pipeline, or an error if not ready. */
    auto get_render_pipeline(CachedPipelineId id) const noexcept
        -> std::expected<std::reference_wrapper<const RenderPipeline>, GetPipelineError>;
    /** @brief Get the compiled compute pipeline, or an error if not ready. */
    auto get_compute_pipeline(CachedPipelineId id) const noexcept
        -> std::expected<std::reference_wrapper<const ComputePipeline>, GetPipelineError>;
    /** @brief Queue a render pipeline for asynchronous creation. */
    CachedPipelineId queue_render_pipeline(RenderPipelineDescriptor descriptor) const;
    /** @brief Queue a compute pipeline for asynchronous creation. */
    CachedPipelineId queue_compute_pipeline(ComputePipelineDescriptor descriptor) const;

   private:
    friend struct RenderPlugin;

    void set_shader(assets::AssetId<shader::Shader> id, shader::Shader shader);
    void remove_shader(assets::AssetId<shader::Shader> id);
    void process_queue();
    void process_pipeline(CachedPipeline& cached_pipeline, CachedPipelineId id);

    static void process_pipeline_system(epix::ecs::ResMut<PipelineServer> pipeline_server);
    static void extract_shaders(
        epix::ecs::ResMut<PipelineServer> pipeline_server,
        epix::app::Extract<epix::ecs::Res<assets::Assets<shader::Shader>>> shaders,
        epix::app::Extract<epix::ecs::EventReader<assets::AssetEvent<shader::Shader>>> shader_events);

    std::shared_ptr<PipelineServerData> m_data;
};
}  // namespace epix::render
