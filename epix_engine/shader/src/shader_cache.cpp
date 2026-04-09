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

std::vector<ShaderImport> visible_imports(const Shader& shader) {
    auto visible = std::vector<ShaderImport>{ShaderImport::asset_path(shader.path)};
    if (shader.import_path != visible.front()) visible.push_back(shader.import_path);
    return visible;
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

ShaderImport resolve_import_key(const Shader& importer, const ShaderImport& import_ref) {
    return import_ref.is_custom() ? import_ref
                                  : ShaderImport::asset_path(resolve_dependency_path(importer.path, import_ref));
}

bool all_imports_resolved(const Shader& shader, const ShaderData& shader_data) {
    return std::ranges::all_of(shader.imports, [&](const ShaderImport& import_ref) {
        return shader_data.resolved_imports.contains(import_ref);
    });
}

bool shader_ready(const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders,
                  const std::unordered_map<assets::AssetId<Shader>, ShaderData>& data,
                  assets::AssetId<Shader> shader_id,
                  std::unordered_set<assets::AssetId<Shader>>& visiting) {
    if (!visiting.insert(shader_id).second) return true;

    auto shader_it = shaders.find(shader_id);
    auto data_it   = data.find(shader_id);
    if (shader_it == shaders.end() || data_it == data.end()) return false;

    const Shader& shader          = shader_it->second;
    const ShaderData& shader_data = data_it->second;
    for (const auto& import_ref : shader.imports) {
        auto resolved_it = shader_data.resolved_imports.find(import_ref);
        if (resolved_it == shader_data.resolved_imports.end()) return false;
        if (!shader_ready(shaders, data, resolved_it->second, visiting)) return false;
    }
    return true;
}

void erase_waiting_shader(
    std::unordered_map<ShaderImport, std::unordered_set<assets::AssetId<Shader>>>& waiting_on_import,
    assets::AssetId<Shader> shader_id) {
    for (auto it = waiting_on_import.begin(); it != waiting_on_import.end();) {
        it->second.erase(shader_id);
        if (it->second.empty()) {
            it = waiting_on_import.erase(it);
        } else {
            ++it;
        }
    }
}

void disconnect_from_providers(std::unordered_map<assets::AssetId<Shader>, ShaderData>& data,
                               assets::AssetId<Shader> shader_id) {
    auto data_it = data.find(shader_id);
    if (data_it == data.end()) {
        return;
    }

    for (const auto& [_, provider_id] : data_it->second.resolved_imports) {
        auto provider_it = data.find(provider_id);
        if (provider_it != data.end()) {
            provider_it->second.dependents.erase(shader_id);
        }
    }
    data_it->second.resolved_imports.clear();
}

void reset_shader_links(
    std::unordered_map<assets::AssetId<Shader>, ShaderData>& data,
    std::unordered_map<ShaderImport, std::unordered_set<assets::AssetId<Shader>>>& waiting_on_import,
    assets::AssetId<Shader> shader_id) {
    erase_waiting_shader(waiting_on_import, shader_id);
    disconnect_from_providers(data, shader_id);
    auto& shader_data = data[shader_id];
    shader_data.resolved_imports.clear();
    shader_data.dependents.clear();
}

void invalidate_dependents(
    const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders,
    std::unordered_map<assets::AssetId<Shader>, ShaderData>& data,
    std::unordered_map<ShaderImport, std::unordered_set<assets::AssetId<Shader>>>& waiting_on_import,
    assets::AssetId<Shader> provider_id) {
    auto provider_data_it = data.find(provider_id);
    if (provider_data_it == data.end()) {
        return;
    }

    auto dependents = std::vector<assets::AssetId<Shader>>(provider_data_it->second.dependents.begin(),
                                                           provider_data_it->second.dependents.end());
    provider_data_it->second.dependents.clear();

    for (auto dependent_id : dependents) {
        auto shader_it = shaders.find(dependent_id);
        auto data_it   = data.find(dependent_id);
        if (shader_it == shaders.end() || data_it == data.end()) {
            continue;
        }

        const Shader& dependent_shader = shader_it->second;
        auto& dependent_data           = data_it->second;
        for (const auto& import_ref : dependent_shader.imports) {
            auto resolved_it = dependent_data.resolved_imports.find(import_ref);
            if (resolved_it == dependent_data.resolved_imports.end() || resolved_it->second != provider_id) {
                continue;
            }

            dependent_data.resolved_imports.erase(resolved_it);
            waiting_on_import[resolve_import_key(dependent_shader, import_ref)].insert(dependent_id);
        }
    }
}

std::string canonical_custom_request(std::string_view request) {
    auto normalized_request = normalize_path_string(std::string(request));
    std::ranges::replace(normalized_request, '.', '/');
    std::ranges::replace(normalized_request, '_', '-');
    if (std::filesystem::path(normalized_request).extension().empty()) {
        normalized_request += ".slang";
    }
    return normalized_request;
}

assets::AssetPath concrete_request_path(const assets::AssetPath& importer_path, std::string_view request) {
    auto normalized_request = normalize_path_string(std::string(request));
    if (std::filesystem::path(normalized_request).extension().empty()) {
        normalized_request += ".slang";
    }
    return resolve_dependency_path(importer_path, ShaderImport::asset_path(assets::AssetPath(normalized_request)));
}

Slang::ComPtr<ISlangBlob> make_terminated_blob(std::string_view text) {
    std::string terminated(text);
    terminated.push_back('\0');
    Slang::ComPtr<ISlangBlob> blob;
    blob.attach(slang_createBlob(terminated.data(), terminated.size()));
    return blob;
}

std::string format_slang_message(std::string prefix, slang::IBlob* diagnostics) {
    if (diagnostics) {
        prefix += ": ";
        prefix += static_cast<const char*>(diagnostics->getBufferPointer());
    }
    return prefix;
}

std::vector<ShaderDefVal> merge_shader_defs(std::span<const ShaderDefVal> shader_defs, const Shader& shader) {
    std::vector<ShaderDefVal> merged(shader_defs.begin(), shader_defs.end());
    merged.reserve(shader_defs.size() + shader.shader_defs.size());
    std::unordered_set<std::string_view> seen_names;
    seen_names.reserve(merged.size() + shader.shader_defs.size());
    for (const auto& def : merged) seen_names.insert(def.name);
    for (const auto& def : shader.shader_defs) {
        if (seen_names.insert(def.name).second) {
            merged.push_back(def);
        }
    }
    return merged;
}

}  // namespace

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
            return it == import_map_.end() ? std::nullopt : std::optional{it->second};
        }

        const Shader* get_shader(assets::AssetId<Shader> id) const {
            auto it = shaders_.find(id);
            return it == shaders_.end() ? nullptr : &it->second;
        }

        const Shader* get_shader_by_path(std::string_view path) const {
            auto id = find_shader_id(ShaderImport::asset_path(assets::AssetPath(std::string(path))));
            return id.has_value() ? get_shader(*id) : nullptr;
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

    class ScopedFileSystem : public ISlangFileSystemExt {
        const GlobalFileSystem& global_;
        std::unordered_map<std::string, std::string> logical_to_actual_;

        static std::string asset_alias(const assets::AssetPath& path) { return normalize_asset_path_string(path); }

        void register_alias(std::string key, const std::string& actual_path) {
            key = normalize_path_string(std::move(key));
            if (!key.empty()) {
                logical_to_actual_.insert_or_assign(std::move(key), actual_path);
            }
        }

        void seed_import_aliases(const Shader& shader, std::unordered_set<assets::AssetId<Shader>>& visited) {
            for (const auto& import_ref : shader.imports) {
                auto provider_id = global_.find_shader_id(resolve_import_key(shader, import_ref));
                if (!provider_id.has_value()) {
                    continue;
                }

                auto provider = global_.get_shader(*provider_id);
                if (!provider) {
                    continue;
                }

                auto actual_path = asset_alias(provider->path);
                if (import_ref.is_custom()) {
                    register_alias(import_ref.as_custom(), actual_path);
                } else {
                    register_alias(asset_alias(import_ref.as_asset_path()), actual_path);
                    register_alias(import_ref.as_asset_path().path.generic_string(), actual_path);
                    if (!import_ref.as_asset_path().source.is_default()) {
                        register_alias('/' + import_ref.as_asset_path().path.generic_string(), actual_path);
                    }
                }

                if (visited.insert(*provider_id).second) {
                    seed_import_aliases(*provider, visited);
                }
            }
        }

        std::string resolve_alias(std::string_view path) const {
            auto normalized = normalize_path_string(std::string(path));
            auto it         = logical_to_actual_.find(normalized);
            return it == logical_to_actual_.end() ? normalized : it->second;
        }

        struct ResolvedRequest {
            std::string logical_path;
            std::string actual_path;
        };

       public:
        ScopedFileSystem(const GlobalFileSystem& global, const Shader& root_shader) : global_(global) {
            std::unordered_set<assets::AssetId<Shader>> visited;
            seed_import_aliases(root_shader, visited);
        }

        std::optional<ResolvedRequest> resolve_request_path(const Shader& importer, std::string_view request) const {
            for (const auto& import_ref : importer.imports) {
                auto import_key = resolve_import_key(importer, import_ref);
                if (import_key.is_custom()) {
                    if (canonical_custom_request(request) != import_key.as_custom()) {
                        continue;
                    }
                } else if (concrete_request_path(importer.path, request) != import_key.as_asset_path()) {
                    continue;
                }

                auto provider_id = global_.find_shader_id(import_key);
                if (!provider_id.has_value()) {
                    return std::nullopt;
                }
                auto provider = global_.get_shader(*provider_id);
                if (!provider) {
                    return std::nullopt;
                }

                auto actual_path = asset_alias(provider->path);
                if (import_key.is_custom()) {
                    return ResolvedRequest{import_key.as_custom(), std::move(actual_path)};
                }
                return ResolvedRequest{actual_path, std::move(actual_path)};
            }

            return std::nullopt;
        }

        const Shader* find_importer(std::string_view fromPath) const {
            return global_.get_shader_by_path(resolve_alias(fromPath));
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
            return global_.load_shader_source(resolve_alias(path), outBlob);
        }

        SLANG_NO_THROW SlangResult SLANG_MCALL getFileUniqueIdentity(const char* path,
                                                                     ISlangBlob** outUniqueIdentity) override {
            auto normalized    = resolve_alias(path);
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
            if (!importer) return *pathOut = nullptr, SLANG_E_NOT_FOUND;

            auto resolved = resolve_request_path(*importer, path);
            if (!resolved.has_value()) return *pathOut = nullptr, SLANG_E_NOT_FOUND;

            logical_to_actual_.insert_or_assign(normalize_path_string(std::string(path)), resolved->actual_path);
            logical_to_actual_.insert_or_assign(resolved->logical_path, resolved->actual_path);
            logical_to_actual_.insert_or_assign(resolved->actual_path, resolved->actual_path);

            auto blob = make_terminated_blob(resolved->logical_path);
            *pathOut  = blob.detach();
            return SLANG_OK;
        }

        SLANG_NO_THROW SlangResult SLANG_MCALL getPathType(const char* path, SlangPathType* pathTypeOut) override {
            auto normalized = resolve_alias(path);
            if (global_.get_shader_by_path(normalized)) {
                *pathTypeOut = SLANG_PATH_TYPE_FILE;
                return SLANG_OK;
            }
            return SLANG_E_NOT_FOUND;
        }

        SLANG_NO_THROW SlangResult SLANG_MCALL getPath(PathKind kind, const char* path, ISlangBlob** outPath) override {
            (void)kind;
            auto normalized = resolve_alias(path);
            auto blob       = make_terminated_blob(normalized);
            *outPath        = blob.detach();
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

        std::vector<std::string> def_values;
        std::vector<slang::PreprocessorMacroDesc> macros;
        def_values.reserve(shader_defs.size());
        macros.reserve(shader_defs.size());
        for (const auto& def : shader_defs) {
            def_values.push_back(def.value_as_string());
            macros.push_back({def.name.c_str(), def_values.back().c_str()});
        }

        slang::TargetDesc target_desc = {};
        target_desc.format            = SLANG_SPIRV;
        target_desc.profile           = global_session->findProfile("sm_6_0");

        GlobalFileSystem global_fs(import_map, shaders);
        ScopedFileSystem fs(global_fs, shader);

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

        Slang::ComPtr<slang::IBlob> diagnostics;
        auto normalized_path = normalize_asset_path_string(path);
        slang::IModule* mod  = session->loadModuleFromSourceString("shader", normalized_path.c_str(), source.c_str(),
                                                                   diagnostics.writeRef());
        if (!mod) {
            return std::unexpected(ShaderCacheError::slang_error(
                Stage::ModuleLoad, format_slang_message("Slang compilation failed", diagnostics.get())));
        }

        std::vector<slang::IComponentType*> components;
        components.push_back(mod);
        SlangInt ep_count = mod->getDefinedEntryPointCount();
        std::vector<Slang::ComPtr<slang::IEntryPoint>> entry_points(ep_count);
        for (SlangInt i = 0; i < ep_count; ++i) {
            mod->getDefinedEntryPoint(i, entry_points[i].writeRef());
            components.push_back(entry_points[i].get());
        }

        Slang::ComPtr<slang::IComponentType> composed;
        {
            Slang::ComPtr<slang::IBlob> diag;
            if (SLANG_FAILED(session->createCompositeComponentType(components.data(),
                                                                   static_cast<SlangInt>(components.size()),
                                                                   composed.writeRef(), diag.writeRef()))) {
                return std::unexpected(ShaderCacheError::slang_error(
                    Stage::Compose, format_slang_message("Slang compose failed", diag.get())));
            }
        }

        Slang::ComPtr<slang::IComponentType> linked;
        {
            Slang::ComPtr<slang::IBlob> diag;
            if (SLANG_FAILED(composed->link(linked.writeRef(), diag.writeRef()))) {
                return std::unexpected(
                    ShaderCacheError::slang_error(Stage::Link, format_slang_message("Slang link failed", diag.get())));
            }
        }

        Slang::ComPtr<slang::IBlob> spirv_code;
        {
            Slang::ComPtr<slang::IBlob> diag;
            if (SLANG_FAILED(linked->getTargetCode(0, spirv_code.writeRef(), diag.writeRef()))) {
                return std::unexpected(ShaderCacheError::slang_error(
                    Stage::CodeGeneration, format_slang_message("Slang code generation failed", diag.get())));
            }
        }

        auto ptr = static_cast<const std::uint8_t*>(spirv_code->getBufferPointer());
        return std::vector<std::uint8_t>(ptr, ptr + spirv_code->getBufferSize());
    }
};

ShaderCache::ShaderCache(wgpu::Device device, LoadModuleFn load_module)
    : device_(std::move(device)), load_module_(std::move(load_module)), slang_(std::make_shared<SlangCompiler>()) {}

std::expected<void, ShaderCacheError> ShaderCache::add_import_to_composer(
    ShaderComposer& composer,
    const std::unordered_map<assets::AssetId<Shader>, ShaderData>& data,
    const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders,
    assets::AssetId<Shader> shader_id,
    const ShaderImport& import_ref) {
    const std::string module_name = import_ref.module_name();

    if (composer.contains_module(module_name)) return {};

    auto data_it = data.find(shader_id);
    if (data_it == data.end()) return std::unexpected(ShaderCacheError::import_not_available());

    auto provider_it = data_it->second.resolved_imports.find(import_ref);
    if (provider_it == data_it->second.resolved_imports.end()) {
        return std::unexpected(ShaderCacheError::import_not_available());
    }

    auto sit = shaders.find(provider_it->second);
    if (sit == shaders.end()) return std::unexpected(ShaderCacheError::import_not_available());
    const Shader& shader = sit->second;

    for (const auto& dep : shader.imports) {
        if (auto res = add_import_to_composer(composer, data, shaders, provider_it->second, dep); !res)
            return std::unexpected(res.error());
    }

    if (auto res = composer.add_module(module_name, shader.source.as_str(), shader.shader_defs); !res)
        return std::unexpected(ShaderCacheError::process_error(std::move(res.error())));

    spdlog::trace("[shader.cache] Added module '{}' to composer.", module_name);
    return {};
}

std::vector<CachedPipelineId> ShaderCache::clear(assets::AssetId<Shader> id) {
    std::unordered_set<CachedPipelineId> affected;

    std::queue<assets::AssetId<Shader>> work;
    work.push(id);
    std::unordered_set<assets::AssetId<Shader>> visited;

    while (!work.empty()) {
        auto cur = work.front();
        work.pop();
        if (!visited.insert(cur).second) continue;

        auto dit = data_.find(cur);
        if (dit != data_.end()) {
            affected.insert(dit->second.pipelines.begin(), dit->second.pipelines.end());
            dit->second.processed_shaders.clear();
            for (auto dep_id : dit->second.dependents) work.push(dep_id);
        }

        auto sit = shaders_.find(cur);
        if (sit != shaders_.end()) {
            for (const auto& visible_name : visible_imports(sit->second)) {
                composer_.remove_module(visible_name.module_name());
            }
        }
    }

    return {affected.begin(), affected.end()};
}

std::expected<std::shared_ptr<wgpu::ShaderModule>, ShaderCacheError> ShaderCache::get(
    CachedPipelineId pipeline, assets::AssetId<Shader> id, std::span<const ShaderDefVal> shader_defs) {
    auto sit = shaders_.find(id);
    if (sit == shaders_.end()) return std::unexpected(ShaderCacheError::not_loaded(id));
    const Shader& shader = sit->second;

    auto& shader_data = data_[id];

    shader_data.pipelines.insert(pipeline);

    std::unordered_set<assets::AssetId<Shader>> visiting;
    if (!shader_ready(shaders_, data_, id, visiting)) {
        return std::unexpected(ShaderCacheError::import_not_available());
    }

    auto merged_defs = merge_shader_defs(shader_defs, shader);

    auto cache_it = shader_data.processed_shaders.find(merged_defs);
    if (cache_it != shader_data.processed_shaders.end()) {
        spdlog::trace("[shader.cache] Cache hit for shader '{}'.", assets::UntypedAssetId(id));
        return cache_it->second;
    }

    spdlog::debug("[shader.cache] Compiling shader '{}' with {} defs.", assets::UntypedAssetId(id), merged_defs.size());

    ShaderCacheSource source;
    std::vector<std::uint8_t> spirv_storage;  // keep SPIR-V alive for span
    if (std::holds_alternative<Source::SpirV>(shader.source.data)) {
        const auto& bytes = std::get<Source::SpirV>(shader.source.data).bytes;
        source            = ShaderCacheSource{ShaderCacheSource::SpirV{std::span<const std::uint8_t>(bytes)}};
    } else if (std::holds_alternative<Source::Slang>(shader.source.data)) {
        auto spirv = slang_->compile(shader, std::get<Source::Slang>(shader.source.data).code, shader.path, merged_defs,
                                     import_path_shaders_, shaders_);
        if (!spirv) return std::unexpected(spirv.error());
        spirv_storage = std::move(spirv.value());
        source        = ShaderCacheSource{ShaderCacheSource::SpirV{std::span<const std::uint8_t>(spirv_storage)}};
    } else {
        for (const auto& imp : shader.imports) {
            if (auto res = add_import_to_composer(composer_, data_, shaders_, id, imp); !res)
                return std::unexpected(res.error());
        }
        auto composed =
            composer_.compose(shader.source.as_str(), normalize_asset_path_string(shader.path), merged_defs);
        if (!composed) return std::unexpected(ShaderCacheError::process_error(std::move(composed.error())));
        source = ShaderCacheSource{ShaderCacheSource::Wgsl{std::move(composed.value())}};
    }

    auto module_result = load_module_(device_, source, shader.validate_shader);
    if (!module_result) return std::unexpected(module_result.error());

    auto ptr = std::make_shared<wgpu::ShaderModule>(std::move(module_result.value()));
    shader_data.processed_shaders.emplace(merged_defs, ptr);
    return ptr;
}

void ShaderCache::register_import_names(const Shader& shader, assets::AssetId<Shader> id) {
    for (const auto& visible_name : visible_imports(shader)) {
        import_path_shaders_.insert_or_assign(visible_name, id);
    }
}

void ShaderCache::unregister_import_names(const Shader& shader) {
    for (const auto& visible_name : visible_imports(shader)) {
        import_path_shaders_.erase(visible_name);
    }
}

std::vector<CachedPipelineId> ShaderCache::set_shader(assets::AssetId<Shader> id, Shader shader) {
    auto cleared = clear(id);
    std::unordered_set<CachedPipelineId> affected(cleared.begin(), cleared.end());

    if (auto existing = shaders_.find(id); existing != shaders_.end()) {
        unregister_import_names(existing->second);
    }
    invalidate_dependents(shaders_, data_, waiting_on_import_, id);
    reset_shader_links(data_, waiting_on_import_, id);

    shaders_[id]                = shader;
    const Shader& stored_shader = shaders_.at(id);
    auto& shader_data           = data_[id];

    register_import_names(stored_shader, id);

    auto resolve_waiters = [&](const ShaderImport& visible_name) {
        auto wit = waiting_on_import_.find(visible_name);
        if (wit == waiting_on_import_.end()) {
            return;
        }

        auto waiting_ids = std::move(wit->second);
        waiting_on_import_.erase(wit);
        for (auto waiting_id : waiting_ids) {
            auto waiter_it = shaders_.find(waiting_id);
            if (waiter_it == shaders_.end()) {
                continue;
            }

            auto& waiter_data = data_[waiting_id];
            for (const auto& import_ref : waiter_it->second.imports) {
                if (resolve_import_key(waiter_it->second, import_ref) != visible_name) {
                    continue;
                }
                waiter_data.resolved_imports.insert_or_assign(import_ref, id);
                shader_data.dependents.insert(waiting_id);
            }

            if (all_imports_resolved(waiter_it->second, waiter_data)) {
                affected.insert(waiter_data.pipelines.begin(), waiter_data.pipelines.end());
            }
        }
    };

    for (const auto& visible_name : visible_imports(stored_shader)) {
        resolve_waiters(visible_name);
    }

    for (const auto& import_ref : stored_shader.imports) {
        auto import_key = resolve_import_key(stored_shader, import_ref);
        if (auto provider_it = import_path_shaders_.find(import_key); provider_it != import_path_shaders_.end()) {
            shader_data.resolved_imports.insert_or_assign(import_ref, provider_it->second);
            data_[provider_it->second].dependents.insert(id);
        } else {
            waiting_on_import_[import_key].insert(id);
        }
    }

    spdlog::debug("[shader.cache] Set shader '{}'. {} pipelines affected.", assets::UntypedAssetId(id),
                  affected.size());
    return {affected.begin(), affected.end()};
}

std::vector<CachedPipelineId> ShaderCache::remove(assets::AssetId<Shader> id) {
    auto affected = clear(id);
    auto sit      = shaders_.find(id);
    if (sit == shaders_.end()) {
        return affected;
    }

    unregister_import_names(sit->second);
    invalidate_dependents(shaders_, data_, waiting_on_import_, id);
    reset_shader_links(data_, waiting_on_import_, id);
    shaders_.erase(sit);

    spdlog::debug("[shader.cache] Removed shader '{}'. {} pipelines affected.", assets::UntypedAssetId(id),
                  affected.size());
    return affected;
}

std::vector<CachedPipelineId> ShaderCache::sync(utils::input_iterable<assets::AssetEvent<Shader>> events,
                                                const assets::Assets<Shader>& shaders) {
    std::unordered_set<CachedPipelineId> affected;
    for (const auto& event : events) {
        if (event.is_loaded_with_dependencies() || event.is_modified()) {
            auto val = shaders.get(event.id);
            if (val.has_value()) {
                auto ids = set_shader(event.id, val.value().get());
                affected.insert(ids.begin(), ids.end());
            }
        } else if (event.is_unused()) {
            auto ids = remove(event.id);
            affected.insert(ids.begin(), ids.end());
        }
    }
    return affected | std::ranges::to<std::vector>();
}

}  // namespace epix::shader
