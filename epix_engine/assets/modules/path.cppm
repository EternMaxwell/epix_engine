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
    AssetPath(std::string_view str) {
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