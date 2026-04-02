module;

#include <spdlog/spdlog.h>

module epix.shader;

import epix.meta;

import :shader;

using namespace epix::shader;

namespace {

constexpr std::array<std::uint8_t, 8> k_processed_shader_magic = {'E', 'P', 'S', 'H', 'P', 'R', '0', '1'};
constexpr std::uint32_t k_processed_shader_version             = 1;

enum class ProcessedSourceKind : std::uint8_t {
    Wgsl  = 1,
    SpirV = 2,
    Slang = 3,
};

enum class ProcessedImportKind : std::uint8_t {
    AssetPath = 1,
    Custom    = 2,
};

struct ProcessedDecodedShader {
    Source source;
    ShaderImport import_path;
    std::vector<ShaderImport> imports;
};

bool validate_utf8_bytes(const std::vector<char>& bytes, std::size_t& invalid_offset) {
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
            invalid_offset = i;
            return false;
        }
        for (int j = 1; j < seq; ++j) {
            if (i + j >= bytes.size() || (static_cast<unsigned char>(bytes[i + j]) & 0xC0) != 0x80) {
                invalid_offset = i;
                return false;
            }
        }
        i += seq;
    }
    return true;
}

void write_u8(std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }

void write_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 0) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

void write_string(std::vector<std::uint8_t>& out, std::string_view s) {
    write_u32(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

void write_import(std::vector<std::uint8_t>& out, const ShaderImport& imp) {
    if (imp.is_asset_path()) {
        write_u8(out, static_cast<std::uint8_t>(ProcessedImportKind::AssetPath));
        write_string(out, imp.as_asset_path().string());
    } else {
        write_u8(out, static_cast<std::uint8_t>(ProcessedImportKind::Custom));
        write_string(out, imp.as_custom());
    }
}

std::vector<std::uint8_t> serialize_processed_shader(const Shader& shader) {
    std::vector<std::uint8_t> out;
    out.reserve(64 + shader.imports.size() * 24);

    out.insert(out.end(), k_processed_shader_magic.begin(), k_processed_shader_magic.end());
    write_u32(out, k_processed_shader_version);

    if (shader.source.is_wgsl()) {
        write_u8(out, static_cast<std::uint8_t>(ProcessedSourceKind::Wgsl));
        write_string(out, shader.source.as_str());
    } else if (shader.source.is_slang()) {
        write_u8(out, static_cast<std::uint8_t>(ProcessedSourceKind::Slang));
        write_string(out, shader.source.as_str());
    } else {
        write_u8(out, static_cast<std::uint8_t>(ProcessedSourceKind::SpirV));
        const auto& bytes = std::get<Source::SpirV>(shader.source.data).bytes;
        write_u32(out, static_cast<std::uint32_t>(bytes.size()));
        out.insert(out.end(), bytes.begin(), bytes.end());
    }

    write_import(out, shader.import_path);
    write_u32(out, static_cast<std::uint32_t>(shader.imports.size()));
    for (const auto& imp : shader.imports) {
        write_import(out, imp);
    }
    return out;
}

bool has_processed_magic(const std::vector<char>& bytes) {
    if (bytes.size() < k_processed_shader_magic.size()) return false;
    return std::equal(k_processed_shader_magic.begin(), k_processed_shader_magic.end(),
                      reinterpret_cast<const std::uint8_t*>(bytes.data()));
}

bool read_u8(const std::vector<char>& bytes, std::size_t& pos, std::uint8_t& out) {
    if (pos + 1 > bytes.size()) return false;
    out = static_cast<std::uint8_t>(bytes[pos]);
    ++pos;
    return true;
}

bool read_u32(const std::vector<char>& bytes, std::size_t& pos, std::uint32_t& out) {
    if (pos + 4 > bytes.size()) return false;
    const auto b0 = static_cast<std::uint8_t>(bytes[pos + 0]);
    const auto b1 = static_cast<std::uint8_t>(bytes[pos + 1]);
    const auto b2 = static_cast<std::uint8_t>(bytes[pos + 2]);
    const auto b3 = static_cast<std::uint8_t>(bytes[pos + 3]);
    out           = static_cast<std::uint32_t>(b0) | (static_cast<std::uint32_t>(b1) << 8) |
                    (static_cast<std::uint32_t>(b2) << 16) | (static_cast<std::uint32_t>(b3) << 24);
    pos += 4;
    return true;
}

bool read_string(const std::vector<char>& bytes, std::size_t& pos, std::string& out) {
    std::uint32_t len = 0;
    if (!read_u32(bytes, pos, len)) return false;
    if (pos + len > bytes.size()) return false;
    out.assign(bytes.data() + pos, bytes.data() + pos + len);
    pos += len;
    return true;
}

bool read_import(const std::vector<char>& bytes, std::size_t& pos, ShaderImport& out) {
    std::uint8_t kind_raw = 0;
    if (!read_u8(bytes, pos, kind_raw)) return false;

    std::string payload;
    if (!read_string(bytes, pos, payload)) return false;

    auto kind = static_cast<ProcessedImportKind>(kind_raw);
    if (kind == ProcessedImportKind::AssetPath) {
        out = ShaderImport::asset_path(epix::assets::AssetPath(std::move(payload)));
        return true;
    }
    if (kind == ProcessedImportKind::Custom) {
        out = ShaderImport::custom(std::move(payload));
        return true;
    }
    return false;
}

std::optional<ProcessedDecodedShader> deserialize_processed_shader(const std::vector<char>& bytes) {
    if (!has_processed_magic(bytes)) return std::nullopt;
    std::size_t pos = k_processed_shader_magic.size();

    std::uint32_t version = 0;
    if (!read_u32(bytes, pos, version) || version != k_processed_shader_version) return std::nullopt;

    std::uint8_t source_kind_raw = 0;
    if (!read_u8(bytes, pos, source_kind_raw)) return std::nullopt;
    auto source_kind = static_cast<ProcessedSourceKind>(source_kind_raw);

    Source source;
    if (source_kind == ProcessedSourceKind::Wgsl || source_kind == ProcessedSourceKind::Slang) {
        std::string text;
        if (!read_string(bytes, pos, text)) return std::nullopt;
        if (source_kind == ProcessedSourceKind::Wgsl) {
            source = Source::wgsl(std::move(text));
        } else {
            source = Source::slang(std::move(text));
        }
    } else if (source_kind == ProcessedSourceKind::SpirV) {
        std::uint32_t len = 0;
        if (!read_u32(bytes, pos, len) || pos + len > bytes.size()) return std::nullopt;
        std::vector<std::uint8_t> spirv;
        spirv.reserve(len);
        for (std::uint32_t i = 0; i < len; ++i) {
            spirv.push_back(static_cast<std::uint8_t>(bytes[pos + i]));
        }
        pos += len;
        source = Source::spirv(std::move(spirv));
    } else {
        return std::nullopt;
    }

    ShaderImport import_path;
    if (!read_import(bytes, pos, import_path)) return std::nullopt;

    std::uint32_t import_count = 0;
    if (!read_u32(bytes, pos, import_count)) return std::nullopt;
    std::vector<ShaderImport> imports;
    imports.reserve(import_count);
    for (std::uint32_t i = 0; i < import_count; ++i) {
        ShaderImport imp;
        if (!read_import(bytes, pos, imp)) return std::nullopt;
        imports.push_back(std::move(imp));
    }

    if (pos != bytes.size()) return std::nullopt;
    return ProcessedDecodedShader{std::move(source), std::move(import_path), std::move(imports)};
}

}  // namespace

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
                // #import "asset/path.wgsl"  (optionally with source:// prefix)
                // Preserve the full AssetPath including any source:// scheme so the
                // loader can route to the right asset source.
                auto end_q    = rest.find('"', 1);
                std::string p = end_q == std::string_view::npos ? std::string(rest.substr(1))
                                                                : std::string(rest.substr(1, end_q - 1));
                imports.emplace_back(ShaderImport::asset_path(epix::assets::AssetPath(std::move(p))));
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

// ─── Slang identifier-to-file-path helper ──────────────────────────────────
// Per the Slang spec:
//   - Dots (.) become path separators (/)
//   - Underscores (_) become hyphens (-)
//   - String-literal syntax is used verbatim (without quotes)
// The .slang extension is appended if not already present.
static std::string slang_name_to_path(std::string_view name) {
    // String-literal syntax: import "path/to/file"; or __include "file";
    if (name.size() >= 2 && name.front() == '"') {
        auto end_q = name.find('"', 1);
        std::string p(end_q == std::string_view::npos ? name.substr(1) : name.substr(1, end_q - 1));
        // Preserve source:// prefix for asset routing; CacheFileSystem will strip it
        // when matching against shader.path (Slang passes the path verbatim).
        // Append .slang if no extension present (checking only after any scheme).
        std::string_view path_part = p;
        if (auto scheme = p.find("://"); scheme != std::string::npos)
            path_part = std::string_view(p).substr(scheme + 3);
        std::filesystem::path raw_path(path_part);
        if (raw_path.extension().empty()) {
            p += ".slang";
        }
        return p;
    }
    // Identifier-token syntax: import dir.file_name;
    std::string file_path;
    file_path.reserve(name.size() + 6);
    for (char c : name) {
        if (c == '.')
            file_path += '/';
        else if (c == '_')
            file_path += '-';
        else
            file_path += c;
    }
    file_path += ".slang";
    return file_path;
}

// ─── Shader::preprocess_slang ──────────────────────────────────────────────
// Scans Slang source for:
//   module <name>;              → sets import_path
//   import <name>;              → inter-module dependency (AssetPath import)
//   import "path";              → inter-module dependency (AssetPath import)
//   __include <name>;           → intra-module file inclusion (AssetPath import)
//   __include "path";           → intra-module file inclusion (AssetPath import)
// Per spec, identifier underscores → hyphens, dots → path separators.
std::pair<ShaderImport, std::vector<ShaderImport>> Shader::preprocess_slang(std::string_view source,
                                                                            std::string_view path) {
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

        // Skip single-line comments
        if (t.starts_with("//")) continue;

        // ── module <name> ; ──
        if (t.starts_with("module ") || t.starts_with("module\t")) {
            auto rest      = t.substr(7);
            std::size_t rs = rest.find_first_not_of(" \t");
            if (rs == std::string_view::npos) continue;
            rest                  = rest.substr(rs);
            std::size_t semi      = rest.find(';');
            std::string_view name = (semi == std::string_view::npos) ? rest : rest.substr(0, semi);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
            if (!name.empty()) {
                import_path = ShaderImport::asset_path(slang_name_to_path(name));
            }
            continue;
        }

        // ── import <name> ; ──
        if (t.starts_with("import ") || t.starts_with("import\t")) {
            auto rest      = t.substr(7);
            std::size_t rs = rest.find_first_not_of(" \t");
            if (rs == std::string_view::npos) continue;
            rest                  = rest.substr(rs);
            std::size_t semi      = rest.find(';');
            std::string_view name = (semi == std::string_view::npos) ? rest : rest.substr(0, semi);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
            if (!name.empty()) {
                imports.emplace_back(ShaderImport::asset_path(slang_name_to_path(name)));
            }
            continue;
        }

        // ── __include <name> ; ──
        if (t.starts_with("__include ") || t.starts_with("__include\t")) {
            auto rest      = t.substr(10);
            std::size_t rs = rest.find_first_not_of(" \t");
            if (rs == std::string_view::npos) continue;
            rest                  = rest.substr(rs);
            std::size_t semi      = rest.find(';');
            std::string_view name = (semi == std::string_view::npos) ? rest : rest.substr(0, semi);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
            if (!name.empty()) {
                imports.emplace_back(ShaderImport::asset_path(slang_name_to_path(name)));
            }
            continue;
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

Shader Shader::from_slang(std::string source, std::string path) {
    auto [ip, imps] = Shader::preprocess_slang(source, path);
    Shader s;
    s.path        = std::move(path);
    s.source      = Source::slang(std::move(source));
    s.import_path = std::move(ip);
    s.imports     = std::move(imps);
    return s;
}

Shader Shader::from_slang_with_defs(std::string source, std::string path, std::vector<ShaderDefVal> shader_defs) {
    auto s        = Shader::from_slang(std::move(source), std::move(path));
    s.shader_defs = std::move(shader_defs);
    return s;
}

// ─── ShaderLoader ──────────────────────────────────────────────────────────
std::span<std::string_view> ShaderLoader::extensions() {
    static std::array exts = {std::string_view{"wgsl"}, std::string_view{"spv"}, std::string_view{"slang"}};
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

    auto attach_dependencies = [&](Shader& shader) {
        for (const auto& imp : shader.imports) {
            if (imp.is_asset_path()) {
                // resolve() resolves relative paths against the parent's dir,
                // and respects the source in the import (from source:// syntax).
                auto dep_path = context.path().resolve(imp.as_asset_path());
                auto handle   = context.asset_server().template load<Shader>(dep_path);
                context.track_dependency(assets::UntypedAssetId(handle.id()));
                shader.file_dependencies.push_back(std::move(handle));
            }
        }
    };

    if (has_processed_magic(bytes)) {
        auto decoded = deserialize_processed_shader(bytes);
        if (!decoded.has_value()) {
            return std::unexpected(ShaderLoaderError::parse(raw_path, 0));
        }

        Shader shader;
        shader.path        = path_str;
        shader.source      = std::move(decoded->source);
        shader.import_path = std::move(decoded->import_path);
        shader.imports     = std::move(decoded->imports);
        shader.shader_defs = settings.shader_defs;
        attach_dependencies(shader);
        return shader;
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
        std::size_t bad_utf8 = 0;
        if (!validate_utf8_bytes(bytes, bad_utf8)) {
            return std::unexpected(ShaderLoaderError::parse(raw_path, bad_utf8));
        }
        std::string text(bytes.begin(), bytes.end());
        auto shader = Shader::from_wgsl_with_defs(std::move(text), path_str, settings.shader_defs);
        attach_dependencies(shader);
        return shader;
    }

    if (ext == "slang") {
        std::size_t bad_utf8 = 0;
        if (!validate_utf8_bytes(bytes, bad_utf8)) {
            return std::unexpected(ShaderLoaderError::parse(raw_path, bad_utf8));
        }
        std::string text(bytes.begin(), bytes.end());
        auto shader = Shader::from_slang_with_defs(std::move(text), path_str, settings.shader_defs);
        attach_dependencies(shader);
        return shader;
    }

    return std::unexpected(ShaderLoaderError::parse(raw_path, 0));
}

std::expected<ShaderProcessor::OutputLoader::Settings, std::exception_ptr> ShaderProcessor::process(
    assets::ProcessContext& context, const Settings& settings, std::ostream& writer) const {
    try {
        const auto& raw_path = context.path().path;
        std::string path_str = raw_path.string();
        std::ranges::replace(path_str, '\\', '/');

        std::vector<char> bytes = std::ranges::subrange(std::istreambuf_iterator<char>(context.asset_reader()),
                                                        std::istreambuf_iterator<char>()) |
                                  std::ranges::to<std::vector<char>>();

        if (context.asset_reader().fail() && !context.asset_reader().eof()) {
            return std::unexpected(
                std::make_exception_ptr(std::runtime_error("Failed to read shader source for processing")));
        }

        std::string ext = raw_path.extension().string();
        if (!ext.empty() && ext.front() == '.') ext = ext.substr(1);

        auto register_process_deps = [&](const Shader& shader) {
            for (const auto& imp : shader.imports) {
                if (!imp.is_asset_path()) continue;
                auto dep = context.path().resolve(imp.as_asset_path());
                context.new_processed_info().process_dependencies.push_back(
                    epix::assets::ProcessDependencyInfo{.full_hash = 0, .path = dep.string()});
            }
        };

        auto write_serialized = [&](const Shader& shader) -> bool {
            auto encoded = serialize_processed_shader(shader);
            writer.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
            return writer.good();
        };

        if (ext == "wgsl" && settings.preprocess_wgsl) {
            std::size_t bad_utf8 = 0;
            if (!validate_utf8_bytes(bytes, bad_utf8)) {
                return std::unexpected(std::make_exception_ptr(std::runtime_error("Invalid UTF-8 in WGSL")));
            }

            auto shader = Shader::from_wgsl_with_defs(std::string(bytes.begin(), bytes.end()), path_str,
                                                      settings.loader_settings.shader_defs);
            register_process_deps(shader);
            if (!write_serialized(shader)) {
                return std::unexpected(std::make_exception_ptr(std::runtime_error("Failed to write processed WGSL")));
            }
            return settings.loader_settings;
        }

        if (ext == "slang" && settings.preprocess_slang) {
            std::size_t bad_utf8 = 0;
            if (!validate_utf8_bytes(bytes, bad_utf8)) {
                return std::unexpected(std::make_exception_ptr(std::runtime_error("Invalid UTF-8 in Slang")));
            }

            auto shader = Shader::from_slang_with_defs(std::string(bytes.begin(), bytes.end()), path_str,
                                                       settings.loader_settings.shader_defs);
            register_process_deps(shader);
            if (!write_serialized(shader)) {
                return std::unexpected(std::make_exception_ptr(std::runtime_error("Failed to write processed Slang")));
            }
            return settings.loader_settings;
        }

        // Copy-through for all other formats/extensions.
        writer.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!writer.good()) {
            return std::unexpected(
                std::make_exception_ptr(std::runtime_error("Failed to write processed shader bytes")));
        }
        return settings.loader_settings;
    } catch (...) {
        return std::unexpected(std::current_exception());
    }
}

// ─── ShaderPlugin ──────────────────────────────────────────────────────────
void ShaderPlugin::build(core::App& app) {
    spdlog::debug("[shader] Building ShaderPlugin, registering shader asset loader.");
    assets::app_register_asset<Shader>(app);
    assets::app_register_loader<ShaderLoader>(app);

    if (app.world_mut().get_resource<assets::AssetProcessor>().has_value()) {
        assets::app_register_asset_processor<ShaderProcessor>(app, ShaderProcessor{});
        assets::app_set_default_asset_processor<ShaderProcessor>(app, "wgsl");
        assets::app_set_default_asset_processor<ShaderProcessor>(app, "slang");
    }
}
