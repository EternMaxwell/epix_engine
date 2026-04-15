module;

export module epix.render:pipeline_server;

import epix.core;
import epix.utils;
import epix.shader;
import BS.thread_pool;

import :pipeline;

using namespace epix::core;

namespace epix::render {
/** @brief Key type used to cache pipeline layouts by their bind group
 * layout IDs. */
export using LayoutCacheKey   = std::vector<wgpu::BindGroupLayoutId>;
export using CachedPipelineId = shader::CachedPipelineId;
struct LayoutKeyHash {
    std::size_t operator()(const LayoutCacheKey& key) const {
        std::size_t hash = 0;
        for (const auto& id : key) {
            hash ^= std::hash<std::size_t>()(id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};
/** @brief Cache of pipeline layouts to avoid redundant creation.
 *
 * Deduplicates layouts by their constituent bind group layout IDs. */
export struct LayoutCache {
   public:
    LayoutCache()                              = default;
    LayoutCache(const LayoutCache&)            = delete;
    LayoutCache& operator=(const LayoutCache&) = delete;

    /** @brief Get or create a pipeline layout for the given bind group layouts. */
    wgpu::PipelineLayout get(const wgpu::Device& device, std::ranges::range auto&& layouts)
        requires std::convertible_to<std::ranges::range_value_t<decltype(layouts)>, wgpu::BindGroupLayout>
    {
        LayoutCacheKey key = std::ranges::to<std::vector>(
            std::views::transform(layouts, [](const auto& layout) { return layout.id(); }));
        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second;
        }
        wgpu::PipelineLayout layout =
            device.createPipelineLayout(wgpu::PipelineLayoutDescriptor().setBindGroupLayouts(layouts));
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
export using PipelineDescriptor = std::variant<RenderPipelineDescriptor, ComputePipelineDescriptor>;
/** @brief Variant holding a created render or compute pipeline. */
export using Pipeline = std::variant<RenderPipeline, ComputePipeline>;
/** @brief Error code for pipeline creation failure. */
export enum PipelineError {
    CreationFailure,
};
/** @brief Variant of errors that can occur during pipeline server
 * operations. */
export using PipelineServerError = std::variant<PipelineError, shader::ShaderCacheError>;
/** @brief Returned when a queried pipeline is still queued or being
 * compiled. */
export struct GetPipelineNotReady {};
/** @brief Returned when a pipeline ID is out of range. */
export struct GetPipelineInvalidId {};
/** @brief Error variant returned by pipeline retrieval methods. */
export using GetPipelineError = std::variant<GetPipelineNotReady, GetPipelineInvalidId, PipelineServerError>;
export struct PipelineStateQueued {};
export using PipelineStateCreating = std::future<std::expected<Pipeline, PipelineServerError>>;
export struct PipelineStateRecoverableShaderError {
    shader::ShaderCacheError error;
    std::string signature;
    std::chrono::steady_clock::time_point first_seen;
    std::size_t repeat_count = 0;
    bool logged              = false;
};
/** @brief Current state of a cached pipeline in its lifecycle. */
export using CachedPipelineState = std::variant<PipelineStateQueued,
                                                PipelineStateCreating,
                                                PipelineStateRecoverableShaderError,
                                                Pipeline,
                                                PipelineServerError>;
struct CachedPipeline {
    PipelineDescriptor descriptor;
    CachedPipelineState state;
    std::optional<std::reference_wrapper<const Pipeline>> get_pipeline() const {
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
 * Mutation methods are private and driven by RenderPlugin systems in
 * ExtractSchedule.
 */
export struct PipelineServer {
   public:
    PipelineServer(const PipelineServer&)            = default;
    PipelineServer& operator=(const PipelineServer&) = default;
    PipelineServer(PipelineServer&&)                 = default;
    PipelineServer& operator=(PipelineServer&&)      = default;

    PipelineServer(wgpu::Device device);

    /** @brief Get the current state of a cached pipeline by id. */
    auto get_pipeline_state(CachedPipelineId id) const
        -> std::optional<std::reference_wrapper<const CachedPipelineState>>;
    /** @brief Get the render pipeline descriptor for a cached pipeline. */
    auto get_render_pipeline_descriptor(CachedPipelineId id) const
        -> std::optional<std::reference_wrapper<const RenderPipelineDescriptor>>;
    /** @brief Get the compute pipeline descriptor for a cached pipeline. */
    auto get_compute_pipeline_descriptor(CachedPipelineId id) const
        -> std::optional<std::reference_wrapper<const ComputePipelineDescriptor>>;
    /** @brief Get the compiled render pipeline, or an error if not ready. */
    auto get_render_pipeline(CachedPipelineId id) const
        -> std::expected<std::reference_wrapper<const RenderPipeline>, GetPipelineError>;
    /** @brief Get the compiled compute pipeline, or an error if not ready. */
    auto get_compute_pipeline(CachedPipelineId id) const
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

    static void process_pipeline_system(ResMut<PipelineServer> pipeline_server);
    static void extract_shaders(ResMut<PipelineServer> pipeline_server,
                                Extract<Res<assets::Assets<shader::Shader>>> shaders,
                                Extract<EventReader<assets::AssetEvent<shader::Shader>>> shader_events);

    std::shared_ptr<PipelineServerData> m_data;
};
}  // namespace epix::render