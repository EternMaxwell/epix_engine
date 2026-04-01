module;

#include <spdlog/spdlog.h>

module epix.shader;

import :shader_cache;

using namespace epix::shader;

namespace epix::shader {

// ─── ShaderCache constructor ───────────────────────────────────────────────
ShaderCache::ShaderCache(wgpu::Device device, LoadModuleFn load_module)
    : device_(std::move(device)), load_module_(std::move(load_module)) {}

// ─── add_import_to_composer (private static) ──────────────────────────────
std::expected<void, ShaderCacheError> ShaderCache::add_import_to_composer(
    ShaderComposer& composer,
    const std::unordered_map<ShaderImport, assets::AssetId<Shader>>& import_path_shaders,
    const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders,
    const ShaderImport& import_ref) {
    const std::string module_name = import_ref.module_name();

    // Already registered
    if (composer.contains_module(module_name)) return {};

    // Look up the shader that provides this import
    auto it = import_path_shaders.find(import_ref);
    if (it == import_path_shaders.end()) return std::unexpected(ShaderCacheError::import_not_available());
    auto sit = shaders.find(it->second);
    if (sit == shaders.end()) return std::unexpected(ShaderCacheError::import_not_available());
    const Shader& shader = sit->second;

    // Recurse into this shader's own imports first
    for (const auto& dep : shader.imports) {
        if (auto res = add_import_to_composer(composer, import_path_shaders, shaders, dep); !res)
            return std::unexpected(res.error());
    }

    // Register this module
    if (auto res = composer.add_module(module_name, shader.source.as_str(), shader.shader_defs); !res)
        return std::unexpected(ShaderCacheError::process_error(std::move(res.error())));

    spdlog::trace("[shader.cache] Added module '{}' to composer.", module_name);
    return {};
}

// ─── clear (private) ──────────────────────────────────────────────────────
std::vector<CachedPipelineId> ShaderCache::clear(assets::AssetId<Shader> id) {
    std::vector<CachedPipelineId> affected;

    // BFS
    std::queue<assets::AssetId<Shader>> work;
    work.push(id);
    std::unordered_set<assets::AssetId<Shader>> visited;

    while (!work.empty()) {
        auto cur = work.front();
        work.pop();
        if (!visited.insert(cur).second) continue;

        auto dit = data_.find(cur);
        if (dit != data_.end()) {
            for (auto pid : dit->second.pipelines) affected.push_back(pid);
            dit->second.processed_shaders.clear();
            // Enqueue dependents for transitive invalidation
            for (auto dep_id : dit->second.dependents) work.push(dep_id);
        }

        // Remove from composer
        auto sit = shaders_.find(cur);
        if (sit != shaders_.end()) {
            composer_.remove_module(sit->second.import_path.module_name());
        }
    }

    return affected;
}

// ─── get ──────────────────────────────────────────────────────────────────
std::expected<std::shared_ptr<wgpu::ShaderModule>, ShaderCacheError> ShaderCache::get(
    CachedPipelineId pipeline, assets::AssetId<Shader> id, std::span<const ShaderDefVal> shader_defs) {
    auto sit = shaders_.find(id);
    if (sit == shaders_.end()) return std::unexpected(ShaderCacheError::not_loaded(id));
    const Shader& shader = sit->second;

    auto& shader_data = data_[id];

    // Check all AssetPath imports are resolved
    std::size_t n_asset_imports = std::ranges::count_if(
        shader.imports, [](const ShaderImport& i) { return i.kind == ShaderImport::Kind::AssetPath; });
    std::size_t n_resolved = std::ranges::count_if(
        shader_data.resolved_imports, [](const auto& kv) { return kv.first.kind == ShaderImport::Kind::AssetPath; });
    if (n_asset_imports != n_resolved) return std::unexpected(ShaderCacheError::import_not_available());

    shader_data.pipelines.insert(pipeline);

    // Merge caller defs with the shader's own defs (caller wins)
    std::vector<ShaderDefVal> merged_defs(shader_defs.begin(), shader_defs.end());
    for (const auto& d : shader.shader_defs) {
        bool already = std::ranges::any_of(merged_defs, [&](const ShaderDefVal& e) { return e.name == d.name; });
        if (!already) merged_defs.push_back(d);
    }

    // Cache lookup
    auto cache_it = shader_data.processed_shaders.find(merged_defs);
    if (cache_it != shader_data.processed_shaders.end()) {
        spdlog::trace("[shader.cache] Cache hit for shader '{}'.", assets::UntypedAssetId(id));
        return cache_it->second;
    }

    spdlog::debug("[shader.cache] Compiling shader '{}' with {} defs.", assets::UntypedAssetId(id), merged_defs.size());

    // Compose source
    ShaderCacheSource source;
    if (std::holds_alternative<Source::SpirV>(shader.source.data)) {
        const auto& bytes = std::get<Source::SpirV>(shader.source.data).bytes;
        source            = ShaderCacheSource{ShaderCacheSource::SpirV{std::span<const std::uint8_t>(bytes)}};
    } else {
        // WGSL: register all imports, then compose
        for (const auto& imp : shader.imports) {
            if (auto res = add_import_to_composer(composer_, import_path_shaders_, shaders_, imp); !res)
                return std::unexpected(res.error());
        }
        auto composed = composer_.compose(shader.source.as_str(), shader.path, merged_defs);
        if (!composed) return std::unexpected(ShaderCacheError::process_error(std::move(composed.error())));
        source = ShaderCacheSource{ShaderCacheSource::Wgsl{std::move(composed.value())}};
    }

    // Ask renderer to compile
    auto module_result = load_module_(device_, source, shader.validate_shader);
    if (!module_result) return std::unexpected(module_result.error());

    auto ptr = std::make_shared<wgpu::ShaderModule>(std::move(module_result.value()));
    shader_data.processed_shaders.emplace(merged_defs, ptr);
    return ptr;
}

// ─── set_shader ───────────────────────────────────────────────────────────
std::vector<CachedPipelineId> ShaderCache::set_shader(assets::AssetId<Shader> id, Shader shader) {
    auto affected = clear(id);

    // Register import path → id
    import_path_shaders_.insert_or_assign(shader.import_path, id);

    // Resolve waiting shaders that were waiting for this import
    auto wit = waiting_on_import_.find(shader.import_path);
    if (wit != waiting_on_import_.end()) {
        for (auto waiting_id : wit->second) {
            data_[waiting_id].resolved_imports.insert_or_assign(shader.import_path, id);
            data_[id].dependents.insert(waiting_id);
        }
        waiting_on_import_.erase(wit);
    }

    // For each import this shader needs, resolve or enqueue
    for (const auto& imp : shader.imports) {
        auto iit = import_path_shaders_.find(imp);
        if (iit != import_path_shaders_.end()) {
            data_[id].resolved_imports.insert_or_assign(imp, iit->second);
            data_[iit->second].dependents.insert(id);
        } else {
            waiting_on_import_[imp].push_back(id);
        }
    }

    shaders_[id] = std::move(shader);
    spdlog::debug("[shader.cache] Set shader '{}'. {} pipelines affected.", assets::UntypedAssetId(id),
                  affected.size());
    return affected;
}

// ─── remove ───────────────────────────────────────────────────────────────
std::vector<CachedPipelineId> ShaderCache::remove(assets::AssetId<Shader> id) {
    auto affected = clear(id);
    auto sit      = shaders_.find(id);
    if (sit != shaders_.end()) {
        import_path_shaders_.erase(sit->second.import_path);
        shaders_.erase(sit);
    }
    spdlog::debug("[shader.cache] Removed shader '{}'. {} pipelines affected.", assets::UntypedAssetId(id),
                  affected.size());
    return affected;
}

}  // namespace epix::shader
