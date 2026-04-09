module;

export module epix.shader:shader_cache;

import epix.core;
import epix.utils;
import :shader;
import :shader_composer;

namespace epix::shader {

/** @brief Id used by `ShaderCache` to track which pipeline owns cached shader variants. */
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

/** @brief Cache state stored for one shader asset.
 *
 * This type is exported, but it mainly exists so `ShaderCache` can track which
 * pipelines use a shader, which compiled variants already exist, and which
 * imports have been resolved.
 */
export struct ShaderData {
    /** @brief Pipelines currently using this shader. */
    std::unordered_set<CachedPipelineId> pipelines;
    /** @brief Cached compiled modules keyed by definition set. */
    std::unordered_map<std::vector<ShaderDefVal>, std::shared_ptr<wgpu::ShaderModule>> processed_shaders;
    /** @brief Imports already matched to concrete shader assets. */
    std::unordered_map<ShaderImport, assets::AssetId<Shader>> resolved_imports;
    /** @brief Shaders that depend on this shader. */
    std::unordered_set<assets::AssetId<Shader>> dependents;
};

/** @brief Source payload passed to the backend shader-module loader. */
export struct ShaderCacheSource {
    /** @brief SPIR-V bytes ready for backend module creation. */
    struct SpirV {
        std::span<const std::uint8_t> bytes;
    };
    /** @brief Final WGSL text ready for backend module creation. */
    struct Wgsl {
        std::string source;
    };

    /** @brief The active source form. */
    std::variant<SpirV, Wgsl> data;
};

/** @brief Error returned by `ShaderCache`.
 *
 * Common cases are:
 *
 * - the requested shader is not loaded yet,
 * - one of its imports is still missing,
 * - WGSL composition failed,
 * - Slang compilation failed,
 * - backend shader-module creation failed.
 */
export struct ShaderCacheError {
    /** @brief The requested shader asset is missing from the cache. */
    struct ShaderNotLoaded {
        assets::AssetId<Shader> id;
    };
    /** @brief WGSL processing failed before backend shader-module creation. */
    struct ProcessShaderError {
        ComposeError error;
    };
    /** @brief At least one imported shader is not available yet. */
    struct ShaderImportNotYetAvailable {};
    /** @brief Backend shader-module creation failed. */
    struct CreateShaderModule {
        std::string wgpu_message;
    };
    /** @brief Slang compilation failed. */
    struct SlangCompileError {
        /** @brief Step where Slang failed. */
        enum class Stage {
            SessionCreation,
            ModuleLoad,
            Compose,
            Link,
            CodeGeneration,
        };
        /** @brief Step where the failure happened. */
        Stage stage;
        /** @brief Error text returned by Slang. */
        std::string message;
    };

    /** @brief The active error value. */
    std::
        variant<ShaderNotLoaded, ProcessShaderError, ShaderImportNotYetAvailable, CreateShaderModule, SlangCompileError>
            data;

    /** @brief Create a `ShaderNotLoaded` error. */
    static ShaderCacheError not_loaded(assets::AssetId<Shader> id) { return {ShaderNotLoaded{id}}; }
    /** @brief Create a `ProcessShaderError`. */
    static ShaderCacheError process_error(ComposeError error) { return {ProcessShaderError{std::move(error)}}; }
    /** @brief Create a `ShaderImportNotYetAvailable` error. */
    static ShaderCacheError import_not_available() { return {ShaderImportNotYetAvailable{}}; }
    /** @brief Create a `CreateShaderModule` error. */
    static ShaderCacheError create_module_failed(std::string wgpu_message) {
        return {CreateShaderModule{std::move(wgpu_message)}};
    }
    /** @brief Create a `SlangCompileError`. */
    static ShaderCacheError slang_error(SlangCompileError::Stage stage, std::string message) {
        return {SlangCompileError{stage, std::move(message)}};
    }

    /** @brief Returns `true` for errors that may succeed later without changing code.
     *
     * For example, a missing import may become available after another shader is
     * loaded.
     */
    bool is_recoverable() const {
        return std::holds_alternative<ShaderNotLoaded>(data) ||
               std::holds_alternative<ShaderImportNotYetAvailable>(data);
    }

    /** @brief Build a readable error message. */
    std::string message() const {
        return std::visit(
            [](auto&& e) -> std::string {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, ShaderNotLoaded>) {
                    return std::format("shader not loaded (id={})", assets::UntypedAssetId(e.id));
                } else if constexpr (std::is_same_v<T, ProcessShaderError>) {
                    return "shader processing error";
                } else if constexpr (std::is_same_v<T, ShaderImportNotYetAvailable>) {
                    return "shader import not yet available";
                } else if constexpr (std::is_same_v<T, CreateShaderModule>) {
                    return std::format("create shader module failed: {}", e.wgpu_message);
                } else if constexpr (std::is_same_v<T, SlangCompileError>) {
                    return std::format("Slang compilation failed: {}", e.message);
                }
            },
            data);
    }
};

/** @brief Runtime cache for resolved and compiled shaders.
 *
 * Typical flow:
 *
 * - call `set_shader(...)` when a shader asset is loaded or changed,
 * - call `get(...)` when a pipeline needs a compiled shader variant,
 * - call `remove(...)` or `sync(...)` when assets are removed or events arrive.
 *
 * `ShaderCache` resolves recursive imports, composes WGSL, compiles Slang,
 * caches compiled modules by definition set, and tells you which pipelines need
 * to rebuild when something changes.
 */
export struct ShaderCache {
    /** @brief Callback that turns final WGSL or SPIR-V into a backend shader module. */
    using LoadModuleFn = std::function<std::expected<wgpu::ShaderModule, ShaderCacheError>(
        const wgpu::Device&, const ShaderCacheSource&, ValidateShader)>;

    /** @brief Create a shader cache bound to one device and one backend loader callback. */
    ShaderCache(wgpu::Device device, LoadModuleFn load_module);

    /** @brief Get or build one compiled shader variant.
     *
     * `pipeline` tells the cache which pipeline is using the result.
     * `id` is the root shader asset.
     * `shader_defs` are extra definitions for this specific variant.
     *
     * If imports are still missing, this returns
     * `ShaderCacheError::import_not_available()` instead of forcing a hard
     * failure.
     */
    std::expected<std::shared_ptr<wgpu::ShaderModule>, ShaderCacheError> get(CachedPipelineId pipeline,
                                                                             assets::AssetId<Shader> id,
                                                                             std::span<const ShaderDefVal> shader_defs);

    /** @brief Insert or replace one shader in the cache.
     *
     * This updates import resolution, clears stale compiled variants, and
     * returns pipeline ids that should rebuild.
     */
    std::vector<CachedPipelineId> set_shader(assets::AssetId<Shader> id, Shader shader);
    /** @brief Remove one shader from the cache and return affected pipelines. */
    std::vector<CachedPipelineId> remove(assets::AssetId<Shader> id);

    /** @brief Apply a batch of shader asset events, and return affected pipelines.
     *
     * `set_shader` when `LoadedWithDependencies` or `Modified`, this will take the shader from `shaders` to this cache.
     * `remove` when `Unused` (but not `Removed`, removed asset may still have living handles).
     *
     * The return value is the full list of pipelines that should rebuild.
     */
    std::vector<CachedPipelineId> sync(utils::input_iterable<assets::AssetEvent<Shader>> events,
                                       const assets::Assets<Shader>& shaders);

   private:
    wgpu::Device device_;
    std::unordered_map<assets::AssetId<Shader>, ShaderData> data_;
    LoadModuleFn load_module_;
    std::unordered_map<assets::AssetId<Shader>, Shader> shaders_;
    std::unordered_map<ShaderImport, assets::AssetId<Shader>> import_path_shaders_;
    std::unordered_map<ShaderImport, std::unordered_set<assets::AssetId<Shader>>> waiting_on_import_;
    ShaderComposer composer_;

    struct SlangCompiler;
    std::shared_ptr<SlangCompiler> slang_;

    std::vector<CachedPipelineId> clear(assets::AssetId<Shader> id);

    // Register/unregister the secondary import name (asset path from file path)
    // when it differs from the primary import_path.
    void register_import_names(const Shader& shader, assets::AssetId<Shader> id);
    void unregister_import_names(const Shader& shader);

    static std::expected<void, ShaderCacheError> add_import_to_composer(
        ShaderComposer& composer,
        const std::unordered_map<assets::AssetId<Shader>, ShaderData>& data,
        const std::unordered_map<assets::AssetId<Shader>, Shader>& shaders,
        assets::AssetId<Shader> shader_id,
        const ShaderImport& import_ref);
};

}  // namespace epix::shader
