#pragma once

#include <cctype>
#include <concepts>
#include <string>
#include <string_view>
#include <vector>

namespace epix::shader::internal {
struct SlangDirective {
    std::string_view keyword;
    std::string name;
    std::size_t begin;
    std::size_t end;
};

// Metadata and path rewriting share one token scanner. Statements need not
// start on a new line, and quoted asset paths may contain spaces/semicolons.
inline std::vector<SlangDirective> slang_directives(std::string_view source) {
    std::vector<SlangDirective> result;
    const auto skip_comment = [&](std::size_t& pos) {
        if (source.substr(pos, 2) == "//") {
            const auto end = source.find('\n', pos + 2);
            pos = end == std::string_view::npos ? source.size() : end;
            return true;
        }
        if (source.substr(pos, 2) == "/*") {
            const auto end = source.find("*/", pos + 2);
            pos = end == std::string_view::npos ? source.size() : end + 2;
            return true;
        }
        return false;
    };
    const auto skip_string = [&](std::size_t& pos) {
        const auto quote = source[pos++];
        while (pos < source.size()) {
            if (source[pos] == '\\' && pos + 1 < source.size()) pos += 2;
            else if (source[pos++] == quote) break;
        }
    };
    for (std::size_t pos = 0; pos < source.size();) {
        if (skip_comment(pos)) continue;
        if (source[pos] == '"' || source[pos] == '\'') { skip_string(pos); continue; }
        if (source[pos] == '#') {
            do {
                const auto end = source.find('\n', pos);
                if (end == std::string_view::npos) { pos = source.size(); break; }
                pos = end + 1;
                if (end == 0 || source[end - 1] != '\\') break;
            } while (pos < source.size());
            continue;
        }
        if (!std::isalpha(static_cast<unsigned char>(source[pos])) && source[pos] != '_') { ++pos; continue; }
        const auto begin = pos++;
        while (pos < source.size() &&
               (std::isalnum(static_cast<unsigned char>(source[pos])) || source[pos] == '_')) ++pos;
        const auto keyword = source.substr(begin, pos - begin);
        if (keyword != "module" && keyword != "import" && keyword != "__include") continue;
        std::string name;
        while (pos < source.size() && source[pos] != ';') {
            if (skip_comment(pos)) { name.push_back(' '); continue; }
            if (source[pos] == '"' || source[pos] == '\'') {
                const auto start = pos;
                skip_string(pos);
                name.append(source.substr(start, pos - start));
            } else name.push_back(source[pos++]);
        }
        if (pos == source.size()) break;
        const auto first = name.find_first_not_of(" \t\r\n");
        if (first != std::string::npos) {
            const auto last = name.find_last_not_of(" \t\r\n");
            result.push_back({keyword, name.substr(first, last - first + 1), begin, pos + 1});
        }
        ++pos;
    }
    return result;
}

template <typename Rewrite>
    requires requires(Rewrite& rewrite, const SlangDirective& directive) {
        { rewrite(directive) } -> std::convertible_to<std::string_view>;
    }
std::string rewrite_slang_directives(std::string_view source, Rewrite&& rewrite) {
    std::string result;
    result.reserve(source.size());
    std::size_t cursor = 0;
    for (const auto& directive : slang_directives(source)) {
        result.append(source.substr(cursor, directive.begin - cursor));
        result.append(directive.keyword);
        result.push_back(' ');
        result.append(rewrite(directive));
        result.push_back(';');
        // Keep subsequent diagnostic line numbers when an import spans lines.
        for (const auto character : source.substr(directive.begin, directive.end - directive.begin))
            if (character == '\n') result.push_back('\n');
        cursor = directive.end;
    }
    result.append(source.substr(cursor));
    return result;
}
}  // namespace epix::shader::internal
