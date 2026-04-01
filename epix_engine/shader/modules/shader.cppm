export module epix.shader:shader;

import epix.assets;
import epix.core;
import webgpu;
import std;

namespace epix::shader {

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

    std::variant<Wgsl, SpirV> data;

    static Source wgsl(std::string code) { return {Wgsl{std::move(code)}}; }
    static Source spirv(std::vector<std::uint8_t> bytes) { return {SpirV{std::move(bytes)}}; }

    bool is_wgsl() const { return std::holds_alternative<Wgsl>(data); }
    bool is_spirv() const { return std::holds_alternative<SpirV>(data); }

    // Asserts / UB for SpirV.
    std::string_view as_str() const { return std::get<Wgsl>(data).code; }
};

// ─── ShaderImport ──────────────────────────────────────────────────────────
export struct ShaderImport {
    enum class Kind { AssetPath, Custom };
    Kind kind;
    std::string path;

    static ShaderImport asset_path(std::string p) { return {Kind::AssetPath, std::move(p)}; }
    static ShaderImport custom(std::string name) { return {Kind::Custom, std::move(name)}; }

    // Returns the canonical module name used by ShaderComposer:
    //   AssetPath → '"' + path + '"'
    //   Custom    → path verbatim
    std::string module_name() const {
        if (kind == Kind::AssetPath) return '"' + path + '"';
        return path;
    }

    bool operator==(const ShaderImport&) const = default;
};

// ─── Forward-declare Shader so Handle<Shader> can be used in fields ────────
export struct Shader;

// ─── Shader ────────────────────────────────────────────────────────────────
export struct Shader {
    std::string path;
    Source source;
    ShaderImport import_path = ShaderImport::asset_path({});
    std::vector<ShaderImport> imports;
    std::vector<ShaderDefVal> shader_defs;
    std::vector<assets::Handle<Shader>> file_dependencies;
    ValidateShader validate_shader = ValidateShader::Disabled;

    // Parses WGSL source to extract #define_import_path and #import directives.
    static std::pair<ShaderImport, std::vector<ShaderImport>> preprocess(std::string_view source,
                                                                         std::string_view path);

    static Shader from_wgsl(std::string source, std::string path);
    static Shader from_wgsl_with_defs(std::string source, std::string path, std::vector<ShaderDefVal> shader_defs);
    static Shader from_spirv(std::vector<std::uint8_t> source, std::string path);
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
        return std::hash<std::string>{}(import.path) ^ static_cast<std::size_t>(import.kind);
    }
};

template <>
struct std::hash<epix::assets::AssetId<epix::shader::Shader>> {
    std::size_t operator()(const epix::assets::AssetId<epix::shader::Shader>& id) const noexcept {
        return std::visit([]<typename T>(const T& index) { return std::hash<T>{}(index); }, id);
    }
};
