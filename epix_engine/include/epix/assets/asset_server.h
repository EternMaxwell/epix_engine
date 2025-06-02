#pragma once

#include <epix/app.h>

#include <filesystem>
#include <ranges>

#include "assets.h"

namespace epix::assets {
struct AssetServer;

struct LoadContext {
    const AssetServer& server;
    std::filesystem::path path;
};
template <typename T>
concept AssetLoader = requires(T t) {
    // need a static function which returns a viewable range of const char*
    // indicating the supported file extensions
    { T::extensions() } -> std::ranges::viewable_range;
    { T::extensions().begin() } -> std::convertible_to<const char*>;
    // a static load function which takes a path and a LoadContext
    {
        T::load(
            std::declval<std::filesystem::path>(), std::declval<LoadContext>()
        )
    };
};
template <AssetLoader T>
struct LoaderInfo {
    using load_return_type           = decltype(T::load(
        std::declval<std::filesystem::path>(), std::declval<LoadContext>()
    ));
    using asset_type                 = std::remove_pointer_t<load_return_type>;
    static constexpr bool return_ptr = std::is_pointer_v<load_return_type>;
};
struct AssetContainer {
    virtual ~AssetContainer()                                   = default;
    virtual std::type_index type() const                        = 0;
    virtual void insert(const UntypedAssetId& id, World& world) = 0;
};
template <typename T>
struct AssetContainerImpl : T, AssetContainer {
    using asset_type = T;
    using T::T;  // inherit constructors
    std::type_index type() const override { return typeid(T); }
    void insert(const UntypedAssetId& id, World& world) override {
        world.resource<Assets<T>>().insert(id.typed<T>(), std::move((T&)*this));
    }
};
template <typename T>
struct AssetContainerImpl<T*> : AssetContainer {
    using asset_type = T;
    T* asset;
    AssetContainerImpl(T* asset) : asset(asset) {}
    ~AssetContainerImpl() {
        if (asset) {
            delete asset;
        }
    }
    std::type_index type() const override { return typeid(T); }
    void insert(const UntypedAssetId& id, World& world) override {
        world.resource<Assets<T>>().insert(id.typed<T>(), std::move(*asset));
    }
};
struct ErasedLoadedAsset {
    std::unique_ptr<AssetContainer> value;
};
struct ErasedAssetLoader {
    virtual ~ErasedAssetLoader()                = default;
    virtual std::type_index asset_type() const  = 0;
    virtual std::type_index loader_type() const = 0;
    virtual ErasedLoadedAsset load(
        const std::filesystem::path& path, const LoadContext& context
    )                                                   = 0;
    virtual std::vector<const char*> extensions() const = 0;
};
template <AssetLoader T>
struct ErasedAssetLoaderImpl : ErasedAssetLoader {
    using loader_info = LoaderInfo<T>;
    std::type_index asset_type() const override {
        return typeid(typename loader_info::asset_type);
    }
    std::type_index loader_type() const override { return typeid(T); }
    ErasedLoadedAsset load(
        const std::filesystem::path& path, const LoadContext& context
    ) override {
        if constexpr (loader_info::return_ptr) {
            auto asset = T::load(path, context);
            if (asset) {
                return ErasedLoadedAsset{std::make_unique<
                    AssetContainerImpl<typename loader_info::asset_type>>(asset)
                };
            } else {
                return ErasedLoadedAsset{nullptr};
            }
        } else {
            auto asset = T::load(path, context);
            return ErasedLoadedAsset{std::make_unique<
                AssetContainerImpl<typename loader_info::asset_type>>(
                std::move(asset)
            )};
        }
        // this function does not catch exceptions, and the exceptions should be
        // handled by the caller, most likely the AssetServer, to send the
        // related event
    }
    std::vector<const char*> extensions() const override {
        return T::extensions() |
               std::views::transform([](const char* ext) { return ext; }) |
               std::ranges::to<std::vector>();
    }
};
enum LoadState {
    Pending,  // Asset is pending to be loaded
    Loading,  // Asset is currently being loaded
    Loaded,   // Asset has been loaded successfully
    Failed,   // Asset failed to load
};
struct AssetInfo {
    std::weak_ptr<StrongHandle> weak_handle;
    std::filesystem::path path;
    LoadState state;
    std::shared_future<void> waiter;
};
struct AssetInfos {
   private:
    entt::dense_map<
        std::filesystem::path,
        entt::dense_map<std::type_index, UntypedAssetId>>
        path_to_ids;
    entt::dense_map<UntypedAssetId, AssetInfo> infos;
    entt::dense_map<std::type_index, std::shared_ptr<HandleProvider>>
        handle_providers;

    friend struct AssetServer;  // Allow AssetServer to access private members

   public:
    std::optional<AssetInfo> get_info(const UntypedAssetId& id) const {
        if (auto it = infos.find(id); it != infos.end()) {
            return it->second;
        }
        return std::nullopt;  // No info found for the given id
    }
    std::optional<UntypedHandle> get_or_create_handle_internal(
        const std::filesystem::path& path,
        const std::optional<std::type_index>& type,
        bool force_new = false
    ) {
        auto&& ids = path_to_ids[path];
        auto asset_type_opt =
            type.or_else([&]() -> std::optional<std::type_index> {
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
                info.state = info.state == LoadState::Loading
                                 ? LoadState::Loading
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
                info.state = LoadState::Pending;  // Initial state is Pending
                infos.emplace(id, std::move(info));
                return new_handle;
            }
        }
        return std::nullopt;  // No handle created
    }
    std::optional<UntypedHandle> get_or_create_handle_untyped(
        const std::filesystem::path& path,
        const std::type_index& type,
        bool force_new = false
    ) {
        return get_or_create_handle_internal(
            path, type, force_new
        );  // Call the internal function with the type
    }
    template <typename T>
    std::optional<Handle<T>> get_or_create_handle(
        const std::filesystem::path& path, bool force_new = false
    ) {
        auto type   = std::type_index(typeid(T));
        auto handle = get_or_create_handle_internal(path, type, force_new);
        if (handle) {
            return handle->try_typed<T>();
        }
        return std::nullopt;
    }
    bool process_handle_destruction(const UntypedAssetId& id) {
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
                return true;  // Successfully processed the handle destruction
            } else {
                // This means that living handles are all destructed but a new
                // handle for this asset is required from
                // `get_or_create_handle_internal`.
            }
        }
        return false;  // No action taken, either no info found or handle not
                       // expired
    }
};
struct AssetLoaders {
   private:
    std::vector<std::unique_ptr<ErasedAssetLoader>> loaders;
    entt::dense_map<std::type_index, std::vector<uint32_t>> type_to_loaders;
    entt::dense_map<const char*, std::vector<uint32_t>> ext_to_loaders;

   public:
    ErasedAssetLoader* get_by_index(uint32_t index) {
        if (index < loaders.size()) {
            return loaders[index].get();
        }
        return nullptr;
    }
    ErasedAssetLoader* get_by_type(const std::type_index& type) {
        auto it = type_to_loaders.find(type);
        if (it != type_to_loaders.end() && !it->second.empty()) {
            return get_by_index(it->second[0]);
        }
        return nullptr;
    }
    ErasedAssetLoader* get_by_extension(const std::string_view& ext) {
        auto it = ext_to_loaders.find(ext.data());
        if (it != ext_to_loaders.end() && !it->second.empty()) {
            return get_by_index(it->second[0]);
        }
        return nullptr;
    }
    // get by path is a wrapper method for get_by_extension, but much easier to
    // use
    ErasedAssetLoader* get_by_path(const std::filesystem::path& path
    ) {  // get the loader by the file extension of the path
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
    template <AssetLoader T>
    void push(const T&) {
        using loader_info = LoaderInfo<T>;
        std::unique_ptr<ErasedAssetLoader> loader =
            std::make_unique<ErasedAssetLoaderImpl<T>>();
        auto type      = loader->loader_type();
        auto ext       = loader->extensions();
        uint32_t index = static_cast<uint32_t>(loaders.size());
        loaders.emplace_back(std::move(loader));
        type_to_loaders[type].push_back(index);
        for (const char* e : ext) {
            ext_to_loaders[e].push_back(index);
        }
    }
};
struct AssetLoadedEvent {
    UntypedAssetId id;        // the id of the asset that was loaded
    ErasedLoadedAsset asset;  // the loaded asset, erased to a generic type
};
struct AssetLoadFailedEvent {
    UntypedAssetId id;  // the id of the asset that failed to load
    std::string error;  // the error message
};
using InternalAssetEvent = std::variant<
    AssetLoadedEvent,     // asset was loaded successfully
    AssetLoadFailedEvent  // asset failed to load
    >;
struct AssetServer {
    AssetServer() : asset_infos(), asset_loaders() {
        std::tie(event_sender, event_receiver) =
            epix::utils::async::make_channel<InternalAssetEvent>();
    }
    AssetServer(const AssetServer&)            = delete;
    AssetServer(AssetServer&&)                 = delete;
    AssetServer& operator=(const AssetServer&) = delete;
    AssetServer& operator=(AssetServer&&)      = delete;
    ~AssetServer()                             = default;
    /**
     * @brief register an loader to the asset server.
     */
    template <AssetLoader T>
    void register_loader(const T& t) {
        asset_loaders.push(t);
        size_t size = pending_loads.size();
        while (size) {
            auto id = pending_loads.front();
            pending_loads.pop_front();
            load_internal(id);  // Try to load the pending asset
            --size;             // Decrease the size of pending loads
        }
    }
    /**
     * @brief register an asset type to the asset server.
     * This will copy the HandleProvider ptr from the Assets<T> resource
     *
     * @tparam T
     * @param assets
     */
    template <typename T>
    void register_assets(const Assets<T>& assets) {
        auto type = std::type_index(typeid(T));
        if (asset_infos.handle_providers.contains(type)) {
            return;  // already registered
        }
        asset_infos.handle_providers[type] = assets.get_handle_provider();
    }
    std::optional<LoadState> get_state(const UntypedAssetId& id) const {
        return asset_infos.get_info(id).transform([](const AssetInfo& info) {
            return info.state;
        });  // Return the state of the asset if it exists
    }
    void load_internal(const UntypedAssetId& id) {
        if (auto opt = asset_infos.get_info(id);
            opt && (opt->state == LoadState::Pending ||
                    opt->state == LoadState::Failed)) {
            auto& info = *opt;
            info.state = LoadState::Loading;  // Set the state to Loading
            // get by ext first cause same type can have different loaders, we
            // want it to be the most suitable loader for the asset if no loader
            // found by extension, try to get by type, but this may cause errors
            // resulting in AssetLoadFailedEvent
            auto loader = asset_loaders.get_by_path(info.path);
            if (!loader) {
                // If no loader found by path, try to get by type
                loader = asset_loaders.get_by_type(id.type);
            }
            // check loader type
            if (loader && loader->asset_type() != id.type) {
                // If the loader type does not match the asset type, we cannot
                // use this loader
                loader = nullptr;
            }
            if (loader) {
                LoadContext context{*this, info.path};
                info.waiter = std::async(
                    std::launch::async,
                    [this, loader, id, context]() {
                        try {
                            auto asset = loader->load(context.path, context);
                            if (asset.value) {
                                event_sender.send(
                                    AssetLoadedEvent{id, std::move(asset)}
                                );
                            } else {
                                event_sender.send(AssetLoadFailedEvent{
                                    id, "Failed to load asset"
                                });
                            }
                        } catch (const std::exception& e) {
                            event_sender.send(AssetLoadFailedEvent{id, e.what()}
                            );
                        }
                    }
                );
            } else {
                // No loader found for the asset type, we will keep it pending
                // and try to load it later
                pending_loads.push_back(id);
                info.state = LoadState::Pending;  // Keep the state as Pending
            }
        }
    }
    template <typename T>
    Handle<T> load(const std::filesystem::path& path) {}
    UntypedHandle load_untyped(const std::filesystem::path& path) {}

   private:
    AssetInfos asset_infos;
    AssetLoaders asset_loaders;
    using EventSender   = epix::utils::async::Sender<InternalAssetEvent>;
    using EventReceiver = epix::utils::async::Receiver<InternalAssetEvent>;
    EventSender event_sender;
    EventReceiver event_receiver;
    std::deque<UntypedAssetId> pending_loads;  // Assets that are pending to be
                                               // loaded but no loaders found
};
}  // namespace epix::assets