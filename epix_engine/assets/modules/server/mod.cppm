module;

#include <spdlog/spdlog.h>

export module epix.assets:server;

import std;
import epix.meta;
import epix.utils;

import :server.info;
import :server.loader;
import :server.loaders;

import :meta;
import :io.source;

namespace epix::assets {
/** @brief Operational mode of the asset server. */
export enum class AssetServerMode {
    Unprocessed, /**< Assets are loaded directly without preprocessing. */
    Processed,   /**< Assets go through a processing pipeline before use. */
};

struct AssetServerData {
    utils::RwLock<AssetInfos> infos;
    std::shared_ptr<utils::RwLock<AssetLoaders>> loaders;
    utils::Sender<InternalAssetEvent> asset_event_sender;
    utils::Receiver<InternalAssetEvent> asset_event_receiver;
    std::shared_ptr<AssetSources> sources;
    AssetServerMode mode                    = AssetServerMode::Unprocessed;
    bool watching_for_changes_flag          = false;
    AssetMetaCheck meta_check               = AssetMetaCheck::Always;
    UnapprovedPathMode unapproved_path_mode = UnapprovedPathMode::Forbid;
};

/** @brief Central server that manages asset loading, tracking, and lifecycle.
 *  Thread-safe: all accesses go through internal RwLock guards. */
export struct AssetServer {
    std::shared_ptr<AssetServerData> data;

    AssetServer()                              = default;
    AssetServer(const AssetServer&)            = default;
    AssetServer(AssetServer&&)                 = default;
    AssetServer& operator=(const AssetServer&) = default;
    AssetServer& operator=(AssetServer&&)      = default;

    /** @brief Construct an asset server with the given sources and mode. */
    AssetServer(std::shared_ptr<AssetSources> sources,
                AssetServerMode mode      = AssetServerMode::Unprocessed,
                bool watching_for_changes = false);

    /** @brief Construct with meta check policy.
     *  Matches bevy_asset's AssetServer::new_with_meta_check. */
    AssetServer(std::shared_ptr<AssetSources> sources,
                AssetServerMode mode,
                AssetMetaCheck meta_check,
                bool watching_for_changes,
                UnapprovedPathMode unapproved_path_mode = UnapprovedPathMode::Forbid);

    /** @brief Construct with shared loaders (for sharing loaders between processor and main server).
     *  Matches bevy_asset's AssetServer::new_with_loaders. */
    AssetServer(std::shared_ptr<AssetSources> sources,
                std::shared_ptr<utils::RwLock<AssetLoaders>> loaders,
                AssetServerMode mode,
                AssetMetaCheck meta_check,
                bool watching_for_changes,
                UnapprovedPathMode unapproved_path_mode = UnapprovedPathMode::Forbid);

    // ---- Loader Registration ----

    /** @brief Register an asset loader. */
    template <AssetLoader L>
    void register_loader(const L& loader) const {
        auto guard = data->loaders->write();
        guard->push(loader);
    }

    /** @brief Register a handle provider and event senders for the given asset type.
     *  Matches bevy_asset's AssetServer::register_asset. */
    template <std::movable A>
    void register_assets(const Assets<A>& assets) const {
        auto guard = data->infos.write();
        guard->handle_providers.emplace(meta::type_id<A>{}, assets.get_handle_provider());

        guard->dependency_loaded_event_sender.emplace(
            meta::type_id<A>{}, +[](core::World& world, AssetIndex index) {
                auto events_opt = world.get_resource_mut<core::Events<AssetEvent<A>>>();
                if (events_opt) {
                    events_opt->get().push(AssetEvent<A>::loaded_with_dependencies(AssetId<A>(index)));
                }
            });
        guard->dependency_failed_event_sender.emplace(
            meta::type_id<A>{}, +[](core::World& world, AssetIndex index, AssetPath path, AssetLoadError error) {
                auto events_opt = world.get_resource_mut<core::Events<AssetLoadFailedEvent<A>>>();
                if (events_opt) {
                    events_opt->get().push(AssetLoadFailedEvent<A>{
                        AssetId<A>(index), std::move(path),
                        std::visit(utils::visitor{
                                       [](const load_error::RequestHandleMismatch& e)
                                           -> std::variant<std::string, std::exception_ptr> {
                                           return std::string("Request handle type mismatch for ") + e.path.string();
                                       },
                                       [](const load_error::MissingAssetLoader& e)
                                           -> std::variant<std::string, std::exception_ptr> {
                                           return std::string("Missing asset loader for ") + e.path.string();
                                       },
                                       [](const load_error::AssetLoaderException& e)
                                           -> std::variant<std::string, std::exception_ptr> { return e.exception; },
                                   },
                                   error)});
                }
            });
    }

    /** @brief Pre-register a loader type with specific extensions before the loader is available. */
    template <AssetLoader L>
    void preregister_loader(std::span<std::string_view> extensions) const {
        auto guard = data->loaders->write();
        guard->template reserve<L>(extensions);
    }

    // ---- Loading (typed) ----

    /** @brief Load an asset from a path. Returns a handle immediately; loading happens asynchronously.
     *  If the asset is already loaded or loading, returns the existing handle. */
    template <typename A>
    Handle<A> load(const AssetPath& path) const {
        return load_with_meta_transform<A>(path, std::nullopt, false);
    }
    /** @brief Load an asset, overriding any existing load for that path (force reload). */
    template <typename A>
    Handle<A> load_override(const AssetPath& path) const {
        return load_with_meta_transform<A>(path, std::nullopt, true);
    }

    /** @brief Load an asset with loader-specific settings.
     *  @tparam A  Asset type.
     *  @tparam S  Settings type derived from assets::Settings.
     *  @param path     Asset path.
     *  @param settings Function that mutates the loader settings. */
    template <typename A, typename S>
        requires std::derived_from<S, Settings>
    Handle<A> load_with_settings(const AssetPath& path, std::function<void(S&)> settings) const {
        return load_with_meta_transform<A>(path, loader_settings_meta_transform<S>(std::move(settings)), false);
    }

    /** @brief Load an asset with settings, overriding any existing load (force). */
    template <typename A, typename S>
        requires std::derived_from<S, Settings>
    Handle<A> load_with_settings_override(const AssetPath& path, std::function<void(S&)> settings) const {
        return load_with_meta_transform<A>(path, loader_settings_meta_transform<S>(std::move(settings)), true);
    }

    /** @brief Core typed load function with optional meta transform.
     *  All typed load methods delegate to this.
     *  Matches bevy_asset's AssetServer::load_with_meta_transform. */
    template <typename A>
    Handle<A> load_with_meta_transform(const AssetPath& path,
                                       std::optional<MetaTransform> meta_transform,
                                       bool force) const {
        auto mode                  = force ? HandleLoadingMode::Force : HandleLoadingMode::Request;
        auto guard                 = data->infos.write();
        auto [handle, should_load] = guard->template get_or_create_handle<A>(path, mode, std::move(meta_transform));
        if (should_load) {
            spawn_load_task(handle.untyped(), path, *guard);
        }
        return handle;
    }

    // ---- Loading (untyped) ----

    /** @brief Load an asset by path, using the registered loader's type inferred from extension. */
    UntypedHandle load_untyped(const AssetPath& path) const {
        std::optional<meta::type_index> loader_type;
        {
            auto loaders_guard = data->loaders->read();
            auto maybe         = loaders_guard->get_by_path(path.path);
            if (maybe) {
                auto loader = maybe->get();
                if (loader) loader_type = loader->asset_type();
            }
        }
        auto guard                 = data->infos.write();
        auto [handle, should_load] = guard->get_or_create_handle_untyped(path, loader_type, HandleLoadingMode::Request);
        if (should_load) {
            spawn_load_task(handle, path, *guard);
        }
        return handle;
    }

    /** @brief Load an asset by explicit type id and path. */
    UntypedHandle load_erased(meta::type_index type_id, const AssetPath& path) const {
        auto guard                 = data->infos.write();
        auto [handle, should_load] = guard->get_or_create_handle_untyped(path, type_id, HandleLoadingMode::Request);
        if (should_load) {
            spawn_load_task(handle, path, *guard);
        }
        return handle;
    }

    // ---- Folder Loading ----

    /** @brief Load all assets in a folder. Returns a handle to a LoadedFolder.
     *  Matches bevy_asset's AssetServer::load_folder. */
    Handle<LoadedFolder> load_folder(const AssetPath& path) const {
        auto guard = data->infos.write();
        auto [handle, should_load] =
            guard->template get_or_create_handle<LoadedFolder>(path, HandleLoadingMode::Request);
        if (!should_load) return handle;
        load_folder_internal(handle.id(), path);
        return handle;
    }

    // ---- Blocking / Acquire Loading ----

    /** @brief Load an asset and block until it is fully loaded.
     *  Matches bevy_asset's AssetServer::load_acquire.
     *  @warning Blocks the calling thread. Do not call from the main thread
     *           if the asset system runs on the same thread. */
    template <typename A>
    Handle<A> load_acquire(const AssetPath& path) const {
        auto handle = load<A>(path);
        while (!is_loaded(handle.id())) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return handle;
    }

    /** @brief Load an asset with settings and block until it is fully loaded.
     *  Matches bevy_asset's AssetServer::load_acquire_with_settings. */
    template <typename A, typename S>
        requires std::derived_from<S, Settings>
    Handle<A> load_acquire_with_settings(const AssetPath& path, std::function<void(S&)> settings) const {
        auto handle = load_with_settings<A, S>(path, std::move(settings));
        while (!is_loaded(handle.id())) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return handle;
    }

    // ---- Reload ----

    /** @brief Force reload an asset at the given path.
     *  Matches bevy_asset's AssetServer::reload / reload_internal. */
    void reload(const AssetPath& path) const {
        std::vector<UntypedHandle> handles_to_reload;
        {
            auto guard = data->infos.write();
            for (auto id : guard->get_path_ids(path)) {
                auto info = guard->get_info_mut(id);
                if (info) {
                    info->get().state         = LoadStateOK::Loading;
                    info->get().dep_state     = LoadStateOK::Loading;
                    info->get().rec_dep_state = LoadStateOK::Loading;
                    auto handle               = guard->get_handle_by_id(id);
                    if (handle) {
                        handles_to_reload.push_back(*handle);
                    }
                }
            }
        }
        for (auto& handle : handles_to_reload) {
            spawn_load_task(handle, path);
        }
    }

    // ---- Direct Asset Insertion ----

    /** @brief Add a non-file asset directly and return a handle to it.
     *  The asset is not associated with any path. */
    template <typename A>
    Handle<A> add(A asset) const;

    // ---- Load State Queries ----

    /** @brief Get all three load states (self, dependency, recursive dependency) at once. */
    std::optional<std::tuple<LoadState, LoadState, LoadState>> get_load_states(const UntypedAssetId& id) const {
        auto guard = data->infos.read();
        auto info  = guard->get_info(id);
        if (!info) return std::nullopt;
        return std::make_tuple(info->get().state, info->get().dep_state, info->get().rec_dep_state);
    }

    /** @brief Get the load state of an asset (self only). */
    std::optional<LoadState> get_load_state(const UntypedAssetId& id) const {
        auto guard = data->infos.read();
        auto info  = guard->get_info(id);
        if (!info) return std::nullopt;
        return info->get().state;
    }
    /** @brief Get the load state, returning NotLoaded if not tracked. */
    LoadState load_state(const UntypedAssetId& id) const {
        return get_load_state(id).value_or(LoadState{LoadStateOK::NotLoaded});
    }
    /** @brief Get the direct dependency load state. */
    std::optional<LoadState> get_dependency_load_state(const UntypedAssetId& id) const {
        auto guard = data->infos.read();
        auto info  = guard->get_info(id);
        if (!info) return std::nullopt;
        return info->get().dep_state;
    }
    /** @brief Get the dependency load state, returning NotLoaded if not tracked. */
    LoadState dependency_load_state(const UntypedAssetId& id) const {
        return get_dependency_load_state(id).value_or(LoadState{LoadStateOK::NotLoaded});
    }
    /** @brief Get the recursive dependency load state. */
    std::optional<LoadState> get_recursive_dependency_load_state(const UntypedAssetId& id) const {
        auto guard = data->infos.read();
        auto info  = guard->get_info(id);
        if (!info) return std::nullopt;
        return info->get().rec_dep_state;
    }
    /** @brief Get the recursive dependency load state, returning NotLoaded if not tracked. */
    LoadState recursive_dependency_load_state(const UntypedAssetId& id) const {
        return get_recursive_dependency_load_state(id).value_or(LoadState{LoadStateOK::NotLoaded});
    }
    /** @brief Check if the asset is fully loaded (self only). */
    bool is_loaded(const UntypedAssetId& id) const {
        auto state = get_load_state(id);
        return state && std::holds_alternative<LoadStateOK>(*state) &&
               std::get<LoadStateOK>(*state) == LoadStateOK::Loaded;
    }
    /** @brief Check if the asset and all its direct dependencies are loaded. */
    bool is_loaded_with_direct_dependencies(const UntypedAssetId& id) const {
        return is_loaded(id) && [&] {
            auto dep = get_dependency_load_state(id);
            return dep && std::holds_alternative<LoadStateOK>(*dep) &&
                   std::get<LoadStateOK>(*dep) == LoadStateOK::Loaded;
        }();
    }
    /** @brief Check if the asset and all its recursive dependencies are loaded. */
    bool is_loaded_with_dependencies(const UntypedAssetId& id) const {
        return is_loaded(id) && [&] {
            auto rec = get_recursive_dependency_load_state(id);
            return rec && std::holds_alternative<LoadStateOK>(*rec) &&
                   std::get<LoadStateOK>(*rec) == LoadStateOK::Loaded;
        }();
    }

    // ---- Handle Queries ----

    /** @brief Get a typed handle for an asset at the given path, if one exists. */
    template <typename A>
    std::optional<Handle<A>> get_handle(const AssetPath& path) const {
        auto guard  = data->infos.read();
        auto handle = guard->get_handle_by_path_type(path, meta::type_id<A>{});
        if (!handle) return std::nullopt;
        return handle->template try_typed<A>();
    }
    /** @brief Get an untyped handle for an asset at the given path, if one exists. */
    std::optional<UntypedHandle> get_handle_untyped(const AssetPath& path) const {
        auto guard = data->infos.read();
        auto ids   = guard->get_path_ids(path);
        for (auto id : ids) {
            auto handle = guard->get_handle_by_id(id);
            if (handle) return handle;
        }
        return std::nullopt;
    }
    /** @brief Get all untyped handles for assets at the given path.
     *  Matches bevy_asset's AssetServer::get_handles_untyped. */
    std::vector<UntypedHandle> get_handles_untyped(const AssetPath& path) const {
        std::vector<UntypedHandle> result;
        auto guard = data->infos.read();
        auto ids   = guard->get_path_ids(path);
        for (auto id : ids) {
            auto handle = guard->get_handle_by_id(id);
            if (handle) result.push_back(*handle);
        }
        return result;
    }
    /** @brief Get a handle by its typed AssetId, if tracked. */
    template <typename A>
    std::optional<Handle<A>> get_id_handle(const AssetId<A>& id) const {
        auto guard  = data->infos.read();
        auto handle = guard->get_handle_by_id(UntypedAssetId(id));
        if (!handle) return std::nullopt;
        return handle->template try_typed<A>();
    }
    /** @brief Get a handle by UntypedAssetId, if tracked. */
    std::optional<UntypedHandle> get_id_handle_untyped(const UntypedAssetId& id) const {
        auto guard = data->infos.read();
        return guard->get_handle_by_id(id);
    }
    /** @brief Get a handle by path and type_id combined lookup.
     *  Matches bevy_asset's AssetServer::get_path_and_type_id_handle. */
    std::optional<UntypedHandle> get_path_and_type_id_handle(const AssetPath& path, meta::type_index type_id) const {
        auto guard = data->infos.read();
        return guard->get_handle_by_path_type(path, type_id);
    }

    // ---- Path & Info Queries ----

    /** @brief Get the path associated with an asset id, if known. */
    std::optional<AssetPath> get_path(const UntypedAssetId& id) const {
        auto guard = data->infos.read();
        auto info  = guard->get_info(id);
        if (!info) return std::nullopt;
        return info->get().path;
    }
    /** @brief Get a single asset id for a path (first match), if any.
     *  Matches bevy_asset's AssetServer::get_path_id. */
    std::optional<UntypedAssetId> get_path_id(const AssetPath& path) const {
        auto guard = data->infos.read();
        auto ids   = guard->get_path_ids(path);
        for (auto id : ids) return id;
        return std::nullopt;
    }
    /** @brief Get all asset ids for a path.
     *  Matches bevy_asset's AssetServer::get_path_ids. */
    std::vector<UntypedAssetId> get_path_ids(const AssetPath& path) const {
        auto guard = data->infos.read();
        std::vector<UntypedAssetId> result;
        for (auto id : guard->get_path_ids(path)) {
            result.push_back(id);
        }
        return result;
    }
    /** @brief Check if an asset id is managed by this server. */
    bool is_managed(const UntypedAssetId& id) const {
        auto guard = data->infos.read();
        return guard->contains_key(id);
    }
    /** @brief Get the current server mode (Unprocessed or Processed). */
    AssetServerMode mode() const { return data->mode; }
    /** @brief Check if the server is watching for file changes. */
    bool watching_for_changes() const { return data->watching_for_changes_flag; }

    /** @brief Get an asset source by id. Returns std::nullopt if not found. */
    std::optional<std::reference_wrapper<const AssetSource>> get_source(const AssetSourceId& source_id) const {
        if (!data->sources) return std::nullopt;
        return data->sources->get(source_id);
    }

    // ---- Loader Queries ----

    /** @brief Get a loader registered for the given file extension.
     *  Matches bevy_asset's AssetServer::get_asset_loader_with_extension. */
    std::shared_ptr<ErasedAssetLoader> get_asset_loader_with_extension(std::string_view extension) const {
        auto guard = data->loaders->read();
        auto maybe = guard->get_by_extension(extension);
        if (!maybe) return nullptr;
        return maybe->get();
    }

    /** @brief Get a loader registered under the given type name.
     *  Matches bevy_asset's AssetServer::get_asset_loader_with_type_name. */
    std::shared_ptr<ErasedAssetLoader> get_asset_loader_with_type_name(std::string_view type_name) const {
        auto guard = data->loaders->read();
        auto maybe = guard->get_by_name(type_name);
        if (!maybe) return nullptr;
        return maybe->get();
    }

    /** @brief Get a loader that would handle the given path (based on extension).
     *  Matches bevy_asset's AssetServer::get_path_asset_loader. */
    std::shared_ptr<ErasedAssetLoader> get_path_asset_loader(const AssetPath& path) const {
        auto guard = data->loaders->read();
        auto maybe = guard->get_by_path(path.path);
        if (!maybe) return nullptr;
        return maybe->get();
    }

    /** @brief Get a loader registered for the given asset type.
     *  Matches bevy_asset's AssetServer::get_asset_loader_with_asset_type_id. */
    std::shared_ptr<ErasedAssetLoader> get_asset_loader_with_asset_type_id(meta::type_index type_id) const {
        auto guard = data->loaders->read();
        auto maybe = guard->get_by_type(type_id);
        if (!maybe) return nullptr;
        return maybe->get();
    }

    /** @brief Get a loader for asset type A.
     *  Matches bevy_asset's AssetServer::get_asset_loader_with_asset_type. */
    template <typename A>
    std::shared_ptr<ErasedAssetLoader> get_asset_loader_with_asset_type() const {
        return get_asset_loader_with_asset_type_id(meta::type_id<A>{});
    }

    // ---- Internal ----

    /** @brief Process handle destruction across the info table.
     *  @return true if the asset should be removed from its collection. */
    bool process_handle_destruction(const UntypedAssetId& id) const {
        auto guard = data->infos.write();
        return guard->process_handle_destruction(id);
    }

    /** @brief Process internal asset events (called from system). */
    static void handle_internal_events(core::ParamSet<core::World&, core::Res<AssetServer>> params);

   private:
    /** @brief Spawn an IO task to load an asset. Takes the handle (which carries meta_transform)
     *  and the infos write guard for pending_tasks tracking.
     *  Matches bevy_asset's AssetServer::spawn_load_task. */
    void spawn_load_task(const UntypedHandle& handle, const AssetPath& path, AssetInfos& infos) const;
    /** @brief Overload without infos guard, for reload path. */
    void spawn_load_task(const UntypedHandle& handle, const AssetPath& path) const;
    void load_folder_internal(const UntypedAssetId& id, const AssetPath& path) const;

    /** @brief Send an internal asset event. */
    void send_asset_event(InternalAssetEvent event) const { data->asset_event_sender.send(std::move(event)); }

    /** @brief Helper to create a LoadContext. Defined here in the module interface
     *  where AssetServer is complete, working around MSVC C++20 modules bug
     *  where forward-declared and fully-defined types are treated as mismatched. */
    static LoadContext make_load_context(const AssetServer& s, AssetPath p) { return LoadContext(s, std::move(p)); }
};
}  // namespace assets