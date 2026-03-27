export module epix.render:shader;

import epix.assets;
import epix.core;
import webgpu;
import std;

import :assets;

namespace render {
/**
 * @brief A cpu side shader representation.
 *
 * Only contains the shader code, not the compiled shader.
 */
export struct ShaderSource {
   private:
    std::variant<std::vector<std::uint32_t>, std::string> code;

   public:
    bool is_spirv() const { return std::holds_alternative<std::vector<std::uint32_t>>(code); }
    bool is_wgsl() const { return std::holds_alternative<std::string>(code); }

    std::optional<std::span<const std::uint32_t>> get_spirv() const {
        if (is_spirv()) {
            const auto& spirv_code = std::get<std::vector<std::uint32_t>>(code);
            return std::span<const std::uint32_t>(spirv_code.data(), spirv_code.size());
        }
        return std::nullopt;
    }
    std::optional<std::string_view> get_wgsl() const {
        if (is_wgsl()) {
            const auto& wgsl_code = std::get<std::string>(code);
            return std::string_view(wgsl_code);
        }
        return std::nullopt;
    }

    static ShaderSource wgsl(std::string code) {
        ShaderSource shader;
        shader.code = std::move(code);
        return shader;
    }
    static ShaderSource spirv(std::vector<std::uint32_t> code) {
        ShaderSource shader;
        shader.code = std::move(code);
        return shader;
    }
};
/** @brief Loaded shader asset containing its file path, label, and
 * source code. */
export struct Shader {
    /** @brief File system path of the shader source file. */
    std::filesystem::path path;
    /** @brief Human-readable label for debugging. */
    std::string label;
    /** @brief The shader source code (WGSL or SPIR-V). */
    ShaderSource source;
};
/** @brief Asset loader for WGSL shader files. */
export struct ShaderLoaderWGSL {
    using Asset = Shader;
    struct Settings : assets::Settings {};
    using Error = std::exception_ptr;

    static std::span<std::string_view> extensions();
    static std::expected<Shader, Error> load(std::istream& reader,
                                             const Settings& settings,
                                             assets::LoadContext& context);
};
/** @brief Asset loader for SPIR-V shader files. */
export struct ShaderLoaderSPIRV {
    using Asset = Shader;
    struct Settings : assets::Settings {};
    using Error = std::exception_ptr;

    static std::span<std::string_view> extensions();
    static std::expected<Shader, Error> load(std::istream& reader,
                                             const Settings& settings,
                                             assets::LoadContext& context);
};

/** @brief Plugin that registers shader asset loaders and the shader
 * cache. */
export struct ShaderPlugin {
    void build(App& app);
};
}  // namespace render

template <>
struct std::hash<assets::AssetId<render::Shader>> {
    std::size_t operator()(const assets::AssetId<render::Shader>& id) const noexcept {
        return std::visit([]<typename T>(const T& index) { return std::hash<T>()(index); }, id);
    }
};