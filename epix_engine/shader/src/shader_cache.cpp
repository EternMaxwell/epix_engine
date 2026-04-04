module;

#include <slang-com-ptr.h>
#include <slang.h>
#include <spdlog/spdlog.h>

module epix.shader;

import :shader_cache;

using namespace epix::shader;

namespace epix::shader {

namespace {

std::string normalize_path_string(std::string path) {
    std::ranges::replace(path, '\\', '/');
    return path;
}

std::string normalize_asset_path_string(const assets::AssetPath& path) {
    std::string normalized;
    if (!path.source.is_default()) {
        normalized += *path.source.as_str();
        normalized += "://";
    }

    if (path.path.has_root_directory()) {
        normalized += '/';
        normalized += path.path.relative_path().generic_string();
    } else {
        normalized += normalize_path_string(path.path.generic_string());
    }

    if (path.label) {
        normalized += '#';
        normalized += *path.label;
    }
    return normalized;
}

std::string asset_path_request_string(const assets::AssetPath& path) { return normalize_asset_path_string(path); }

std::optional<std::string> strip_path_extension(std::string_view path) {
    std::string_view path_part = path;
    std::size_t prefix_len     = 0;
    if (auto scheme = path.find("://"); scheme != std::string_view::npos) {
        prefix_len = scheme + 3;
        path_part  = path.substr(prefix_len);
    }

    auto ext = std::filesystem::path(path_part).extension().string();
    if (ext.empty()) {
        return std::nullopt;
    }

    std::string stripped(path.substr(0, path.size() - ext.size()));
    return normalize_path_string(std::move(stripped));
}

assets::AssetPath resolve_dependency_path(const assets::AssetPath& parent_path, const ShaderImport& import_path) {
    auto resolved = import_path.as_asset_path();
    if (!resolved.source.is_default()) {
        return resolved;
    }
    if (resolved.path.has_root_directory()) {
        return assets::AssetPath(parent_path.source, resolved.path.relative_path(), resolved.label);
    }
    return parent_path.resolve(resolved);
}

Slang::ComPtr<ISlangBlob> make_terminated_blob(std::string_view text) {
    std::string terminated(text);
    terminated.push_back('\0');
    Slang::ComPtr<ISlangBlob> blob;
    blob.attach(slang_createBlob(terminated.data(), terminated.size()));
    return blob;
}

}  // namespace

// ─── SlangCompiler: owns the Slang global session and provides compilation ──
struct ShaderCache::SlangCompiler {
    Slang::ComPtr<slang::IGlobalSession> global_session;

    SlangCompiler() {
        if (SLANG_FAILED(slang::createGlobalSession(global_session.writeRef()))) {
            spdlog::error("[shader.cache] Failed to create Slang global session.");
        }
    }

    class GlobalFileSystem {
        const std::unordered_map<ShaderImport, assets::AssetId<Shader>>& import_map_;
        const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders_;

       public:
        GlobalFileSystem(const std::unordered_map<ShaderImport, assets::AssetId<Shader>>& import_map,
                         const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders)
            : import_map_(import_map), shaders_(shaders) {}

        std::optional<assets::AssetId<Shader>> find_shader_id(const ShaderImport& import_ref) const {
            auto it = import_map_.find(import_ref);
            if (it == import_map_.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        std::optional<assets::AssetId<Shader>> find_shader_id_by_path(std::string_view path) const {
            return find_shader_id(ShaderImport::asset_path(assets::AssetPath(std::string(path))));
        }

        const Shader* get_shader(assets::AssetId<Shader> id) const {
            if (auto it = shaders_.find(id); it != shaders_.end()) {
                return &it->second;
            }
            return nullptr;
        }

        const Shader* get_shader_by_path(std::string_view path) const {
            auto id = find_shader_id_by_path(path);
            if (!id.has_value()) {
                return nullptr;
            }
            return get_shader(*id);
        }

        SlangResult load_shader_source(std::string_view path, ISlangBlob** outBlob) const {
            auto shader = get_shader_by_path(path);
            if (!shader || !shader->source.is_slang()) {
                *outBlob = nullptr;
                return SLANG_E_NOT_FOUND;
            }

            const auto& code = std::get<Source::Slang>(shader->source.data).code;
            *outBlob         = slang_createBlob(code.data(), code.size());
            return SLANG_OK;
        }
    };

    // Scoped file system that resolves paths through the importing shader's declared imports.
    class ScopedFileSystem : public ISlangFileSystemExt {
        const GlobalFileSystem& global_;
        std::unordered_set<std::string> visible_paths_;
        std::unordered_map<std::string, std::string> path_aliases_;

        void register_path_alias(std::string alias, const std::string& resolved) {
            alias = normalize_path_string(std::move(alias));
            if (alias.empty()) {
                return;
            }
            visible_paths_.insert(alias);
            path_aliases_.insert_or_assign(alias, resolved);
        }

        void register_alias_variants(const std::string& alias, const std::string& resolved) {
            register_path_alias(alias, resolved);
            if (auto stripped = strip_path_extension(alias); stripped.has_value()) {
                register_path_alias(*stripped, resolved);
            }
        }

       public:
        ScopedFileSystem(const GlobalFileSystem& global) : global_(global) {}

        static std::vector<std::string> make_request_candidates(std::string_view path) {
            std::vector<std::string> candidates;
            auto normalized = normalize_path_string(std::string(path));
            candidates.push_back(normalized);
            if (std::filesystem::path(normalized).extension().empty()) {
                candidates.push_back(normalized + ".slang");
            }
            return candidates;
        }

        std::optional<std::string> resolve_request_path(const Shader& importer, std::string_view request) const {
            const auto& importer_path = importer.path;
            auto candidates           = make_request_candidates(request);

            for (const auto& candidate : candidates) {
                for (const auto& import_ref : importer.imports) {
                    if (import_ref.is_custom()) {
                        if (import_ref.as_custom() != candidate) {
                            continue;
                        }
                        auto id = global_.find_shader_id(import_ref);
                        if (id.has_value()) {
                            auto shader = global_.get_shader(*id);
                            if (!shader) {
                                return std::nullopt;
                            }
                            return normalize_asset_path_string(shader->path);
                        }

                        auto shader = global_.get_shader_by_path(candidate);
                        if (shader) {
                            return normalize_asset_path_string(shader->path);
                        }

                        return std::nullopt;
                    }

                    auto raw_request        = asset_path_request_string(import_ref.as_asset_path());
                    auto resolved_path      = resolve_dependency_path(importer_path, import_ref);
                    auto resolved_request   = normalize_asset_path_string(resolved_path);
                    auto resolved_no_source = normalize_path_string(resolved_path.path.generic_string());
                    auto import_path_no_source =
                        normalize_path_string(import_ref.as_asset_path().path.generic_string());

                    if (candidate != raw_request && candidate != resolved_request && candidate != resolved_no_source &&
                        candidate != import_path_no_source) {
                        continue;
                    }
                    return resolved_request;
                }
            }

            return std::nullopt;
        }

        void seed_visible_imports(const Shader& importer) {
            std::unordered_set<std::string> visited_paths;

            auto seed_imports = [&](auto&& self, const Shader& current_shader) -> void {
                const auto& importer_path = current_shader.path;
                auto importer_key         = normalize_asset_path_string(importer_path);
                if (!visited_paths.insert(importer_key).second) {
                    return;
                }

                for (const auto& import_ref : current_shader.imports) {
                    if (!import_ref.is_asset_path()) {
                        continue;
                    }

                    auto raw_request        = asset_path_request_string(import_ref.as_asset_path());
                    auto resolved_path      = resolve_dependency_path(importer_path, import_ref);
                    auto resolved_request   = normalize_asset_path_string(resolved_path);
                    auto resolved_no_source = normalize_path_string(resolved_path.path.generic_string());
                    auto import_no_source   = normalize_path_string(import_ref.as_asset_path().path.generic_string());

                    register_alias_variants(resolved_request, resolved_request);
                    register_alias_variants(raw_request, resolved_request);
                    register_alias_variants(resolved_no_source, resolved_request);
                    register_alias_variants(import_no_source, resolved_request);

                    if (auto shader = global_.get_shader_by_path(resolved_request)) {
                        self(self, *shader);
                    }
                }
            };

            seed_imports(seed_imports, importer);
        }

        const Shader* find_importer(std::string_view fromPath) const {
            auto candidates = make_request_candidates(fromPath);
            for (const auto& candidate : candidates) {
                if (auto shader = global_.get_shader_by_path(candidate)) {
                    return shader;
                }

                auto importer_id = global_.find_shader_id(ShaderImport::custom(candidate));
                if (importer_id.has_value()) {
                    if (auto shader = global_.get_shader(*importer_id)) {
                        return shader;
                    }
                }
            }
            return nullptr;
        }

        SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override {
            if (uuid == ISlangUnknown::getTypeGuid() || uuid == ISlangCastable::getTypeGuid() ||
                uuid == ISlangFileSystem::getTypeGuid() || uuid == ISlangFileSystemExt::getTypeGuid()) {
                *outObject = static_cast<ISlangFileSystemExt*>(this);
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
            if (guid == ISlangFileSystemExt::getTypeGuid()) return static_cast<ISlangFileSystemExt*>(this);
            return nullptr;
        }

        SLANG_NO_THROW SlangResult SLANG_MCALL loadFile(char const* path, ISlangBlob** outBlob) override {
            auto normalized = normalize_path_string(std::string(path));
            auto alias_it   = path_aliases_.find(normalized);
            if (alias_it != path_aliases_.end()) {
                return global_.load_shader_source(alias_it->second, outBlob);
            }
            if (!visible_paths_.contains(normalized)) {
                *outBlob = nullptr;
                return SLANG_E_NOT_FOUND;
            }
            return global_.load_shader_source(normalized, outBlob);
        }

        SLANG_NO_THROW SlangResult SLANG_MCALL getFileUniqueIdentity(const char* path,
                                                                     ISlangBlob** outUniqueIdentity) override {
            auto normalized = normalize_path_string(std::string(path));
            if (auto alias_it = path_aliases_.find(normalized); alias_it != path_aliases_.end()) {
                normalized = alias_it->second;
            }
            auto blob          = make_terminated_blob(normalized);
            *outUniqueIdentity = blob.detach();
            return SLANG_OK;
        }

        SLANG_NO_THROW SlangResult SLANG_MCALL calcCombinedPath(SlangPathType fromPathType,
                                                                const char* fromPath,
                                                                const char* path,
                                                                ISlangBlob** pathOut) override {
            (void)fromPathType;

            auto importer = find_importer(fromPath);
            if (!importer) {
                *pathOut = nullptr;
                return SLANG_E_NOT_FOUND;
            }

            auto resolved = resolve_request_path(*importer, path);
            if (!resolved.has_value()) {
                *pathOut = nullptr;
                return SLANG_E_NOT_FOUND;
            }

            register_path_alias(normalize_path_string(std::string(path)), *resolved);
            register_path_alias(*resolved, *resolved);
            auto blob = make_terminated_blob(*resolved);
            *pathOut  = blob.detach();
            return SLANG_OK;
        }

        SLANG_NO_THROW SlangResult SLANG_MCALL getPathType(const char* path, SlangPathType* pathTypeOut) override {
            auto normalized = normalize_path_string(std::string(path));
            if (auto alias_it = path_aliases_.find(normalized); alias_it != path_aliases_.end()) {
                normalized = alias_it->second;
            }
            if (visible_paths_.contains(normalized) || global_.get_shader_by_path(normalized)) {
                *pathTypeOut = SLANG_PATH_TYPE_FILE;
                return SLANG_OK;
            }
            return SLANG_E_NOT_FOUND;
        }

        SLANG_NO_THROW SlangResult SLANG_MCALL getPath(PathKind kind, const char* path, ISlangBlob** outPath) override {
            (void)kind;
            auto normalized = normalize_path_string(std::string(path));
            if (auto alias_it = path_aliases_.find(normalized); alias_it != path_aliases_.end()) {
                normalized = alias_it->second;
            }
            auto blob = make_terminated_blob(normalized);
            *outPath  = blob.detach();
            return SLANG_OK;
        }

        SLANG_NO_THROW void SLANG_MCALL clearCache() override {}

        SLANG_NO_THROW SlangResult SLANG_MCALL enumeratePathContents(const char* path,
                                                                     FileSystemContentsCallBack callback,
                                                                     void* userData) override {
            (void)path;
            (void)callback;
            (void)userData;
            return SLANG_E_NOT_IMPLEMENTED;
        }

        SLANG_NO_THROW OSPathKind SLANG_MCALL getOSPathKind() override { return OSPathKind::None; }
    };

    std::expected<std::vector<std::uint8_t>, ShaderCacheError> compile(
        const Shader& shader,
        const std::string& source,
        const assets::AssetPath& path,
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

        GlobalFileSystem global_fs(import_map, shaders);
        ScopedFileSystem fs(global_fs);
        fs.seed_visible_imports(shader);

        // Empty search path so Slang also tries bare module paths
        // (not only relative to the importing file's directory).
        const char* search_paths[]                     = {""};
        slang::CompilerOptionEntry compiler_options[1] = {};
        compiler_options[0].name                       = slang::CompilerOptionName::VulkanUseEntryPointName;
        compiler_options[0].value.kind                 = slang::CompilerOptionValueKind::Int;
        compiler_options[0].value.intValue0            = 1;

        slang::SessionDesc session_desc       = {};
        session_desc.targets                  = &target_desc;
        session_desc.targetCount              = 1;
        session_desc.defaultMatrixLayoutMode  = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
        session_desc.searchPaths              = search_paths;
        session_desc.searchPathCount          = 1;
        session_desc.preprocessorMacros       = macros.data();
        session_desc.preprocessorMacroCount   = static_cast<SlangInt>(macros.size());
        session_desc.compilerOptionEntries    = compiler_options;
        session_desc.compilerOptionEntryCount = 1;
        session_desc.fileSystem               = &fs;

        Slang::ComPtr<slang::ISession> session;
        if (SLANG_FAILED(global_session->createSession(session_desc, session.writeRef()))) {
            return std::unexpected(
                ShaderCacheError::slang_error(Stage::SessionCreation, "Failed to create Slang session"));
        }

        // Load module from source
        Slang::ComPtr<slang::IBlob> diagnostics;
        auto normalized_path = normalize_asset_path_string(path);
        slang::IModule* mod  = session->loadModuleFromSourceString("shader", normalized_path.c_str(), source.c_str(),
                                                                   diagnostics.writeRef());
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
            ShaderImport file_import = ShaderImport::asset_path(sit->second.path);
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
        auto spirv = slang_->compile(shader, std::get<Source::Slang>(shader.source.data).code, shader.path, merged_defs,
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
        auto composed =
            composer_.compose(shader.source.as_str(), normalize_asset_path_string(shader.path), merged_defs);
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
    ShaderImport file_import = ShaderImport::asset_path(shader.path);
    if (file_import != shader.import_path) {
        import_path_shaders_.insert_or_assign(file_import, id);
    }
}

void ShaderCache::unregister_import_names(const Shader& shader) {
    import_path_shaders_.erase(shader.import_path);
    ShaderImport file_import = ShaderImport::asset_path(shader.path);
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
    ShaderImport file_import = ShaderImport::asset_path(shader.path);
    if (file_import != shader.import_path) {
        resolve_waiters(file_import);
    }

    auto resolve_asset_path_waiters = [&](const assets::AssetPath& provided_path) {
        for (auto it = waiting_on_import_.begin(); it != waiting_on_import_.end();) {
            if (!it->first.is_asset_path()) {
                ++it;
                continue;
            }

            auto& waiting_ids = it->second;
            std::erase_if(waiting_ids, [&](assets::AssetId<Shader> waiting_id) {
                auto waiter_it = shaders_.find(waiting_id);
                if (waiter_it == shaders_.end()) {
                    return false;
                }

                auto resolved_path = resolve_dependency_path(waiter_it->second.path, it->first);
                if (resolved_path != provided_path) {
                    return false;
                }

                data_[waiting_id].resolved_imports.insert_or_assign(it->first, id);
                data_[id].dependents.insert(waiting_id);
                return true;
            });

            if (waiting_ids.empty()) {
                it = waiting_on_import_.erase(it);
            } else {
                ++it;
            }
        }
    };
    resolve_asset_path_waiters(shader.path);

    // For each import this shader needs, resolve or enqueue
    auto find_provider = [&](const ShaderImport& imp) -> std::optional<assets::AssetId<Shader>> {
        if (auto iit = import_path_shaders_.find(imp); iit != import_path_shaders_.end()) {
            return iit->second;
        }

        if (imp.is_asset_path()) {
            auto resolved_import = ShaderImport::asset_path(resolve_dependency_path(shader.path, imp));
            if (auto iit = import_path_shaders_.find(resolved_import); iit != import_path_shaders_.end()) {
                return iit->second;
            }
        }

        return std::nullopt;
    };

    for (const auto& imp : shader.imports) {
        if (auto provider_id = find_provider(imp); provider_id.has_value()) {
            data_[id].resolved_imports.insert_or_assign(imp, *provider_id);
            data_[*provider_id].dependents.insert(id);
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
        if (event.is_loaded_with_dependencies() || event.is_modified()) {
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
