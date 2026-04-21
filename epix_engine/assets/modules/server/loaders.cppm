module;

#ifndef EPIX_IMPORT_STD
#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <spdlog/spdlog.h>

export module epix.assets:server.loaders;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.meta;
import epix.utils;
import epix.tasks;

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
    std::shared_ptr<ErasedAssetLoader> get() const;
    base& as_base() { return static_cast<base&>(*this); }
    const base& as_base() const { return static_cast<const base&>(*this); }
};
struct AssetLoaders {
    std::vector<MaybeAssetLoader> loaders;
    std::unordered_map<meta::type_index, std::vector<std::size_t>> type_to_loaders;
    std::unordered_map<std::string_view, std::vector<std::size_t>> extension_to_loaders;
    std::unordered_map<std::string_view, std::size_t> type_name_to_loader;
    std::unordered_map<std::string_view, std::size_t> type_name_to_preregistered_loader;

    std::optional<MaybeAssetLoader> get_by_index(std::size_t index) const;
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
            std::visit(
                utils::visitor{[&](std::shared_ptr<ErasedAssetLoader>&) { std::unreachable(); },
                               [&](PendingAssetLoader& pending) {
                                   auto loader =
                                       std::get<std::shared_ptr<ErasedAssetLoader>>(loaders[loader_index].as_base());
                                   auto sender = std::move(pending.sender);
                                   tasks::IoTaskPool::get()
                                       .spawn([sender = std::move(sender), loader]() mutable { sender.send(loader); })
                                       .detach();
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
    std::optional<MaybeAssetLoader> get_by_name(std::string_view loader_type_name) const;
    std::optional<MaybeAssetLoader> get_by_type(meta::type_index asset_type) const;
    std::optional<MaybeAssetLoader> get_by_extension(std::string_view extension) const;
    std::optional<MaybeAssetLoader> get_by_path(const AssetPath& path) const;

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
                                         std::optional<std::reference_wrapper<const AssetPath>> asset_path) const;
};
}  // namespace epix::assets