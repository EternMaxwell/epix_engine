#include <fstream>

#include "epix/render/shader.hpp"

namespace epix::render {

Shader::Shader(const Shader& other) : type(other.type) {
    if (type == Type::SPIRV) {
        new (&bytes) std::vector<uint32_t>(other.bytes);
    } else {
        new (&str) std::string(other.str);
    }
}

Shader::Shader(Shader&& other) : type(other.type) {
    if (type == Type::SPIRV) {
        new (&bytes) std::vector<uint32_t>(std::move(other.bytes));
    } else {
        new (&str) std::string(std::move(other.str));
    }
}

Shader& Shader::operator=(const Shader& other) {
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

Shader& Shader::operator=(Shader&& other) {
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

Shader::~Shader() {
    if (type == Type::SPIRV) {
        bytes.~vector();
    } else {
        str.~basic_string();
    }
}

std::string Shader::to_string() const {
    if (type == Type::SPIRV) {
        return std::format("SPIR-V code ({} bytes):\n<binary>", bytes.size() * sizeof(uint32_t));
    } else {
        return std::format("Shader code ({}):\n{}", type == Type::HLSL ? "HLSL" : "GLSL", str);
    }
}

nvrhi::ShaderHandle Shader::create_shader(nvrhi::DeviceHandle device,
                                          nvrhi::ShaderType type,
                                          const std::string& entry,
                                          const std::string& debug_name) const {
    if (this->type == Type::SPIRV) {
        return device->createShader(
            nvrhi::ShaderDesc().setShaderType(type).setEntryName(entry).setDebugName(debug_name), bytes.data(),
            bytes.size() * sizeof(uint32_t));
    } else {
        return device->createShader(
            nvrhi::ShaderDesc().setShaderType(type).setEntryName(entry).setDebugName(debug_name), str.data(),
            str.size());
    }
}

Shader ShaderLoaderGLSL::load(const std::filesystem::path& path, epix::assets::LoadContext& context) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + path.string());
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return Shader::glsl(buffer.str());
}

Shader ShaderLoaderHLSL::load(const std::filesystem::path& path, epix::assets::LoadContext& context) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + path.string());
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return Shader::hlsl(buffer.str());
}

Shader ShaderLoaderSPIRV::load(const std::filesystem::path& path, epix::assets::LoadContext& context) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + path.string());
    }
    std::vector<uint32_t> code;
    file.seekg(0, std::ios::end);
    std::streamoff size = file.tellg();
    code.resize(static_cast<size_t>(size) / sizeof(uint32_t));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(code.data()), size);
    return Shader::spirv(std::move(code));
}

void ShaderPlugin::build(epix::App& app) {
    if (auto asset_plugin = app.get_plugin_mut<epix::assets::AssetPlugin>()) {
        asset_plugin->get()
            .register_asset<Shader>()
            .register_loader<ShaderLoaderGLSL>()
            .register_loader<ShaderLoaderHLSL>()
            .register_loader<ShaderLoaderSPIRV>();
    }
    app.add_plugins(assets::ExtractAssetPlugin<Shader>{});
}

Shader Shader::hlsl(std::string code) {
    Shader res;
    res.type = Type::HLSL;
    new (&res.str) std::string(std::move(code));
    return res;
}

Shader Shader::glsl(std::string code) {
    Shader res;
    res.type = Type::GLSL;
    new (&res.str) std::string(std::move(code));
    return res;
}

Shader Shader::spirv(std::vector<uint32_t> code) {
    Shader res;
    res.type = Type::SPIRV;
    new (&res.bytes) std::vector<uint32_t>(std::move(code));
    return res;
}

}  // namespace epix::render