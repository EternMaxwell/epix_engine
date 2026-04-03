module;

#include <slang-com-ptr.h>
#include <slang.h>
#include <spdlog/spdlog.h>

module epix.shader;

import :shader_cache;

using namespace epix::shader;

namespace epix::shader {

// ─── SlangCompiler: owns the Slang global session and provides compilation ──
struct ShaderCache::SlangCompiler {
    Slang::ComPtr<slang::IGlobalSession> global_session;

    SlangCompiler() {
        if (SLANG_FAILED(slang::createGlobalSession(global_session.writeRef()))) {
            spdlog::error("[shader.cache] Failed to create Slang global session.");
        }
    }

    // In-memory file system that resolves imports via import_path_shaders (O(1)).
    class FileSystem : public ISlangFileSystem {
        const std::unordered_map<ShaderImport, assets::AssetId<Shader>>& import_map_;
        const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders_;

       public:
        FileSystem(const std::unordered_map<ShaderImport, assets::AssetId<Shader>>& import_map,
                   const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders)
            : import_map_(import_map), shaders_(shaders) {}

        SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override {
            if (uuid == ISlangUnknown::getTypeGuid() || uuid == ISlangCastable::getTypeGuid() ||
                uuid == ISlangFileSystem::getTypeGuid()) {
                *outObject = static_cast<ISlangFileSystem*>(this);
                return SLANG_OK;
            }
            *outObject = nullptr;
            return SLANG_E_NO_INTERFACE;
        }
        SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
        SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }
        SLANG_NO_THROW void* SLANG_MCALL castAs(const SlangUUID& guid) override {
            if (guid == ISlangUnknown::getTypeGuid() || guid == ISlangCastable::getTypeGuid())
                return static_cast<ISlangCastable*>(this);
            if (guid == ISlangFileSystem::getTypeGuid()) return static_cast<ISlangFileSystem*>(this);
            return nullptr;
        }

        SLANG_NO_THROW SlangResult SLANG_MCALL loadFile(char const* path, ISlangBlob** outBlob) override {
            // Normalize: strip quotes, forward slashes
            std::string norm(path);
            if (norm.size() >= 2 && norm.front() == '"' && norm.back() == '"') norm = norm.substr(1, norm.size() - 2);
            std::ranges::replace(norm, '\\', '/');

            // Build candidates to look up
            std::string with_ext = norm;
            if (std::filesystem::path(norm).extension().empty()) with_ext += ".slang";

            // O(1) lookup via import_path_shaders
            for (const auto& candidate : {norm, with_ext}) {
                auto key = ShaderImport::asset_path(assets::AssetPath(candidate));
                auto it  = import_map_.find(key);
                if (it == import_map_.end()) continue;
                auto sit = shaders_.find(it->second);
                if (sit == shaders_.end()) continue;
                if (!sit->second.source.is_slang()) continue;
                const auto& code = std::get<Source::Slang>(sit->second.source.data).code;
                *outBlob         = slang_createBlob(code.data(), code.size());
                return SLANG_OK;
            }
            return SLANG_E_NOT_FOUND;
        }
    };

    std::expected<std::vector<std::uint8_t>, ShaderCacheError> compile(
        const std::string& source,
        const std::string& path,
        std::span<const ShaderDefVal> shader_defs,
        const std::unordered_map<ShaderImport, assets::AssetId<Shader>>& import_map,
        const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders) {
        using Stage = ShaderCacheError::SlangCompileError::Stage;
        if (!global_session) {
            return std::unexpected(
                ShaderCacheError::slang_error(Stage::SessionCreation, "Slang global session not available"));
        }

        // Preprocessor macros from shader defs
        std::vector<std::string> def_values;
        std::vector<slang::PreprocessorMacroDesc> macros;
        def_values.reserve(shader_defs.size());
        macros.reserve(shader_defs.size());
        for (const auto& def : shader_defs) {
            def_values.push_back(def.value_as_string());
            macros.push_back({def.name.c_str(), def_values.back().c_str()});
        }

        // Target: SPIR-V
        slang::TargetDesc target_desc = {};
        target_desc.format            = SLANG_SPIRV;
        target_desc.profile           = global_session->findProfile("sm_6_0");

        FileSystem fs(import_map, shaders);

        // Empty search path so Slang also tries bare module paths
        // (not only relative to the importing file's directory).
        const char* search_paths[] = {""};

        slang::SessionDesc session_desc      = {};
        session_desc.targets                 = &target_desc;
        session_desc.targetCount             = 1;
        session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
        session_desc.searchPaths             = search_paths;
        session_desc.searchPathCount         = 1;
        session_desc.preprocessorMacros      = macros.data();
        session_desc.preprocessorMacroCount  = static_cast<SlangInt>(macros.size());
        session_desc.fileSystem              = &fs;

        Slang::ComPtr<slang::ISession> session;
        if (SLANG_FAILED(global_session->createSession(session_desc, session.writeRef()))) {
            return std::unexpected(
                ShaderCacheError::slang_error(Stage::SessionCreation, "Failed to create Slang session"));
        }

        // Load module from source
        Slang::ComPtr<slang::IBlob> diagnostics;
        slang::IModule* mod =
            session->loadModuleFromSourceString("shader", path.c_str(), source.c_str(), diagnostics.writeRef());
        if (!mod) {
            std::string msg = "Slang compilation failed";
            if (diagnostics) {
                msg += ": ";
                msg += static_cast<const char*>(diagnostics->getBufferPointer());
            }
            return std::unexpected(ShaderCacheError::slang_error(Stage::ModuleLoad, std::move(msg)));
        }

        // Gather entry points
        std::vector<slang::IComponentType*> components;
        components.push_back(mod);
        SlangInt ep_count = mod->getDefinedEntryPointCount();
        std::vector<Slang::ComPtr<slang::IEntryPoint>> entry_points(ep_count);
        for (SlangInt i = 0; i < ep_count; ++i) {
            mod->getDefinedEntryPoint(i, entry_points[i].writeRef());
            components.push_back(entry_points[i].get());
        }

        // Compose
        Slang::ComPtr<slang::IComponentType> composed;
        {
            Slang::ComPtr<slang::IBlob> diag;
            if (SLANG_FAILED(session->createCompositeComponentType(components.data(),
                                                                   static_cast<SlangInt>(components.size()),
                                                                   composed.writeRef(), diag.writeRef()))) {
                std::string msg = "Slang compose failed";
                if (diag) {
                    msg += ": ";
                    msg += static_cast<const char*>(diag->getBufferPointer());
                }
                return std::unexpected(ShaderCacheError::slang_error(Stage::Compose, std::move(msg)));
            }
        }

        // Link
        Slang::ComPtr<slang::IComponentType> linked;
        {
            Slang::ComPtr<slang::IBlob> diag;
            if (SLANG_FAILED(composed->link(linked.writeRef(), diag.writeRef()))) {
                std::string msg = "Slang link failed";
                if (diag) {
                    msg += ": ";
                    msg += static_cast<const char*>(diag->getBufferPointer());
                }
                return std::unexpected(ShaderCacheError::slang_error(Stage::Link, std::move(msg)));
            }
        }

        // Code generation
        Slang::ComPtr<slang::IBlob> spirv_code;
        {
            Slang::ComPtr<slang::IBlob> diag;
            if (SLANG_FAILED(linked->getTargetCode(0, spirv_code.writeRef(), diag.writeRef()))) {
                std::string msg = "Slang code generation failed";
                if (diag) {
                    msg += ": ";
                    msg += static_cast<const char*>(diag->getBufferPointer());
                }
                return std::unexpected(ShaderCacheError::slang_error(Stage::CodeGeneration, std::move(msg)));
            }
        }

        auto ptr = static_cast<const std::uint8_t*>(spirv_code->getBufferPointer());
        return std::vector<std::uint8_t>(ptr, ptr + spirv_code->getBufferSize());
    }
};

// ─── ShaderCache constructor ───────────────────────────────────────────────
ShaderCache::ShaderCache(wgpu::Device device, LoadModuleFn load_module)
    : device_(std::move(device)), load_module_(std::move(load_module)), slang_(std::make_shared<SlangCompiler>()) {}

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

        // Remove from composer (both primary and secondary names)
        auto sit = shaders_.find(cur);
        if (sit != shaders_.end()) {
            composer_.remove_module(sit->second.import_path.module_name());
            ShaderImport file_import = ShaderImport::asset_path(assets::AssetPath(sit->second.path));
            if (file_import != sit->second.import_path) {
                composer_.remove_module(file_import.module_name());
            }
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
    std::size_t n_asset_imports =
        std::ranges::count_if(shader.imports, [](const ShaderImport& i) { return i.is_asset_path(); });
    std::size_t n_resolved =
        std::ranges::count_if(shader_data.resolved_imports, [](const auto& kv) { return kv.first.is_asset_path(); });
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
    std::vector<std::uint8_t> spirv_storage;  // keep SPIR-V alive for span
    if (std::holds_alternative<Source::SpirV>(shader.source.data)) {
        const auto& bytes = std::get<Source::SpirV>(shader.source.data).bytes;
        source            = ShaderCacheSource{ShaderCacheSource::SpirV{std::span<const std::uint8_t>(bytes)}};
    } else if (std::holds_alternative<Source::Slang>(shader.source.data)) {
        // Slang: compile to SPIR-V via the integrated Slang compiler
        auto spirv = slang_->compile(std::get<Source::Slang>(shader.source.data).code, shader.path, merged_defs,
                                     import_path_shaders_, shaders_);
        if (!spirv) return std::unexpected(spirv.error());
        spirv_storage = std::move(spirv.value());
        source        = ShaderCacheSource{ShaderCacheSource::SpirV{std::span<const std::uint8_t>(spirv_storage)}};
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

// ─── register/unregister import names (dual-name support) ─────────────────
void ShaderCache::register_import_names(const Shader& shader, assets::AssetId<Shader> id) {
    import_path_shaders_.insert_or_assign(shader.import_path, id);
    // Also register the file path as an AssetPath name if it differs
    ShaderImport file_import = ShaderImport::asset_path(assets::AssetPath(shader.path));
    if (file_import != shader.import_path) {
        import_path_shaders_.insert_or_assign(file_import, id);
    }
}

void ShaderCache::unregister_import_names(const Shader& shader) {
    import_path_shaders_.erase(shader.import_path);
    ShaderImport file_import = ShaderImport::asset_path(assets::AssetPath(shader.path));
    if (file_import != shader.import_path) {
        import_path_shaders_.erase(file_import);
    }
}

// ─── set_shader ───────────────────────────────────────────────────────────
std::vector<CachedPipelineId> ShaderCache::set_shader(assets::AssetId<Shader> id, Shader shader) {
    auto affected = clear(id);

    // Register both primary import_path and file-path alias
    register_import_names(shader, id);

    // Resolve waiting shaders — check both names
    auto resolve_waiters = [&](const ShaderImport& name) {
        auto wit = waiting_on_import_.find(name);
        if (wit != waiting_on_import_.end()) {
            for (auto waiting_id : wit->second) {
                data_[waiting_id].resolved_imports.insert_or_assign(name, id);
                data_[id].dependents.insert(waiting_id);
            }
            waiting_on_import_.erase(wit);
        }
    };
    resolve_waiters(shader.import_path);
    ShaderImport file_import = ShaderImport::asset_path(assets::AssetPath(shader.path));
    if (file_import != shader.import_path) {
        resolve_waiters(file_import);
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
        unregister_import_names(sit->second);
        shaders_.erase(sit);
    }
    spdlog::debug("[shader.cache] Removed shader '{}'. {} pipelines affected.", assets::UntypedAssetId(id),
                  affected.size());
    return affected;
}

// ─── sync ─────────────────────────────────────────────────────────────────
std::vector<CachedPipelineId> ShaderCache::sync(utils::input_iterable<assets::AssetEvent<Shader>> events,
                                                const assets::Assets<Shader>& shaders) {
    std::vector<CachedPipelineId> affected;
    for (const auto& event : events) {
        if (event.is_added() || event.is_modified()) {
            auto val = shaders.get(event.id);
            if (val.has_value()) {
                auto ids = set_shader(event.id, val->get());
                affected.insert(affected.end(), ids.begin(), ids.end());
            }
        } else if (event.is_unused() || event.is_removed()) {
            auto ids = remove(event.id);
            affected.insert(affected.end(), ids.begin(), ids.end());
        }
    }
    return affected;
}

}  // namespace epix::shader
