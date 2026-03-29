module;

module epix.render;

import epix.meta;

import :shader;

using namespace epix::render;

std::span<std::string_view> ShaderLoaderWGSL::extensions() {
    static auto exts = std::array{std::string_view{"wgsl"}};
    return std::span<std::string_view>(exts.data(), exts.size());
}
std::expected<Shader, ShaderLoaderWGSL::Error> ShaderLoaderWGSL::load(std::istream& reader,
                                                                      const Settings&,
                                                                      assets::LoadContext& context) {
    try {
        std::stringstream buffer;
        buffer << reader.rdbuf();
        auto& path = context.path().path;
        return Shader{path, path.string(), ShaderSource::wgsl(buffer.str())};
    } catch (...) {
        return std::unexpected(std::current_exception());
    }
}

std::span<std::string_view> ShaderLoaderSPIRV::extensions() {
    static auto exts = std::array{std::string_view{"spv"}};
    return std::span<std::string_view>(exts.data(), exts.size());
}
std::expected<Shader, ShaderLoaderSPIRV::Error> ShaderLoaderSPIRV::load(std::istream& reader,
                                                                        const Settings&,
                                                                        assets::LoadContext& context) {
    try {
        std::vector<char> bytes =
            std::ranges::subrange(std::istreambuf_iterator<char>(reader), std::istreambuf_iterator<char>()) |
            std::ranges::to<std::vector<char>>();
        std::vector<uint32_t> code(bytes.size() / sizeof(uint32_t));
        std::memcpy(code.data(), bytes.data(), code.size() * sizeof(uint32_t));
        auto& path = context.path().path;
        return Shader{path, path.string(), ShaderSource::spirv(std::move(code))};
    } catch (...) {
        return std::unexpected(std::current_exception());
    }
}

void ShaderPlugin::build(App& app) {
    assets::app_register_asset<Shader>(app);
    assets::app_register_loader<ShaderLoaderWGSL>(app);
    assets::app_register_loader<ShaderLoaderSPIRV>(app);
}
