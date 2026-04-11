module;

#include <slang-com-ptr.h>
#include <slang.h>
#include <spdlog/spdlog.h>

module epix.shader;

import epix.meta;

import :shader;

using namespace epix::shader;

namespace {

struct ProcessorCustomShaderRegistry {
    std::mutex mutex;
    std::unordered_map<ShaderImport, Shader> shaders_by_import;
    std::unordered_map<epix::assets::AssetId<Shader>, std::vector<ShaderImport>> provided_imports_by_id;
    std::unordered_map<epix::assets::AssetId<Shader>, std::vector<ShaderImport>> required_imports_by_id;
    std::unordered_map<ShaderImport, std::unordered_set<epix::assets::AssetId<Shader>>> dependents_by_import;

    std::unordered_set<epix::assets::AssetId<Shader>> remove_id(epix::assets::AssetId<Shader> id) {
        std::unordered_set<epix::assets::AssetId<Shader>> affected;

        if (auto pit = provided_imports_by_id.find(id); pit != provided_imports_by_id.end()) {
            for (const auto& import_ref : pit->second) {
                if (auto dit = dependents_by_import.find(import_ref); dit != dependents_by_import.end()) {
                    affected.insert(dit->second.begin(), dit->second.end());
                }
                shaders_by_import.erase(import_ref);
            }
            provided_imports_by_id.erase(pit);
        }

        if (auto rit = required_imports_by_id.find(id); rit != required_imports_by_id.end()) {
            for (const auto& import_ref : rit->second) {
                auto dit = dependents_by_import.find(import_ref);
                if (dit == dependents_by_import.end()) continue;
                dit->second.erase(id);
                if (dit->second.empty()) dependents_by_import.erase(dit);
            }
            required_imports_by_id.erase(rit);
        }

        affected.erase(id);
        return affected;
    }

    std::unordered_set<epix::assets::AssetId<Shader>> upsert(epix::assets::AssetId<Shader> id, const Shader& shader) {
        auto affected = remove_id(id);

        std::vector<ShaderImport> provided_imports;
        if (shader.import_path.is_custom()) {
            provided_imports.push_back(shader.import_path);
            shaders_by_import.insert_or_assign(shader.import_path, shader);
            if (auto dit = dependents_by_import.find(shader.import_path); dit != dependents_by_import.end()) {
                affected.insert(dit->second.begin(), dit->second.end());
            }
        }
        provided_imports_by_id.insert_or_assign(id, std::move(provided_imports));

        std::vector<ShaderImport> required_imports;
        required_imports.reserve(shader.imports.size());
        for (const auto& import_ref : shader.imports) {
            if (!import_ref.is_custom()) continue;
            required_imports.push_back(import_ref);
            dependents_by_import[import_ref].insert(id);
        }
        required_imports_by_id.insert_or_assign(id, std::move(required_imports));

        affected.erase(id);
        return affected;
    }

    std::optional<Shader> find_custom(const ShaderImport& import_ref) const {
        auto it = shaders_by_import.find(import_ref);
        if (it == shaders_by_import.end()) return std::nullopt;
        return it->second;
    }
};

constexpr std::array<std::uint8_t, 8> k_processed_shader_magic = {'E', 'P', 'S', 'H', 'P', 'R', '0', '1'};
constexpr std::uint32_t k_processed_shader_version             = 1;

enum class ProcessedSourceKind : std::uint8_t {
    Wgsl    = 1,
    SpirV   = 2,
    Slang   = 3,
    SlangIr = 4,
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

std::string normalize_path_string(std::string path) {
    std::ranges::replace(path, '\\', '/');
    return path;
}

std::string normalize_asset_path_string(const epix::assets::AssetPath& path) {
    std::string normalized;
    if (!path.source.is_default()) {
        normalized += *path.source.as_str();
        normalized += "://";
    }

    auto path_string = normalize_path_string(path.path.generic_string());
    if (path.path.has_root_directory()) {
        normalized += '/';
        normalized += path.path.relative_path().generic_string();
    } else {
        normalized += path_string;
    }

    if (path.label) {
        normalized += '#';
        normalized += *path.label;
    }
    return normalized;
}

std::filesystem::path normalize_custom_shader_path(std::string_view name) {
    if (name.size() >= 2 && name.front() == '"') {
        auto end_q = name.find('"', 1);
        name       = end_q == std::string_view::npos ? name.substr(1) : name.substr(1, end_q - 1);
    }

    if (auto scheme = name.find("://"); scheme != std::string_view::npos) {
        name = name.substr(scheme + 3);
    }

    std::string normalized;
    normalized.reserve(name.size());
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char current = name[i];
        if (current == ':' && i + 1 < name.size() && name[i + 1] == ':') {
            if (normalized.empty() || normalized.back() != '/') normalized.push_back('/');
            ++i;
            continue;
        }
        if (current == '.' || current == '/' || current == '\\') {
            if (normalized.empty() || normalized.back() != '/') normalized.push_back('/');
            continue;
        }
        normalized.push_back(current);
    }

    auto path = std::filesystem::path(normalized).lexically_normal();
    if (path.extension() == ".slang") {
        path.replace_extension();
    }
    return path;
}

epix::assets::AssetPath resolve_dependency_path(const epix::assets::AssetPath& parent_path,
                                                const ShaderImport& import_path) {
    auto resolved = import_path.as_asset_path();
    if (!resolved.source.is_default()) {
        return resolved;
    }
    if (resolved.path.has_root_directory()) {
        return epix::assets::AssetPath(parent_path.source, resolved.path.relative_path(), resolved.label);
    }
    return parent_path.resolve(resolved);
}

epix::assets::AssetPath resolve_asset_path_literal(const epix::assets::AssetPath& parent_path,
                                                   epix::assets::AssetPath import_path) {
    if (!import_path.source.is_default()) {
        return import_path;
    }
    if (import_path.path.has_root_directory()) {
        return epix::assets::AssetPath(parent_path.source, import_path.path.relative_path(), import_path.label);
    }
    return parent_path.resolve(import_path);
}

void ensure_slang_extension(std::string& path) {
    std::string_view path_part = path;
    if (auto scheme = path.find("://"); scheme != std::string::npos) {
        path_part = std::string_view(path).substr(scheme + 3);
    }
    if (std::filesystem::path(path_part).extension().empty()) {
        path += ".slang";
    }
}

epix::assets::AssetPath parse_slang_asset_path_literal(std::string_view name,
                                                       const epix::assets::AssetPath& importer_path) {
    auto end_q   = name.find('"', 1);
    auto literal = end_q == std::string_view::npos ? name.substr(1) : name.substr(1, end_q - 1);
    auto path    = normalize_path_string(std::string(literal));
    ensure_slang_extension(path);
    return resolve_asset_path_literal(importer_path, epix::assets::AssetPath(std::move(path)));
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
        write_string(out, normalize_asset_path_string(imp.as_asset_path()));
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
    } else if (shader.source.is_slang_ir()) {
        const auto& bytes = std::get<Source::SlangIr>(shader.source.data).bytes;
        write_u8(out, static_cast<std::uint8_t>(ProcessedSourceKind::SlangIr));
        write_u32(out, static_cast<std::uint32_t>(bytes.size()));
        out.insert(out.end(), bytes.begin(), bytes.end());
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
        out = ShaderImport::custom(std::filesystem::path(std::move(payload)));
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
    } else if (source_kind == ProcessedSourceKind::SlangIr) {
        std::uint32_t len = 0;
        if (!read_u32(bytes, pos, len) || pos + len > bytes.size()) return std::nullopt;
        std::vector<std::uint8_t> ir_bytes;
        ir_bytes.reserve(len);
        for (std::uint32_t i = 0; i < len; ++i) ir_bytes.push_back(static_cast<std::uint8_t>(bytes[pos + i]));
        pos += len;
        source = Source::slang_ir(std::move(ir_bytes));
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

// ── Minimal Slang VFS used by the processor's IR compilation helper ─────────
// This replicates just enough of `SlangVFS` from shader_cache.cpp to allow the
// processor to compile a module without depending on the cache internals.
class ProcessorSlangVFS : public ISlangFileSystemExt {
    struct Entry {
        std::string source;
        std::string identity;
    };
    std::unordered_map<std::string, Entry> files_;

    const Entry* find_entry(const char* path) const {
        if (auto it = files_.find(path); it != files_.end()) return &it->second;
        std::string_view sv(path);
        if (sv.ends_with(".slang")) {
            if (auto it = files_.find(std::string(sv.substr(0, sv.size() - 6))); it != files_.end()) return &it->second;
        }
        return nullptr;
    }

   public:
    void add(std::string key, std::string source, std::string identity) {
        files_.try_emplace(std::move(key), Entry{std::move(source), std::move(identity)});
    }
    bool contains(const std::string& key) const { return files_.contains(key); }

    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override {
        if (uuid == ISlangUnknown::getTypeGuid() || uuid == ISlangCastable::getTypeGuid() ||
            uuid == ISlangFileSystem::getTypeGuid() || uuid == ISlangFileSystemExt::getTypeGuid()) {
            *outObject = static_cast<ISlangFileSystemExt*>(this);
            return SLANG_OK;
        }
        *outObject = nullptr;
        return SLANG_E_NO_INTERFACE;
    }
    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }
    SLANG_NO_THROW void* SLANG_MCALL castAs(const SlangUUID& guid) override {
        if (guid == ISlangUnknown::getTypeGuid() || guid == ISlangCastable::getTypeGuid())
            return static_cast<ISlangCastable*>(this);
        if (guid == ISlangFileSystem::getTypeGuid()) return static_cast<ISlangFileSystem*>(this);
        if (guid == ISlangFileSystemExt::getTypeGuid()) return static_cast<ISlangFileSystemExt*>(this);
        return nullptr;
    }
    SLANG_NO_THROW SlangResult SLANG_MCALL loadFile(const char* path, ISlangBlob** outBlob) override {
        auto* e = find_entry(path);
        if (!e) {
            *outBlob = nullptr;
            return SLANG_E_NOT_FOUND;
        }
        *outBlob = slang_createBlob(e->source.data(), e->source.size());
        return SLANG_OK;
    }
    SLANG_NO_THROW SlangResult SLANG_MCALL getFileUniqueIdentity(const char* path, ISlangBlob** outId) override {
        auto* e = find_entry(path);
        if (!e) {
            *outId = nullptr;
            return SLANG_E_NOT_FOUND;
        }
        std::string id = e->identity;
        id.push_back('\0');
        Slang::ComPtr<ISlangBlob> blob;
        blob.attach(slang_createBlob(id.data(), id.size()));
        *outId = blob.detach();
        return SLANG_OK;
    }
    SLANG_NO_THROW SlangResult SLANG_MCALL calcCombinedPath(SlangPathType,
                                                            const char*,
                                                            const char* path,
                                                            ISlangBlob** out) override {
        std::string p(path);
        p.push_back('\0');
        Slang::ComPtr<ISlangBlob> blob;
        blob.attach(slang_createBlob(p.data(), p.size()));
        *out = blob.detach();
        return SLANG_OK;
    }
    SLANG_NO_THROW SlangResult SLANG_MCALL getPathType(const char* path, SlangPathType* t) override {
        if (find_entry(path)) {
            *t = SLANG_PATH_TYPE_FILE;
            return SLANG_OK;
        }
        return SLANG_E_NOT_FOUND;
    }
    SLANG_NO_THROW SlangResult SLANG_MCALL getPath(PathKind, const char* path, ISlangBlob** out) override {
        std::string p(path);
        p.push_back('\0');
        Slang::ComPtr<ISlangBlob> blob;
        blob.attach(slang_createBlob(p.data(), p.size()));
        *out = blob.detach();
        return SLANG_OK;
    }
    SLANG_NO_THROW void SLANG_MCALL clearCache() override {}
    SLANG_NO_THROW SlangResult SLANG_MCALL enumeratePathContents(const char*,
                                                                 FileSystemContentsCallBack,
                                                                 void*) override {
        return SLANG_E_NOT_IMPLEMENTED;
    }
    SLANG_NO_THROW OSPathKind SLANG_MCALL getOSPathKind() override { return OSPathKind::None; }
};

// Rewrite import/module path tokens to canonical form so the VFS keys match.
// This is a simplified version of preprocess_slang_compile_source from shader_cache.cpp.
static std::string processor_preprocess_slang_source(std::string_view source,
                                                     const epix::assets::AssetPath& importer_path) {
    std::string out;
    out.reserve(source.size() + 64);

    auto rewrite_path = [&](std::string_view name, bool force_custom) -> std::string {
        if (force_custom || name.empty() || name.front() != '"') {
            // Custom name: normalize to canonical asset path string
            auto custom_path = normalize_custom_shader_path(name);
            auto ap = epix::assets::AssetPath(epix::assets::AssetSourceId(std::string("shader_custom")), custom_path);
            return '"' + normalize_asset_path_string(ap) + '"';
        }
        auto end_q                 = name.find('"', 1);
        auto literal               = end_q == std::string_view::npos ? name.substr(1) : name.substr(1, end_q - 1);
        auto path_text             = normalize_path_string(std::string(literal));
        std::string_view path_part = path_text;
        if (auto scheme = path_text.find("://"); scheme != std::string::npos)
            path_part = std::string_view(path_text).substr(scheme + 3);
        if (std::filesystem::path(path_part).extension().empty()) path_text += ".slang";
        auto resolved = epix::assets::AssetPath(path_text);
        if (!resolved.source.is_default()) return '"' + normalize_asset_path_string(resolved) + '"';
        if (resolved.path.has_root_directory())
            resolved = epix::assets::AssetPath(importer_path.source, resolved.path.relative_path(), resolved.label);
        else
            resolved = importer_path.resolve(resolved);
        return '"' + normalize_asset_path_string(resolved) + '"';
    };

    std::size_t pos = 0;
    while (pos < source.size()) {
        std::size_t nl        = source.find('\n', pos);
        std::string_view line = (nl == std::string_view::npos) ? source.substr(pos) : source.substr(pos, nl - pos);
        pos                   = (nl == std::string_view::npos) ? source.size() : nl + 1;

        std::size_t ws = line.find_first_not_of(" \t\r");
        if (ws == std::string_view::npos) {
            out.append(line);
            if (nl != std::string_view::npos) out.push_back('\n');
            continue;
        }
        std::string_view indent  = line.substr(0, ws);
        std::string_view trimmed = line.substr(ws);
        if (trimmed.starts_with("//")) {
            out.append(line);
            if (nl != std::string_view::npos) out.push_back('\n');
            continue;
        }

        auto try_rewrite = [&](std::string_view directive, bool force_custom) -> bool {
            if (!(trimmed.starts_with(directive) &&
                  (trimmed.size() == directive.size() || trimmed[directive.size()] == ' ' ||
                   trimmed[directive.size()] == '\t')))
                return false;
            auto rest      = trimmed.substr(directive.size());
            std::size_t rs = rest.find_first_not_of(" \t");
            if (rs == std::string_view::npos) return false;
            rest                  = rest.substr(rs);
            std::size_t semi      = rest.find(';');
            std::string_view name = (semi == std::string_view::npos) ? rest : rest.substr(0, semi);
            std::string_view tail = (semi == std::string_view::npos) ? std::string_view{} : rest.substr(semi + 1);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
            if (name.empty()) return false;
            out.append(indent);
            out.append(directive);
            out.push_back(' ');
            out.append(rewrite_path(name, force_custom));
            out.push_back(';');
            out.append(tail);
            if (nl != std::string_view::npos) out.push_back('\n');
            return true;
        };

        if (try_rewrite("module", true) || try_rewrite("import", false) || try_rewrite("__include", false)) {
            continue;
        }
        out.append(line);
        if (nl != std::string_view::npos) out.push_back('\n');
    }
    return out;
}

// Recursively populate the VFS with all transitive source deps of `current_path`.
// Reads directly from the asset source (not processed) so this works during processing.
static void populate_processor_vfs(ProcessorSlangVFS& vfs,
                                   const epix::assets::AssetPath& current_path,
                                   std::string_view source_text,
                                   const epix::assets::AssetProcessor& processor,
                                   const std::shared_ptr<ProcessorCustomShaderRegistry>& registry,
                                   std::unordered_set<std::string>& visited) {
    auto identity = normalize_asset_path_string(current_path);
    if (!visited.insert(identity).second) return;

    auto preprocessed = processor_preprocess_slang_source(source_text, current_path);
    vfs.add(identity, preprocessed, identity);

    // Derive import_key (canonical custom name) if the module declares one.
    auto [import_info, imports] = Shader::preprocess_slang(source_text, current_path);
    if (import_info.is_custom()) {
        auto& k_shader_custom_source = "shader_custom";
        auto custom_ap  = epix::assets::AssetPath(epix::assets::AssetSourceId(std::string(k_shader_custom_source)),
                                                  import_info.as_custom_path());
        auto import_key = normalize_asset_path_string(custom_ap);
        if (import_key != identity) vfs.add(import_key, preprocessed, identity);
    }

    auto recurse_cached_custom = [&](const ShaderImport& custom_import) {
        if (!registry) return;
        std::optional<Shader> dep_shader;
        {
            std::scoped_lock lock(registry->mutex);
            dep_shader = registry->find_custom(custom_import);
        }
        if (!dep_shader.has_value()) return;

        // Processor-side Slang compilation needs source text; ignore non-text shaders.
        if (!dep_shader->source.is_slang()) return;

        populate_processor_vfs(vfs, dep_shader->path, dep_shader->source.as_str(), processor, registry, visited);
    };

    // Recurse into imports.
    for (const auto& imp : imports) {
        if (imp.is_asset_path()) {
            auto dep_path     = resolve_dependency_path(current_path, imp);
            auto dep_identity = normalize_asset_path_string(dep_path);
            if (visited.contains(dep_identity)) continue;

            auto source_ref = processor.get_source(dep_path.source);
            if (!source_ref.has_value()) continue;

            auto stream = source_ref->get().reader().read(dep_path.path);
            if (!stream.has_value()) continue;

            std::vector<char> dep_bytes =
                std::ranges::subrange(std::istreambuf_iterator<char>(**stream), std::istreambuf_iterator<char>()) |
                std::ranges::to<std::vector<char>>();

            if (dep_bytes.empty()) continue;
            std::string dep_source(dep_bytes.begin(), dep_bytes.end());
            populate_processor_vfs(vfs, dep_path, dep_source, processor, registry, visited);
            continue;
        }

        recurse_cached_custom(imp);
    }
}

// Try to compile a Slang module to a serialized IR blob.
// Returns the blob bytes on success, nullopt on any failure.
static std::optional<std::vector<std::uint8_t>> try_compile_slang_to_ir(
    const std::string& source_text,
    const epix::assets::AssetPath& path,
    const epix::assets::AssetProcessor& processor,
    const std::shared_ptr<ProcessorCustomShaderRegistry>& registry) {
    Slang::ComPtr<slang::IGlobalSession> global_session;
    if (SLANG_FAILED(slang::createGlobalSession(global_session.writeRef()))) return std::nullopt;

    ProcessorSlangVFS vfs;
    std::unordered_set<std::string> visited;
    populate_processor_vfs(vfs, path, source_text, processor, registry, visited);

    const char* search_paths[]      = {""};
    slang::SessionDesc session_desc = {};
    session_desc.searchPaths        = search_paths;
    session_desc.searchPathCount    = 1;
    session_desc.fileSystem         = &vfs;

    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(global_session->createSession(session_desc, session.writeRef()))) return std::nullopt;

    auto identity     = normalize_asset_path_string(path);
    auto preprocessed = processor_preprocess_slang_source(source_text, path);

    Slang::ComPtr<slang::IBlob> diag;
    slang::IModule* mod =
        session->loadModuleFromSourceString("module", identity.c_str(), preprocessed.c_str(), diag.writeRef());
    if (!mod) return std::nullopt;

    Slang::ComPtr<ISlangBlob> blob;
    if (SLANG_FAILED(mod->serialize(blob.writeRef())) || !blob) return std::nullopt;

    const auto* ptr = static_cast<const std::uint8_t*>(blob->getBufferPointer());
    return std::vector<std::uint8_t>(ptr, ptr + blob->getBufferSize());
}

}  // namespace

// ─── Shader::preprocess ────────────────────────────────────────────────────
// Scans WGSL source for:
//   #define_import_path <name>  → import_path = Custom(name)
//   #import "path"              → AssetPath import
//   #import name                → Custom import
std::pair<ShaderImport, std::vector<ShaderImport>> Shader::preprocess(std::string_view source,
                                                                      const epix::assets::AssetPath& path) {
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
            import_path = ShaderImport::custom(normalize_custom_shader_path(rest));
        } else if (directive == "import" && !rest.empty()) {
            if (rest.starts_with('"')) {
                // #import "asset/path.wgsl"  (optionally with source:// prefix)
                // Resolve immediately to the concrete asset path visible from this shader.
                auto end_q    = rest.find('"', 1);
                std::string p = end_q == std::string_view::npos ? std::string(rest.substr(1))
                                                                : std::string(rest.substr(1, end_q - 1));
                imports.emplace_back(
                    ShaderImport::asset_path(resolve_asset_path_literal(path, epix::assets::AssetPath(std::move(p)))));
            } else {
                // #import some::module::name  (possibly with "as alias")
                std::size_t as_pos = rest.find(" as ");
                std::string name   = std::string(as_pos == std::string_view::npos ? rest : rest.substr(0, as_pos));
                // trim trailing whitespace
                while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
                imports.emplace_back(ShaderImport::custom(normalize_custom_shader_path(name)));
            }
        }
    }

    ShaderImport ip = import_path.value_or(ShaderImport::asset_path(path));
    return {std::move(ip), std::move(imports)};
}

// ─── Shader::preprocess_slang ──────────────────────────────────────────────
// Scans Slang source for:
//   module <name>;              → sets import_path as a custom module name
//   import <name>;              → inter-module dependency (custom import)
//   import "path";              → file dependency (AssetPath import)
//   __include <name>;           → intra-module dependency (custom import)
//   __include "path";           → intra-module file inclusion (AssetPath import)
// Unquoted names always become custom names. Quoted names keep asset-path semantics:
// `source://...` is a full asset path, `/...` is source-root-relative, and everything else
// stays relative to the importing asset until resolved against that asset path.
std::pair<ShaderImport, std::vector<ShaderImport>> Shader::preprocess_slang(std::string_view source,
                                                                            const epix::assets::AssetPath& path) {
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
                import_path = ShaderImport::custom(normalize_custom_shader_path(name));
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
                if (name.front() == '"') {
                    imports.emplace_back(ShaderImport::asset_path(parse_slang_asset_path_literal(name, path)));
                } else {
                    imports.emplace_back(ShaderImport::custom(normalize_custom_shader_path(name)));
                }
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
                if (name.front() == '"') {
                    imports.emplace_back(ShaderImport::asset_path(parse_slang_asset_path_literal(name, path)));
                } else {
                    imports.emplace_back(ShaderImport::custom(normalize_custom_shader_path(name)));
                }
            }
            continue;
        }
    }

    ShaderImport ip = import_path.value_or(ShaderImport::asset_path(path));
    return {std::move(ip), std::move(imports)};
}

// ─── Shader constructors ───────────────────────────────────────────────────
Shader Shader::from_wgsl(std::string source, epix::assets::AssetPath path) {
    auto [ip, imps] = Shader::preprocess(source, path);
    Shader s;
    s.path        = std::move(path);
    s.source      = Source::wgsl(std::move(source));
    s.import_path = std::move(ip);
    s.imports     = std::move(imps);
    return s;
}

Shader Shader::from_wgsl_with_defs(std::string source,
                                   epix::assets::AssetPath path,
                                   std::vector<ShaderDefVal> shader_defs) {
    auto s        = Shader::from_wgsl(std::move(source), std::move(path));
    s.shader_defs = std::move(shader_defs);
    return s;
}

Shader Shader::from_spirv(std::vector<std::uint8_t> source, epix::assets::AssetPath path) {
    Shader s;
    s.path        = path;
    s.source      = Source::spirv(std::move(source));
    s.import_path = ShaderImport::asset_path(s.path);
    // imports is empty (no text to parse)
    return s;
}

Shader Shader::from_slang(std::string source, epix::assets::AssetPath path) {
    auto [ip, imps] = Shader::preprocess_slang(source, path);
    Shader s;
    s.path        = std::move(path);
    s.source      = Source::slang(std::move(source));
    s.import_path = std::move(ip);
    s.imports     = std::move(imps);
    return s;
}

Shader Shader::from_slang_with_defs(std::string source,
                                    epix::assets::AssetPath path,
                                    std::vector<ShaderDefVal> shader_defs) {
    auto s        = Shader::from_slang(std::move(source), std::move(path));
    s.shader_defs = std::move(shader_defs);
    return s;
}

Shader Shader::from_slang_ir(std::vector<std::uint8_t> bytes, epix::assets::AssetPath path) {
    Shader s;
    s.path        = path;
    s.source      = Source::slang_ir(std::move(bytes));
    s.import_path = ShaderImport::asset_path(s.path);
    // No imports — the module graph is embedded in the IR blob.
    return s;
}

// ─── ShaderLoader ──────────────────────────────────────────────────────────
std::span<std::string_view> ShaderLoader::extensions() {
    static std::array exts = {std::string_view{"wgsl"}, std::string_view{"spv"}, std::string_view{"slang"},
                              std::string_view{"slang-module"}};
    return exts;
}

std::expected<Shader, ShaderLoaderError> ShaderLoader::load(std::istream& reader,
                                                            const Settings& settings,
                                                            assets::LoadContext& context) {
    const auto& asset_path = context.path();
    const auto& raw_path   = asset_path.path;

    spdlog::trace("[shader] Loading shader from '{}'.", normalize_asset_path_string(asset_path));

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
                auto dep_path = resolve_dependency_path(context.path(), imp);
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
        shader.path        = asset_path;
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
        return Shader::from_spirv(std::move(code), asset_path);
    }

    if (ext == "wgsl") {
        if (!settings.shader_defs.empty()) {
            spdlog::debug("[shader] Applying {} shader_defs to '{}'.", settings.shader_defs.size(),
                          normalize_asset_path_string(asset_path));
        }
        std::size_t bad_utf8 = 0;
        if (!validate_utf8_bytes(bytes, bad_utf8)) {
            return std::unexpected(ShaderLoaderError::parse(raw_path, bad_utf8));
        }
        std::string text(bytes.begin(), bytes.end());
        auto shader = Shader::from_wgsl_with_defs(std::move(text), asset_path, settings.shader_defs);
        attach_dependencies(shader);
        return shader;
    }

    if (ext == "slang") {
        std::size_t bad_utf8 = 0;
        if (!validate_utf8_bytes(bytes, bad_utf8)) {
            return std::unexpected(ShaderLoaderError::parse(raw_path, bad_utf8));
        }
        std::string text(bytes.begin(), bytes.end());
        auto shader = Shader::from_slang_with_defs(std::move(text), asset_path, settings.shader_defs);
        attach_dependencies(shader);
        return shader;
    }

    if (ext == "slang-module") {
        // Raw Slang IR blob — no text to parse, no imports to infer.
        std::vector<std::uint8_t> ir_bytes(bytes.begin(), bytes.end());
        return Shader::from_slang_ir(std::move(ir_bytes), asset_path);
    }

    return std::unexpected(ShaderLoaderError::parse(raw_path, 0));
}

std::expected<ShaderProcessor::OutputLoader::Settings, std::exception_ptr> ShaderProcessor::process(
    assets::ProcessContext& context, const Settings& settings, std::ostream& writer) const {
    try {
        const auto& raw_path = context.path().path;

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
                auto dep = resolve_dependency_path(context.path(), imp);
                context.new_processed_info().process_dependencies.push_back(
                    epix::assets::ProcessDependencyInfo{.full_hash = 0, .path = normalize_asset_path_string(dep)});
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

            auto shader = Shader::from_wgsl_with_defs(std::string(bytes.begin(), bytes.end()), context.path(),
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

            auto source_str = std::string(bytes.begin(), bytes.end());

            // When IR compilation is requested, attempt to compile the module to
            // a serialized Slang IR blob.  Dependencies are read from the asset
            // source reader so the processor is self-contained.  On any failure
            // the code falls through to the normal text-preprocessing path.
            if (settings.preprocess_slang_to_ir) {
                auto registry = std::static_pointer_cast<ProcessorCustomShaderRegistry>(custom_registry_);
                auto ir_opt   = try_compile_slang_to_ir(source_str, context.path(), context.processor(), registry);
                if (ir_opt.has_value()) {
                    // Build a Shader with SlangIr source for serialization.
                    Shader ir_shader;
                    ir_shader.path        = context.path();
                    ir_shader.source      = Source::slang_ir(std::move(*ir_opt));
                    ir_shader.import_path = ShaderImport::asset_path(context.path());
                    // Also carry over any file imports so dependency tracking works.
                    {
                        auto [ip, imps]       = Shader::preprocess_slang(source_str, context.path());
                        ir_shader.imports     = std::move(imps);
                        ir_shader.import_path = std::move(ip);
                    }
                    register_process_deps(ir_shader);
                    if (!write_serialized(ir_shader)) {
                        return std::unexpected(
                            std::make_exception_ptr(std::runtime_error("Failed to write processed Slang IR")));
                    }
                    return settings.loader_settings;
                }
                // Compilation failed — fall through to text-processing path.
                spdlog::debug("[shader] Slang IR compilation failed for '{}', falling back to text.",
                              normalize_asset_path_string(context.path()));
            }

            auto shader = Shader::from_slang_with_defs(std::move(source_str), context.path(),
                                                       settings.loader_settings.shader_defs);
            register_process_deps(shader);
            if (!write_serialized(shader)) {
                return std::unexpected(std::make_exception_ptr(std::runtime_error("Failed to write processed Slang")));
            }
            return settings.loader_settings;
        }

        // slang-module: pass the raw bytes through unchanged.  The loader will
        // handle them as a raw Slang IR blob (identified by the extension).
        if (ext == "slang-module") {
            writer.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!writer.good()) {
                return std::unexpected(
                    std::make_exception_ptr(std::runtime_error("Failed to write slang-module bytes")));
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

    // Sync dependency-ready shader assets to ShaderCache (skipped if no ShaderCache resource).
    app.add_systems(
        core::Last,
        core::into([](core::ResMut<ShaderCache> cache, core::ResMut<assets::Assets<Shader>> shaders,
                      core::EventReader<assets::AssetEvent<Shader>> events) { cache->sync(events.read(), *shaders); })
            .after(assets::AssetSystems::WriteEvents)
            .run_if([](std::optional<core::Res<ShaderCache>> opt) { return opt.has_value(); })
            .set_name("sync shader cache"));

    if (app.world_mut().get_resource<assets::AssetProcessor>().has_value()) {
        auto processor_registry = std::make_shared<ProcessorCustomShaderRegistry>();
        assets::app_register_asset_processor<ShaderProcessor>(app, ShaderProcessor{processor_registry});
        assets::app_set_default_asset_processor<ShaderProcessor>(app, "wgsl");
        assets::app_set_default_asset_processor<ShaderProcessor>(app, "slang");
        assets::app_set_default_asset_processor<ShaderProcessor>(app, "slang-module");

        // Keep a processor-visible registry of manually added shader modules so
        // preprocess_slang_to_ir can resolve custom imports that are not
        // discoverable via file-based dependency traversal.
        app.add_systems(core::Last,
                        core::into([processor_registry](core::ResMut<assets::Assets<Shader>> shaders,
                                                        core::Res<assets::AssetServer> server,
                                                        core::EventReader<assets::AssetEvent<Shader>> events) {
                            std::vector<assets::AssetPath> reload_paths;
                            std::scoped_lock lock(processor_registry->mutex);
                            for (const auto& event : events.read()) {
                                if (event.is_loaded_with_dependencies() || event.is_modified()) {
                                    auto shader = shaders->get(event.id);
                                    if (shader.has_value()) {
                                        auto affected = processor_registry->upsert(event.id, shader->get());
                                        for (auto dep_id : affected) {
                                            auto dep_shader = shaders->get(dep_id);
                                            if (dep_shader.has_value()) {
                                                reload_paths.push_back(dep_shader->get().path);
                                            }
                                        }
                                    }
                                } else if (event.is_unused()) {
                                    auto affected = processor_registry->remove_id(event.id);
                                    for (auto dep_id : affected) {
                                        auto dep_shader = shaders->get(dep_id);
                                        if (dep_shader.has_value()) {
                                            reload_paths.push_back(dep_shader->get().path);
                                        }
                                    }
                                }
                            }

                            for (const auto& path : reload_paths) {
                                (void)server->reload(path);
                            }
                        })
                            .after(assets::AssetSystems::WriteEvents)
                            .set_name("sync processor custom shader registry"));
    }
}
