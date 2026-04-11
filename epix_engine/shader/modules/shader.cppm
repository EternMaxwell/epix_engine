module;

#include <zpp_bits.h>

export module epix.shader:shader;

import epix.assets;
import epix.core;
import webgpu;
import std;

namespace epix::shader {

inline std::string canonical_asset_path_string(const assets::AssetPath& path) {
    std::string normalized;
    if (!path.source.is_default()) {
        normalized += *path.source.as_str();
        normalized += "://";
    }

    if (path.path.has_root_directory()) {
        normalized += '/';
        normalized += path.path.relative_path().generic_string();
    } else {
        normalized += path.path.generic_string();
    }

    if (path.label) {
        normalized += '#';
        normalized += *path.label;
    }
    return normalized;
}

/** @brief One shader definition value.
 *
 * This is used for shader options such as `USE_FOG`, `MAX_LIGHTS`, or
 * `MSAA_SAMPLES`. The stored value is later turned into text like `true`, `4`,
 * or `16` when the shader is composed or compiled.
 */
export struct ShaderDefVal {
   private:
    friend zpp::bits::access;
    using serialize = zpp::bits::members<2>;

   public:
    ShaderDefVal() = default;

    /** @brief Definition name, such as `USE_FOG` or `MAX_LIGHTS`. */
    std::string name;
    /** @brief Definition value. */
    std::variant<bool, std::int32_t, std::uint32_t> value;

    /** @brief Create a boolean definition that defaults to `true`. */
    explicit ShaderDefVal(std::string n) : name(std::move(n)), value(true) {}

    /** @brief Create a boolean definition, for example `from_bool("USE_FOG")`. */
    static ShaderDefVal from_bool(std::string n, bool v = true) {
        ShaderDefVal d(std::move(n));
        d.value = v;
        return d;
    }
    /** @brief Create a signed integer definition, for example `from_int("LOD", -1)`. */
    static ShaderDefVal from_int(std::string n, std::int32_t v) {
        ShaderDefVal d(std::move(n));
        d.value = v;
        return d;
    }
    /** @brief Create an unsigned integer definition, for example `from_uint("MSAA_SAMPLES", 4)`. */
    static ShaderDefVal from_uint(std::string n, std::uint32_t v) {
        ShaderDefVal d(std::move(n));
        d.value = v;
        return d;
    }

    /** @brief Convert the stored value to the string form used by shader code. */
    std::string value_as_string() const {
        return std::visit(
            []<typename T>(const T& v) -> std::string {
                if constexpr (std::is_same_v<T, bool>) {
                    return v ? "true" : "false";
                } else {
                    return std::to_string(v);
                }
            },
            value);
    }

    bool operator==(const ShaderDefVal&) const = default;
};

/** @brief Controls whether backend shader validation is requested. */
export enum class ValidateShader : std::uint8_t {
    Disabled = 0,
    Enabled  = 1,
};

/** @brief Shader source payload.
 *
 * A shader can start from WGSL text, Slang text, or SPIR-V bytes.
 */
export struct Source {
    /** @brief WGSL source text. */
    struct Wgsl {
        std::string code;
    };
    /** @brief SPIR-V bytecode. */
    struct SpirV {
        std::vector<std::uint8_t> bytes;
    };
    /** @brief Slang source text. */
    struct Slang {
        std::string code;
    };
    /** @brief Pre-compiled Slang IR blob (content of a .slang-module file).
     *
     * The bytes are the raw output of Slang's `IModule::serialize()`.  These
     * modules are loaded via `loadModuleFromIRBlob` and bypass source-level
     * preprocessing.  The module's import graph is opaque — dependencies are
     * resolved by Slang internally during deserialization.
     */
    struct SlangIr {
        std::vector<std::uint8_t> bytes;
    };

    /** @brief The active payload. */
    std::variant<Wgsl, SpirV, Slang, SlangIr> data;

    /** @brief Create WGSL source. */
    static Source wgsl(std::string code) { return {Wgsl{std::move(code)}}; }
    /** @brief Create SPIR-V source. */
    static Source spirv(std::vector<std::uint8_t> bytes) { return {SpirV{std::move(bytes)}}; }
    /** @brief Create Slang source. */
    static Source slang(std::string code) { return {Slang{std::move(code)}}; }
    /** @brief Create a pre-compiled Slang IR module source. */
    static Source slang_ir(std::vector<std::uint8_t> bytes) { return {SlangIr{std::move(bytes)}}; }

    /** @brief Returns `true` when this source holds WGSL text. */
    bool is_wgsl() const { return std::holds_alternative<Wgsl>(data); }
    /** @brief Returns `true` when this source holds SPIR-V bytes. */
    bool is_spirv() const { return std::holds_alternative<SpirV>(data); }
    /** @brief Returns `true` when this source holds Slang text. */
    bool is_slang() const { return std::holds_alternative<Slang>(data); }
    /** @brief Returns `true` when this source holds a pre-compiled Slang IR blob. */
    bool is_slang_ir() const { return std::holds_alternative<SlangIr>(data); }

    /** @brief Read WGSL or Slang text as a string view.
     *
     * Call this only when the source is WGSL or Slang. Calling it for SPIR-V
     * is invalid.
     */
    std::string_view as_str() const {
        if (auto* w = std::get_if<Wgsl>(&data)) return w->code;
        if (auto* s = std::get_if<Slang>(&data)) return s->code;
        return std::get<Wgsl>(data).code;  // UB fallback
    }
};

/** @brief One shader import target.
 *
 * This keeps the exact kind of import the shader asked for.
 *
 * There are four cases:
 *
 * - WGSL `#import some::mod` or Slang `import some.mod;` becomes
 *   `ShaderImport::custom("some/mod")`.
 * - WGSL `#import "embedded://shared/math.wgsl"` or Slang
 *   `import "source://shared/math.slang";` becomes
 *   `ShaderImport::asset_path("source://shared/math.slang")`.
 * - WGSL `#import "/shared/math.wgsl"` or Slang
 *   `import "/shared/math.slang";`, given the current shader source `source`
 *   becomes `ShaderImport::asset_path("source://shared/math.slang")`
 * - WGSL `#import "common/math.wgsl"` or Slang
 *   `import "common/math.slang";` given this shader path `path://to/this/shader.[ext]`
 *   becomes `ShaderImport::asset_path("path://to/this/common/math.slang")`
 *
 * This is also used for the shader to declare how other shaders can import a shader itself.
 *
 *  - wgsl: use `#define_import_path ui::button` to expose `ui/button`
 *  - slang: `module epix.core;` or `module "epix/core";` to expose `epix/core`
 *
 * Custom names are stored as normalized paths. `::`, `.`, `/`, and `\` are
 * all accepted as separators and normalized to `/`.
 *
 * Custom imports are looked up by module name, while
 * asset-path imports are resolved as concrete files with the correct source and
 * path semantics preserved.
 */
export struct ShaderImport {
    /** @brief Stored import value.
     *
     * `assets::AssetPath` means "load this file".
     * `std::filesystem::path` means "load the module with this custom name".
     */
    std::variant<assets::AssetPath, std::filesystem::path> data;

    /** @brief Create an empty import value. */
    ShaderImport() = default;
    /** @brief Create a file-backed import. */
    explicit ShaderImport(assets::AssetPath p) : data(std::move(p)) {}
    /** @brief Create a custom-name import. */
    explicit ShaderImport(std::in_place_index_t<1>, std::filesystem::path name)
        : data(std::in_place_index<1>, std::move(name).lexically_normal()) {}

    /** @brief Helper for `ShaderImport` from an asset path. */
    static ShaderImport asset_path(assets::AssetPath p) { return ShaderImport(std::move(p)); }
    /** @brief Helper for `ShaderImport` from a custom module name. */
    static ShaderImport custom(std::filesystem::path name) {
        return ShaderImport(std::in_place_index<1>, std::move(name));
    }

    /** @brief Returns `true` when this import points to a file. */
    bool is_asset_path() const { return std::holds_alternative<assets::AssetPath>(data); }
    /** @brief Returns `true` when this import points to a custom module name. */
    bool is_custom() const { return std::holds_alternative<std::filesystem::path>(data); }

    /** @brief Get the file-backed import path. */
    const assets::AssetPath& as_asset_path() const { return std::get<assets::AssetPath>(data); }
    /** @brief Get the custom module path. */
    const std::filesystem::path& as_custom_path() const { return std::get<std::filesystem::path>(data); }
    /** @brief Get the custom module name as a normalized string. */
    std::string as_custom() const { return as_custom_path().generic_string(); }

    /** @brief Get the canonical module key used by shader composition.
     *
     * A custom import returns the normalized custom path unchanged.
     * A file import returns the canonical path wrapped in quotes so different
     * files with the same filename still stay distinct.
     */
    std::string module_name() const {
        if (is_asset_path()) return '"' + canonical_asset_path_string(as_asset_path()) + '"';
        return as_custom();
    }

    bool operator==(const ShaderImport&) const = default;
};

// ─── Forward-declare Shader so Handle<Shader> can be used in fields ────────
export struct Shader;

/** @brief Parsed shader asset.
 *
 * `path` is where the shader comes from.
 * `import_path` is the name other shaders use to import it.
 * `imports` is what this shader asks for.
 * `file_dependencies` contains only imports that were resolved as actual file
 * dependencies during loading.
 *
 * Note:
 *  - Other shaders can import this shader using either `path` or `import_path` (if an custom name is used)
 *  - Shaders added manually can also have assigned `path`, and will be resolved by the composer and compiler
 *    but will not be visible in loader and asset server, so cannot be imported by asset path, only custom name
 */
export struct Shader {
    /** @brief The shader asset path. */
    assets::AssetPath path;
    /** @brief Original source or bytecode. */
    Source source;
    /** @brief The shader's own import name.
     *
     * If the source does not declare a custom import name, this falls back to
     * `ShaderImport::asset_path(path)`.
     */
    ShaderImport import_path;
    /** @brief Imports declared by this shader. */
    std::vector<ShaderImport> imports;
    /** @brief Default definitions attached to this shader. */
    std::vector<ShaderDefVal> shader_defs;
    /** @brief File dependencies loaded through the asset system. */
    std::vector<assets::Handle<Shader>> file_dependencies;
    /** @brief Validation mode used when creating the backend shader module. */
    ValidateShader validate_shader = ValidateShader::Disabled;

    /** @brief Parse WGSL imports.
     *
     * `#define_import_path ui/button` sets the shader's own import name to a
     * custom import.
     *
     * For `#import`, WGSL has the same four cases described by `ShaderImport`:
     *
     * - `#import some::mod` -> custom import stored as `some/mod`.
     * - `#import "embedded://shared/math.wgsl"` -> file import with explicit source.
     * - `#import "/shared/math.wgsl"` -> file import relative to the source root.
     * - `#import "common/math.wgsl"` -> file import relative to the importing shader.
     */
    static std::pair<ShaderImport, std::vector<ShaderImport>> preprocess(std::string_view source,
                                                                         const assets::AssetPath& path);

    /** @brief Parse Slang imports.
     *
     * `module scene;` sets the shader's own import name to a custom import.
     *
     * For string-literal `import` and `__include`, Slang has the same three
     * file cases as WGSL:
     *
     * - `import lighting.core;` or `__include helpers;` -> custom import
     *   stored as a normalized path like `lighting/core`.
     * - `import "source://shared/lighting.slang";` -> file import with explicit source.
     * - `import "/shared/lighting.slang";` -> file import relative to the source root.
     * - `import "lighting/common.slang";` -> file import relative to the importing shader.
     *
     * The same path rules also apply to string-literal `__include` directives.
     */
    static std::pair<ShaderImport, std::vector<ShaderImport>> preprocess_slang(std::string_view source,
                                                                               const assets::AssetPath& path);

    /** @brief Create a shader from WGSL source and parse its imports. */
    static Shader from_wgsl(std::string source, assets::AssetPath path);
    /** @brief Create a WGSL shader and attach default definitions. */
    static Shader from_wgsl_with_defs(std::string source,
                                      assets::AssetPath path,
                                      std::vector<ShaderDefVal> shader_defs);
    /** @brief Create a shader from SPIR-V bytes. */
    static Shader from_spirv(std::vector<std::uint8_t> source, assets::AssetPath path);
    /** @brief Create a shader from Slang source and parse its imports. */
    static Shader from_slang(std::string source, assets::AssetPath path);
    /** @brief Create a Slang shader and attach default definitions. */
    static Shader from_slang_with_defs(std::string source,
                                       assets::AssetPath path,
                                       std::vector<ShaderDefVal> shader_defs);
    /** @brief Create a shader from a pre-compiled Slang IR blob (.slang-module).
     *
     * The `import_path` defaults to `ShaderImport::asset_path(path)`.  No
     * import scanning is performed — the module's dependency graph is embedded
     * inside the IR blob and resolved by Slang at load time.
     */
    static Shader from_slang_ir(std::vector<std::uint8_t> bytes, assets::AssetPath path);
};

/** @brief Loader settings for shader assets. */
export struct ShaderSettings {
    /** @brief Definitions attached to the loaded shader. */
    std::vector<ShaderDefVal> shader_defs;
};

/** @brief Error returned while loading a shader asset. */
export struct ShaderLoaderError {
    /** @brief File read error. */
    struct Io {
        std::error_code code;
        std::filesystem::path path;
    };
    /** @brief Parse error with byte offset. */
    struct Parse {
        std::filesystem::path path;
        std::size_t byte_offset;
    };
    /** @brief The active error value. */
    std::variant<Io, Parse> data;

    /** @brief Build an I/O error value. */
    static ShaderLoaderError io(std::error_code code, std::filesystem::path p) { return {Io{code, std::move(p)}}; }
    /** @brief Build a parse error value. */
    static ShaderLoaderError parse(std::filesystem::path p, std::size_t offset = 0) {
        return {Parse{std::move(p), offset}};
    }
};

/** @brief Convert `ShaderLoaderError` into `std::exception_ptr`. */
export inline std::exception_ptr to_exception_ptr(const ShaderLoaderError& err) noexcept {
    return std::visit(
        [](const auto& e) -> std::exception_ptr {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::same_as<T, ShaderLoaderError::Io>) {
                return std::make_exception_ptr(
                    std::runtime_error("ShaderLoaderError::Io: " + e.path.string() + " [" + e.code.message() + "]"));
            } else {
                return std::make_exception_ptr(std::runtime_error("ShaderLoaderError::Parse: " + e.path.string() +
                                                                  " at byte " + std::to_string(e.byte_offset)));
            }
        },
        err.data);
}

/** @brief Asset loader for shader files. */
export struct ShaderLoader {
    using Asset    = Shader;
    using Settings = ShaderSettings;
    using Error    = ShaderLoaderError;

    /** @brief File extensions handled by this loader. */
    static std::span<std::string_view> extensions();
    /** @brief Load one shader from a stream.
     *
     * File-backed imports are resolved through `context` and added to
     * `file_dependencies`. Custom-name imports stay as logical imports.
     */
    static std::expected<Shader, Error> load(std::istream& reader,
                                             const Settings& settings,
                                             assets::LoadContext& context);
};

/** @brief Processing settings used before shader loading. */
export struct ShaderProcessorSettings {
    /** @brief Settings forwarded to `ShaderLoader`. */
    ShaderSettings loader_settings;
    /** @brief When `true`, WGSL preprocessing is enabled. */
    bool preprocess_wgsl = true;
    /** @brief When `true`, Slang preprocessing is enabled. */
    bool preprocess_slang = true;
    /** @brief When `true`, `.slang` files are compiled to a Slang IR blob during
     *  asset processing.  The resulting blob is stored as
     *  `ProcessedSourceKind::SlangIr` in the processed asset and loaded back as
     *  `Source::SlangIr`.  Compilation uses a fresh Slang session that reads
     *  transitive source dependencies from the asset source; if any dep cannot
     *  be read or compilation fails the processor falls back to the normal
     *  text-preprocessing path.
     */
    bool preprocess_slang_to_ir = true;
};

/** @brief Asset processor for shader sources. */
export struct ShaderProcessor {
    using Settings     = ShaderProcessorSettings;
    using OutputLoader = ShaderLoader;

    ShaderProcessor() = default;
    explicit ShaderProcessor(std::shared_ptr<void> custom_registry) : custom_registry_(std::move(custom_registry)) {}

    /** @brief Process one shader asset before it is loaded. */
    std::expected<OutputLoader::Settings, std::exception_ptr> process(assets::ProcessContext& context,
                                                                      const Settings& settings,
                                                                      std::ostream& writer) const;

   private:
    std::shared_ptr<void> custom_registry_;
};

/** @brief Reference to a shader.
 *
 * Use this when a system can accept either the default shader, a concrete
 * loaded handle, or a filesystem path that will be resolved later.
 */
export struct ShaderRef {
    /** @brief Use the built-in default shader. */
    struct Default {};
    /** @brief Reference a shader by loaded handle. */
    struct ByHandle {
        assets::Handle<Shader> handle;
    };
    /** @brief Reference a shader by path. */
    struct ByPath {
        std::filesystem::path path;
    };

    /** @brief The active reference form. */
    std::variant<Default, ByHandle, ByPath> value;

    /** @brief Create a default shader reference. */
    ShaderRef() : value(Default{}) {}
    /** @brief Create a handle-based shader reference. */
    ShaderRef(ByHandle h) : value(std::move(h)) {}
    /** @brief Create a path-based shader reference. */
    ShaderRef(ByPath p) : value(std::move(p)) {}

    /** @brief Create a handle-based shader reference. */
    static ShaderRef from_handle(assets::Handle<Shader> h) { return ShaderRef{ByHandle{std::move(h)}}; }
    /** @brief Create a path-based shader reference. */
    static ShaderRef from_path(std::filesystem::path p) { return ShaderRef{ByPath{std::move(p)}}; }
    /** @brief Create a path-based shader reference from text. */
    static ShaderRef from_str(std::string_view s) { return ShaderRef{ByPath{std::filesystem::path{s}}}; }

    /** @brief Returns `true` when this is the default shader. */
    bool is_default() const { return std::holds_alternative<Default>(value); }
    /** @brief Returns `true` when this stores a handle. */
    bool is_handle() const { return std::holds_alternative<ByHandle>(value); }
    /** @brief Returns `true` when this stores a path. */
    bool is_path() const { return std::holds_alternative<ByPath>(value); }
};

/** @brief App plugin that registers shader loading and processing. */
export struct ShaderPlugin {
    /** @brief Register shader systems and asset support into the app. */
    void build(core::App& app);
};

}  // namespace epix::shader

template <>
struct std::hash<epix::shader::ShaderDefVal> {
    std::size_t operator()(const epix::shader::ShaderDefVal& def) const noexcept {
        std::size_t h  = std::hash<std::string>{}(def.name);
        std::size_t vh = std::visit([]<typename T>(const T& v) { return std::hash<T>{}(v); }, def.value);
        return h ^ (vh + 0x9e3779b9 + (h << 6) + (h >> 2));
    }
};

template <>
struct std::hash<std::vector<epix::shader::ShaderDefVal>> {
    std::size_t operator()(const std::vector<epix::shader::ShaderDefVal>& defs) const noexcept {
        std::size_t h = 0;
        for (const auto& d : defs) h ^= std::hash<epix::shader::ShaderDefVal>{}(d) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

template <>
struct std::hash<epix::shader::ShaderImport> {
    std::size_t operator()(const epix::shader::ShaderImport& import) const noexcept {
        return std::visit(
            []<typename T>(const T& v) -> std::size_t {
                if constexpr (std::same_as<T, epix::assets::AssetPath>) {
                    return std::hash<epix::assets::AssetPath>{}(v);
                } else {
                    return std::hash<std::filesystem::path>{}(v) ^ std::size_t(0x1'0000);
                }
            },
            import.data);
    }
};

template <>
struct std::hash<epix::assets::AssetId<epix::shader::Shader>> {
    std::size_t operator()(const epix::assets::AssetId<epix::shader::Shader>& id) const noexcept {
        return std::visit([]<typename T>(const T& index) { return std::hash<T>{}(index); }, id);
    }
};

template <>
struct std::formatter<epix::shader::ShaderImport> : std::formatter<std::string_view> {
    template <typename FormatContext>
    auto format(const epix::shader::ShaderImport& import, FormatContext& ctx) const {
        const auto module_name = import.module_name();
        return std::formatter<std::string_view>::format(module_name, ctx);
    }
};
