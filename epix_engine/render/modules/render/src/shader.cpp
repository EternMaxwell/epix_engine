module;

module epix.render;

import epix.meta;

import :shader;

using namespace render;

std::span<const char* const> ShaderLoaderWGSL::extensions() {
    static constexpr auto exts = std::array{"wgsl"};
    return std::span(exts);
}
Shader ShaderLoaderWGSL::load(const std::filesystem::path& path, assets::LoadContext& context) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + path.string());
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return Shader{path, ShaderSource::wgsl(buffer.str()), {}};
}

std::span<const char* const> ShaderLoaderSPIRV::extensions() {
    static constexpr auto exts = std::array{"spv"};
    return std::span(exts);
}
Shader ShaderLoaderSPIRV::load(const std::filesystem::path& path, assets::LoadContext& context) {
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
    return Shader{path, ShaderSource::spirv(std::move(code)), {}};
}

void ShaderPlugin::build(App& app) {
    app.plugin_scope([](assets::AssetPlugin& asset_plugin) {
           asset_plugin.register_asset<Shader>()
               .register_loader<ShaderLoaderWGSL>()
               .register_loader<ShaderLoaderSPIRV>();
       })
        .add_plugins(ExtractAssetPlugin<Shader>{});
}