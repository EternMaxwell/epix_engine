module;

export module epix.render:shader_cache;

import :shader;
import :pipeline;

namespace epix::render {
/** @brief Strongly-typed ID referencing a cached pipeline in the
 * PipelineServer. */
export struct CachedPipelineId : public utils::int_base<std::uint64_t> {
    using utils::int_base<std::uint64_t>::int_base;
};
}  // namespace render
template <>
struct std::hash<epix::render::CachedPipelineId> {
    std::size_t operator()(const epix::render::CachedPipelineId& id) const { return std::hash<std::uint64_t>()(id.get()); }
};
namespace epix::render {
/** @brief Cached shader data associating a processed shader module with
 * the pipelines that depend on it. */
export struct ShaderData {
    /** @brief Set of pipeline IDs that use this shader. */
    std::unordered_set<CachedPipelineId> pipelines;
    /** @brief The compiled WebGPU shader module. */
    wgpu::ShaderModule processed_shader;
};
/** @brief Error codes for shader cache operations. */
export enum ShaderCacheError {
    /** @brief Shader source not yet loaded. */
    NotLoaded,
    /** @brief Failed to create the shader module from source. */
    ModuleCreationFailure,
};
/** @brief Cache that compiles and stores shader modules, tracking which
 * pipelines depend on each shader for hot-reload invalidation. */
export struct ShaderCache {
    using load_func = std::optional<wgpu::ShaderModule> (*)(const wgpu::Device& device, const Shader& shader);

   private:
    std::unordered_map<assets::AssetId<Shader>, ShaderData> data;
    std::unordered_map<assets::AssetId<Shader>, Shader> shaders;
    load_func load_module;

   public:
    ShaderCache(load_func load_module) : load_module(load_module) {}
    /**
     * @brief Get the processed shader module for given shader id, and cache the pipeline id that uses it. Returns
     * nullopt if shader is not loaded or failed to process.
     *
     * @param pipeline The pipeline id that is trying to use the shader, used for caching which pipelines are affected
     * when shader is updated
     * @param id The asset id of the shader
     * @return std::expected<std::reference_wrapper<const wgpu::ShaderModule>, ShaderError>
     */
    auto get(const wgpu::Device& device, CachedPipelineId pipeline, assets::AssetId<Shader> id)
        -> std::expected<std::reference_wrapper<const wgpu::ShaderModule>, ShaderCacheError>;
    /** @brief Clear cached pipeline IDs associated with a shader, returning the affected IDs. */
    auto clear(assets::AssetId<Shader> id) -> std::vector<CachedPipelineId>;
    /** @brief Set or replace a shader, returning any affected pipeline IDs. */
    auto set_shader(assets::AssetId<Shader> id, Shader shader) -> std::vector<CachedPipelineId>;
    /** @brief Remove a shader from the cache, returning any affected pipeline IDs. */
    auto remove(assets::AssetId<Shader> id) -> std::vector<CachedPipelineId>;
};
}  // namespace render