#pragma once

#include <epix/app.h>

#include <concepts>
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
    { *T::extensions().begin() } -> std::convertible_to<const char*>;
    // a static load function which takes a path and a LoadContext
    {
        T::load(
            std::declval<const std::filesystem::path&>(),
            std::declval<LoadContext&>()
        )
    };
};
template <AssetLoader T>
struct LoaderInfo {
    using load_return_type           = decltype(T::load(
        std::declval<const std::filesystem::path&>(),
        std::declval<LoadContext&>()
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
struct AssetContainerImpl : AssetContainer {
    using asset_type = T;
    T asset;
    AssetContainerImpl(const T& asset) : asset(asset) {}
    AssetContainerImpl(T&& asset) : asset(std::move(asset)) {}
    ~AssetContainerImpl() override = default;
    std::type_index type() const override { return typeid(T); }
    void insert(const UntypedAssetId& id, World& world) override {
        world.resource<Assets<T>>().insert(id.typed<T>(), std::move(asset));
        world.resource<epix::app::Events<AssetEvent<T>>>().push(
            AssetEvent<T>::loaded(id.typed<T>())
        );
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
        world.resource<epix::app::Events<AssetEvent<T>>>().push(
            AssetEvent<T>::loaded(id.typed<T>())
        );
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
        const std::filesystem::path& path, LoadContext& context
    ) const                                             = 0;
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
        const std::filesystem::path& path, LoadContext& context
    ) const override {
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
            return ErasedLoadedAsset{std::make_unique<
                AssetContainerImpl<typename loader_info::asset_type>>(
                T::load(path, context)
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
template <typename Func>
    requires std::invocable<Func, const std::filesystem::path&, LoadContext&>
struct ErasedAssetLoaderFuncImpl : ErasedAssetLoader {
    using asset_t = std::remove_pointer_t<
        std::invoke_result_t<Func, const std::filesystem::path&, LoadContext&>>;
    static constexpr bool return_ptr = std::is_pointer_v<
        std::invoke_result_t<Func, const std::filesystem::path&, LoadContext&>>;
    using FuncStorage = std::decay_t<Func>;
    FuncStorage func;  // Store the function
    std::vector<const char*> _extensions;
    ErasedAssetLoaderFuncImpl(
        Func&& func, epix::util::ArrayProxy<const char*> extensions
    )
        : func(std::forward<Func>(func)),
          _extensions(extensions.begin(), extensions.end()) {}
    std::type_index asset_type() const override { return typeid(asset_t); }
    std::type_index loader_type() const override { return typeid(Func); }
    ErasedLoadedAsset load(
        const std::filesystem::path& path, LoadContext& context
    ) const override {
        if constexpr (std::is_pointer_v<typename Func::asset_type>) {
            auto asset = func(path, context);
            if (asset) {
                return ErasedLoadedAsset{std::make_unique<
                    AssetContainerImpl<typename Func::asset_type>>(asset)};
            } else {
                return ErasedLoadedAsset{nullptr};
            }
        } else {
            auto asset = func(path, context);
            return ErasedLoadedAsset{
                std::make_unique<AssetContainerImpl<asset_t>>(std::move(asset))
            };
        }
    }
    std::vector<const char*> extensions() const override { return _extensions; }
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
    EPIX_API const AssetInfo* get_info(const UntypedAssetId& id) const;
    EPIX_API AssetInfo* get_info(const UntypedAssetId& id);
    EPIX_API std::optional<UntypedHandle> get_or_create_handle_internal(
        const std::filesystem::path& path,
        const std::optional<std::type_index>& type,
        bool force_new = false
    );
    EPIX_API std::optional<UntypedHandle> get_or_create_handle_untyped(
        const std::filesystem::path& path,
        const std::type_index& type,
        bool force_new = false
    );
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
    /**
     * @brief Process handle destruction of asset with the given id.
     *
     * If this asset is re-referenced by another handle after all other handles
     * has destructed, through load functions in AssetServer, this will return
     * true, otherwise false.
     *
     * @param id The id of the asset to process.
     */
    EPIX_API bool process_handle_destruction(const UntypedAssetId& id);
};
struct AssetLoaders {
   private:
    std::vector<std::unique_ptr<ErasedAssetLoader>> loaders;
    entt::dense_map<std::type_index, std::vector<uint32_t>> type_to_loaders;
    entt::dense_map<std::string, std::vector<uint32_t>> ext_to_loaders;

   public:
    EPIX_API const ErasedAssetLoader* get_by_index(uint32_t index) const;
    EPIX_API const ErasedAssetLoader* get_by_type(const std::type_index& type
    ) const;
    EPIX_API std::vector<const ErasedAssetLoader*> get_multi_by_type(
        const std::type_index& type
    ) const;
    EPIX_API const ErasedAssetLoader* get_by_extension(
        const std::string_view& ext
    ) const;
    EPIX_API std::vector<const ErasedAssetLoader*> get_multi_by_extension(
        const std::string_view& ext
    ) const;
    // get by path is a wrapper method for get_by_extension, but much easier to
    // use
    EPIX_API const ErasedAssetLoader* get_by_path(
        const std::filesystem::path& path
    ) const;
    EPIX_API std::vector<const ErasedAssetLoader*> get_multi_by_path(
        const std::filesystem::path& path
    ) const;
    template <AssetLoader T>
    uint32_t push(const T&) {
        using loader_info = LoaderInfo<T>;
        std::unique_ptr<ErasedAssetLoader> loader =
            std::make_unique<ErasedAssetLoaderImpl<T>>();
        auto type      = loader->asset_type();
        auto ext       = loader->extensions();
        uint32_t index = static_cast<uint32_t>(loaders.size());
        loaders.emplace_back(std::move(loader));
        type_to_loaders[type].push_back(index);
        for (const char* e : ext) {
            ext_to_loaders[e].push_back(index);
        }
        return index;  // Return the index of the newly added loader
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
    EPIX_API AssetServer();
    AssetServer(const AssetServer&)            = delete;
    AssetServer(AssetServer&&)                 = delete;
    AssetServer& operator=(const AssetServer&) = delete;
    AssetServer& operator=(AssetServer&&)      = delete;
    EPIX_API ~AssetServer();
    /**
     * @brief register an loader to the asset server.
     */
    template <AssetLoader T>
    uint32_t register_loader(const T& t) {
        auto index = asset_loaders.push(t);
        std::scoped_lock lock(pending_mutex, info_mutex);
        size_t size = pending_loads.size();
        while (size) {
            auto id = pending_loads.front();
            pending_loads.pop_front();
            load_internal(id);  // Try to load the pending asset
            --size;             // Decrease the size of pending loads
        }
        return index;  // Return the index of the newly added loader
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
    EPIX_API std::optional<LoadState> get_state(const UntypedAssetId& id) const;
    EPIX_API void load_internal(
        const UntypedAssetId& id, const ErasedAssetLoader* loader = nullptr
    ) const;
    template <typename T>
    Handle<T> load(const std::filesystem::path& path) const {
        std::scoped_lock lock(info_mutex, pending_mutex);
        auto handle = asset_infos.get_or_create_handle<T>(path);
        if (handle) {
            auto id = handle->id();
            load_internal(id);  // Load the asset internally
            return *handle;  // Return the handle if it was created successfully
        }
        return Handle<T>();  // Return an empty handle if creation failed
    }
    EPIX_API UntypedHandle load_untyped(const std::filesystem::path& path
    ) const;
    EPIX_API bool process_handle_destruction(const UntypedAssetId& id) const;
    EPIX_API static void handle_events(
        World& world, Res<AssetServer> asset_server
    );

   private:
    mutable AssetInfos asset_infos;
    mutable std::mutex info_mutex;
    AssetLoaders asset_loaders;
    using EventSender   = epix::utils::async::Sender<InternalAssetEvent>;
    using EventReceiver = epix::utils::async::Receiver<InternalAssetEvent>;
    EventSender event_sender;
    EventReceiver event_receiver;
    mutable std::mutex pending_mutex;
    mutable std::deque<UntypedAssetId>
        pending_loads;  // Assets that are pending to be
                        // loaded but no loaders found
};

template <typename T>
    requires std::move_constructible<T> && std::is_move_assignable_v<T>
void Assets<T>::handle_events_internal(const AssetServer* asset_server) {
    spdlog::trace("[{}] Handling events", typeid(*this).name());
    while (
        auto&& opt =
            m_handle_provider->index_allocator.reserved_receiver().try_receive()
    ) {
        m_assets.resize_slots(opt->index);
    }
    while (auto&& opt = m_handle_provider->event_receiver.try_receive()) {
        auto id = (*opt).id.template typed<T>();
        if (asset_server && asset_server->process_handle_destruction(id)) {
            continue;
        }
        release(id);
    }
    spdlog::trace("[{}] Finished handling events", typeid(*this).name());
}
}  // namespace epix::assets