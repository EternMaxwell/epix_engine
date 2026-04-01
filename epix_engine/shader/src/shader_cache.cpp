module;

#include <spdlog/spdlog.h>

module epix.shader;

import :shader_cache;

namespace epix::shader {
auto ShaderCache::get(const wgpu::Device& device, CachedPipelineId pipeline, assets::AssetId<Shader> id)
    -> std::expected<std::reference_wrapper<const wgpu::ShaderModule>, ShaderCacheError> {
    auto it = data.find(id);
    if (it != data.end()) {
        // TODO: MSVC partial specialization workaround - cast AssetId<T> to UntypedAssetId
        spdlog::trace("[render.shader_cache] Cache hit for shader '{}'.", assets::UntypedAssetId(id));
        it->second.pipelines.insert(pipeline);
        return std::cref(it->second.processed_shader);
    }
    auto shader_it = shaders.find(id);
    if (shader_it == shaders.end()) {
        spdlog::trace("[render.shader_cache] Shader '{}' not loaded.", assets::UntypedAssetId(id));
        return std::unexpected(ShaderCacheError::NotLoaded);
    }
    spdlog::trace("[render.shader_cache] Compiling shader module for '{}'.", assets::UntypedAssetId(id));
    const Shader& shader                     = shader_it->second;
    std::optional<wgpu::ShaderModule> module = load_module(device, shader);
    if (!module) {
        return std::unexpected(ShaderCacheError::ModuleCreationFailure);
    }
    data[id] = ShaderData{{pipeline}, std::move(*module)};
    return std::cref(data[id].processed_shader);
}
auto ShaderCache::clear(assets::AssetId<Shader> id) -> std::vector<CachedPipelineId> {
    auto affected_pipelines = std::vector<CachedPipelineId>{};
    auto it                 = data.find(id);
    if (it != data.end()) {
        spdlog::trace("[render.shader_cache] Clearing cached module for '{}', {} affected pipelines.",
                      assets::UntypedAssetId(id), it->second.pipelines.size());
        affected_pipelines.append_range(it->second.pipelines);
        data.erase(it);
    }
    return affected_pipelines;
}
auto ShaderCache::set_shader(assets::AssetId<Shader> id, Shader shader) -> std::vector<CachedPipelineId> {
    auto affected_pipelines = clear(id);
    shaders[id]             = std::move(shader);
    return affected_pipelines;
}
auto ShaderCache::remove(assets::AssetId<Shader> id) -> std::vector<CachedPipelineId> {
    auto affected_pipelines = clear(id);
    shaders.erase(id);
    return affected_pipelines;
}
}  // namespace epix::shader
