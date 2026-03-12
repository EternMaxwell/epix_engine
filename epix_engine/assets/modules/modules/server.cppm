module;

#include <spdlog/spdlog.h>

export module epix.assets:server;

import std;

import :store;

using namespace core;

namespace assets {
/** @brief Forward declaration. */
export struct AssetServer;

/** @brief Context passed to asset loaders during load.
 *  Provides the server reference and the resolved file path. */
export struct LoadContext {
    /** @brief The asset server that initiated this load. */
    const AssetServer& server;
    /** @brief The resolved file path of the asset being loaded. */
    std::filesystem::path path;
};
template <typename T>
concept AssetLoader = requires(T t) {
    // need a static function which returns a viewable range of const char*
    // indicating the supported file extensions
    { T::extensions() } -> std::ranges::viewable_range;
    { *T::extensions().begin() } -> std::convertible_to<const char*>;
    // a static load function which takes a path and a LoadContext
    { T::load(std::declval<const std::filesystem::path&>(), std::declval<LoadContext&>()) };
};
template <AssetLoader T>
struct LoaderInfo {
    using load_return_type =
        decltype(T::load(std::declval<const std::filesystem::path&>(), std::declval<LoadContext&>()));
    using asset_type                 = std::remove_pointer_t<load_return_type>;
    static constexpr bool return_ptr = std::is_pointer_v<load_return_type>;
};
struct AssetContainer {
    virtual ~AssetContainer()                                                                              = default;
    virtual meta::type_index type() const                                                                  = 0;
    virtual void insert(const UntypedAssetId& id, World& world, const std::function<void(void*)>& pre_mod) = 0;
};
template <typename T>
struct AssetContainerImpl : AssetContainer {
    using asset_type = T;
    T asset;
    AssetContainerImpl(const T& asset) : asset(asset) {}
    AssetContainerImpl(T&& asset) : asset(std::move(asset)) {}
    ~AssetContainerImpl() override = default;
    meta::type_index type() const override { return meta::type_id<T>{}; }
    void insert(const UntypedAssetId& id, World& world, const std::function<void(void*)>& pre_mod) override {
        if (pre_mod) {
            pre_mod(&asset);
        }
        world.resource_mut<Assets<T>>().insert(id.typed<T>(), std::move(asset));
        world.resource_mut<Events<AssetEvent<T>>>().push(AssetEvent<T>::loaded(id.typed<T>()));
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
    meta::type_index type() const override { return meta::type_id<T>{}; }
    void insert(const UntypedAssetId& id, World& world, const std::function<void(void*)>& pre_mod) override {
        if (pre_mod) {
            pre_mod(asset);
        }
        world.resource<Assets<T>>().insert(id.typed<T>(), std::move(*asset));
        asset = nullptr;  // Prevent double deletion
        world.resource<Events<AssetEvent<T>>>().push(AssetEvent<T>::loaded(id.typed<T>()));
    }
};
struct ErasedLoadedAsset {
    std::unique_ptr<AssetContainer> value;
};
struct ErasedAssetLoader {
    virtual ~ErasedAssetLoader()                                                                  = default;
    virtual meta::type_index asset_type() const                                                   = 0;
    virtual meta::type_index loader_type() const                                                  = 0;
    virtual ErasedLoadedAsset load(const std::filesystem::path& path, LoadContext& context) const = 0;
    virtual std::vector<const char*> extensions() const                                           = 0;
};
template <AssetLoader T>
struct ErasedAssetLoaderImpl : ErasedAssetLoader {
    using loader_info = LoaderInfo<T>;
    meta::type_index asset_type() const override { return meta::type_id<typename loader_info::asset_type>{}; }
    meta::type_index loader_type() const override { return meta::type_id<T>{}; }
    ErasedLoadedAsset load(const std::filesystem::path& path, LoadContext& context) const override {
        if constexpr (loader_info::return_ptr) {
            auto asset = T::load(path, context);
            if (asset) {
                return ErasedLoadedAsset{std::make_unique<AssetContainerImpl<typename loader_info::asset_type>>(asset)};
            } else {
                return ErasedLoadedAsset{nullptr};
            }
        } else {
            return ErasedLoadedAsset{
                std::make_unique<AssetContainerImpl<typename loader_info::asset_type>>(T::load(path, context))};
        }
        // this function does not catch exceptions, and the exceptions should be
        // handled by the caller, most likely the AssetServer, to send the
        // related event
    }
    std::vector<const char*> extensions() const override {
        return T::extensions() | std::views::transform([](const char* ext) { return ext; }) |
               std::ranges::to<std::vector>();
    }
};
template <typename Func>
    requires std::invocable<Func, const std::filesystem::path&, LoadContext&>
struct ErasedAssetLoaderFuncImpl : ErasedAssetLoader {
    using asset_t = std::remove_pointer_t<std::invoke_result_t<Func, const std::filesystem::path&, LoadContext&>>;
    static constexpr bool return_ptr =
        std::is_pointer_v<std::invoke_result_t<Func, const std::filesystem::path&, LoadContext&>>;
    using FuncStorage = std::decay_t<Func>;
    FuncStorage func;  // Store the function
    std::vector<const char*> _extensions;
    ErasedAssetLoaderFuncImpl(Func&& func, std::span<const char*> extensions)
        : func(std::forward<Func>(func)), _extensions(extensions.begin(), extensions.end()) {}
    meta::type_index asset_type() const override { return meta::type_id<asset_t>{}; }
    meta::type_index loader_type() const override { return meta::type_id<Func>{}; }
    ErasedLoadedAsset load(const std::filesystem::path& path, LoadContext& context) const override {
        if constexpr (std::is_pointer_v<typename Func::asset_type>) {
            auto asset = func(path, context);
            if (asset) {
                return ErasedLoadedAsset{std::make_unique<AssetContainerImpl<typename Func::asset_type>>(asset)};
            } else {
                return ErasedLoadedAsset{nullptr};
            }
        } else {
            auto asset = func(path, context);
            return ErasedLoadedAsset{std::make_unique<AssetContainerImpl<asset_t>>(std::move(asset))};
        }
    }
    std::vector<const char*> extensions() const override { return _extensions; }
};
/** @brief Current state of an asset's loading lifecycle. */
export enum LoadState {
    Pending, /**< Asset is queued but no loader has picked it up yet. */
    Loading, /**< A loader is actively loading this asset. */
    Loaded,  /**< Asset has been loaded and is ready to use. */
    Failed,  /**< Loading failed; see error details in events. */
};
struct AssetInfo {
    std::weak_ptr<StrongHandle> weak_handle;
    std::filesystem::path path;
    LoadState state;
    std::shared_future<void> waiter;
    std::function<void(void*)> on_loaded;
};
struct AssetInfos {
   private:
    std::unordered_map<std::filesystem::path, std::unordered_map<meta::type_index, UntypedAssetId>> path_to_ids;
    std::unordered_map<UntypedAssetId, AssetInfo> infos;
    std::unordered_map<meta::type_index, std::shared_ptr<HandleProvider>> handle_providers;

    friend struct AssetServer;  // Allow AssetServer to access private members

   public:
    const AssetInfo* get_info(const UntypedAssetId& id) const;
    AssetInfo* get_info(const UntypedAssetId& id);
    std::optional<UntypedHandle> get_or_create_handle_internal(const std::filesystem::path& path,
                                                               const std::optional<meta::type_index>& type,
                                                               bool force_new = false);
    std::optional<UntypedHandle> get_or_create_handle_untyped(const std::filesystem::path& path,
                                                              const meta::type_index& type,
                                                              bool force_new = false);
    template <typename T>
    std::optional<Handle<T>> get_or_create_handle(const std::filesystem::path& path, bool force_new = false) {
        auto type   = meta::type_id<T>{};
        auto handle = get_or_create_handle_internal(path, type, force_new);
        if (handle) {
            return handle->template try_typed<T>();
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
    bool process_handle_destruction(const UntypedAssetId& id);
};
struct AssetLoaders {
   private:
    std::vector<std::unique_ptr<ErasedAssetLoader>> loaders;
    std::unordered_map<meta::type_index, std::vector<std::uint32_t>> type_to_loaders;
    std::unordered_map<std::string, std::vector<std::uint32_t>> ext_to_loaders;

   public:
    const ErasedAssetLoader* get_by_index(std::uint32_t index) const;
    const ErasedAssetLoader* get_by_type(const meta::type_index& type) const;
    std::vector<const ErasedAssetLoader*> get_multi_by_type(const meta::type_index& type) const;
    const ErasedAssetLoader* get_by_extension(const std::string_view& ext) const;
    std::vector<const ErasedAssetLoader*> get_multi_by_extension(const std::string_view& ext) const;
    // get by path is a wrapper method for get_by_extension, but much easier to
    // use
    const ErasedAssetLoader* get_by_path(const std::filesystem::path& path) const;
    std::vector<const ErasedAssetLoader*> get_multi_by_path(const std::filesystem::path& path) const;
    template <AssetLoader T>
    std::uint32_t push(const T&) {
        using loader_info                         = LoaderInfo<T>;
        std::unique_ptr<ErasedAssetLoader> loader = std::make_unique<ErasedAssetLoaderImpl<T>>();
        auto type                                 = loader->asset_type();
        auto ext                                  = loader->extensions();
        std::uint32_t index                       = static_cast<std::uint32_t>(loaders.size());
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
using InternalAssetEvent = std::variant<AssetLoadedEvent,     // asset was loaded successfully
                                        AssetLoadFailedEvent  // asset failed to load
                                        >;
/** @brief Central service that manages asset loading, caching and handle lifecycle.
 *  Non-copyable and non-movable. Thread-safe for concurrent loads. */
export struct AssetServer {
    /** @brief Construct a new AssetServer. */
    AssetServer();
    AssetServer(const AssetServer&)            = delete;
    AssetServer(AssetServer&&)                 = delete;
    AssetServer& operator=(const AssetServer&) = delete;
    AssetServer& operator=(AssetServer&&)      = delete;
    /** @brief Destroy the AssetServer and release all resources. */
    ~AssetServer();
    /** @brief Register an asset loader with the server.
     *  Any assets pending load whose extension matches will be loaded
     *  immediately.
     *  @tparam T A type satisfying the AssetLoader concept.
     *  @return Index of the newly registered loader. */
    template <AssetLoader T>
    std::uint32_t register_loader(const T& t) {
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
    /** @brief Register an asset type so the server can create handles for it.
     *  Copies the HandleProvider from an existing Assets<T> resource.
     *  @tparam T The asset type to register. */
    template <typename T>
    void register_assets(const Assets<T>& assets) {
        auto type = meta::type_id<T>{};
        if (asset_infos.handle_providers.contains(type)) {
            return;  // already registered
        }
        asset_infos.handle_providers[type] = assets.get_handle_provider();
    }
    /** @brief Query the current load state of an asset.
     *  @return The LoadState, or std::nullopt if the id is unknown. */
    std::optional<LoadState> get_state(const UntypedAssetId& id) const;
    /** @brief Internal: dispatch the actual async load for the given asset id.
     * @param id The asset id to load.
     * @param loader Optional specific loader to use; if null, one is resolved by extension. */
    void load_internal(const UntypedAssetId& id, const ErasedAssetLoader* loader = nullptr) const;
    /** @brief Load an asset by path.
     *  Creates or reuses a handle for the given path. The actual load happens
     *  asynchronously; query state with get_state().
     *  @tparam T The expected asset type.
     *  @param path Filesystem path to the asset.
     *  @return A Handle<T>, or std::nullopt if handle creation fails. */
    template <typename T>
    std::optional<Handle<T>> load(const std::filesystem::path& path) const {
        std::scoped_lock lock(info_mutex, pending_mutex);
        auto handle = asset_infos.get_or_create_handle<T>(path);
        if (handle) {
            auto id = handle->id();
            load_internal(id);  // Load the asset internally
        }
        return std::move(handle);
    }
    /** @brief Load an asset with a pre-modification callback.
     *  @tparam T The expected asset type.
     *  @tparam PreMod A callable invoked with `T&` right before the asset is inserted.
     *  @param path Filesystem path to the asset.
     *  @param pre_mod Callback applied to the loaded asset before insertion.
     *  @return A Handle<T>, or std::nullopt if handle creation fails. */
    template <typename T, typename PreMod>
    std::optional<Handle<T>> load(const std::filesystem::path& path, PreMod&& pre_mod) const {
        static_assert(std::is_invocable_v<PreMod, T&>);
        auto handle = load<T>(path);
        if (handle) {
            std::unique_lock lock(info_mutex);
            auto info = asset_infos.get_info(handle.id());
            if (info) {
                info->on_loaded = [pre_mod = std::forward<PreMod>(pre_mod)](void* asset_ptr) {
                    pre_mod(*static_cast<T*>(asset_ptr));
                };
            }
        }
        return std::move(handle);
    }
    /** @brief Load an asset without compile-time type information.
     *  The loader is determined by the file extension.
     *  @param path Filesystem path to the asset.
     *  @return An UntypedHandle, or std::nullopt if no matching loader is found. */
    std::optional<UntypedHandle> load_untyped(const std::filesystem::path& path) const;
    /** @brief Load an untyped asset with a pre-modification callback.
     * @tparam PreMod Callable accepting a reference to the loaded asset type.
     * @param path Filesystem path to the asset.
     * @param pre_mod Callback applied before insertion. */
    template <typename PreMod>
    std::optional<UntypedHandle> load_untyped(const std::filesystem::path& path, PreMod&& pre_mod) const {
        auto handle = load_untyped(path);
        if (handle) {
            std::unique_lock lock(info_mutex);
            auto info = asset_infos.get_info(handle->id());
            if (info) {
                using arg_raw  = function_traits<PreMod>::first_arg_type;
                using arg_type = std::remove_cvref_t<arg_raw>;
                if (handle->type() == meta::type_id<arg_type>{}) {
                    info->on_loaded = [pre_mod = std::forward<PreMod>(pre_mod)](void* asset_ptr) {
                        pre_mod(*static_cast<arg_type*>(asset_ptr));
                    };
                } else {
                    spdlog::warn(
                        "[asset-server] "
                        "PreMod function argument type {} does not match "
                        "asset type {}. Ignoring PreMod.",
                        meta::type_id<arg_type>::name, handle->type().name());
                }
            }
        }
        return handle;
    }
    /** @brief Process a handle destruction event; returns true if the asset was re-acquired. */
    bool process_handle_destruction(const UntypedAssetId& id) const;
    /** @brief System that processes loaded/failed asset events and inserts them into the world. */
    static void handle_events(ParamSet<World&, Res<AssetServer>>);

   private:
    template <typename T>
    struct function_traits;
    // for functions and function pointers
    template <typename Ret, typename... Args>
    struct function_traits<Ret (*)(Args...)> {
        static_assert(sizeof...(Args) == 1, "PreMod function must take exactly one argument.");
        using first_arg_type = std::tuple_element_t<0, std::tuple<Args...>>;
    };
    template <typename Ret, typename... Args>
    struct function_traits<Ret(Args...)> {
        static_assert(sizeof...(Args) == 1, "PreMod function must take exactly one argument.");
        using first_arg_type = std::tuple_element_t<0, std::tuple<Args...>>;
    };
    template <typename Ret, typename... Args>
    struct function_traits<Ret (&)(Args...)> {
        static_assert(sizeof...(Args) == 1, "PreMod function must take exactly one argument.");
        using first_arg_type = std::tuple_element_t<0, std::tuple<Args...>>;
    };
    // for fake functions (std::function, lambdas, etc.)
    template <typename T>
    struct function_traits : function_traits<decltype(&T::operator())> {};

    mutable AssetInfos asset_infos;
    mutable std::mutex info_mutex;
    AssetLoaders asset_loaders;
    using EventSender   = Sender<InternalAssetEvent>;
    using EventReceiver = Receiver<InternalAssetEvent>;
    EventSender event_sender;
    EventReceiver event_receiver;
    mutable std::mutex pending_mutex;
    mutable std::deque<UntypedAssetId> pending_loads;  // Assets that are pending to be
                                                       // loaded but no loaders found
};
}  // namespace assets