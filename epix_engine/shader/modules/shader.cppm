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

// ─── ShaderId ──────────────────────────────────────────────────────────────
export struct ShaderId {
   private:
    inline static std::atomic<std::uint32_t> s_counter{0};
    std::uint32_t value;
    explicit ShaderId(std::uint32_t v) : value(v) {}

   public:
    static ShaderId next() { return ShaderId{s_counter.fetch_add(1, std::memory_order_relaxed)}; }
    std::uint32_t get() const { return value; }
    auto operator<=>(const ShaderId&) const = default;
};

// ─── ShaderDefVal ──────────────────────────────────────────────────────────
export struct ShaderDefVal {
    std::string name;
    std::variant<bool, std::int32_t, std::uint32_t> value;

    explicit ShaderDefVal(std::string n) : name(std::move(n)), value(true) {}

    static ShaderDefVal from_bool(std::string n, bool v = true) {
        ShaderDefVal d(std::move(n));
        d.value = v;
        return d;
    }
    static ShaderDefVal from_int(std::string n, std::int32_t v) {
        ShaderDefVal d(std::move(n));
        d.value = v;
        return d;
    }
    static ShaderDefVal from_uint(std::string n, std::uint32_t v) {
        ShaderDefVal d(std::move(n));
        d.value = v;
        return d;
    }

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

// ─── ValidateShader ────────────────────────────────────────────────────────
export enum class ValidateShader : std::uint8_t {
    Disabled = 0,
    Enabled  = 1,
};

// ─── Source ────────────────────────────────────────────────────────────────
export struct Source {
    struct Wgsl {
        std::string code;
    };
    struct SpirV {
        std::vector<std::uint8_t> bytes;
    };
    struct Slang {
        std::string code;
    };

    std::variant<Wgsl, SpirV, Slang> data;

    static Source wgsl(std::string code) { return {Wgsl{std::move(code)}}; }
    static Source spirv(std::vector<std::uint8_t> bytes) { return {SpirV{std::move(bytes)}}; }
    static Source slang(std::string code) { return {Slang{std::move(code)}}; }

    bool is_wgsl() const { return std::holds_alternative<Wgsl>(data); }
    bool is_spirv() const { return std::holds_alternative<SpirV>(data); }
    bool is_slang() const { return std::holds_alternative<Slang>(data); }

    // Asserts / UB for SpirV.
    std::string_view as_str() const {
        if (auto* w = std::get_if<Wgsl>(&data)) return w->code;
        if (auto* s = std::get_if<Slang>(&data)) return s->code;
        return std::get<Wgsl>(data).code;  // UB fallback
    }
};

// ─── ShaderImport ──────────────────────────────────────────────────────────
// Mirrors Bevy's ShaderImport: either a full AssetPath (with optional source)
// or a custom module name (e.g. "my::utils" from #define_import_path / Slang custom).
export struct ShaderImport {
    // AssetPath variant: a specific file, may carry a source:// origin.
    // Custom    variant: a named module string (e.g. "my::module").
    std::variant<assets::AssetPath, std::string> data;

    ShaderImport() = default;
    explicit ShaderImport(assets::AssetPath p) : data(std::move(p)) {}
    explicit ShaderImport(std::in_place_index_t<1>, std::string name) : data(std::in_place_index<1>, std::move(name)) {}

    static ShaderImport asset_path(assets::AssetPath p) { return ShaderImport(std::move(p)); }
    static ShaderImport custom(std::string name) { return ShaderImport(std::in_place_index<1>, std::move(name)); }

    bool is_asset_path() const { return std::holds_alternative<assets::AssetPath>(data); }
    bool is_custom() const { return std::holds_alternative<std::string>(data); }

    const assets::AssetPath& as_asset_path() const { return std::get<assets::AssetPath>(data); }
    const std::string& as_custom() const { return std::get<std::string>(data); }

    // Returns the canonical module name used by ShaderComposer.
    //   AssetPath → '"' + AssetPath::string() + '"'
    //   Custom    → name verbatim
    std::string module_name() const {
        if (is_asset_path()) return '"' + canonical_asset_path_string(as_asset_path()) + '"';
        return as_custom();
    }

    bool operator==(const ShaderImport&) const = default;
};

// ─── Forward-declare Shader so Handle<Shader> can be used in fields ────────
export struct Shader;

// ─── Shader ────────────────────────────────────────────────────────────────
export struct Shader {
    assets::AssetPath path;
    Source source;
    ShaderImport import_path;  // default is path, e.g. asset_path(path)
    std::vector<ShaderImport> imports;
    std::vector<ShaderDefVal> shader_defs;
    std::vector<assets::Handle<Shader>> file_dependencies;
    ValidateShader validate_shader = ValidateShader::Disabled;

    // Parses WGSL source to extract #define_import_path and #import directives.
    static std::pair<ShaderImport, std::vector<ShaderImport>> preprocess(std::string_view source,
                                                                         const assets::AssetPath& path);

    // Parses Slang source to extract `import X;` statements.
    static std::pair<ShaderImport, std::vector<ShaderImport>> preprocess_slang(std::string_view source,
                                                                               const assets::AssetPath& path);

    static Shader from_wgsl(std::string source, assets::AssetPath path);
    static Shader from_wgsl_with_defs(std::string source,
                                      assets::AssetPath path,
                                      std::vector<ShaderDefVal> shader_defs);
    static Shader from_spirv(std::vector<std::uint8_t> source, assets::AssetPath path);
    static Shader from_slang(std::string source, assets::AssetPath path);
    static Shader from_slang_with_defs(std::string source,
                                       assets::AssetPath path,
                                       std::vector<ShaderDefVal> shader_defs);
};

// ─── ShaderSettings ────────────────────────────────────────────────────────
export struct ShaderSettings : assets::Settings {
    std::vector<ShaderDefVal> shader_defs;
};

// ─── ShaderLoaderError ─────────────────────────────────────────────────────
export struct ShaderLoaderError {
    struct Io {
        std::error_code code;
        std::filesystem::path path;
    };
    struct Parse {
        std::filesystem::path path;
        std::size_t byte_offset;
    };
    std::variant<Io, Parse> data;

    static ShaderLoaderError io(std::error_code code, std::filesystem::path p) { return {Io{code, std::move(p)}}; }
    static ShaderLoaderError parse(std::filesystem::path p, std::size_t offset = 0) {
        return {Parse{std::move(p), offset}};
    }
};

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

// ─── ShaderLoader ──────────────────────────────────────────────────────────
export struct ShaderLoader {
    using Asset    = Shader;
    using Settings = ShaderSettings;
    using Error    = ShaderLoaderError;

    static std::span<std::string_view> extensions();
    static std::expected<Shader, Error> load(std::istream& reader,
                                             const Settings& settings,
                                             assets::LoadContext& context);
};

// ─── ShaderProcessor ───────────────────────────────────────────────────────
export struct ShaderProcessorSettings : assets::Settings {
    ShaderSettings loader_settings;
    bool preprocess_wgsl  = true;
    bool preprocess_slang = true;
};

export struct ShaderProcessor {
    using Settings     = ShaderProcessorSettings;
    using OutputLoader = ShaderLoader;

    std::expected<OutputLoader::Settings, std::exception_ptr> process(assets::ProcessContext& context,
                                                                      const Settings& settings,
                                                                      std::ostream& writer) const;
};

// ─── ShaderRef ─────────────────────────────────────────────────────────────
export struct ShaderRef {
    struct Default {};
    struct ByHandle {
        assets::Handle<Shader> handle;
    };
    struct ByPath {
        std::filesystem::path path;
    };

    std::variant<Default, ByHandle, ByPath> value;

    ShaderRef() : value(Default{}) {}
    ShaderRef(ByHandle h) : value(std::move(h)) {}
    ShaderRef(ByPath p) : value(std::move(p)) {}

    static ShaderRef from_handle(assets::Handle<Shader> h) { return ShaderRef{ByHandle{std::move(h)}}; }
    static ShaderRef from_path(std::filesystem::path p) { return ShaderRef{ByPath{std::move(p)}}; }
    static ShaderRef from_str(std::string_view s) { return ShaderRef{ByPath{std::filesystem::path{s}}}; }

    bool is_default() const { return std::holds_alternative<Default>(value); }
    bool is_handle() const { return std::holds_alternative<ByHandle>(value); }
    bool is_path() const { return std::holds_alternative<ByPath>(value); }
};

// ─── ShaderPlugin ──────────────────────────────────────────────────────────
export struct ShaderPlugin {
    void build(core::App& app);
};

}  // namespace epix::shader

// ─── std::hash specializations ─────────────────────────────────────────────
template <>
struct std::hash<epix::shader::ShaderId> {
    std::size_t operator()(const epix::shader::ShaderId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.get());
    }
};

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
                    return std::hash<std::string>{}(v) ^ std::size_t(0x1'0000);
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
