#include "epix/assets/asset_server.h"

using namespace epix::assets;

EPIX_API const AssetInfo* AssetInfos::get_info(const UntypedAssetId& id) const {
    if (auto it = infos.find(id); it != infos.end()) {
        return &it->second;
    }
    return nullptr;
}
EPIX_API AssetInfo* AssetInfos::get_info(const UntypedAssetId& id) {
    if (auto it = infos.find(id); it != infos.end()) {
        return &it->second;
    }
    return nullptr;
}
EPIX_API std::optional<UntypedHandle> AssetInfos::get_or_create_handle_internal(
    const std::filesystem::path& path,
    const std::optional<std::type_index>& type,
    bool force_new
) {
    auto&& ids          = path_to_ids[path];
    auto asset_type_opt = type.or_else([&]() -> std::optional<std::type_index> {
        // No type provided, get the first type from the ids
        if (auto it = ids.begin(); it != ids.end()) {
            return it->first;  // Return the first type found
        }
        return std::nullopt;  // No types found
    });
    if (!asset_type_opt) {
        return std::nullopt;  // No type found, cannot create handle
    }
    auto asset_type = *asset_type_opt;
    if (auto it = ids.find(asset_type); it != ids.end()) {
        auto& info = infos.at(it->second);
        // Check if the handle is expired we create a new one
        if (info.weak_handle.expired()) {
            // If the weak handle is expired, we need to create a new handle
            if (auto provider_it = handle_providers.find(asset_type);
                provider_it != handle_providers.end()) {
                // If a provider for the type exists, use it to create a new
                // handle
                auto& provider   = *provider_it->second;
                auto new_handle  = provider.reserve(true, path);
                info.weak_handle = new_handle;
                info.path        = path;
                // If it is loading, we keep the state as Loading,
                // otherwise we set it to Pending
                info.state = info.state == LoadState::Loading
                                 ? LoadState::Loading
                                 : LoadState::Pending;
                return new_handle;
            }
        } else if (!force_new) {
            info.state = info.state == LoadState::Loading ? LoadState::Loading
                                                          : LoadState::Pending;
        }
        return info.weak_handle.lock();  // Return the existing handle
    } else {
        // a new asset should be created.
        if (auto provider_it = handle_providers.find(asset_type);
            provider_it != handle_providers.end()) {
            // If a provider for the type exists, use it to create a new
            // handle
            auto& provider  = *provider_it->second;
            auto new_handle = provider.reserve(true, path);
            auto id         = new_handle->id;
            // Insert to path_to_ids
            ids.emplace(asset_type, id);  // Insert the new id for the type
            // Create a new AssetInfo and insert it
            AssetInfo info;
            info.weak_handle = new_handle;
            info.path        = path;
            info.state       = LoadState::Pending;  // Initial state is Pending
            infos.emplace(id, std::move(info));
            return new_handle;
        }
    }
    return std::nullopt;  // No handle created
}
EPIX_API std::optional<UntypedHandle> AssetInfos::get_or_create_handle_untyped(
    const std::filesystem::path& path,
    const std::type_index& type,
    bool force_new
) {
    return get_or_create_handle_internal(
        path, type, force_new
    );  // Call the internal function with the type
}
EPIX_API bool AssetInfos::process_handle_destruction(const UntypedAssetId& id) {
    if (auto it = infos.find(id); it != infos.end()) {
        auto&& info = it->second;
        if (info.weak_handle.expired()) {
            // remove the id from the path_to_ids map
            if (auto path_it = path_to_ids.find(info.path);
                path_it != path_to_ids.end()) {
                auto& ids = path_it->second;
                ids.erase(id.type);
                if (ids.empty()) {
                    path_to_ids.erase(path_it);  // Remove empty paths
                }
            }
            // If the weak handle is expired, remove the asset info
            infos.erase(it);
            // Successfully processed the handle destruction
        } else {
            // This means that living handles are all destructed but a new
            // handle for this asset is required from
            // `get_or_create_handle_internal`.
            return true;
        }
    }
    return false;  // No action taken, either no info found or handle not
                   // expired
}

EPIX_API const ErasedAssetLoader* AssetLoaders::get_by_index(uint32_t index
) const {
    if (index < loaders.size()) {
        return loaders[index].get();
    }
    return nullptr;
}
EPIX_API const ErasedAssetLoader* AssetLoaders::get_by_type(
    const std::type_index& type
) const {
    auto it = type_to_loaders.find(type);
    if (it != type_to_loaders.end() && !it->second.empty()) {
        return get_by_index(it->second.back()
        );  // Get the last loader of this type
    }
    return nullptr;
}
EPIX_API std::vector<const ErasedAssetLoader*> AssetLoaders::get_multi_by_type(
    const std::type_index& type
) const {  // get all loaders of a specific type
    if (auto it = type_to_loaders.find(type); it != type_to_loaders.end()) {
        return it->second | std::views::transform([this](uint32_t index) {
                   return get_by_index(index);
               }) |
               std::ranges::to<std::vector>();
    }
    return {};  // Return an empty vector if no loaders of that type exist
}
EPIX_API const ErasedAssetLoader* AssetLoaders::get_by_extension(
    const std::string_view& ext
) const {
    auto it = ext_to_loaders.find(ext.data());
    if (it != ext_to_loaders.end() && !it->second.empty()) {
        return get_by_index(it->second.back());
    }
    return nullptr;
}
EPIX_API std::vector<const ErasedAssetLoader*>
AssetLoaders::get_multi_by_extension(const std::string_view& ext
) const {  // get all loaders of a specific extension
    if (auto it = ext_to_loaders.find(ext.data()); it != ext_to_loaders.end()) {
        return it->second | std::views::transform([this](uint32_t index) {
                   return get_by_index(index);
               }) |
               std::ranges::to<std::vector>();
    }
    return {};  // Return an empty vector if no loaders of that extension
                // exist
}
// get by path is a wrapper method for get_by_extension, but much easier to
// use
EPIX_API const ErasedAssetLoader* AssetLoaders::get_by_path(
    const std::filesystem::path& path
) const {  // get the loader by the file extension of the path
    // the extension name should not include the dot
    if (path.has_extension()) {
        auto ext      = path.extension().string();
        auto ext_view = std::string_view(ext);
        if (ext.starts_with('.')) {
            ext_view.remove_prefix(1);  // remove the leading dot
        }
        return get_by_extension(ext_view);
    }
    return nullptr;
}
EPIX_API std::vector<const ErasedAssetLoader*> AssetLoaders::get_multi_by_path(
    const std::filesystem::path& path
) const {  // get all loaders by the file extension of the path
    // the extension name should not include the dot
    if (path.has_extension()) {
        auto ext      = path.extension().string();
        auto ext_view = std::string_view(ext);
        if (ext.starts_with('.')) {
            ext_view.remove_prefix(1);  // remove the leading dot
        }
        return get_multi_by_extension(ext_view);
    }
    return {};  // Return an empty vector if no loaders found
}

EPIX_API AssetServer::AssetServer() : asset_infos(), asset_loaders() {
    std::tie(event_sender, event_receiver) =
        epix::utils::async::make_channel<InternalAssetEvent>();
}
EPIX_API std::optional<LoadState> AssetServer::get_state(
    const UntypedAssetId& id
) const {
    std::unique_lock lock(info_mutex);
    if (auto info = asset_infos.get_info(id)) {
        return info->state;
    }
    return std::nullopt;
}
EPIX_API void AssetServer::load_internal(
    const UntypedAssetId& id, const ErasedAssetLoader* loader
) const {
    if (auto opt = asset_infos.get_info(id);
        opt &&
        (opt->state == LoadState::Pending || opt->state == LoadState::Failed)) {
        auto& info = *opt;
        info.state = LoadState::Loading;  // Set the state to Loading
        // check loader type
        if (loader && loader->asset_type() != id.type) {
            // If the loader type does not match the asset type, we cannot
            // use this loader
            loader = nullptr;
        }
        LoadContext context{*this, info.path};
        if (loader) {
            spdlog::trace(
                "Loading asset {} of type {} with loader {} in trys of given "
                "loader",
                info.path.string(), id.type.name(), loader->loader_type().name()
            );
            info.waiter = std::async(
                std::launch::async,
                [this, loader, id, context]() mutable {
                    try {
                        auto asset = loader->load(context.path, context);
                        if (asset.value) {
                            event_sender.send(
                                AssetLoadedEvent{id, std::move(asset)}
                            );
                        } else {
                            event_sender.send(AssetLoadFailedEvent{
                                id,
                                "Failed to load asset. Loader returned no "
                                "value."
                            });
                        }
                    } catch (const std::exception& e) {
                        event_sender.send(AssetLoadFailedEvent{id, e.what()});
                    }
                }
            );
        } else if (auto loaders = asset_loaders.get_multi_by_path(info.path);
                   !loaders.empty() &&
                   std::ranges::any_of(
                       loaders, [&id](const ErasedAssetLoader* l
                                ) { return l->asset_type() == id.type; }
                   )) {
            // There are loaders for the asset path, try them;
            info.waiter = std::async(
                std::launch::async,
                [this, id, context, loaders = std::move(loaders)]() mutable {
                    std::vector<std::string> errors;
                    for (auto& loader :
                         loaders | std::views::reverse |
                             std::views::filter([&id](const ErasedAssetLoader* l
                                                ) {
                                 return l->asset_type() == id.type;
                             })) {
                        spdlog::trace(
                            "Attempting to load asset {} of type {} with "
                            "loader {} in trys of get by path",
                            context.path.string(), id.type.name(),
                            loader->loader_type().name()
                        );
                        try {
                            auto asset = loader->load(context.path, context);
                            if (asset.value) {
                                event_sender.send(
                                    AssetLoadedEvent{id, std::move(asset)}
                                );
                                return;  // Successfully loaded, exit
                            } else {
                                errors.emplace_back("Loader returned no value."
                                );
                            }
                        } catch (const std::exception& e) {
                            errors.emplace_back(e.what());
                        }
                    }
                    std::string error_message =
                        "Failed to load asset: " +
                        std::accumulate(
                            errors.begin(), errors.end(), std::string(),
                            [index = 0](
                                const std::string& a, const std::string& b
                            ) mutable {
                                return a + "\n" + "\tattempt " +
                                       std::to_string(++index) + ": " + b;
                            }
                        );
                    event_sender.send(
                        AssetLoadFailedEvent{id, std::move(error_message)}
                    );
                }
            );
        } else if (auto loaders = asset_loaders.get_multi_by_type(id.type
                   );  // Get loaders by type, if any exist
                   !loaders.empty()) {
            // There are loaders for the asset type, try them;
            info.waiter = std::async(
                std::launch::async,
                [this, id, context, loaders = std::move(loaders)]() mutable {
                    std::vector<std::string> errors;
                    for (auto& loader : loaders | std::views::reverse) {
                        spdlog::trace(
                            "Attempting to load asset {} of type {} with "
                            "loader {} in trys of get by type",
                            context.path.string(), id.type.name(),
                            loader->loader_type().name()
                        );
                        try {
                            auto asset = loader->load(context.path, context);
                            if (asset.value) {
                                event_sender.send(
                                    AssetLoadedEvent{id, std::move(asset)}
                                );
                                return;  // Successfully loaded, exit
                            } else {
                                errors.emplace_back("Loader returned no value."
                                );
                            }
                        } catch (const std::exception& e) {
                            errors.emplace_back(e.what());
                        }
                    }
                    std::string error_message =
                        "Failed to load asset: " +
                        std::accumulate(
                            errors.begin(), errors.end(), std::string(),
                            [index = 0](
                                const std::string& a, const std::string& b
                            ) mutable {
                                return a + "\n" + "\tattempt " +
                                       std::to_string(++index) + ": " + b;
                            }
                        );
                    event_sender.send(
                        AssetLoadFailedEvent{id, std::move(error_message)}
                    );
                }
            );
        } else {
            // No loader found for the asset type, we will keep it pending
            // and try to load it later
            spdlog::warn(
                "No loader found for asset {} of type {}, keeping it pending",
                info.path.string(), id.type.name()
            );
            pending_loads.push_back(id);
            info.state = LoadState::Pending;  // Keep the state as Pending
        }
    }
}
EPIX_API UntypedHandle
AssetServer::load_untyped(const std::filesystem::path& path) const {
    std::scoped_lock lock(info_mutex, pending_mutex);
    auto loader = asset_loaders.get_by_path(path);  // Get the loader by path
    if (!loader)
        return UntypedHandle();          // No loader found, return empty handle
    auto type   = loader->asset_type();  // Get the asset type from the loader
    auto handle = asset_infos.get_or_create_handle_untyped(path, type);
    if (!handle)
        return UntypedHandle();  // No handle created, return empty handle
    auto& id = handle->id();
    load_internal(id, loader);  // Load the asset internally with the loader
    return *handle;  // Return the handle if it was created successfully
}

EPIX_API bool AssetServer::process_handle_destruction(const UntypedAssetId& id
) const {
    // only lock infos
    std::unique_lock lock(info_mutex);
    // call the process_handle_destruction method of AssetInfos
    return asset_infos.process_handle_destruction(id);
}

EPIX_API void AssetServer::handle_events(
    World& world, Res<AssetServer> asset_server
) {
    // Process events from the event receiver
    auto receiver = asset_server->event_receiver;
    std::unique_lock lock(asset_server->info_mutex
    );  // Lock the info mutex to ensure thread safety
    while (auto event = receiver.try_receive()) {
        if (std::holds_alternative<AssetLoadedEvent>(*event)) {
            auto& loaded_event = std::get<AssetLoadedEvent>(*event);
            loaded_event.asset.value->insert(
                loaded_event.id, world
            );  // Insert the loaded asset into the world
        } else if (std::holds_alternative<AssetLoadFailedEvent>(*event)) {
            auto& failed_event = std::get<AssetLoadFailedEvent>(*event);
            auto& id           = failed_event.id;
            spdlog::error(
                "Failed to load asset {}: {}", id.to_string(),
                failed_event.error
            );
            if (auto info = asset_server->asset_infos.get_info(id)) {
                info->state = LoadState::Failed;  // Set the state to Failed
            }
        }
    }
}

EPIX_API AssetServer::~AssetServer() {
    std::unique_lock lock(info_mutex);
    for (const auto& [id, info] : asset_infos.infos) {
        info.waiter.wait();  // Wait for all asset loading tasks to finish
    }
}