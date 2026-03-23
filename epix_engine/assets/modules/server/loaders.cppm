module;

#include <spdlog/spdlog.h>

export module epix.assets:server.loaders;

import std;
import epix.meta;
import epix.utils;

import :server.info;
import :server.loader;

namespace assets {
struct PendingAssetLoader {
    utils::Sender<std::shared_ptr<ErasedAssetLoader>> sender;
    utils::Receiver<std::shared_ptr<ErasedAssetLoader>> receiver;
};
struct MaybeAssetLoader : std::variant<std::shared_ptr<ErasedAssetLoader>, PendingAssetLoader> {
    using variant::variant;
    using base = std::variant<std::shared_ptr<ErasedAssetLoader>, PendingAssetLoader>;
    std::shared_ptr<ErasedAssetLoader> get() const {
        if (std::holds_alternative<std::shared_ptr<ErasedAssetLoader>>(*this)) {
            return std::get<std::shared_ptr<ErasedAssetLoader>>(*this);
        } else {
            auto& pending = std::get<PendingAssetLoader>(*this);
            return pending.receiver.receive();
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
    void push(T&& loader) {
        using loader_type                  = std::remove_cvref_t<T>;
        meta::type_index loader_asset_type = meta::type_id<typename loader_type::AssetType>{};

        auto loader = std::make_shared<ErasedAssetLoaderImpl<std::remove_cvref_t<T>>>(std::forward<T>(loader));
        auto [loader_index, is_new] = [&]() {
            if (auto it = type_name_to_preregistered_loader.find(loader->loader_type().name());
                it != type_name_to_preregistered_loader.end()) {
                return std::make_pair(it->second, false);
            } else {
                return std::make_pair(loaders.size(), true);
            }
        }();

        if (is_new) {
            auto&& existing_loaders_for_asset = type_to_loaders[loader_asset_type];
            std::vector<std::string_view> duplicate_extensions;
            for (auto&& extension : loader->extensions()) {
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

            type_name_to_loader.emplace(loader->loader_type().name(), loader_index);
            type_to_loaders[loader_asset_type].push_back(loader_index);
            loaders.push_back(std::move(loader));
        } else {
            MaybeAssetLoader maybe_loader = std::move(loaders[loader_index]);
            loaders[loader_index]         = std::move(loader);
            std::visit(
                utils::visitor{[&](std::shared_ptr<ErasedAssetLoader>&) { std::unreachable(); },
                               [&](PendingAssetLoader& pending) {
                                   utils::IOTaskPool::get().detach_task([maybe_loader = std::move(maybe_loader)]() {
                                       auto _ = pending.receiver.receive();  // get and destruct.
                                   });
                               }},
                maybe_loader.as_base());
        }
    }
    template <AssetLoader loader_type>
    void reserve(std::span<std::string_view> extensions) {
        meta::type_index loader_asset_type = meta::type_id<typename loader_type::AssetType>{};
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
        auto&& [sender, receiver] = utils::make_channel<std::shared_ptr<ErasedAssetLoader>>();
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
    std::optional<MaybeAssetLoader> get_by_path(const std::filesystem::path& path) const {
        std::string extension = path.extension().string();
        extension.erase(0, extension.find_first_not_of('.'));
        return get_by_extension(extension);
    }
};
}  // namespace assets