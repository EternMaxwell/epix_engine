module;

export module epix.assets:path;

import std;

namespace assets {
/**
 * @brief A reference to an 'asset source', which maps to an `AssetReader` or `AssetWriter`
 *
 * - `std::monostate` corresponds to 'default asset paths', with no source: '/path/to/asset.extension'
 * - `std::string` corresponds to asset paths that have a source: 'source://path/to/asset.extension'
 */
export struct AssetSourceId : std::optional<std::string> {
    using base = std::optional<std::string>;
    using base::base;
    bool is_default() const { return !this->has_value(); }
    std::optional<std::string_view> as_str() const { return static_cast<const base&>(*this); }
    auto operator<=>(const AssetSourceId& other) const = default;
    bool operator==(const AssetSourceId& other) const  = default;
};
/**
 * @brief A full asset path, including the source and the path within the source, and an optional label.
 *
 */
export struct AssetPath {
    AssetSourceId source;
    std::filesystem::path path;
    std::optional<std::string> label;

    AssetPath() = default;
    AssetPath(AssetSourceId source, std::filesystem::path path, std::optional<std::string> label = std::nullopt)
        : source(std::move(source)), path(std::move(path).lexically_normal()), label(std::move(label)) {}
    AssetPath(std::convertible_to<std::string_view> auto&& str) {
        // Format: [source://]path/to/asset[#label]
        std::string_view remaining = str;
        // Parse source (if present)
        if (auto pos = remaining.find("://"); pos != std::string_view::npos) {
            source    = AssetSourceId(std::string(remaining.substr(0, pos)));
            remaining = remaining.substr(pos + 3);
        }
        // Parse label (if present)
        if (auto pos = remaining.find('#'); pos != std::string_view::npos) {
            label     = std::string(remaining.substr(pos + 1));
            remaining = remaining.substr(0, pos);
        }
        path = std::filesystem::path(remaining).lexically_normal();
    }
    std::string string() const {
        std::stringstream ss;
        if (!source.is_default()) ss << *source << "://";
        ss << path.string();
        if (label) ss << "#" << *label;
        return ss.str();
    }

    /** @brief Return a copy of this path with a different label. */
    AssetPath with_label(std::convertible_to<std::string_view> auto&& new_label) const {
        return AssetPath(source, path, std::string(std::forward<decltype(new_label)>(new_label)));
    }
    /** @brief Return a copy of this path with a different source. */
    AssetPath with_source(AssetSourceId new_source) const { return AssetPath(std::move(new_source), path, label); }
    /** @brief Return a copy of this path without the label. */
    AssetPath without_label() const { return AssetPath(source, path); }
    /** @brief Remove the label from this path in place. */
    void remove_label() { label.reset(); }
    /** @brief Take the label out of this path, leaving it empty. */
    std::optional<std::string> take_label() {
        auto l = std::move(label);
        label.reset();
        return l;
    }
    /** @brief Return the parent directory of this asset path, or std::nullopt if there is no parent. */
    std::optional<AssetPath> parent() const {
        auto p = path.parent_path();
        if (p.empty() || p == path) return std::nullopt;
        return AssetPath(source, std::move(p));
    }
    /** @brief Resolve a relative path against this path's directory. */
    AssetPath resolve(const AssetPath& relative) const {
        auto base     = path.parent_path();
        auto resolved = (base / relative.path).lexically_normal();
        return AssetPath(relative.source.is_default() ? source : relative.source, std::move(resolved), relative.label);
    }
    /** @brief Get the full extension (e.g. "gltf.json" for "model.gltf.json"). */
    std::optional<std::string> get_full_extension() const {
        auto filename = path.filename().string();
        auto dot      = filename.find('.');
        if (dot == std::string::npos) return std::nullopt;
        return filename.substr(dot + 1);
    }
    /** @brief Get the short extension (e.g. "json" for "model.gltf.json"). */
    std::optional<std::string> get_extension() const {
        auto ext = path.extension().string();
        if (ext.empty()) return std::nullopt;
        if (ext.starts_with('.')) ext.erase(0, 1);
        return ext;
    }
    /** @brief Iterate secondary extensions (all extensions except the final one).
     *  E.g. for "model.gltf.json" yields {"gltf"}. */
    std::vector<std::string> iter_secondary_extensions() const {
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
    /** @brief Try to parse a string as an AssetPath, returning std::nullopt on failure. */
    static std::optional<AssetPath> try_parse(std::string_view str) {
        if (str.empty()) return std::nullopt;
        return AssetPath(str);
    }

    bool operator==(const AssetPath&) const  = default;
    auto operator<=>(const AssetPath&) const = default;
};
static_assert(std::three_way_comparable<AssetPath>);
}  // namespace assets

template <>
struct std::hash<assets::AssetPath> {
    size_t operator()(const assets::AssetPath& ap) const noexcept {
        size_t h     = 0;
        auto combine = [&](size_t v) { h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2); };
        combine(std::hash<std::optional<std::string>>{}(static_cast<const assets::AssetSourceId::base&>(ap.source)));
        combine(std::hash<std::filesystem::path>{}(ap.path));
        combine(std::hash<std::optional<std::string>>{}(ap.label));
        return h;
    }
};