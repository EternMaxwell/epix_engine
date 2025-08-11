#pragma once

#include <epix/app.h>
#include <epix/vulkan.h>

#include <fstream>

#include "assets.h"
#include "common.h"

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
    Shader(const Shader& other) : type(other.type) {
        if (type == Type::SPIRV) {
            new (&bytes) std::vector<uint32_t>(other.bytes);
        } else {
            new (&str) std::string(other.str);
        }
    }
    Shader(Shader&& other) : type(other.type) {
        if (type == Type::SPIRV) {
            new (&bytes) std::vector<uint32_t>(std::move(other.bytes));
        } else {
            new (&str) std::string(std::move(other.str));
        }
    }
    Shader& operator=(const Shader& other) {
        if (this != &other) {
            if (type == Type::SPIRV) {
                bytes.~vector();
            } else {
                str.~basic_string();
            }
            type = other.type;
            if (type == Type::SPIRV) {
                new (&bytes) std::vector<uint32_t>(other.bytes);
            } else {
                new (&str) std::string(other.str);
            }
        }
        return *this;
    }
    Shader& operator=(Shader&& other) {
        if (this != &other) {
            if (type == Type::SPIRV) {
                bytes.~vector();
            } else {
                str.~basic_string();
            }
            type = other.type;
            if (type == Type::SPIRV) {
                new (&bytes) std::vector<uint32_t>(std::move(other.bytes));
            } else {
                new (&str) std::string(std::move(other.str));
            }
        }
        return *this;
    }
    ~Shader() {
        if (type == Type::SPIRV) {
            bytes.~vector();
        } else {
            str.~basic_string();
        }
    }

    bool is_spirv() const { return type == Type::SPIRV; }
    bool is_hlsl() const { return type == Type::HLSL; }
    bool is_glsl() const { return type == Type::GLSL; }

    std::string view_code() const {
        if (type == Type::SPIRV) {
            return std::format("SPIR-V code ({} bytes):\n{}",
                               bytes.size() * sizeof(uint32_t),
                               std::views::all(bytes));
        } else {
            return std::format("Shader code ({}):\n{}",
                               type == Type::HLSL ? "HLSL" : "GLSL", str);
        }
    }

    static Shader hlsl(std::string code) {
        Shader res;
        res.type = Type::HLSL;
        new (&res.str) std::string(std::move(code));
        return res;
    }
    static Shader glsl(std::string code) {
        Shader res;
        res.type = Type::GLSL;
        new (&res.str) std::string(std::move(code));
        return res;
    }
    static Shader spirv(std::vector<uint32_t> code) {
        Shader res;
        res.type = Type::SPIRV;
        new (&res.bytes) std::vector<uint32_t>(std::move(code));
        return res;
    }

    nvrhi::ShaderHandle create_shader(
        nvrhi::DeviceHandle device,
        nvrhi::ShaderType type,
        const std::string& entry,
        const std::string& debug_name = "") const {
        if (this->type == Type::SPIRV) {
            return device->createShader(nvrhi::ShaderDesc()
                                            .setShaderType(type)
                                            .setEntryName(entry)
                                            .setDebugName(debug_name),
                                        bytes.data(),
                                        bytes.size() * sizeof(uint32_t));
        } else {
            return device->createShader(nvrhi::ShaderDesc()
                                            .setShaderType(type)
                                            .setEntryName(entry)
                                            .setDebugName(debug_name),
                                        str.data(), str.size());
        }
    }
};

struct ShaderLoaderGLSL {
    static auto extensions() { return std::views::all(exts); }
    static Shader load(const std::filesystem::path& path,
                       epix::assets::LoadContext& context) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " +
                                     path.string());
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return Shader::glsl(buffer.str());
    }
    static constexpr std::array<const char*, 4> exts = {"glsl", "vert", "frag",
                                                        "geom"};
};
struct ShaderLoaderHLSL {
    static auto extensions() { return std::views::all(exts); }
    static Shader load(const std::filesystem::path& path,
                       epix::assets::LoadContext& context) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " +
                                     path.string());
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return Shader::hlsl(buffer.str());
    }
    static constexpr std::array<const char*, 2> exts = {"hlsl", "dxil"};
};
struct ShaderLoaderSPIRV {
    static auto extensions() { return std::views::all(exts); }
    static Shader load(const std::filesystem::path& path,
                       epix::assets::LoadContext& context) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " +
                                     path.string());
        }

        std::vector<uint32_t> code;
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        code.resize(size / sizeof(uint32_t));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(code.data()), size);
        return Shader::spirv(std::move(code));
    }
    static constexpr std::array<const char*, 1> exts = {"spv"};
};

template <>
struct epix::render::assets::RenderAsset<Shader> {
    // nvrhi's shader module is created with its type specified, unlike
    // vulkan. So we won't do anything on the asset, just pass it through.
    using Param          = ParamSet<>;
    using ProcessedAsset = Shader;

    ProcessedAsset process(Shader&& asset, Param& param) {
        // Process the shader asset with the given parameters
        return asset;
    }
    RenderAssetUsage usage(const Shader& asset) const {
        // Shaders are always used in the render world.
        return RenderAssetUsageBits::RENDER_WORLD;
    }
};

static_assert(epix::app::ValidParam<assets::RenderAsset<Shader>::Param>,
              "RenderAsset<Shader>::Param should be a valid parameter type");

struct ShaderPlugin {
    void build(epix::App& app) {
        if (auto asset_plugin = app.get_plugin<epix::assets::AssetPlugin>()) {
            asset_plugin->register_asset<Shader>()
                .register_loader<ShaderLoaderGLSL>()
                .register_loader<ShaderLoaderHLSL>()
                .register_loader<ShaderLoaderSPIRV>();
        }
        app.add_plugins(assets::ExtractAssetPlugin<Shader>{});
    }
};
}  // namespace epix::render