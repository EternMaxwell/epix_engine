#pragma once

#include <concepts>
#include <epix/core.hpp>
#include <filesystem>
#include <ranges>

#include "assets.hpp"

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
        world.resource_mut<epix::core::Events<AssetEvent<T>>>().push(AssetEvent<T>::loaded(id.typed<T>()));
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
        world.resource<epix::core::Events<AssetEvent<T>>>().push(AssetEvent<T>::loaded(id.typed<T>()));
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
    std::unordered_map<meta::type_index, std::vector<uint32_t>> type_to_loaders;
    std::unordered_map<std::string, std::vector<uint32_t>> ext_to_loaders;

   public:
    const ErasedAssetLoader* get_by_index(uint32_t index) const;
    const ErasedAssetLoader* get_by_type(const meta::type_index& type) const;
    std::vector<const ErasedAssetLoader*> get_multi_by_type(const meta::type_index& type) const;
    const ErasedAssetLoader* get_by_extension(const std::string_view& ext) const;
    std::vector<const ErasedAssetLoader*> get_multi_by_extension(const std::string_view& ext) const;
    // get by path is a wrapper method for get_by_extension, but much easier to
    // use
    const ErasedAssetLoader* get_by_path(const std::filesystem::path& path) const;
    std::vector<const ErasedAssetLoader*> get_multi_by_path(const std::filesystem::path& path) const;
    template <AssetLoader T>
    uint32_t push(const T&) {
        using loader_info                         = LoaderInfo<T>;
        std::unique_ptr<ErasedAssetLoader> loader = std::make_unique<ErasedAssetLoaderImpl<T>>();
        auto type                                 = loader->asset_type();
        auto ext                                  = loader->extensions();
        uint32_t index                            = static_cast<uint32_t>(loaders.size());
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
struct AssetServer {
    AssetServer();
    AssetServer(const AssetServer&)            = delete;
    AssetServer(AssetServer&&)                 = delete;
    AssetServer& operator=(const AssetServer&) = delete;
    AssetServer& operator=(AssetServer&&)      = delete;
    ~AssetServer();
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
        auto type = meta::type_id<T>{};
        if (asset_infos.handle_providers.contains(type)) {
            return;  // already registered
        }
        asset_infos.handle_providers[type] = assets.get_handle_provider();
    }
    std::optional<LoadState> get_state(const UntypedAssetId& id) const;
    void load_internal(const UntypedAssetId& id, const ErasedAssetLoader* loader = nullptr) const;
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
    std::optional<UntypedHandle> load_untyped(const std::filesystem::path& path) const;
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
    bool process_handle_destruction(const UntypedAssetId& id) const;
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
    using EventSender   = epix::utils::async::Sender<InternalAssetEvent>;
    using EventReceiver = epix::utils::async::Receiver<InternalAssetEvent>;
    EventSender event_sender;
    EventReceiver event_receiver;
    mutable std::mutex pending_mutex;
    mutable std::deque<UntypedAssetId> pending_loads;  // Assets that are pending to be
                                                       // loaded but no loaders found
};

template <typename T>
    requires std::move_constructible<T> && std::is_move_assignable_v<T>
void Assets<T>::handle_events_internal(const AssetServer* asset_server) {
    spdlog::trace("[{}] Handling events", meta::type_id<T>::short_name());
    while (auto&& opt = m_handle_provider->index_allocator.reserved_receiver().try_receive()) {
        m_assets.resize_slots(opt->index());
    }
    while (auto&& opt = m_handle_provider->event_receiver.try_receive()) {
        auto id = (*opt).id.template typed<T>();
        if (asset_server && asset_server->process_handle_destruction(id)) {
            continue;
        }
        release(id);
    }
    spdlog::trace("[{}] Finished handling events", meta::type_id<T>::short_name());
}
}  // namespace epix::assets