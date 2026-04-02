module;

#include <slang-com-ptr.h>
#include <slang.h>
#include <spdlog/spdlog.h>

module epix.shader;

import :shader_cache;

using namespace epix::shader;

namespace epix::shader {

// ─── In-memory file system for resolving Slang imports from the shader cache ──
class CacheFileSystem : public ISlangFileSystem {
    const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders_;

   public:
    explicit CacheFileSystem(const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders) : shaders_(shaders) {}

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
        std::string_view requested(path);
        // Slang may pass the path verbatim from source text — strip any source://
        // scheme prefix since shader.path stores the scheme-free path component.
        std::string_view path_part = requested;
        if (auto scheme_end = requested.find("://"); scheme_end != std::string_view::npos)
            path_part = requested.substr(scheme_end + 3);

        auto normalize = [](std::string_view p) {
            std::string s(p);
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        };

        std::string requested_norm = normalize(path_part);
        std::string requested_ext  = requested_norm;
        if (std::filesystem::path(requested_norm).extension().empty()) {
            requested_ext += ".slang";
        }

        for (const auto& [id, shader] : shaders_) {
            if (!std::holds_alternative<Source::Slang>(shader.source.data)) continue;
            // Match by bare file path or by the AssetPath component of import_path.
            // Custom-name import_paths are never the target of file-based loading.
            const std::string shader_path_norm = normalize(shader.path);
            const std::string import_path_norm = shader.import_path.is_asset_path()
                                                     ? shader.import_path.as_asset_path().path.generic_string()
                                                     : std::string{};
            if (shader_path_norm == requested_norm || shader_path_norm == requested_ext ||
                import_path_norm == requested_norm || import_path_norm == requested_ext) {
                const auto& code = std::get<Source::Slang>(shader.source.data).code;
                *outBlob         = slang_createBlob(code.data(), code.size());
                return SLANG_OK;
            }
        }
        return SLANG_E_NOT_FOUND;
    }
};

// ─── Slang → SPIR-V compilation helper ─────────────────────────────────────
static std::expected<std::vector<std::uint8_t>, ShaderCacheError> compile_slang_to_spirv(
    const std::string& source,
    const std::string& path,
    std::span<const ShaderDefVal> shader_defs,
    const std::unordered_map<assets::AssetId<Shader>, Shader>& all_shaders) {
    // Create global session
    Slang::ComPtr<slang::IGlobalSession> globalSession;
    if (SLANG_FAILED(slang::createGlobalSession(globalSession.writeRef()))) {
        return std::unexpected(ShaderCacheError::create_module_failed("Failed to create Slang global session"));
    }

    // Build preprocessor macros from shader defs
    std::vector<std::string> def_values;  // keep strings alive
    std::vector<slang::PreprocessorMacroDesc> macros;
    def_values.reserve(shader_defs.size());
    macros.reserve(shader_defs.size());
    for (const auto& def : shader_defs) {
        def_values.push_back(def.value_as_string());
        macros.push_back({def.name.c_str(), def_values.back().c_str()});
    }

    // Configure target: SPIR-V output (wgpu-native loads SPIR-V directly)
    slang::TargetDesc targetDesc = {};
    targetDesc.format            = SLANG_SPIRV;
    targetDesc.profile           = globalSession->findProfile("sm_6_0");

    // In-memory file system so Slang can resolve `import` from cache
    CacheFileSystem cacheFs(all_shaders);

    // Create session
    slang::SessionDesc sessionDesc     = {};
    sessionDesc.targets                = &targetDesc;
    sessionDesc.targetCount            = 1;
    sessionDesc.preprocessorMacros     = macros.data();
    sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macros.size());
    sessionDesc.fileSystem             = &cacheFs;

    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef()))) {
        return std::unexpected(ShaderCacheError::create_module_failed("Failed to create Slang session"));
    }

    // Load module from source string
    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module =
        session->loadModuleFromSourceString("shader", path.c_str(), source.c_str(), diagnostics.writeRef());
    if (!module) {
        std::string msg = "Slang compilation failed";
        if (diagnostics) {
            msg += ": ";
            msg += static_cast<const char*>(diagnostics->getBufferPointer());
        }
        return std::unexpected(ShaderCacheError::create_module_failed(std::move(msg)));
    }

    // Gather module + all defined entry points into a composite
    std::vector<slang::IComponentType*> components;
    components.push_back(module);

    SlangInt entryPointCount = module->getDefinedEntryPointCount();
    std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPoints(entryPointCount);
    for (SlangInt i = 0; i < entryPointCount; ++i) {
        module->getDefinedEntryPoint(i, entryPoints[i].writeRef());
        components.push_back(entryPoints[i].get());
    }

    Slang::ComPtr<slang::IComponentType> composed;
    {
        Slang::ComPtr<slang::IBlob> linkDiag;
        if (SLANG_FAILED(session->createCompositeComponentType(components.data(),
                                                               static_cast<SlangInt>(components.size()),
                                                               composed.writeRef(), linkDiag.writeRef()))) {
            std::string msg = "Slang linking failed";
            if (linkDiag) {
                msg += ": ";
                msg += static_cast<const char*>(linkDiag->getBufferPointer());
            }
            return std::unexpected(ShaderCacheError::create_module_failed(std::move(msg)));
        }
    }

    // Link — resolves all transitively imported modules
    Slang::ComPtr<slang::IComponentType> linked;
    {
        Slang::ComPtr<slang::IBlob> linkDiag2;
        if (SLANG_FAILED(composed->link(linked.writeRef(), linkDiag2.writeRef()))) {
            std::string msg = "Slang link step failed";
            if (linkDiag2) {
                msg += ": ";
                msg += static_cast<const char*>(linkDiag2->getBufferPointer());
            }
            return std::unexpected(ShaderCacheError::create_module_failed(std::move(msg)));
        }
    }

    // Get compiled SPIR-V output
    Slang::ComPtr<slang::IBlob> spirvCode;
    {
        Slang::ComPtr<slang::IBlob> codeDiag;
        if (SLANG_FAILED(linked->getTargetCode(0, spirvCode.writeRef(), codeDiag.writeRef()))) {
            std::string msg = "Slang code generation failed";
            if (codeDiag) {
                msg += ": ";
                msg += static_cast<const char*>(codeDiag->getBufferPointer());
            }
            return std::unexpected(ShaderCacheError::create_module_failed(std::move(msg)));
        }
    }

    auto ptr = static_cast<const std::uint8_t*>(spirvCode->getBufferPointer());
    return std::vector<std::uint8_t>(ptr, ptr + spirvCode->getBufferSize());
}

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
        // Slang: compile to SPIR-V via Slang compiler (bypasses ShaderComposer)
        auto spirv = compile_slang_to_spirv(std::get<Source::Slang>(shader.source.data).code, shader.path, merged_defs,
                                            shaders_);
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
