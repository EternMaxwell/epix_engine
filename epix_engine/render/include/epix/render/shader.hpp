#pragma once

#include <epix/core.hpp>

#include "assets.hpp"
#include "vulkan.hpp"

namespace epix::render {
/**
 * @brief A cpu side shader representation.
 *
 * Only contains the shader code, not the compiled shader.
 *
 */
struct Shader {
   private:
    enum class Type {
        HLSL,
        GLSL,
        SPIRV,
    } type;
    union {
        std::vector<uint32_t> bytes;
        std::string str;
    };

    Shader() {};

   public:
    Shader(const Shader& other);
    Shader(Shader&& other);
    Shader& operator=(const Shader& other);
    Shader& operator=(Shader&& other);
    ~Shader();

    bool is_spirv() const { return type == Type::SPIRV; }
    bool is_hlsl() const { return type == Type::HLSL; }
    bool is_glsl() const { return type == Type::GLSL; }

    std::string to_string() const;

    static Shader hlsl(std::string code);
    static Shader glsl(std::string code);
    static Shader spirv(std::vector<uint32_t> code);

    nvrhi::ShaderHandle create_shader(nvrhi::DeviceHandle device,
                                      nvrhi::ShaderType type,
                                      const std::string& entry,
                                      const std::string& debug_name = "") const;
};

struct ShaderLoaderGLSL {
    static auto extensions() { return std::views::all(exts); }
    static Shader load(const std::filesystem::path& path, epix::assets::LoadContext& context);
    static constexpr std::array<const char*, 4> exts = {"glsl", "vert", "frag", "geom"};
};
struct ShaderLoaderHLSL {
    static auto extensions() { return std::views::all(exts); }
    static Shader load(const std::filesystem::path& path, epix::assets::LoadContext& context);
    static constexpr std::array<const char*, 2> exts = {"hlsl", "dxil"};
};
struct ShaderLoaderSPIRV {
    static auto extensions() { return std::views::all(exts); }
    static Shader load(const std::filesystem::path& path, epix::assets::LoadContext& context);
    static constexpr std::array<const char*, 1> exts = {"spv"};
};

template <>
struct epix::render::assets::RenderAsset<Shader> {
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

static_assert(epix::core::system::valid_system_param<core::system::SystemParam<assets::RenderAsset<Shader>::Param>>,
              "RenderAsset<Shader>::Param should be a valid parameter type");

struct ShaderPlugin {
    void build(epix::App& app);
};
}  // namespace epix::render