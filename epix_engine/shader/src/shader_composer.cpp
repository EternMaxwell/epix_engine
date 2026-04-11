module;

#include <spdlog/spdlog.h>

module epix.shader;

import :shader_composer;

using namespace epix::shader;

// ─── Helpers ───────────────────────────────────────────────────────────────

static std::string trim(std::string_view sv) {
    std::size_t start = sv.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return {};
    std::size_t end = sv.find_last_not_of(" \t\r\n");
    return std::string(sv.substr(start, end - start + 1));
}

static std::string_view trim_sv(std::string_view sv) {
    std::size_t start = sv.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return {};
    std::size_t end = sv.find_last_not_of(" \t\r\n");
    return sv.substr(start, end - start + 1);
}

static bool looks_like_asset_context(std::string_view name) {
    return name.find("://") != std::string_view::npos || name.find('/') != std::string_view::npos ||
           name.find('\\') != std::string_view::npos || name.ends_with(".wgsl") || name.ends_with(".slang") ||
           name.ends_with(".spv");
}

static std::optional<epix::assets::AssetPath> asset_path_from_context_name(std::string_view context_name) {
    if (context_name.size() >= 2 && context_name.front() == '"' && context_name.back() == '"') {
        context_name.remove_prefix(1);
        context_name.remove_suffix(1);
    }

    if (!looks_like_asset_context(context_name)) {
        return std::nullopt;
    }

    return epix::assets::AssetPath(std::string(context_name));
}

static std::string canonicalize_custom_module_name(std::string_view raw_import) {
    std::string normalized;
    normalized.reserve(raw_import.size());
    for (std::size_t i = 0; i < raw_import.size(); ++i) {
        const char current = raw_import[i];
        if (current == ':' && i + 1 < raw_import.size() && raw_import[i + 1] == ':') {
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
    return path.generic_string();
}

static std::string canonicalize_import_name(std::string_view context_name, std::string_view raw_import) {
    std::string_view literal = raw_import;
    if (literal.starts_with('"')) {
        auto end_quote = literal.find('"', 1);
        literal        = end_quote == std::string_view::npos ? literal.substr(1) : literal.substr(1, end_quote - 1);
    }

    auto context_path = asset_path_from_context_name(context_name);
    if (!context_path.has_value()) {
        return '"' + std::string(literal) + '"';
    }

    epix::assets::AssetPath import_path{std::string(literal)};
    epix::assets::AssetPath resolved = import_path;
    if (import_path.source.is_default()) {
        if (import_path.path.has_root_directory()) {
            resolved =
                epix::assets::AssetPath(context_path->source, import_path.path.relative_path(), import_path.label);
        } else {
            resolved = context_path->resolve(import_path);
        }
    }

    return epix::shader::ShaderImport::asset_path(std::move(resolved)).module_name();
}

// ─── substitute_defs ───────────────────────────────────────────────────────
// Replace #{NAME} and #NAME occurrences in a line with their def values.
// #{NAME} is always matched (braced form); #NAME is matched only when the
// character(s) after the name are non-alphanumeric/non-underscore (word boundary).
static std::string substitute_defs(std::string_view line,
                                   const std::unordered_map<std::string, const ShaderDefVal*>& defs) {
    std::string out;
    out.reserve(line.size());
    std::size_t i = 0;
    while (i < line.size()) {
        if (line[i] != '#') {
            out += line[i++];
            continue;
        }
        // Try braced form: #{NAME}
        if (i + 1 < line.size() && line[i + 1] == '{') {
            auto close = line.find('}', i + 2);
            if (close != std::string_view::npos) {
                std::string name(line.substr(i + 2, close - i - 2));
                auto it = defs.find(name);
                if (it != defs.end()) {
                    out += it->second->value_as_string();
                    i = close + 1;
                    continue;
                }
            }
        }
        // Try bare form: #NAME (only at word boundary, not a directive keyword)
        if (i + 1 < line.size() && (std::isalpha(static_cast<unsigned char>(line[i + 1])) || line[i + 1] == '_')) {
            std::size_t j = i + 1;
            while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) ++j;
            std::string name(line.substr(i + 1, j - i - 1));
            // Only substitute if the name is a known def (avoids clobbering #ifdef etc.)
            auto it = defs.find(name);
            if (it != defs.end()) {
                out += it->second->value_as_string();
                i = j;
                continue;
            }
        }
        out += line[i++];
    }
    return out;
}

// ─── ShaderComposer member implementations ────────────────────────────────

std::expected<void, ComposeError> ShaderComposer::add_module(const std::string& module_name,
                                                             std::string_view source,
                                                             std::span<const ShaderDefVal> defs) {
    auto normalized_name = module_name;
    if (!looks_like_asset_context(normalized_name) &&
        !(normalized_name.size() >= 2 && normalized_name.front() == '"' && normalized_name.back() == '"')) {
        normalized_name = canonicalize_custom_module_name(module_name);
    }

    if (normalized_name.empty()) {
        return std::unexpected(ComposeError{ComposeError::ParseError{module_name, "empty module name"}});
    }
    modules_[normalized_name] = ModuleEntry{
        std::string(source),
        std::vector<ShaderDefVal>(defs.begin(), defs.end()),
    };
    spdlog::trace("[shader.composer] Registered module '{}'.", normalized_name);
    return {};
}

void ShaderComposer::remove_module(const std::string& module_name) {
    if (!looks_like_asset_context(module_name) &&
        !(module_name.size() >= 2 && module_name.front() == '"' && module_name.back() == '"')) {
        modules_.erase(canonicalize_custom_module_name(module_name));
        return;
    }
    modules_.erase(module_name);
}

bool ShaderComposer::contains_module(const std::string& module_name) const {
    if (!looks_like_asset_context(module_name) &&
        !(module_name.size() >= 2 && module_name.front() == '"' && module_name.back() == '"')) {
        return modules_.contains(canonicalize_custom_module_name(module_name));
    }
    return modules_.contains(module_name);
}

// ─── build_def_map ─────────────────────────────────────────────────────────
std::unordered_map<std::string, const ShaderDefVal*> ShaderComposer::build_def_map(std::span<const ShaderDefVal> a,
                                                                                   std::span<const ShaderDefVal> b) {
    std::unordered_map<std::string, const ShaderDefVal*> map;
    for (const auto& d : a) map[d.name] = &d;
    for (const auto& d : b) map[d.name] = &d;  // b wins on collision
    return map;
}

// ─── eval_condition ────────────────────────────────────────────────────────
// directive: "ifdef" | "ifndef" | "if"
// expr: the rest of the line after the directive keyword
bool ShaderComposer::eval_condition(std::string_view directive,
                                    std::string_view expr,
                                    const std::unordered_map<std::string, const ShaderDefVal*>& defs) {
    std::string e = trim(expr);
    if (directive == "ifdef") {
        return defs.contains(e);
    }
    if (directive == "ifndef") {
        return !defs.contains(e);
    }
    if (directive == "if") {
        // Ordered operators: >=, <=, >, < (checked before == / != to avoid prefix ambiguity)
        static constexpr std::array<std::pair<std::string_view, int>, 6> kOps{{
            {" >= ", 4},
            {" <= ", 4},
            {" > ", 3},
            {" < ", 3},
            {" != ", 4},
            {" == ", 4},
        }};

        for (const auto& [op, len] : kOps) {
            auto pos = e.find(op);
            if (pos == std::string::npos) continue;

            std::string lhs_name = trim(e.substr(0, pos));
            std::string rhs_str  = trim(e.substr(pos + len));
            auto it              = defs.find(lhs_name);

            if (op == " == ") {
                if (it == defs.end()) return false;  // not defined → eq is false
                return it->second->value_as_string() == rhs_str;
            }
            if (op == " != ") {
                if (it == defs.end()) return true;  // not defined → ne is true
                return it->second->value_as_string() != rhs_str;
            }

            // Ordered comparisons: parse both sides as int64 for numeric comparison.
            if (it == defs.end()) return false;  // not defined → ordered compare is false
            std::int64_t lhs_n{}, rhs_n{};
            auto lhs_str = it->second->value_as_string();
            auto r1      = std::from_chars(lhs_str.data(), lhs_str.data() + lhs_str.size(), lhs_n);
            auto r2      = std::from_chars(rhs_str.data(), rhs_str.data() + rhs_str.size(), rhs_n);
            if (r1.ec != std::errc{} || r2.ec != std::errc{}) return false;  // non-numeric → false

            if (op == " >= ") return lhs_n >= rhs_n;
            if (op == " <= ") return lhs_n <= rhs_n;
            if (op == " > ") return lhs_n > rhs_n;
            if (op == " < ") return lhs_n < rhs_n;
        }

        // bare #if NAME - treat as #ifdef
        return defs.contains(e);
    }
    return true;  // unknown directive: include by default
}

// ─── compose_internal ──────────────────────────────────────────────────────
std::expected<std::string, ComposeError> ShaderComposer::compose_internal(
    std::string_view source,
    std::string_view context_name,
    const std::unordered_map<std::string, const ShaderDefVal*>& defs,
    std::vector<std::string>& visiting) {
    std::string output;
    output.reserve(source.size());

    // Conditional stack: each entry = (active, else_seen)
    struct CondFrame {
        bool active;
        bool else_seen;
    };
    std::vector<CondFrame> cond_stack;

    auto is_active = [&]() {
        for (const auto& f : cond_stack)
            if (!f.active) return false;
        return true;
    };

    // Parse source line by line
    std::size_t pos = 0;
    while (pos <= source.size()) {
        std::size_t nl = source.find('\n', pos);
        std::string_view line_raw =
            (nl == std::string_view::npos) ? source.substr(pos) : source.substr(pos, nl - pos + 1);
        pos = (nl == std::string_view::npos) ? source.size() + 1 : nl + 1;

        std::string_view line_trimmed = trim_sv(line_raw);

        // ── preprocessor directives ───────────────────────────────────────
        if (line_trimmed.starts_with('#')) {
            std::size_t sp = line_trimmed.find_first_of(" \t", 1);
            std::string directive =
                std::string(sp == std::string_view::npos ? line_trimmed.substr(1) : line_trimmed.substr(1, sp - 1));
            std::string_view rest =
                (sp == std::string_view::npos) ? std::string_view{} : trim_sv(line_trimmed.substr(sp + 1));

            if (directive == "ifdef" || directive == "ifndef" || directive == "if") {
                bool result = is_active() && eval_condition(directive, rest, defs);
                cond_stack.push_back({result, false});
                continue;
            }
            if (directive == "else") {
                if (!cond_stack.empty()) {
                    auto& top = cond_stack.back();
                    if (!top.else_seen) {
                        // Determine if the parent scope is active
                        bool parent_active = true;
                        for (std::size_t i = 0; i + 1 < cond_stack.size(); ++i)
                            if (!cond_stack[i].active) {
                                parent_active = false;
                                break;
                            }
                        top.active    = parent_active && !top.active;
                        top.else_seen = true;
                    }
                }
                continue;
            }
            if (directive == "endif") {
                if (!cond_stack.empty()) cond_stack.pop_back();
                continue;
            }
            if (directive == "define_import_path") {
                // Strip this directive from output (it's metadata)
                continue;
            }
            if (directive == "import") {
                if (!is_active()) continue;
                // Determine module name from rest
                std::string import_name;
                std::string_view r = trim_sv(rest);
                if (r.starts_with('"')) {
                    import_name = canonicalize_import_name(context_name, r);
                } else {
                    // custom name (possibly "name as alias" - take first token)
                    auto sp2    = r.find_first_of(" \t");
                    import_name = canonicalize_custom_module_name(sp2 == std::string_view::npos ? r : r.substr(0, sp2));
                }

                // Cycle detection
                for (const auto& v : visiting) {
                    if (v == import_name) {
                        std::vector<std::string> cycle(visiting.begin(), visiting.end());
                        cycle.push_back(import_name);
                        return std::unexpected(ComposeError{ComposeError::CircularImport{std::move(cycle)}});
                    }
                }

                // Look up registered module
                auto it = modules_.find(import_name);
                if (it == modules_.end()) {
                    spdlog::warn("[shader.composer] Import '{}' not found (from '{}').", import_name, context_name);
                    return std::unexpected(ComposeError{ComposeError::ImportNotFound{import_name}});
                }

                // Merge base defs of the imported module with current defs
                auto merged = defs;
                for (const auto& d : it->second.base_defs) merged.emplace(d.name, &d);

                visiting.push_back(import_name);
                auto inlined = compose_internal(it->second.source, import_name, merged, visiting);
                visiting.pop_back();

                if (!inlined) return std::unexpected(inlined.error());
                output += inlined.value();
                output += '\n';
                continue;
            }
            // Any other # directive (e.g. #define) - pass through if active
            if (is_active()) {
                output += substitute_defs(line_raw, defs);
            }
            continue;
        }

        // ── regular code line ─────────────────────────────────────────────
        if (is_active()) {
            output += substitute_defs(line_raw, defs);
        }
    }

    return output;
}

// ─── compose (public) ──────────────────────────────────────────────────────
std::expected<std::string, ComposeError> ShaderComposer::compose(std::string_view source,
                                                                 std::string_view file_path,
                                                                 std::span<const ShaderDefVal> additional_defs) {
    auto defs = build_def_map(additional_defs);
    std::vector<std::string> visiting;
    visiting.push_back(std::string(file_path));
    return compose_internal(source, file_path, defs, visiting);
}
