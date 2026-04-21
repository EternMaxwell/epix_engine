module;

#ifndef EPIX_IMPORT_STD
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#endif
export module epix.assets:path;
#ifdef EPIX_IMPORT_STD
import std;
#endif
namespace epix::assets {
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
    std::string string() const;

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
    std::optional<std::string> take_label();
    /** @brief Return the parent directory of this asset path, or std::nullopt if there is no parent. */
    std::optional<AssetPath> parent() const;
    /** @brief Resolve a relative path against this path's directory. */
    AssetPath resolve(const AssetPath& relative) const;
    /** @brief Resolve a relative path using RFC 1808 (embedded) semantics, replacing the last component.
     *  Unlike resolve(), the base path's last component is stripped before joining.
     *  Matches bevy_asset's AssetPath::resolve_embed(). */
    AssetPath resolve_embed(const AssetPath& relative) const;
    /** @brief Returns true if this path escapes the asset directory (has a prefix, root, or parent dirs).
     *  Matches bevy_asset's AssetPath::is_unapproved(). */
    bool is_unapproved() const;
    /** @brief Get the full extension (e.g. "gltf.json" for "model.gltf.json"). */
    std::optional<std::string> get_full_extension() const;
    /** @brief Get the short extension (e.g. "json" for "model.gltf.json"). */
    std::optional<std::string> get_extension() const;
    /** @brief Iterate secondary extensions (all extensions except the final one).
     *  E.g. for "model.gltf.json" yields {"gltf"}. */
    std::vector<std::string> iter_secondary_extensions() const;
    /** @brief Try to parse a string as an AssetPath, returning std::nullopt on failure. */
    static std::optional<AssetPath> try_parse(std::string_view str);

    bool operator==(const AssetPath&) const  = default;
    auto operator<=>(const AssetPath&) const = default;
};
static_assert(std::three_way_comparable<AssetPath>);
}  // namespace epix::assets

namespace std {
template <>
struct hash<epix::assets::AssetPath> {
    size_t operator()(const epix::assets::AssetPath& ap) const noexcept {
        size_t h     = 0;
        auto combine = [&](size_t v) { h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2); };
        combine(
            std::hash<std::optional<std::string>>{}(static_cast<const epix::assets::AssetSourceId::base&>(ap.source)));
        combine(std::hash<std::filesystem::path>{}(ap.path));
        combine(std::hash<std::optional<std::string>>{}(ap.label));
        return h;
    }
};
}  // namespace std