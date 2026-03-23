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
};
export struct AssetPath {
    AssetSourceId source;
    std::filesystem::path path;
    std::optional<std::string> label;
};
}  // namespace assets