module;

#include <spdlog/spdlog.h>

module epix.assets;

import :server.loaders;

import std;
import epix.meta;
import epix.utils;

namespace epix::assets {
std::shared_ptr<ErasedAssetLoader> MaybeAssetLoader::get() const {
    if (std::holds_alternative<std::shared_ptr<ErasedAssetLoader>>(*this)) {
        return std::get<std::shared_ptr<ErasedAssetLoader>>(*this);
    } else {
        auto& pending = std::get<PendingAssetLoader>(*this);
        return pending.receiver.receive().value();
    }
}
std::optional<MaybeAssetLoader> AssetLoaders::get_by_index(std::size_t index) const {
    if (index >= loaders.size()) return std::nullopt;
    return loaders[index];
}
std::optional<MaybeAssetLoader> AssetLoaders::get_by_name(std::string_view loader_type_name) const {
    if (auto it = type_name_to_loader.find(loader_type_name); it != type_name_to_loader.end()) {
        return loaders[it->second];
    }
    return std::nullopt;
}
std::optional<MaybeAssetLoader> AssetLoaders::get_by_type(meta::type_index asset_type) const {
    if (auto it = type_to_loaders.find(asset_type); it != type_to_loaders.end() && !it->second.empty()) {
        return loaders[it->second.back()];
    }
    return std::nullopt;
}
std::optional<MaybeAssetLoader> AssetLoaders::get_by_extension(std::string_view extension) const {
    if (auto it = extension_to_loaders.find(extension); it != extension_to_loaders.end() && !it->second.empty()) {
        return loaders[it->second.back()];
    }
    return std::nullopt;
}
std::optional<MaybeAssetLoader> AssetLoaders::get_by_path(const AssetPath& path) const {
    if (auto full_ext = path.get_full_extension()) {
        if (auto r = get_by_extension(*full_ext)) return r;
        for (auto& sec : path.iter_secondary_extensions()) {
            if (auto r = get_by_extension(sec)) return r;
        }
    }
    return std::nullopt;
}
std::optional<MaybeAssetLoader> AssetLoaders::find(
    std::optional<std::string_view> type_name,
    std::optional<meta::type_index> asset_type_id,
    std::optional<std::string_view> extension,
    std::optional<std::reference_wrapper<const AssetPath>> asset_path) const {
    // Step 1: loader name wins immediately
    if (type_name) return get_by_name(*type_name);

    bool has_label = asset_path && asset_path->get().label.has_value();

    // Step 2: candidates narrowed by asset type
    const std::vector<std::size_t>* candidates = nullptr;
    if (asset_type_id && !has_label) {
        auto it = type_to_loaders.find(*asset_type_id);
        if (it == type_to_loaders.end() || it->second.empty()) return std::nullopt;
        candidates = &it->second;
        // Do NOT short-circuit here for single candidates: extension check must still run
        // so that e.g. loading "file.unknown" with only TestTextLoader (for .txt) correctly fails.
    }

    // Helper: try an extension, filtered by candidates
    auto try_ext = [&](std::string_view ext) -> std::optional<std::size_t> {
        auto it2 = extension_to_loaders.find(ext);
        if (it2 == extension_to_loaders.end() || it2->second.empty()) return std::nullopt;
        const auto& list = it2->second;
        if (candidates) {
            // find the LAST loader that is in both list and candidates (like Bevy's .rev().find())
            for (auto it_r = list.rbegin(); it_r != list.rend(); ++it_r) {
                if (std::ranges::contains(*candidates, *it_r)) return *it_r;
            }
            return std::nullopt;
        }
        return list.back();
    };

    // Step 3: explicit extension parameter
    if (extension) {
        if (auto idx = try_ext(*extension)) return get_by_index(*idx);
    }

    // Step 4: full extension from path, then secondary extensions
    if (asset_path) {
        if (auto full_ext = asset_path->get().get_full_extension()) {
            if (auto idx = try_ext(*full_ext)) return get_by_index(*idx);
            for (auto& sec : asset_path->get().iter_secondary_extensions()) {
                if (auto idx = try_ext(sec)) return get_by_index(*idx);
            }
        }
    }

    // Step 5: fallback — only when path has no extension (avoids silently using wrong loader)
    bool path_has_ext = (extension.has_value()) || (asset_path && asset_path->get().get_full_extension().has_value());
    if (!path_has_ext && candidates && !candidates->empty()) {
        if (candidates->size() > 1) {
            spdlog::warn("Multiple AssetLoaders found for Asset: {}; Path: {}; Extension: {}",
                         asset_type_id ? std::string(asset_type_id->name()) : std::string("?"),
                         asset_path ? asset_path->get().string() : std::string("?"),
                         extension ? std::string(*extension) : std::string("?"));
        }
        return get_by_index(candidates->back());
    }

    return std::nullopt;
}
}  // namespace epix::assets
