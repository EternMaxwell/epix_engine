module;
#ifndef EPIX_IMPORT_STD
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#endif
module epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
namespace epix::assets {

std::string AssetPath::string() const {
    std::stringstream ss;
    if (!source.is_default()) ss << *source << "://";
    ss << path.string();
    if (label) ss << "#" << *label;
    return ss.str();
}

std::optional<std::string> AssetPath::take_label() {
    auto l = std::move(label);
    label.reset();
    return l;
}

std::optional<AssetPath> AssetPath::parent() const {
    auto p = path.parent_path();
    if (p.empty() || p == path) return std::nullopt;
    return AssetPath(source, std::move(p));
}

AssetPath AssetPath::resolve(const AssetPath& relative) const {
    auto base     = path.parent_path();
    auto resolved = (base / relative.path).lexically_normal();
    return AssetPath(relative.source.is_default() ? source : relative.source, std::move(resolved), relative.label);
}

AssetPath AssetPath::resolve_embed(const AssetPath& relative) const {
    // If relative is label-only, keep our path and just change the label
    if (relative.source.is_default() && relative.path.empty() && relative.label) {
        AssetPath result = *this;
        result.label     = relative.label;
        return result;
    }
    // RFC 1808: pop last component of base before joining
    auto base = path;
    if (!path.empty()) base = path.parent_path();
    auto resolved = (base / relative.path).lexically_normal();
    return AssetPath(relative.source.is_default() ? source : relative.source, std::move(resolved), relative.label);
}

bool AssetPath::is_unapproved() const {
    namespace fs = std::filesystem;
    fs::path simplified;
    for (auto component : path) {
        if (component == fs::path("..")) {
            if (!simplified.has_relative_path()) return true;
            simplified = simplified.parent_path();
        } else if (component.is_absolute() || component.root_name() != fs::path{}) {
            return true;
        } else if (component != fs::path(".")) {
            simplified /= component;
        }
    }
    return false;
}

std::optional<std::string> AssetPath::get_full_extension() const {
    auto filename = path.filename().string();
    auto dot      = filename.find('.');
    if (dot == std::string::npos) return std::nullopt;
    return filename.substr(dot + 1);
}

std::optional<std::string> AssetPath::get_extension() const {
    auto ext = path.extension().string();
    if (ext.empty()) return std::nullopt;
    if (ext.starts_with('.')) ext.erase(0, 1);
    return ext;
}

std::vector<std::string> AssetPath::iter_secondary_extensions() const {
    auto full = get_full_extension();
    if (!full) return {};
    std::vector<std::string> parts;
    std::string_view sv = *full;
    for (auto dot = sv.find('.'); dot != std::string_view::npos; dot = sv.find('.')) {
        parts.emplace_back(sv.substr(0, dot));
        sv = sv.substr(dot + 1);
    }
    // The last part is the primary extension, secondary = all but last
    return parts;
}

std::optional<AssetPath> AssetPath::try_parse(std::string_view str) {
    if (str.empty()) return std::nullopt;
    return AssetPath(str);
}

}  // namespace epix::assets
