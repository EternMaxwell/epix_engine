module;

export module epix.render:shader_cache;

import :shader;
import :pipeline;

namespace render {
export struct CachedPipelineId : public utils::int_base<std::uint64_t> {
    using utils::int_base<std::uint64_t>::int_base;
};
}  // namespace render
template <>
struct std::hash<render::CachedPipelineId> {
    std::size_t operator()(const render::CachedPipelineId& id) const { return std::hash<std::uint64_t>()(id.get()); }
};
namespace render {
export struct ShaderData {
    std::unordered_set<CachedPipelineId> pipelines;
    wgpu::ShaderModule processed_shader;
};
export enum ShaderCacheError {
    NotLoaded,
    ModuleCreationFailure,
};
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
    /// Clears the cached pipeline ids associated with the shader, return the affected pipeline ids
    auto clear(assets::AssetId<Shader> id) -> std::vector<CachedPipelineId>;
    /// Set shader for shader id, return the affected pipeline ids if any
    auto set_shader(assets::AssetId<Shader> id, Shader shader) -> std::vector<CachedPipelineId>;
    /// Remove a shader from cache, return the affected pipeline ids if any
    auto remove(assets::AssetId<Shader> id) -> std::vector<CachedPipelineId>;
};
}  // namespace render