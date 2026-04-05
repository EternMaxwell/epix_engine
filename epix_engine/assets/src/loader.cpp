module;

module epix.assets;

import :server.loader;

import std;
import epix.meta;

namespace epix::assets {
std::optional<std::reference_wrapper<const ErasedLoadedAsset>> ErasedLoadedAsset::get_labeled(
    const std::string& label) const {
    auto it = labeled_assets.find(label);
    if (it == labeled_assets.end()) return std::nullopt;
    return std::cref(it->second.asset);
}
std::optional<std::reference_wrapper<const ErasedLoadedAsset>> ErasedLoadedAsset::get_labeled_by_id(
    const UntypedAssetId& id) const {
    for (const auto& [_, labeled] : labeled_assets) {
        if (labeled.handle.id() == id) return std::cref(labeled.asset);
    }
    return std::nullopt;
}
std::vector<std::string_view> ErasedLoadedAsset::labels() const {
    std::vector<std::string_view> result;
    result.reserve(labeled_assets.size());
    for (const auto& [label, _] : labeled_assets) result.push_back(label);
    return result;
}
}  // namespace epix::assets
