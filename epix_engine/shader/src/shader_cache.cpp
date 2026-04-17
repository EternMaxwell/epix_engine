module;

#include <slang-com-ptr.h>
#include <slang.h>
#include <spdlog/spdlog.h>

module epix.shader;

import :shader_cache;

using namespace epix::shader;

namespace epix::shader {

namespace {

constexpr std::string_view k_shader_custom_source = "shader_custom";

std::string normalize_path_string(std::string path) {
    std::ranges::replace(path, '\\', '/');
    return path;
}

std::filesystem::path normalize_custom_shader_path(std::string_view name) {
    if (name.size() >= 2 && name.front() == '"') {
        auto end_q = name.find('"', 1);
        name       = end_q == std::string_view::npos ? name.substr(1) : name.substr(1, end_q - 1);
    }

    if (auto scheme = name.find("://"); scheme != std::string_view::npos) {
        name = name.substr(scheme + 3);
    }

    std::string normalized;
    normalized.reserve(name.size());
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char current = name[i];
        if (current == ':' && i + 1 < name.size() && name[i + 1] == ':') {
            if (normalized.empty() || normalized.back() != '/') normalized.push_back('/');
            ++i;
            continue;
        }
        if (current == '.' || current == '/' || current == '\\') {
            if (normalized.empty() || normalized.back() != '/') normalized.push_back('/');
            continue;
        }
        normalized.push_back(current);
    }

    auto path = std::filesystem::path(normalized).lexically_normal();
    if (path.extension() == ".slang") {
        path.replace_extension();
    }
    return path;
}

assets::AssetPath shader_custom_path(std::filesystem::path path) {
    return assets::AssetPath(assets::AssetSourceId(std::string(k_shader_custom_source)),
                             std::move(path).lexically_normal());
}

assets::AssetPath resolve_slang_compile_path(std::string_view name,
                                             const assets::AssetPath& importer_path,
                                             bool force_custom) {
    if (force_custom || name.empty() || name.front() != '"') {
        return shader_custom_path(normalize_custom_shader_path(name));
    }

    auto end_q     = name.find('"', 1);
    auto literal   = end_q == std::string_view::npos ? name.substr(1) : name.substr(1, end_q - 1);
    auto path_text = normalize_path_string(std::string(literal));
    auto path_part = std::string_view(path_text);
    if (auto scheme = path_text.find("://"); scheme != std::string::npos) {
        path_part = std::string_view(path_text).substr(scheme + 3);
    }
    if (std::filesystem::path(path_part).extension().empty()) {
        path_text += ".slang";
    }

    auto resolved = assets::AssetPath(std::move(path_text));
    if (!resolved.source.is_default()) {
        return resolved;
    }
    if (resolved.path.has_root_directory()) {
        return assets::AssetPath(importer_path.source, resolved.path.relative_path(), resolved.label);
    }
    return importer_path.resolve(resolved);
}

std::string slang_literal(const assets::AssetPath& path) { return '"' + canonical_asset_path_string(path) + '"'; }

std::string preprocess_slang_compile_source(std::string_view source, const assets::AssetPath& importer_path) {
    std::string out;
    out.reserve(source.size() + 64);

    std::size_t pos = 0;
    while (pos < source.size()) {
        std::size_t nl        = source.find('\n', pos);
        std::string_view line = (nl == std::string_view::npos) ? source.substr(pos) : source.substr(pos, nl - pos);
        pos                   = (nl == std::string_view::npos) ? source.size() : nl + 1;

        std::size_t ws = line.find_first_not_of(" \t\r");
        if (ws == std::string_view::npos) {
            out.append(line);
            if (nl != std::string_view::npos) out.push_back('\n');
            continue;
        }

        std::string_view indent  = line.substr(0, ws);
        std::string_view trimmed = line.substr(ws);
        if (trimmed.starts_with("//")) {
            out.append(line);
            if (nl != std::string_view::npos) out.push_back('\n');
            continue;
        }

        auto rewrite = [&](std::string_view directive) -> bool {
            if (!(trimmed.starts_with(directive) &&
                  (trimmed.size() == directive.size() || trimmed[directive.size()] == ' ' ||
                   trimmed[directive.size()] == '\t'))) {
                return false;
            }

            auto rest      = trimmed.substr(directive.size());
            std::size_t rs = rest.find_first_not_of(" \t");
            if (rs == std::string_view::npos) return false;
            rest                  = rest.substr(rs);
            std::size_t semi      = rest.find(';');
            std::string_view name = (semi == std::string_view::npos) ? rest : rest.substr(0, semi);
            std::string_view tail = (semi == std::string_view::npos) ? std::string_view{} : rest.substr(semi + 1);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
            if (name.empty()) return false;

            auto rewritten_path = resolve_slang_compile_path(name, importer_path, directive == "module");

            out.append(indent);
            out.append(directive);
            out.push_back(' ');
            out.append(slang_literal(rewritten_path));
            out.push_back(';');
            out.append(tail);
            if (nl != std::string_view::npos) out.push_back('\n');
            return true;
        };

        if (rewrite("module") || rewrite("import") || rewrite("__include")) {
            continue;
        }

        out.append(line);
        if (nl != std::string_view::npos) out.push_back('\n');
    }

    return out;
}

// ─── ShaderCache helpers ───────────────────────────────────────────────────

std::vector<ShaderImport> visible_imports(const Shader& shader) {
    auto visible = std::vector<ShaderImport>{ShaderImport::asset_path(shader.path)};
    if (shader.import_path != visible.front()) visible.push_back(shader.import_path);
    return visible;
}

ShaderImport resolve_import_key(const Shader& importer, const ShaderImport& import_ref) {
    if (import_ref.is_custom()) return import_ref;
    auto resolved = import_ref.as_asset_path();
    if (!resolved.source.is_default()) return ShaderImport::asset_path(std::move(resolved));
    if (resolved.path.has_root_directory()) {
        return ShaderImport::asset_path(
            assets::AssetPath(importer.path.source, resolved.path.relative_path(), resolved.label));
    }
    return ShaderImport::asset_path(importer.path.resolve(resolved));
}

std::vector<ShaderDefVal> merge_shader_defs(std::span<const ShaderDefVal> shader_defs, const Shader& shader) {
    std::vector<ShaderDefVal> merged(shader_defs.begin(), shader_defs.end());
    merged.reserve(shader_defs.size() + shader.shader_defs.size());
    std::unordered_set<std::string_view> seen;
    seen.reserve(merged.size() + shader.shader_defs.size());
    for (const auto& d : merged) seen.insert(d.name);
    for (const auto& d : shader.shader_defs) {
        if (seen.insert(d.name).second) merged.push_back(d);
    }
    return merged;
}

void reset_shader_links(
    std::unordered_map<assets::AssetId<Shader>, ShaderData>& data,
    std::unordered_map<ShaderImport, std::unordered_set<assets::AssetId<Shader>>>& waiting_on_import,
    assets::AssetId<Shader> shader_id) {
    for (auto it = waiting_on_import.begin(); it != waiting_on_import.end();) {
        it->second.erase(shader_id);
        it = it->second.empty() ? waiting_on_import.erase(it) : std::next(it);
    }
    if (auto data_it = data.find(shader_id); data_it != data.end()) {
        for (const auto& [_, provider_id] : data_it->second.resolved_imports) {
            if (auto pit = data.find(provider_id); pit != data.end()) pit->second.dependents.erase(shader_id);
        }
        data_it->second.resolved_imports.clear();
    }
    data[shader_id].dependents.clear();
}

void invalidate_dependents(
    const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders,
    std::unordered_map<assets::AssetId<Shader>, ShaderData>& data,
    std::unordered_map<ShaderImport, std::unordered_set<assets::AssetId<Shader>>>& waiting_on_import,
    assets::AssetId<Shader> provider_id) {
    auto pit = data.find(provider_id);
    if (pit == data.end()) return;

    auto dependents = std::vector(pit->second.dependents.begin(), pit->second.dependents.end());
    pit->second.dependents.clear();

    for (auto dep_id : dependents) {
        auto sit = shaders.find(dep_id);
        auto dit = data.find(dep_id);
        if (sit == shaders.end() || dit == data.end()) continue;

        for (const auto& imp : sit->second.imports) {
            auto rit = dit->second.resolved_imports.find(imp);
            if (rit == dit->second.resolved_imports.end() || rit->second != provider_id) continue;
            dit->second.resolved_imports.erase(rit);
            waiting_on_import[resolve_import_key(sit->second, imp)].insert(dep_id);
        }
    }
}

std::vector<ShaderImport> missing_imports_for_shader(
    const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders,
    const std::unordered_map<assets::AssetId<Shader>, ShaderData>& data,
    assets::AssetId<Shader> shader_id) {
    auto sit = shaders.find(shader_id);
    if (sit == shaders.end()) return {};

    auto dit = data.find(shader_id);
    std::vector<ShaderImport> missing;
    for (const auto& imp : sit->second.imports) {
        if (dit == data.end() || !dit->second.resolved_imports.contains(imp)) {
            missing.push_back(resolve_import_key(sit->second, imp));
        }
    }
    return missing;
}

// ─── Slang compilation helpers ─────────────────────────────────────────────

std::string format_diagnostics(std::string prefix, slang::IBlob* diagnostics) {
    if (diagnostics) {
        prefix += ": ";
        prefix += static_cast<const char*>(diagnostics->getBufferPointer());
    }
    return prefix;
}

Slang::ComPtr<ISlangBlob> make_string_blob(std::string_view text) {
    std::string terminated(text);
    terminated.push_back('\0');
    Slang::ComPtr<ISlangBlob> blob;
    blob.attach(slang_createBlob(terminated.data(), terminated.size()));
    return blob;
}

// VFS backed by canonical asset-path strings.  Preprocessing already rewrites
// every import/module path to its canonical form, so no runtime path resolution
// is needed — the VFS is a flat string→source map.
class SlangVFS : public ISlangFileSystemExt {
    struct Entry {
        std::string source;
        std::string identity;  // canonical file path (shared across aliases)
    };
    std::unordered_map<std::string, Entry> files_;

    // Slang may append .slang when resolving imports — fall back to the
    // base key when the suffixed lookup misses.
    const Entry* find_entry(const char* path) const {
        if (auto it = files_.find(path); it != files_.end()) return &it->second;
        std::string_view sv(path);
        if (sv.ends_with(".slang")) {
            if (auto it = files_.find(std::string(sv.substr(0, sv.size() - 6))); it != files_.end()) return &it->second;
        }
        return nullptr;
    }

   public:
    void add(const std::string& key, const std::string& source, const std::string& identity) {
        files_.insert_or_assign(key, Entry{source, identity});
    }

    void remove(const std::string& key) { files_.erase(key); }

    const std::string& get_source(const std::string& key) const { return files_.at(key).source; }

    // ── ISlangUnknown / ISlangCastable ──
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

    // ── ISlangFileSystem ──
    SLANG_NO_THROW SlangResult SLANG_MCALL loadFile(char const* path, ISlangBlob** outBlob) override {
        auto* entry = find_entry(path);
        if (!entry) {
            *outBlob = nullptr;
            return SLANG_E_NOT_FOUND;
        }
        *outBlob = slang_createBlob(entry->source.data(), entry->source.size());
        return SLANG_OK;
    }

    // ── ISlangFileSystemExt ──
    SLANG_NO_THROW SlangResult SLANG_MCALL getFileUniqueIdentity(const char* path,
                                                                 ISlangBlob** outUniqueIdentity) override {
        auto* entry = find_entry(path);
        if (!entry) {
            *outUniqueIdentity = nullptr;
            return SLANG_E_NOT_FOUND;
        }
        *outUniqueIdentity = make_string_blob(entry->identity).detach();
        return SLANG_OK;
    }

    // All paths are already canonical after preprocessing — return as-is.
    SLANG_NO_THROW SlangResult SLANG_MCALL calcCombinedPath(SlangPathType,
                                                            const char*,
                                                            const char* path,
                                                            ISlangBlob** pathOut) override {
        *pathOut = make_string_blob(path).detach();
        return SLANG_OK;
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL getPathType(const char* path, SlangPathType* pathTypeOut) override {
        if (find_entry(path)) {
            *pathTypeOut = SLANG_PATH_TYPE_FILE;
            return SLANG_OK;
        }
        return SLANG_E_NOT_FOUND;
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL getPath(PathKind, const char* path, ISlangBlob** outPath) override {
        *outPath = make_string_blob(path).detach();
        return SLANG_OK;
    }

    SLANG_NO_THROW void SLANG_MCALL clearCache() override {}
    SLANG_NO_THROW SlangResult SLANG_MCALL enumeratePathContents(const char*,
                                                                 FileSystemContentsCallBack,
                                                                 void*) override {
        return SLANG_E_NOT_IMPLEMENTED;
    }
    SLANG_NO_THROW OSPathKind SLANG_MCALL getOSPathKind() override { return OSPathKind::None; }
};

}  // namespace

// ─── SlangCompiler ─────────────────────────────────────────────────────────

struct ShaderCache::SlangCompiler {
    Slang::ComPtr<slang::IGlobalSession> global_session;

    SlangCompiler() {
        if (SLANG_FAILED(slang::createGlobalSession(global_session.writeRef()))) {
            spdlog::error("[shader.cache] Failed to create Slang global session.");
        }
    }

    void invalidate(const assets::AssetPath& path) {
        auto key = canonical_asset_path_string(path);
        module_ir_cache_.erase(key);
        vfs_.remove(key);
    }

    void set_preprocessed(const Shader& shader) {
        if (!shader.source.is_slang()) return;
        auto identity     = canonical_asset_path_string(shader.path);
        auto import_key   = shader.import_path.is_custom()
                                ? canonical_asset_path_string(shader_custom_path(shader.import_path.as_custom_path()))
                                : canonical_asset_path_string(shader.import_path.as_asset_path());
        auto preprocessed = preprocess_slang_compile_source(shader.source.as_str(), shader.path);
        vfs_.add(identity, preprocessed, identity);
        if (import_key != identity) vfs_.add(import_key, preprocessed, identity);
    }

    std::expected<std::vector<std::uint8_t>, ShaderCacheError> compile(
        assets::AssetId<Shader> id,
        const Shader& shader,
        std::span<const ShaderDefVal> shader_defs,
        const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders) {
        using Stage = ShaderCacheError::SlangCompileError::Stage;

        if (!global_session) {
            return std::unexpected(
                ShaderCacheError::slang_error(Stage::SessionCreation, "Slang global session not available"));
        }

        auto root_path = canonical_asset_path_string(shader.path);
        // Use the pre-cached preprocessed source (computed at set_shader time);
        // fall back to computing it here if somehow absent.
        const std::string* preprocessed_ptr = nullptr;
        std::string preprocessed_fallback;
        try {
            preprocessed_ptr = &vfs_.get_source(root_path);
        } catch (const std::out_of_range&) {
            preprocessed_fallback = preprocess_slang_compile_source(shader.source.as_str(), shader.path);
            preprocessed_ptr      = &preprocessed_fallback;
        }
        auto defs_key = defs_cache_key(shader_defs);

        // Session-level preprocessor macros.
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
        target_desc.profile           = global_session->findProfile("spirv_1_3");
        target_desc.flags             = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

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
        session_desc.fileSystem               = &vfs_;

        Slang::ComPtr<slang::ISession> session;
        if (SLANG_FAILED(global_session->createSession(session_desc, session.writeRef()))) {
            return std::unexpected(
                ShaderCacheError::slang_error(Stage::SessionCreation, "Failed to create Slang session"));
        }

        // Pre-load cached dependency modules from serialized IR blobs.
        // Do not preload the root itself; it is loaded explicitly below as the
        // compile target and double-loading the same identity can trip Slang's
        // internal dictionary asserts.
        std::unordered_map<assets::AssetId<Shader>, Shader> deps_only = shaders;
        deps_only.erase(id);
        preload_cached_modules(session.get(), defs_key, deps_only);

        // Load root module from preprocessed source.
        Slang::ComPtr<slang::IBlob> diagnostics;
        slang::IModule* mod = session->loadModuleFromSourceString("shader", root_path.c_str(),
                                                                  preprocessed_ptr->c_str(), diagnostics.writeRef());
        if (!mod) {
            return std::unexpected(ShaderCacheError::slang_error(
                Stage::ModuleLoad, format_diagnostics("Slang compilation failed", diagnostics.get())));
        }

        // Cache any newly compiled dependency modules for future reuse.
        cache_loaded_modules(session.get(), defs_key, root_path);

        // Gather entry points.
        std::vector<slang::IComponentType*> components;
        components.push_back(mod);
        SlangInt ep_count = mod->getDefinedEntryPointCount();

        // SPIR-V requires at least one entry point.  Library-only shaders cannot
        // be used as root pipeline modules; the caller must use them as imports.
        if (ep_count == 0) {
            return std::unexpected(ShaderCacheError::slang_error(
                Stage::NoEntryPoints,
                "shader has no entry points and cannot be compiled to SPIR-V directly; "
                "use it as an imported library module instead"));
        }

        std::vector<Slang::ComPtr<slang::IEntryPoint>> entry_points(ep_count);
        for (SlangInt i = 0; i < ep_count; ++i) {
            mod->getDefinedEntryPoint(i, entry_points[i].writeRef());
            components.push_back(entry_points[i].get());
        }

        // Compose.
        Slang::ComPtr<slang::IComponentType> composed;
        {
            Slang::ComPtr<slang::IBlob> diag;
            if (SLANG_FAILED(session->createCompositeComponentType(components.data(),
                                                                   static_cast<SlangInt>(components.size()),
                                                                   composed.writeRef(), diag.writeRef()))) {
                return std::unexpected(ShaderCacheError::slang_error(
                    Stage::Compose, format_diagnostics("Slang compose failed", diag.get())));
            }
        }

        // Link.
        Slang::ComPtr<slang::IComponentType> linked;
        {
            Slang::ComPtr<slang::IBlob> diag;
            if (SLANG_FAILED(composed->link(linked.writeRef(), diag.writeRef()))) {
                return std::unexpected(
                    ShaderCacheError::slang_error(Stage::Link, format_diagnostics("Slang link failed", diag.get())));
            }
        }

        // Code generation → SPIR-V.
        Slang::ComPtr<slang::IBlob> spirv_code;
        {
            Slang::ComPtr<slang::IBlob> diag;
            if (SLANG_FAILED(linked->getTargetCode(0, spirv_code.writeRef(), diag.writeRef()))) {
                return std::unexpected(ShaderCacheError::slang_error(
                    Stage::CodeGeneration, format_diagnostics("Slang code generation failed", diag.get())));
            }
        }

        auto* ptr = static_cast<const std::uint8_t*>(spirv_code->getBufferPointer());
        return std::vector<std::uint8_t>(ptr, ptr + spirv_code->getBufferSize());
    }

    std::expected<std::vector<std::uint8_t>, ShaderCacheError> compile_ir_root(
        assets::AssetId<Shader> id,
        const Shader& shader,
        std::span<const ShaderDefVal> shader_defs,
        const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders) {
        using Stage = ShaderCacheError::SlangCompileError::Stage;

        if (!global_session) {
            return std::unexpected(
                ShaderCacheError::slang_error(Stage::SessionCreation, "Slang global session not available"));
        }

        auto root_path = canonical_asset_path_string(shader.path);
        auto defs_key  = defs_cache_key(shader_defs);

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
        target_desc.profile           = global_session->findProfile("spirv_1_3");
        target_desc.flags             = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

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
        session_desc.fileSystem               = &vfs_;

        Slang::ComPtr<slang::ISession> session;
        if (SLANG_FAILED(global_session->createSession(session_desc, session.writeRef()))) {
            return std::unexpected(
                ShaderCacheError::slang_error(Stage::SessionCreation, "Failed to create Slang session"));
        }

        preload_cached_modules(session.get(), defs_key, shaders);

        const auto& ir_bytes = std::get<Source::SlangIr>(shader.source.data).bytes;
        Slang::ComPtr<ISlangBlob> ir_blob;
        ir_blob.attach(slang_createBlob(ir_bytes.data(), ir_bytes.size()));

        Slang::ComPtr<slang::IBlob> diagnostics;
        slang::IModule* mod =
            session->loadModuleFromIRBlob(root_path.c_str(), root_path.c_str(), ir_blob.get(), diagnostics.writeRef());
        if (!mod) {
            return std::unexpected(ShaderCacheError::slang_error(
                Stage::ModuleLoad, format_diagnostics("Slang IR root load failed", diagnostics.get())));
        }

        cache_loaded_modules(session.get(), defs_key, root_path);

        std::vector<slang::IComponentType*> components;
        components.push_back(mod);
        SlangInt ep_count = mod->getDefinedEntryPointCount();

        // SPIR-V requires at least one entry point.  Library-only shaders cannot
        // be used as root pipeline modules; the caller must use them as imports.
        if (ep_count == 0) {
            return std::unexpected(ShaderCacheError::slang_error(
                Stage::NoEntryPoints,
                "shader has no entry points and cannot be compiled to SPIR-V directly; "
                "use it as an imported library module instead"));
        }

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
                    Stage::Compose, format_diagnostics("Slang compose failed", diag.get())));
            }
        }

        Slang::ComPtr<slang::IComponentType> linked;
        {
            Slang::ComPtr<slang::IBlob> diag;
            if (SLANG_FAILED(composed->link(linked.writeRef(), diag.writeRef()))) {
                return std::unexpected(
                    ShaderCacheError::slang_error(Stage::Link, format_diagnostics("Slang link failed", diag.get())));
            }
        }

        Slang::ComPtr<slang::IBlob> spirv_code;
        {
            Slang::ComPtr<slang::IBlob> diag;
            if (SLANG_FAILED(linked->getTargetCode(0, spirv_code.writeRef(), diag.writeRef()))) {
                return std::unexpected(ShaderCacheError::slang_error(
                    Stage::CodeGeneration, format_diagnostics("Slang code generation failed", diag.get())));
            }
        }

        auto* ptr = static_cast<const std::uint8_t*>(spirv_code->getBufferPointer());
        return std::vector<std::uint8_t>(ptr, ptr + spirv_code->getBufferSize());
    }

   private:
    // Precompiled module IR cache: identity → (defs_key → serialized IR blob).
    // Session-level macros affect all modules, so blobs are keyed by both
    // the module identity and the definition set used during compilation.
    // Invalidation by identity removes all def variants at once.
    std::unordered_map<std::string, std::unordered_map<std::string, Slang::ComPtr<ISlangBlob>>> module_ir_cache_;
    // Persistent VFS: populated at set_shader time with preprocessed Slang
    // sources for all registered shaders.  Serves as both the preprocessed-
    // source cache and the file system passed to each Slang session.
    SlangVFS vfs_;

    static std::string defs_cache_key(std::span<const ShaderDefVal> defs) {
        std::string key;
        for (const auto& d : defs) {
            if (!key.empty()) key += '|';
            key += d.name;
            key += '=';
            key += d.value_as_string();
        }
        return key;
    }

    // Pre-load cached dependency modules into the session from serialized IR.
    // Iterates all registered shaders (same policy as populate_vfs).
    //
    // For SlangIr shaders the IR bytes are read directly from dep.source — they
    // bypass module_ir_cache_ entirely (no defs-key concept; IR has any defs
    // baked in).
    //
    // For Slang-text shaders the blob is looked up in module_ir_cache_ for the
    // exact defs_key; no fallback to other keys, preserving cache correctness.
    void preload_cached_modules(slang::ISession* session,
                                const std::string& defs_key,
                                const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders) {
        for (const auto& [_, dep] : shaders) {
            // Only handle Slang-family sources.
            if (!dep.source.is_slang() && !dep.source.is_slang_ir()) continue;

            auto identity    = canonical_asset_path_string(dep.path);
            auto import_name = dep.import_path.is_custom()
                                   ? canonical_asset_path_string(shader_custom_path(dep.import_path.as_custom_path()))
                                   : canonical_asset_path_string(dep.import_path.as_asset_path());

            if (dep.source.is_slang_ir()) {
                // Load the pre-compiled IR blob from the live source bytes —
                // independent of any compiled-with-defs cache.
                const auto& ir_bytes = std::get<Source::SlangIr>(dep.source.data).bytes;
                Slang::ComPtr<ISlangBlob> ir_blob;
                ir_blob.attach(slang_createBlob(ir_bytes.data(), ir_bytes.size()));
                Slang::ComPtr<slang::IBlob> diag;
                session->loadModuleFromIRBlob(import_name.c_str(), identity.c_str(), ir_blob.get(), diag.writeRef());
                continue;
            }

            // Slang text shader: look up compiled IR for this exact defs_key.
            auto outer_it = module_ir_cache_.find(identity);
            if (outer_it == module_ir_cache_.end()) continue;
            auto inner_it = outer_it->second.find(defs_key);
            if (inner_it == outer_it->second.end()) continue;

            Slang::ComPtr<slang::IBlob> diag;
            auto* loaded = session->loadModuleFromIRBlob(import_name.c_str(), identity.c_str(), inner_it->second.get(),
                                                         diag.writeRef());
            if (!loaded) {
                spdlog::debug("[shader.cache] Cached IR blob stale for '{}', evicting.", identity);
                outer_it->second.erase(inner_it);
                if (outer_it->second.empty()) module_ir_cache_.erase(outer_it);
            }
        }
    }

    // After compilation, serialize any newly loaded dependency modules and
    // store them in module_ir_cache_ for future sessions.
    void cache_loaded_modules(slang::ISession* session, const std::string& defs_key, const std::string& root_identity) {
        SlangInt count = session->getLoadedModuleCount();
        for (SlangInt i = 0; i < count; ++i) {
            auto* loaded_mod = session->getLoadedModule(i);
            if (!loaded_mod) continue;
            const char* uid = loaded_mod->getUniqueIdentity();
            if (!uid) continue;
            std::string identity(uid);
            // Skip root module — it varies by preprocessor macros and is already
            // cached at the ShaderData::processed_shaders level.
            if (identity == root_identity) continue;
            if (module_ir_cache_[identity].contains(defs_key)) continue;

            Slang::ComPtr<ISlangBlob> blob;
            if (SLANG_SUCCEEDED(loaded_mod->serialize(blob.writeRef())) && blob) {
                spdlog::trace("[shader.cache] Cached serialized IR for '{}' (defs='{}').", identity, defs_key);
                module_ir_cache_[identity].emplace(defs_key, std::move(blob));
            }
        }
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
    auto missing_import           = [&]() {
        if (auto shader_it = shaders.find(shader_id); shader_it != shaders.end()) {
            return resolve_import_key(shader_it->second, import_ref);
        }
        return import_ref;
    };

    if (composer.contains_module(module_name)) return {};

    auto data_it = data.find(shader_id);
    if (data_it == data.end()) return std::unexpected(ShaderCacheError::import_not_available({missing_import()}));

    auto provider_it = data_it->second.resolved_imports.find(import_ref);
    if (provider_it == data_it->second.resolved_imports.end()) {
        return std::unexpected(ShaderCacheError::import_not_available({missing_import()}));
    }

    auto sit = shaders.find(provider_it->second);
    if (sit == shaders.end()) return std::unexpected(ShaderCacheError::import_not_available({missing_import()}));
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

    auto missing_imports = missing_imports_for_shader(shaders_, data_, id);
    if (!missing_imports.empty()) {
        return std::unexpected(ShaderCacheError::import_not_available(std::move(missing_imports)));
    }

    auto merged_defs = merge_shader_defs(shader_defs, shader);

    auto cache_it = shader_data.processed_shaders.find(merged_defs);
    if (cache_it != shader_data.processed_shaders.end()) {
        spdlog::trace("[shader.cache] Cache hit for shader '{}'.", assets::UntypedAssetId(id));
        return cache_it->second;
    }

    spdlog::debug("[shader.cache] Compiling shader '{}' with {} defs.", assets::UntypedAssetId(id), merged_defs.size());

    ShaderCacheSource source;
    std::vector<std::uint8_t> slang_spirv_bytes;  // backing storage for Slang-compiled SPIR-V
    std::string composed_wgsl;                    // backing storage for WGSL composition
    if (std::holds_alternative<Source::SpirV>(shader.source.data)) {
        const auto& bytes = std::get<Source::SpirV>(shader.source.data).bytes;
        source            = ShaderCacheSource{ShaderCacheSource::SpirV{std::span<const std::uint8_t>(bytes)}};
    } else if (std::holds_alternative<Source::Slang>(shader.source.data)) {
        auto spirv = slang_->compile(id, shader, merged_defs, shaders_);
        if (!spirv) return std::unexpected(spirv.error());
        slang_spirv_bytes = std::move(spirv.value());
        source = ShaderCacheSource{ShaderCacheSource::SpirV{std::span<const std::uint8_t>(slang_spirv_bytes)}};
    } else if (std::holds_alternative<Source::SlangIr>(shader.source.data)) {
        // Keep explicit .slang-module assets as dependency-only modules.
        if (shader.path.path.extension() == ".slang-module") {
            return std::unexpected(
                ShaderCacheError::slang_error(ShaderCacheError::SlangCompileError::Stage::ModuleLoad,
                                              "Root shader cannot be a pre-compiled Slang IR module (.slang-module); "
                                              "SlangIr sources are only valid as imported dependencies"));
        }

        // Processed .slang assets may carry SlangIr roots when
        // preprocess_slang_to_ir is enabled. Compile those as root modules.
        auto spirv = slang_->compile_ir_root(id, shader, merged_defs, shaders_);
        if (!spirv) return std::unexpected(spirv.error());
        slang_spirv_bytes = std::move(spirv.value());
        source = ShaderCacheSource{ShaderCacheSource::SpirV{std::span<const std::uint8_t>(slang_spirv_bytes)}};
    } else {
        for (const auto& imp : shader.imports) {
            if (auto res = add_import_to_composer(composer_, data_, shaders_, id, imp); !res)
                return std::unexpected(res.error());
        }
        auto composed =
            composer_.compose(shader.source.as_str(), canonical_asset_path_string(shader.path), merged_defs);
        if (!composed) return std::unexpected(ShaderCacheError::process_error(std::move(composed.error())));
        composed_wgsl = std::move(composed.value());
        source = ShaderCacheSource{ShaderCacheSource::Wgsl{std::string_view(composed_wgsl)}};
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
        slang_->invalidate(existing->second.path);
        unregister_import_names(existing->second);
    }
    slang_->invalidate(shader.path);
    invalidate_dependents(shaders_, data_, waiting_on_import_, id);
    reset_shader_links(data_, waiting_on_import_, id);

    shaders_[id]                = shader;
    const Shader& stored_shader = shaders_.at(id);
    auto& shader_data           = data_[id];

    slang_->set_preprocessed(stored_shader);
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

            if (std::ranges::all_of(waiter_it->second.imports, [&](const ShaderImport& import_ref) {
                    return waiter_data.resolved_imports.contains(import_ref);
                })) {
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
    auto sit = shaders_.find(id);
    if (sit != shaders_.end()) slang_->invalidate(sit->second.path);
    auto affected = clear(id);
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
    return std::ranges::to<std::vector>(affected);
}

}  // namespace epix::shader
