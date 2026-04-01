module;

#include <spdlog/spdlog.h>

module epix.shader;

import epix.meta;

import :shader;

using namespace epix::shader;

// ─── Shader::preprocess ────────────────────────────────────────────────────
// Scans WGSL source for:
//   #define_import_path <name>  → import_path = Custom(name)
//   #import "path"              → AssetPath import
//   #import name                → Custom import
std::pair<ShaderImport, std::vector<ShaderImport>> Shader::preprocess(std::string_view source, std::string_view path) {
    std::optional<ShaderImport> import_path;
    std::vector<ShaderImport> imports;

    std::size_t pos = 0;
    while (pos < source.size()) {
        std::size_t nl        = source.find('\n', pos);
        std::string_view line = (nl == std::string_view::npos) ? source.substr(pos) : source.substr(pos, nl - pos);
        pos                   = (nl == std::string_view::npos) ? source.size() : nl + 1;

        // Trim leading whitespace
        std::size_t ws = line.find_first_not_of(" \t\r");
        if (ws == std::string_view::npos) continue;
        std::string_view t = line.substr(ws);

        if (!t.starts_with('#')) continue;

        // Skip the '#'
        t                          = t.substr(1);
        std::size_t sp             = t.find_first_of(" \t");
        std::string_view directive = (sp == std::string_view::npos) ? t : t.substr(0, sp);
        std::string_view rest      = (sp == std::string_view::npos) ? std::string_view{} : t.substr(sp + 1);
        // trim rest
        std::size_t rs = rest.find_first_not_of(" \t");
        rest           = (rs == std::string_view::npos) ? std::string_view{} : rest.substr(rs);

        if (directive == "define_import_path" && !rest.empty()) {
            // e.g. #define_import_path some::module::name
            import_path = ShaderImport::custom(std::string(rest));
        } else if (directive == "import" && !rest.empty()) {
            if (rest.starts_with('"')) {
                // #import "asset/path.wgsl"
                auto end_q    = rest.find('"', 1);
                std::string p = end_q == std::string_view::npos ? std::string(rest.substr(1))
                                                                : std::string(rest.substr(1, end_q - 1));
                imports.emplace_back(ShaderImport::asset_path(std::move(p)));
            } else {
                // #import some::module::name  (possibly with "as alias")
                std::size_t as_pos = rest.find(" as ");
                std::string name   = std::string(as_pos == std::string_view::npos ? rest : rest.substr(0, as_pos));
                // trim trailing whitespace
                while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
                imports.emplace_back(ShaderImport::custom(std::move(name)));
            }
        }
    }

    ShaderImport ip = import_path.value_or(ShaderImport::asset_path(std::string(path)));
    return {std::move(ip), std::move(imports)};
}

// ─── Shader constructors ───────────────────────────────────────────────────
Shader Shader::from_wgsl(std::string source, std::string path) {
    auto [ip, imps] = Shader::preprocess(source, path);
    Shader s;
    s.path        = std::move(path);
    s.source      = Source::wgsl(std::move(source));
    s.import_path = std::move(ip);
    s.imports     = std::move(imps);
    return s;
}

Shader Shader::from_wgsl_with_defs(std::string source, std::string path, std::vector<ShaderDefVal> shader_defs) {
    auto s        = Shader::from_wgsl(std::move(source), std::move(path));
    s.shader_defs = std::move(shader_defs);
    return s;
}

Shader Shader::from_spirv(std::vector<std::uint8_t> source, std::string path) {
    Shader s;
    s.path        = path;
    s.source      = Source::spirv(std::move(source));
    s.import_path = ShaderImport::asset_path(std::move(path));
    // imports is empty (no text to parse)
    return s;
}

// ─── ShaderLoader ──────────────────────────────────────────────────────────
std::span<std::string_view> ShaderLoader::extensions() {
    static std::array exts = {std::string_view{"wgsl"}, std::string_view{"spv"}};
    return exts;
}

std::expected<Shader, ShaderLoaderError> ShaderLoader::load(std::istream& reader,
                                                            const Settings& settings,
                                                            assets::LoadContext& context) {
    const auto& raw_path = context.path().path;
    // Normalize to forward slashes
    std::string path_str = raw_path.string();
    std::ranges::replace(path_str, '\\', '/');

    spdlog::trace("[shader] Loading shader from '{}'.", path_str);

    // Read all bytes
    std::vector<char> bytes =
        std::ranges::subrange(std::istreambuf_iterator<char>(reader), std::istreambuf_iterator<char>()) |
        std::ranges::to<std::vector<char>>();

    if (reader.fail() && !reader.eof()) {
        return std::unexpected(ShaderLoaderError::io(std::make_error_code(std::io_errc::stream), raw_path));
    }

    auto ext = raw_path.extension().string();
    // strip leading dot
    if (!ext.empty() && ext.front() == '.') ext = ext.substr(1);

    if (ext == "spv") {
        if (bytes.size() % sizeof(std::uint8_t) != 0) {
            return std::unexpected(ShaderLoaderError::parse(raw_path, 0));
        }
        std::vector<std::uint8_t> code(bytes.begin(), bytes.end());
        return Shader::from_spirv(std::move(code), path_str);
    }

    if (ext == "wgsl") {
        if (!settings.shader_defs.empty()) {
            spdlog::debug("[shader] Applying {} shader_defs to '{}'.", settings.shader_defs.size(), path_str);
        }
        // Validate UTF-8
        for (std::size_t i = 0; i < bytes.size();) {
            unsigned char c = static_cast<unsigned char>(bytes[i]);
            int seq         = 0;
            if (c <= 0x7F) {
                seq = 1;
            } else if ((c & 0xE0) == 0xC0) {
                seq = 2;
            } else if ((c & 0xF0) == 0xE0) {
                seq = 3;
            } else if ((c & 0xF8) == 0xF0) {
                seq = 4;
            } else {
                return std::unexpected(ShaderLoaderError::parse(raw_path, i));
            }
            for (int j = 1; j < seq; ++j) {
                if (i + j >= bytes.size() || (static_cast<unsigned char>(bytes[i + j]) & 0xC0) != 0x80)
                    return std::unexpected(ShaderLoaderError::parse(raw_path, i));
            }
            i += seq;
        }
        std::string text(bytes.begin(), bytes.end());
        return Shader::from_wgsl_with_defs(std::move(text), path_str, settings.shader_defs);
    }

    return std::unexpected(ShaderLoaderError::parse(raw_path, 0));
}

// ─── ShaderPlugin ──────────────────────────────────────────────────────────
void ShaderPlugin::build(core::App& app) {
    spdlog::debug("[shader] Building ShaderPlugin, registering shader asset loader.");
    assets::app_register_asset<Shader>(app);
    assets::app_register_loader<ShaderLoader>(app);
}
