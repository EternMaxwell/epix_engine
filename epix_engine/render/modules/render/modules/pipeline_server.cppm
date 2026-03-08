module;

export module epix.render:pipeline_server;

import epix.core;
import epix.utils;
import BS.thread_pool;

import :pipeline;
import :shader_cache;

using namespace core;

namespace render {
export using LayoutCacheKey = std::vector<wgpu::BindGroupLayoutId>;
struct LayoutKeyHash {
    std::size_t operator()(const LayoutCacheKey& key) const {
        std::size_t hash = 0;
        for (const auto& id : key) {
            hash ^= std::hash<std::size_t>()(id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};
export struct LayoutCache {
   public:
    LayoutCache()                              = default;
    LayoutCache(const LayoutCache&)            = delete;
    LayoutCache& operator=(const LayoutCache&) = delete;

    wgpu::PipelineLayout get(const wgpu::Device& device, std::ranges::range auto&& layouts)
        requires std::convertible_to<std::ranges::range_value_t<decltype(layouts)>, wgpu::BindGroupLayout>
    {
        LayoutCacheKey key = layouts | std::views::transform([](const auto& layout) { return layout.id(); }) |
                             std::ranges::to<std::vector>();
        auto it            = cache.find(key);
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

std::optional<wgpu::ShaderModule> load_module(const wgpu::Device& device, const ShaderSource& source);

export using PipelineDescriptor = std::variant<RenderPipelineDescriptor, ComputePipelineDescriptor>;
export using Pipeline           = std::variant<RenderPipeline, ComputePipeline>;
export enum PipelineError {
    CreationFailure,
};
export struct GetPipelineNotReady {};   // pipeline is still queued or being compiled
export struct GetPipelineInvalidId {};  // id is out of range
// Failed carries the underlying PipelineServerError with its details
export using GetPipelineError = std::variant<GetPipelineNotReady, GetPipelineInvalidId, PipelineServerError>;
export using PipelineServerError = std::variant<PipelineError, ShaderCacheError>;
export struct PipelineStateQueued {};
export using PipelineStateCreating = std::future<std::expected<Pipeline, PipelineServerError>>;
export using CachedPipelineState =
    std::variant<PipelineStateQueued, PipelineStateCreating, Pipeline, PipelineServerError>;
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
export struct PipelineServer {
   public:
    PipelineServer(const PipelineServer&)            = delete;
    PipelineServer& operator=(const PipelineServer&) = delete;
    PipelineServer(PipelineServer&&)                 = default;
    PipelineServer& operator=(PipelineServer&&)      = default;

    PipelineServer(wgpu::Device device);

    auto get_pipeline_state(CachedPipelineId id) const
        -> std::optional<std::reference_wrapper<const CachedPipelineState>>;
    auto get_render_pipeline_descriptor(CachedPipelineId id) const
        -> std::optional<std::reference_wrapper<const RenderPipelineDescriptor>>;
    auto get_compute_pipeline_descriptor(CachedPipelineId id) const
        -> std::optional<std::reference_wrapper<const ComputePipelineDescriptor>>;
    auto get_render_pipeline(CachedPipelineId id) const
        -> std::expected<std::reference_wrapper<const RenderPipeline>, GetPipelineError>;
    auto get_compute_pipeline(CachedPipelineId id) const
        -> std::expected<std::reference_wrapper<const ComputePipeline>, GetPipelineError>;
    CachedPipelineId queue_render_pipeline(RenderPipelineDescriptor descriptor) const;
    CachedPipelineId queue_compute_pipeline(ComputePipelineDescriptor descriptor) const;

    void set_shader(assets::AssetId<Shader> id, Shader shader);
    void remove_shader(assets::AssetId<Shader> id);
    void process_queue();
    void process_pipeline(CachedPipeline& cached_pipeline, CachedPipelineId id);

    static void process_pipeline_system(ResMut<PipelineServer> pipeline_server);
    static void extract_shaders(ResMut<PipelineServer> pipeline_server,
                                Extract<Res<assets::Assets<Shader>>> shaders,
                                Extract<EventReader<assets::AssetEvent<Shader>>> shader_events);

   private:
    std::shared_ptr<utils::Mutex<LayoutCache>> layout_cache;
    std::shared_ptr<utils::Mutex<ShaderCache>> shader_cache;
    wgpu::Device device;
    std::vector<CachedPipeline> pipelines;
    std::unordered_set<CachedPipelineId> waiting_pipelines;
    utils::Mutex<std::vector<CachedPipeline>> new_pipelines;

    std::unique_ptr<BS::thread_pool<BS::tp::none>> pipeline_create_task_pool;
};
}  // namespace render