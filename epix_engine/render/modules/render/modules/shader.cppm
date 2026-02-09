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
export struct Shader {
    std::filesystem::path path;
    ShaderSource source;
    std::vector<wgpu::ConstantEntry> constant_entries;

    void add_constant_entry(std::string_view key, double value) {
        constant_entries.push_back(wgpu::ConstantEntry().setKey(key).setValue(value));
    }
};
export struct ShaderLoaderWGSL {
    static std::span<const char* const> extensions();
    static Shader load(const std::filesystem::path& path, assets::LoadContext& context);
};
export struct ShaderLoaderSPIRV {
    static std::span<const char* const> extensions();
    static Shader load(const std::filesystem::path& path, assets::LoadContext& context);
};

template <>
struct render::RenderAsset<Shader> {
    // nvrhi's shader module is created with its type specified, unlike
    // vulkan. So we won't do anything on the asset, just pass it through.
    using Param          = ParamSet<>;
    using ProcessedAsset = Shader;

    ProcessedAsset process(Shader&& asset, Param param) {
        // Process the shader asset with the given parameters
        return asset;
    }
    RenderAssetUsage usage(const Shader& asset) const {
        // Shaders are always used in the render world.
        return RenderAssetUsageBits::RENDER_WORLD;
    }
};

static_assert(core::system_param<RenderAsset<Shader>::Param>,
              "RenderAsset<Shader>::Param should be a valid parameter type");

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