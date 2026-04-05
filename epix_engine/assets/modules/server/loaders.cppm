module;

#include <spdlog/spdlog.h>

export module epix.assets:server.loaders;

import std;
import epix.meta;
import epix.utils;

import :server.info;
import :server.loader;

namespace epix::assets {
struct PendingAssetLoader {
    utils::BroadcastSender<std::shared_ptr<ErasedAssetLoader>> sender;
    utils::BroadcastReceiver<std::shared_ptr<ErasedAssetLoader>> receiver;
};
struct MaybeAssetLoader : std::variant<std::shared_ptr<ErasedAssetLoader>, PendingAssetLoader> {
    using variant::variant;
    using base = std::variant<std::shared_ptr<ErasedAssetLoader>, PendingAssetLoader>;
    std::shared_ptr<ErasedAssetLoader> get() const {
        if (std::holds_alternative<std::shared_ptr<ErasedAssetLoader>>(*this)) {
            return std::get<std::shared_ptr<ErasedAssetLoader>>(*this);
        } else {
            auto& pending = std::get<PendingAssetLoader>(*this);
            return pending.receiver.receive().value();
        }
    }
    base& as_base() { return static_cast<base&>(*this); }
    const base& as_base() const { return static_cast<const base&>(*this); }
};
struct AssetLoaders {
    std::vector<MaybeAssetLoader> loaders;
    std::unordered_map<meta::type_index, std::vector<std::size_t>> type_to_loaders;
    std::unordered_map<std::string_view, std::vector<std::size_t>> extension_to_loaders;
    std::unordered_map<std::string_view, std::size_t> type_name_to_loader;
    std::unordered_map<std::string_view, std::size_t> type_name_to_preregistered_loader;

    std::optional<MaybeAssetLoader> get_by_index(std::size_t index) const {
        if (index >= loaders.size()) return std::nullopt;
        return loaders[index];
    }
    template <typename T>
        requires AssetLoader<std::remove_cvref_t<T>>
    void push(T&& loader_value) {
        using loader_type                  = std::remove_cvref_t<T>;
        meta::type_index loader_asset_type = meta::type_id<typename loader_type::Asset>{};

        auto erased_loader =
            std::make_shared<ErasedAssetLoaderImpl<std::remove_cvref_t<T>>>(std::forward<T>(loader_value));
        auto [loader_index, is_new] = [&]() {
            if (auto it = type_name_to_preregistered_loader.find(erased_loader->loader_type().name());
                it != type_name_to_preregistered_loader.end()) {
                return std::make_pair(it->second, false);
            } else {
                return std::make_pair(loaders.size(), true);
            }
        }();

        if (is_new) {
            auto&& existing_loaders_for_asset = type_to_loaders[loader_asset_type];
            std::vector<std::string_view> duplicate_extensions;
            for (auto&& extension : erased_loader->extensions()) {
                auto& loaders_for_extension = extension_to_loaders[extension];
                if (!loaders_for_extension.empty() &&
                    std::ranges::any_of(loaders_for_extension, [&](std::size_t index) {
                        return std::ranges::contains(existing_loaders_for_asset, index);
                    })) {
                    duplicate_extensions.push_back(extension);
                }
                loaders_for_extension.push_back(loader_index);
            }
            if (!duplicate_extensions.empty()) {
                spdlog::warn("Duplicate AssetLoader registered for Asset type `{}` with extensions `{}`.",
                             loader_asset_type.short_name(), duplicate_extensions);
            }

            type_name_to_loader.emplace(erased_loader->loader_type().name(), loader_index);
            type_to_loaders[loader_asset_type].push_back(loader_index);
            loaders.push_back(std::move(erased_loader));
        } else {
            MaybeAssetLoader maybe_loader = std::move(loaders[loader_index]);
            loaders[loader_index]         = std::move(erased_loader);
            std::visit(utils::visitor{[&](std::shared_ptr<ErasedAssetLoader>&) { std::unreachable(); },
                                      [&](PendingAssetLoader& pending) {
                                          auto loader = std::get<std::shared_ptr<ErasedAssetLoader>>(
                                              loaders[loader_index].as_base());
                                          auto sender = std::move(pending.sender);
                                          utils::IOTaskPool::instance().detach_task(
                                              [sender = std::move(sender), loader]() mutable { sender.send(loader); });
                                      }},
                       maybe_loader.as_base());
        }
    }
    template <AssetLoader loader_type>
    void reserve(std::span<std::string_view> extensions) {
        meta::type_index loader_asset_type = meta::type_id<typename loader_type::Asset>{};
        std::string_view loader_type_name  = meta::type_id<loader_type>{}.name();
        std::size_t loader_index           = loaders.size();
        type_name_to_preregistered_loader.emplace(loader_type_name, loader_index);
        type_name_to_loader.emplace(loader_type_name, loader_index);

        auto& existing_loaders_for_asset = type_to_loaders[loader_asset_type];
        std::vector<std::string_view> duplicate_extensions;
        for (auto&& extension : extensions) {
            auto& loaders_for_extension = extension_to_loaders[extension];
            if (!loaders_for_extension.empty() && std::ranges::any_of(loaders_for_extension, [&](std::size_t index) {
                    return std::ranges::contains(existing_loaders_for_asset, index);
                })) {
                duplicate_extensions.push_back(extension);
            }
            loaders_for_extension.push_back(loader_index);
        }
        if (!duplicate_extensions.empty()) {
            spdlog::warn("Duplicate AssetLoader registered for Asset type `{}` with extensions `{}`.",
                         loader_asset_type.short_name(), duplicate_extensions);
        }

        type_to_loaders[loader_asset_type].push_back(loader_index);
        auto&& [sender, receiver] = utils::make_broadcast_channel<std::shared_ptr<ErasedAssetLoader>>();
        loaders.push_back(PendingAssetLoader{std::move(sender), std::move(receiver)});
    }
    std::optional<MaybeAssetLoader> get_by_name(std::string_view loader_type_name) const {
        if (auto it = type_name_to_loader.find(loader_type_name); it != type_name_to_loader.end()) {
            return loaders[it->second];
        }
        return std::nullopt;
    }
    std::optional<MaybeAssetLoader> get_by_type(meta::type_index asset_type) const {
        if (auto it = type_to_loaders.find(asset_type); it != type_to_loaders.end() && !it->second.empty()) {
            return loaders[it->second.back()];  // return the most recently registered loader for the asset type
        }
        return std::nullopt;
    }
    std::optional<MaybeAssetLoader> get_by_extension(std::string_view extension) const {
        if (auto it = extension_to_loaders.find(extension); it != extension_to_loaders.end() && !it->second.empty()) {
            return loaders[it->second.back()];  // return the most recently registered loader for the extension
        }
        return std::nullopt;
    }
    std::optional<MaybeAssetLoader> get_by_path(const AssetPath& path) const {
        // try full extension, then each secondary extension
        if (auto full_ext = path.get_full_extension()) {
            if (auto r = get_by_extension(*full_ext)) return r;
            for (auto& sec : path.iter_secondary_extensions()) {
                if (auto r = get_by_extension(sec)) return r;
            }
        }
        return std::nullopt;
    }

    /** @brief Unified loader lookup matching bevy_asset's AssetLoaders::find.
     *
     *  Precedence (matching Bevy):
     *  1. loader type_name (exact match)
     *  2. asset_type_id only (when no label on path)
     *  3. provided extension
     *  4. full extension extracted from path, then each secondary extension
     *  5. fallback to last entry of candidates (with warn on ambiguity)
     */
    std::optional<MaybeAssetLoader> find(std::optional<std::string_view> type_name,
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
        bool path_has_ext =
            (extension.has_value()) || (asset_path && asset_path->get().get_full_extension().has_value());
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
};
}  // namespace epix::assets