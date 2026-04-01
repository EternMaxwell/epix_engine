module;

export module epix.shader:shader_cache;

import :shader;
import :shader_composer;

namespace epix::shader {

// ─── CachedPipelineId ──────────────────────────────────────────────────────
export struct CachedPipelineId : utils::int_base<std::uint64_t> {
    using utils::int_base<std::uint64_t>::int_base;
};

}  // namespace epix::shader

template <>
struct std::hash<epix::shader::CachedPipelineId> {
    std::size_t operator()(const epix::shader::CachedPipelineId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.get());
    }
};

namespace epix::shader {

// ─── ShaderData (internal) ─────────────────────────────────────────────────
export struct ShaderData {
    std::unordered_set<CachedPipelineId> pipelines;
    std::unordered_map<std::vector<ShaderDefVal>, std::shared_ptr<wgpu::ShaderModule>> processed_shaders;
    std::unordered_map<ShaderImport, assets::AssetId<Shader>> resolved_imports;
    std::unordered_set<assets::AssetId<Shader>> dependents;
};

// ─── ShaderCacheSource ─────────────────────────────────────────────────────
export struct ShaderCacheSource {
    struct SpirV {
        std::span<const std::uint8_t> bytes;
    };
    struct Wgsl {
        std::string source;
    };

    std::variant<SpirV, Wgsl> data;
};

// ─── ShaderCacheError ──────────────────────────────────────────────────────
// Note: ComposeError is defined in :shader_composer.
export struct ShaderCacheError {
    struct ShaderNotLoaded {
        assets::AssetId<Shader> id;
    };
    struct ProcessShaderError {
        ComposeError error;
    };
    struct ShaderImportNotYetAvailable {};
    struct CreateShaderModule {
        std::string wgpu_message;
    };

    std::variant<ShaderNotLoaded, ProcessShaderError, ShaderImportNotYetAvailable, CreateShaderModule> data;

    static ShaderCacheError not_loaded(assets::AssetId<Shader> id) { return {ShaderNotLoaded{id}}; }
    static ShaderCacheError process_error(ComposeError error) { return {ProcessShaderError{std::move(error)}}; }
    static ShaderCacheError import_not_available() { return {ShaderImportNotYetAvailable{}}; }
    static ShaderCacheError create_module_failed(std::string wgpu_message) {
        return {CreateShaderModule{std::move(wgpu_message)}};
    }
};

// ─── ShaderCache ───────────────────────────────────────────────────────────
export struct ShaderCache {
    using LoadModuleFn = std::function<std::expected<wgpu::ShaderModule, ShaderCacheError>(
        const wgpu::Device&, const ShaderCacheSource&, ValidateShader)>;

   private:
    wgpu::Device device_;
    std::unordered_map<assets::AssetId<Shader>, ShaderData> data_;
    LoadModuleFn load_module_;
    std::unordered_map<assets::AssetId<Shader>, Shader> shaders_;
    std::unordered_map<ShaderImport, assets::AssetId<Shader>> import_path_shaders_;
    std::unordered_map<ShaderImport, std::vector<assets::AssetId<Shader>>> waiting_on_import_;
    ShaderComposer composer_;

   public:
    ShaderCache(wgpu::Device device, LoadModuleFn load_module);

    std::expected<std::shared_ptr<wgpu::ShaderModule>, ShaderCacheError> get(CachedPipelineId pipeline,
                                                                             assets::AssetId<Shader> id,
                                                                             std::span<const ShaderDefVal> shader_defs);

    std::vector<CachedPipelineId> set_shader(assets::AssetId<Shader> id, Shader shader);
    std::vector<CachedPipelineId> remove(assets::AssetId<Shader> id);

   private:
    std::vector<CachedPipelineId> clear(assets::AssetId<Shader> id);

    static std::expected<void, ShaderCacheError> add_import_to_composer(
        ShaderComposer& composer,
        const std::unordered_map<ShaderImport, assets::AssetId<Shader>>& import_path_shaders,
        const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders,
        const ShaderImport& import_ref);
};

}  // namespace epix::shader
