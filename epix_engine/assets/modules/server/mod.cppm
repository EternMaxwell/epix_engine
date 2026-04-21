module;

#ifndef EPIX_IMPORT_STD
#include <concepts>
#include <exception>
#include <expected>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
#endif
#include <spdlog/spdlog.h>

#include <asio/awaitable.hpp>

export module epix.assets:server;
#ifdef EPIX_IMPORT_STD
import std;
#endif
import epix.meta;
import epix.utils;

import :server.info;
import :server.loader;
import :server.loaders;

import :meta;
import :io.source;
import :io.reader;

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
    bool watching_for_changes               = false;
    AssetMetaCheck meta_check               = AssetMetaCheck{asset_meta_check::Always{}};
    UnapprovedPathMode unapproved_path_mode = UnapprovedPathMode::Forbid;
    // Set by AssetProcessor when active: returns true if extension has a registered processor.
    // When set and returns false in Processed mode, the source reader is used (no processed copy).
    std::function<bool(std::string_view)> has_processor_for_ext;
};

/** @brief Error returned when attempting to reload an asset from an unrecognised source.
 *  Matches bevy_asset's MissingAssetSourceError. */
export struct MissingAssetSourceError {
    AssetSourceId source_id;
    bool operator==(const MissingAssetSourceError&) const = default;
};

/** @brief Central server that manages asset loading, tracking, and lifecycle.
 *  Thread-safe: all accesses go through internal RwLock guards. */
export struct AssetServer {
   private:
    std::shared_ptr<AssetServerData> data;

    friend struct AssetProcessor;

   public:
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

    /** @brief Get the shared loaders handle (for sharing between processor and main server). */
    const std::shared_ptr<utils::RwLock<AssetLoaders>>& get_loaders() const { return data->loaders; }

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
    void register_asset(const Assets<A>& assets) const {
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
                    events_opt->get().push(
                        AssetLoadFailedEvent<A>{AssetId<A>(index), std::move(path), format_asset_load_error(error)});
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
        return load_with_meta_transform<A>(path, std::nullopt, true, /*override_unapproved=*/true);
    }

    /** @brief Load an asset with loader-specific settings.
     *  @tparam A  Asset type.
     *  @tparam S  Settings aggregate type.
     *  @param path     Asset path.
     *  @param settings Function that mutates the loader settings. */
    template <typename A, typename S>
    Handle<A> load_with_settings(const AssetPath& path, std::function<void(S&)> settings) const {
        return load_with_meta_transform<A>(path, loader_settings_meta_transform<S>(std::move(settings)), false);
    }

    /** @brief Load an asset with settings, overriding any existing load (force). */
    template <typename A, typename S>
    Handle<A> load_with_settings_override(const AssetPath& path, std::function<void(S&)> settings) const {
        return load_with_meta_transform<A>(path, loader_settings_meta_transform<S>(std::move(settings)), true,
                                           /*override_unapproved=*/true);
    }

    /** @brief Core typed load function with optional meta transform.
     *  All typed load methods delegate to this.
     *  Matches bevy_asset's AssetServer::load_with_meta_transform. */
    template <typename A>
    Handle<A> load_with_meta_transform(const AssetPath& path,
                                       std::optional<MetaTransform> meta_transform,
                                       bool force,
                                       bool override_unapproved = false) const {
        // Enforce UnapprovedPathMode when not overriding
        if (!override_unapproved && path.is_unapproved()) {
            if (data->unapproved_path_mode == UnapprovedPathMode::Forbid) {
                throw std::runtime_error("UnapprovedPathMode::Forbid: path \"" + path.string() + "\" is unapproved");
            } else if (data->unapproved_path_mode == UnapprovedPathMode::Deny) {
                spdlog::error("UnapprovedPathMode::Deny: path \"{}\" is unapproved", path.string());
                return Handle<A>(AssetId<A>::invalid());
            }
        }
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
    UntypedHandle load_untyped(const AssetPath& path) const;

    /** @brief Load an asset by explicit type id and path. */
    UntypedHandle load_erased(meta::type_index type_id, const AssetPath& path) const;

    // ---- Folder Loading ----

    /** @brief Load all assets in a folder. Returns a handle to a LoadedFolder.
     *  Matches bevy_asset's AssetServer::load_folder. */
    Handle<LoadedFolder> load_folder(const AssetPath& path) const;

    // ---- Blocking / Acquire Loading ----

    /** @brief Load an asset and block until it is fully loaded (self + all dependencies).
     *  Matches bevy_asset's AssetServer::load_acquire.
     *  @note The Bevy version accepts a guard param to hold Assets<A> read-lock during load;
     *        C++ omits the guard since Assets<T> is not RwLock-protected.
     *  @warning Blocks the calling thread. Do not call from the main thread
     *           if the asset system runs on the same thread. */
    template <typename A>
    Handle<A> load_acquire(const AssetPath& path) const {
        auto handle = load<A>(path);
        wait_for_asset(handle);
        return handle;
    }

    /** @brief Load an asset with settings and block until it is fully loaded.
     *  Matches bevy_asset's AssetServer::load_acquire_with_settings.
     *  @note Guard parameter omitted — see load_acquire note. */
    template <typename A, typename S>
        requires is_settings<S>
    Handle<A> load_acquire_with_settings(const AssetPath& path, std::function<void(S&)> settings) const {
        auto handle = load_with_settings<A, S>(path, std::move(settings));
        wait_for_asset(handle);
        return handle;
    }

    // ---- Reload ----

    /** @brief Force reload an asset at the given path.
     *  Returns an error if the source is not registered.
     *  Matches bevy_asset's AssetServer::reload. */
    std::expected<void, MissingAssetSourceError> reload(const AssetPath& path) const;

    // ---- Direct Asset Insertion ----

    /** @brief Add a non-file asset directly and return a handle to it.
     *  The asset is not associated with any path.
     *  Matches bevy_asset's AssetServer::add. */
    template <typename A>
    Handle<A> add(A asset) const {
        auto erased = ErasedLoadedAsset::from_asset(std::move(asset));
        return load_asset_untyped(std::nullopt, std::move(erased)).template typed<A>();
    }

    /** @brief Add an asset asynchronously. The future is run on the IOTaskPool.
     *  When the future resolves, the asset is inserted via load_asset_untyped.
     *  Matches bevy_asset's AssetServer::add_async.
     *  @tparam A  Asset type.
     *  @tparam E  Error type (must be convertible to std::exception_ptr). */
    template <typename A, typename E>
    Handle<A> add_async(std::function<std::expected<A, E>()> future) const {
        // Create a handle for the not-yet-ready asset
        auto handle = [&] {
            auto guard = data->infos.write();
            return guard->create_loading_handle_untyped(meta::type_id<A>{});
        }();
        auto typed_handle = handle.template typed<A>();
        auto server       = *this;
        auto owned_handle = handle;
        utils::IOTaskPool::instance().detach_task([server, owned_handle, future = std::move(future)]() mutable {
            auto result = future();
            if (result) {
                auto erased = ErasedLoadedAsset::from_asset(std::move(*result));
                server.send_asset_event(
                    InternalAssetEvent{internal_asset_event::Loaded{owned_handle.id(), std::move(erased)}});
            } else {
                auto err_ptr = [&]() -> std::exception_ptr {
                    if constexpr (std::same_as<E, std::exception_ptr>) {
                        return result.error();
                    } else {
                        // Wrap in a generic exception
                        try {
                            throw result.error();
                        } catch (...) {
                            return std::current_exception();
                        }
                    }
                }();
                server.send_asset_event(InternalAssetEvent{internal_asset_event::Failed{
                    owned_handle.id(), AssetPath{},
                    AssetLoadError{load_error::AssetLoaderException{err_ptr, AssetPath{}, "add_async"}}}});
            }
        });
        return typed_handle;
    }

    // ---- Async waiting (true blocking via std::promise) ----

    /** @brief Block until an asset is fully loaded or an error occurs.
     *  Matches bevy_asset's AssetServer::wait_for_asset. */
    template <typename A>
    std::expected<void, WaitForAssetError> wait_for_asset(const Handle<A>& handle) const {
        return wait_for_asset_id(UntypedAssetId(handle.id()));
    }

    /** @brief Block until an untyped asset is fully loaded or an error occurs.
     *  Matches bevy_asset's AssetServer::wait_for_asset_untyped. */
    std::expected<void, WaitForAssetError> wait_for_asset_untyped(const UntypedHandle& handle) const;

    /** @brief Block until the given asset and all its recursive dependencies are loaded.
     *  Registers a std::promise on AssetInfo::waiting_tasks which is resolved by the event
     *  handler (success via LoadedWithDeps, failure via process_asset_fail /
     *  propagate_failed_state). No polling or sleep — this is a proper blocking wait on a future.
     *  Matches bevy_asset's AssetServer::wait_for_asset_id (async poll_fn equivalent).
     *  IMPORTANT: Must NOT be called on the same thread as handle_internal_asset_events. */
    std::expected<void, WaitForAssetError> wait_for_asset_id(const UntypedAssetId& id) const;

    /** @brief Retrieve or create a path handle for type A without triggering a load.
     *  Matches bevy_asset's AssetServer::get_or_create_path_handle. */
    template <typename A>
    Handle<A> get_or_create_path_handle(const AssetPath& path, std::optional<MetaTransform> meta_transform) const {
        return data->infos.write()
            ->template get_or_create_handle<A>(path, HandleLoadingMode::NotLoading, std::move(meta_transform))
            .first;
    }

    /** @brief Retrieve or create a path handle by type_id without triggering a load.
     *  Matches bevy_asset's AssetServer::get_or_create_path_handle_erased. */
    UntypedHandle get_or_create_path_handle_erased(const AssetPath& path,
                                                   meta::type_index type_id,
                                                   std::optional<MetaTransform> meta_transform) const;

    /** @brief Insert a pre-built asset, send a Loaded event, return an untyped handle.
     *  Matches bevy_asset's AssetServer::load_asset_untyped. */
    UntypedHandle load_asset_untyped(std::optional<AssetPath> path, ErasedLoadedAsset asset) const;

    // ---- Load State Queries ----

    /** @brief Get all three load states (self, dependency, recursive dependency) at once. */
    std::optional<std::tuple<LoadState, DependencyLoadState, RecursiveDependencyLoadState>> get_load_states(
        const UntypedAssetId& id) const;

    /** @brief Get the load state of an asset (self only). */
    std::optional<LoadState> get_load_state(const UntypedAssetId& id) const;
    /** @brief Get the load state, returning NotLoaded if not tracked. */
    LoadState load_state(const UntypedAssetId& id) const;
    /** @brief Get the direct dependency load state. */
    std::optional<DependencyLoadState> get_dependency_load_state(const UntypedAssetId& id) const;
    /** @brief Get the dependency load state, returning NotLoaded if not tracked. */
    DependencyLoadState dependency_load_state(const UntypedAssetId& id) const;
    /** @brief Get the recursive dependency load state. */
    std::optional<RecursiveDependencyLoadState> get_recursive_dependency_load_state(const UntypedAssetId& id) const;
    /** @brief Get the recursive dependency load state, returning NotLoaded if not tracked. */
    RecursiveDependencyLoadState recursive_dependency_load_state(const UntypedAssetId& id) const;
    /** @brief Check if the asset is fully loaded (self only). */
    bool is_loaded(const UntypedAssetId& id) const;
    /** @brief Check if the asset and all its direct dependencies are loaded. */
    bool is_loaded_with_direct_dependencies(const UntypedAssetId& id) const;
    /** @brief Check if the asset and all its recursive dependencies are loaded. */
    bool is_loaded_with_dependencies(const UntypedAssetId& id) const;

    // ---- Handle Queries ----

    /** @brief Get a typed handle for an asset at the given path, if one exists. */
    template <typename A>
    std::optional<Handle<A>> get_handle(const AssetPath& path) const {
        auto guard  = data->infos.read();
        auto handle = guard->get_handle_by_path_type(path, meta::type_id<A>{});
        if (!handle) return std::nullopt;
        auto typed = handle->template try_typed<A>();
        return typed ? std::make_optional(*typed) : std::nullopt;
    }
    /** @brief Get an untyped handle for an asset at the given path, if one exists. */
    std::optional<UntypedHandle> get_handle_untyped(const AssetPath& path) const;
    /** @brief Get all untyped handles for assets at the given path.
     *  Matches bevy_asset's AssetServer::get_handles_untyped. */
    std::vector<UntypedHandle> get_handles_untyped(const AssetPath& path) const;
    /** @brief Get a handle by its typed AssetId, if tracked. */
    template <typename A>
    std::optional<Handle<A>> get_id_handle(const AssetId<A>& id) const {
        auto guard  = data->infos.read();
        auto handle = guard->get_handle_by_id(UntypedAssetId(id));
        if (!handle) return std::nullopt;
        auto typed = handle->template try_typed<A>();
        return typed ? std::make_optional(*typed) : std::nullopt;
    }
    /** @brief Get a handle by UntypedAssetId, if tracked. */
    std::optional<UntypedHandle> get_id_handle_untyped(const UntypedAssetId& id) const;
    /** @brief Get a handle by path and type_id combined lookup.
     *  Matches bevy_asset's AssetServer::get_path_and_type_id_handle. */
    std::optional<UntypedHandle> get_path_and_type_id_handle(const AssetPath& path, meta::type_index type_id) const;

    // ---- Path & Info Queries ----

    /** @brief Get the path associated with an asset id, if known. */
    std::optional<AssetPath> get_path(const UntypedAssetId& id) const;
    /** @brief Get a single asset id for a path (first match), if any.
     *  Matches bevy_asset's AssetServer::get_path_id. */
    std::optional<UntypedAssetId> get_path_id(const AssetPath& path) const;
    /** @brief Get all asset ids for a path.
     *  Matches bevy_asset's AssetServer::get_path_ids. */
    std::vector<UntypedAssetId> get_path_ids(const AssetPath& path) const;
    /** @brief Check if an asset id is managed by this server. */
    bool is_managed(const UntypedAssetId& id) const;
    /** @brief Get the current server mode (Unprocessed or Processed). */
    AssetServerMode mode() const;
    /** @brief Check if the server is watching for file changes. */
    bool watching_for_changes() const;
    /** @brief Install a callback used in Processed mode to decide whether an extension has a
     *  processor. Extensions without a processor are loaded directly from the source reader
     *  instead of the processed output directory. Call this after the AssetProcessor is fully
     *  configured (all processors registered). */
    void set_processor_check(std::function<bool(std::string_view)> check) const;

    /** @brief Get an asset source by id. Returns std::nullopt if not found. */
    std::optional<std::reference_wrapper<const AssetSource>> get_source(const AssetSourceId& source_id) const;

    // ---- Loader Queries ----

    /** @brief Get a loader registered for the given file extension.
     *  Matches bevy_asset's AssetServer::get_asset_loader_with_extension. */
    std::shared_ptr<ErasedAssetLoader> get_asset_loader_with_extension(std::string_view extension) const;

    /** @brief Get a loader registered under the given type name.
     *  Matches bevy_asset's AssetServer::get_asset_loader_with_type_name. */
    std::shared_ptr<ErasedAssetLoader> get_asset_loader_with_type_name(std::string_view type_name) const;

    /** @brief Get a loader that would handle the given path (based on extension).
     *  Matches bevy_asset's AssetServer::get_path_asset_loader. */
    std::shared_ptr<ErasedAssetLoader> get_path_asset_loader(const AssetPath& path) const;

    /** @brief Get a loader registered for the given asset type.
     *  Matches bevy_asset's AssetServer::get_asset_loader_with_asset_type_id. */
    std::shared_ptr<ErasedAssetLoader> get_asset_loader_with_asset_type_id(meta::type_index type_id) const;

    /** @brief Get a loader for asset type A.
     *  Matches bevy_asset's AssetServer::get_asset_loader_with_asset_type. */
    template <typename A>
    std::shared_ptr<ErasedAssetLoader> get_asset_loader_with_asset_type() const {
        return get_asset_loader_with_asset_type_id(meta::type_id<A>{});
    }

    // ---- Direct (uncached) loading ----

    /** @brief Load an asset at path asynchronously without caching the result.
     *  Runs the full loader pipeline (meta check, loader selection, read, load).
     *  Matches bevy_asset's AssetServer::load_direct (async).
     *  Returns the type-erased loaded asset or an error. */
    asio::awaitable<std::expected<ErasedLoadedAsset, AssetLoadError>> load_direct_untyped(const AssetPath& path) const;

    /** @brief Load an asset using an already-open Reader, without caching.
     *  @param path   The logical asset path (used for meta/loader selection).
     *  @param reader An open Reader positioned at the start of the asset data.
     *  Matches bevy_asset's AssetServer::load_direct_with_reader (async). */
    asio::awaitable<std::expected<ErasedLoadedAsset, AssetLoadError>> load_direct_with_reader_untyped(
        const AssetPath& path, Reader& reader) const;

    // ---- Internal ----

    /** @brief Process handle destruction across the info table.
     *  @return true if the asset should be removed from its collection. */
    bool process_handle_destruction(const UntypedAssetId& id) const;

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

    /** @brief Core loading pipeline: read meta, pick loader, load asset, send event.
     *  Matches bevy_asset's AssetServer::load_internal (async). */
    asio::awaitable<void> load_internal(std::optional<UntypedHandle> input_handle,
                                        AssetPath path,
                                        bool force,
                                        std::optional<MetaTransform> meta_transform) const;

    /** @brief Get the meta, loader, and a Reader for an asset path.
     *  Matches bevy_asset's AssetServer::get_meta_loader_and_reader (async). */
    struct MetaLoaderReader {
        std::unique_ptr<AssetMetaDyn> meta;
        std::shared_ptr<ErasedAssetLoader> loader;
        std::unique_ptr<Reader> reader;
    };
    asio::awaitable<std::expected<MetaLoaderReader, AssetLoadError>> get_meta_loader_and_reader(
        const AssetPath& asset_path, std::optional<meta::type_index> asset_type_id) const;

    /** @brief Run loader and return ErasedLoadedAsset, catching exceptions.
     *  Matches bevy_asset's AssetServer::load_with_settings_loader_and_reader (async). */
    asio::awaitable<std::expected<ErasedLoadedAsset, AssetLoadError>> load_with_settings_loader_and_reader(
        const AssetPath& asset_path, const Settings& settings, const ErasedAssetLoader& loader, Reader& reader) const;

    /** @brief Spawn a task that calls load_internal for each existing handle to the path.
     *  Matches bevy_asset's AssetServer::reload_internal. */
    void reload_internal(const AssetPath& path, bool log) const;

    /** @brief Send an internal asset event. */
    void send_asset_event(InternalAssetEvent event) const { data->asset_event_sender.send(std::move(event)); }

    /** @brief Helper to create a LoadContext. Defined here in the module interface
     *  where AssetServer is complete, working around MSVC C++20 modules bug
     *  where forward-declared and fully-defined types are treated as mismatched. */
    static LoadContext make_load_context(const AssetServer& s, AssetPath p) { return LoadContext(s, std::move(p)); }
};

// --- NestedLoader template implementations (AssetServer must be complete) ---

template <Asset A>
Handle<A> NestedLoader::load(const AssetPath& path) {
    Handle<A> handle;
    if (m_meta_transform) {
        handle = m_context.asset_server().template load_with_meta_transform<A>(path, m_meta_transform, false, false);
    } else if (m_context.should_load_dependencies()) {
        handle = m_context.asset_server().template load<A>(path);
    } else {
        // should_load_dependencies = false: get existing handle without scheduling a new load
        auto existing = m_context.asset_server().template get_handle<A>(path);
        handle        = existing ? *existing : m_context.asset_server().template load<A>(path);
    }
    m_context.track_dependency(UntypedAssetId(handle.id()));
    return handle;
}

// --- LoadContext template implementations (AssetServer must be complete) ---

template <Asset A>
asio::awaitable<std::expected<LoadedAsset<A>, AssetLoadError>> LoadContext::load_direct(const AssetPath& path) const {
    auto result = co_await m_server.load_direct_untyped(path);
    if (!result) co_return std::unexpected(result.error());
    auto found_type = result->asset_type_id();
    auto down       = std::move(*result).template downcast<A>();
    if (!down) {
        co_return std::unexpected(
            AssetLoadError{load_error::RequestHandleMismatch{path, meta::type_id<A>{}, found_type, "load_direct"}});
    }
    co_return std::move(*down);
}

template <Asset A>
asio::awaitable<std::expected<LoadedAsset<A>, AssetLoadError>> LoadContext::load_direct_with_reader(
    const AssetPath& path, Reader& reader) const {
    auto result = co_await m_server.load_direct_with_reader_untyped(path, reader);
    if (!result) co_return std::unexpected(result.error());
    auto found_type = result->asset_type_id();
    auto down       = std::move(*result).template downcast<A>();
    if (!down) {
        co_return std::unexpected(AssetLoadError{
            load_error::RequestHandleMismatch{path, meta::type_id<A>{}, found_type, "load_direct_with_reader"}});
    }
    co_return std::move(*down);
}

}  // namespace epix::assets