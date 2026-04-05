module;

#include <spdlog/spdlog.h>

export module epix.assets:server.info;

import std;
import epix.meta;
import epix.utils;
import epix.core;

import :store;
export import :server.loader;
import :meta;

namespace epix::assets {
struct AssetInfo {
    std::weak_ptr<StrongHandle> weak_handle;
    std::optional<AssetPath> path;
    LoadState state;
    DependencyLoadState dep_state;
    RecursiveDependencyLoadState rec_dep_state;
    std::unordered_set<UntypedAssetId> loading_deps;
    std::unordered_set<UntypedAssetId> failed_deps;
    std::unordered_set<UntypedAssetId> loading_rec_deps;
    std::unordered_set<UntypedAssetId> failed_rec_deps;
    std::unordered_set<UntypedAssetId> deps_wait_on_load;
    std::unordered_set<UntypedAssetId> deps_wait_on_rec_dep_load;
    /// Loader dependencies: asset paths the loader read during loading, with their hashes.
    /// Only populated when watching_for_changes is true, to save memory.
    std::unordered_map<AssetPath, AssetHash> loader_dependencies;
    /// Tasks waiting for this asset to finish loading. Each entry is resolved when loading completes
    /// or fails. Mirrors bevy_asset's AssetInfo::waiting_tasks (Vec<Waker>).
    std::vector<std::shared_ptr<std::promise<std::expected<void, WaitForAssetError>>>> waiting_tasks;
    std::size_t handle_destruct_skip = 0;
    /// Reverse dependency tracking: assets that have this asset as a direct dependency.
    /// Matches bevy_asset's AssetInfo::dependants (HashSet<ErasedAssetIndex>).
    std::unordered_set<UntypedAssetId> dependants;

    AssetInfo(std::weak_ptr<StrongHandle> weak_handle, std::optional<AssetPath> path)
        : weak_handle(std::move(weak_handle)), path(std::move(path)), state(LoadStateOK::NotLoaded) {}
};

/** @brief Statistics reported by the asset server.
 *  Matches bevy_asset's AssetServerStats. */
export struct AssetServerStats {
    std::size_t started_load_tasks  = 0;
    std::size_t finished_load_tasks = 0;
};

/** @brief Error variants for GetOrCreateHandleInternalError.
 *  Matches bevy_asset's GetOrCreateHandleInternalError variants. */
export namespace get_or_create_handle_internal_errors {
/** @brief No HandleProvider is registered for the given asset type. Contains the unregistered TypeId.
 *  Matches bevy_asset GetOrCreateHandleInternalError::MissingHandleProviderError. */
struct MissingHandleProviderError {
    epix::meta::type_index type_id; /**< The type that has no registered HandleProvider. */
};
/** @brief The path has no associated handle and no TypeId was provided to create one.
 *  Matches bevy_asset GetOrCreateHandleInternalError::HandleMissingButTypeIdNotSpecified. */
struct HandleMissingButTypeIdNotSpecified {};
}  // namespace get_or_create_handle_internal_errors

/** @brief Internal error from get_or_create_handle_internal.
 *  Matches bevy_asset's GetOrCreateHandleInternalError. */
export using GetOrCreateHandleInternalError =
    std::variant<get_or_create_handle_internal_errors::MissingHandleProviderError,
                 get_or_create_handle_internal_errors::HandleMissingButTypeIdNotSpecified>;

enum class HandleLoadingMode {
    NotLoading, /**< The handle is for an asset that isn't loading/loaded yet. */
    Request,    /**< The handle is for an asset that is begin requested to load if not loading */
    Force,      /**< The handle is for an asset that should be forced to load, even if already loading/loaded. */
};

struct AssetInfos {
    std::unordered_map<AssetPath, std::unordered_map<epix::meta::type_index, UntypedAssetId>> path_to_ids;
    std::unordered_map<UntypedAssetId, AssetInfo> infos;
    std::unordered_map<epix::meta::type_index, std::shared_ptr<HandleProvider>> handle_providers;

    std::unordered_map<epix::meta::type_index, void (*)(epix::core::World&, AssetIndex)> dependency_loaded_event_sender;
    std::unordered_map<epix::meta::type_index, void (*)(epix::core::World&, AssetIndex, AssetPath, AssetLoadError)>
        dependency_failed_event_sender;

    std::unordered_map<UntypedAssetId, std::variant<std::packaged_task<void()>, std::shared_future<void>>>
        pending_tasks;
    AssetServerStats stats;
    bool watching_for_changes = false;
    /// Monotonically increasing counter; incremented each time an AssetInfo entry is created or removed.
    /// Matches bevy_asset's AssetInfos::infos_generation.
    uint64_t infos_generation = 0;
    /// Tracks assets that depend on the "key" asset path inside their asset loaders ("loader dependencies").
    /// Only set when watching for changes to avoid unnecessary work.
    std::unordered_map<AssetPath, std::unordered_set<AssetPath>> loader_dependents;
    /// Tracks living labeled assets for a given source asset.
    /// Only set when watching for changes to avoid unnecessary work.
    std::unordered_map<AssetPath, std::unordered_set<std::string>> living_labeled_assets;

   private:
    static std::expected<UntypedHandle, GetOrCreateHandleInternalError> create_handle_internal(
        decltype(infos)& infos,
        decltype(handle_providers)& handle_providers,
        decltype(living_labeled_assets)& living_labeled_assets,
        bool watching_for_changes,
        epix::meta::type_index type,
        std::optional<AssetPath> path,
        std::optional<MetaTransform> meta_transform,
        bool loading);
    static void remove_dependents_and_labels(const AssetInfo& info,
                                             decltype(loader_dependents)& loader_dependents,
                                             const AssetPath& path,
                                             decltype(living_labeled_assets)& living_labeled_assets);

   public:
    /** @brief Create a loading handle for a non-path asset (e.g., from add() or add_async()).
     *  Matches bevy_asset's AssetInfos::create_loading_handle_untyped. */
    UntypedHandle create_loading_handle_untyped(epix::meta::type_index type_id);

    template <typename T>
    std::pair<Handle<T>, bool> get_or_create_handle(const AssetPath& path,
                                                    HandleLoadingMode loading_mode,
                                                    std::optional<MetaTransform> meta_transform = std::nullopt);
    std::pair<UntypedHandle, bool> get_or_create_handle_untyped(
        const AssetPath& path,
        std::optional<epix::meta::type_index> type,
        HandleLoadingMode loading_mode,
        std::optional<MetaTransform> meta_transform = std::nullopt);
    auto get_or_create_handle_internal(const AssetPath& path,
                                       std::optional<epix::meta::type_index> type,
                                       HandleLoadingMode loading_mode,
                                       std::optional<MetaTransform> meta_transform = std::nullopt)
        -> std::expected<std::pair<UntypedHandle, bool>, GetOrCreateHandleInternalError>;
    bool contains_key(const UntypedAssetId& id) const { return infos.contains(id); }
    std::optional<std::reference_wrapper<const AssetInfo>> get_info(const UntypedAssetId& id) const;
    std::optional<std::reference_wrapper<AssetInfo>> get_info_mut(const UntypedAssetId& id);
    epix::utils::input_iterable<UntypedAssetId> get_path_ids(const AssetPath& path) const;
    std::optional<UntypedHandle> get_handle_by_id(const UntypedAssetId& id) const;
    auto get_handles_by_path(const AssetPath& path) const;
    auto get_handle_by_path_type(const AssetPath& path, epix::meta::type_index type) const
        -> std::optional<UntypedHandle>;
    bool is_path_alive(const AssetPath& path) const;
    bool should_reload(const AssetPath& path) const;
    /** @brief Returns `true` if the asset should be removed from collection. */
    bool process_handle_destruction(const UntypedAssetId& id);
    void process_asset_load(const UntypedAssetId& loaded_asset_id,
                            ErasedLoadedAsset loaded_asset,
                            epix::core::World& world,
                            const epix::utils::Sender<InternalAssetEvent>& event_sender);
    void propagate_loaded_state(UntypedAssetId loaded_asset_id,
                                UntypedAssetId waiting_id,
                                const epix::utils::Sender<InternalAssetEvent>& sender);
    void propagate_failed_state(UntypedAssetId loaded_asset_id, UntypedAssetId waiting_id, const AssetLoadError& error);
    void process_asset_fail(const UntypedAssetId& failed_id, const AssetLoadError& error);
    // void remove_deps(const AssetInfo& info,
    //                  std::unordered_map<AssetPath, std::unordered_set<AssetPath>>&
    //                  loader_deps, const AssetPath& path) {}
};
}  // namespace epix::assets

namespace epix::assets {
template <typename T>
std::pair<Handle<T>, bool> AssetInfos::get_or_create_handle(const AssetPath& path,
                                                            HandleLoadingMode loading_mode,
                                                            std::optional<MetaTransform> meta_transform) {
    auto res = get_or_create_handle_internal(path, epix::meta::type_id<T>{}, loading_mode, std::move(meta_transform));
    if (!res) {
        // error handling
        throw std::runtime_error("Failed to get or create handle: " + path.string());
    }
    return res
        .transform([](auto&& pair) {
            auto& [handle, should_load] = pair;
            return std::make_pair(handle.typed<T>(), should_load);
        })
        .value();
}
auto AssetInfos::get_handle_by_path_type(const AssetPath& path, epix::meta::type_index type) const
    -> std::optional<UntypedHandle> {
    auto it = path_to_ids.find(path);
    if (it != path_to_ids.end()) {
        auto& type_map = it->second;
        auto type_it   = type_map.find(type);
        if (type_it != type_map.end()) {
            auto id = type_it->second;
            return get_handle_by_id(id);
        }
    }
    return std::nullopt;
}
}  // namespace epix::assets

namespace epix::assets {
void AssetInfos::propagate_loaded_state(UntypedAssetId loaded_asset_id,
                                        UntypedAssetId waiting_id,
                                        const epix::utils::Sender<InternalAssetEvent>& sender) {
    auto deps_wait_on_rec_load = [&]() -> std::optional<std::unordered_set<UntypedAssetId>> {
        if (auto info_opt = get_info_mut(waiting_id)) {
            auto& info = info_opt->get();
            info.loading_rec_deps.erase(loaded_asset_id);
            if (info.loading_rec_deps.empty() && info.failed_rec_deps.empty()) {
                info.rec_dep_state = LoadStateOK::Loaded;
                if (info.state == LoadState{LoadStateOK::Loaded}) {
                    sender.send(InternalAssetEvent{internal_asset_event::LoadedWithDeps{waiting_id}});
                }
                return std::move(info.deps_wait_on_rec_dep_load);
            }
        }
        return std::nullopt;
    }();

    if (deps_wait_on_rec_load) {
        for (auto&& dep_id : *deps_wait_on_rec_load) {
            propagate_loaded_state(waiting_id, dep_id, sender);
        }
    }
}
void AssetInfos::propagate_failed_state(UntypedAssetId loaded_asset_id,
                                        UntypedAssetId waiting_id,
                                        const AssetLoadError& error) {
    auto deps_wait_on_rec_load = [&]() -> std::optional<std::unordered_set<UntypedAssetId>> {
        if (auto info_opt = get_info_mut(waiting_id)) {
            auto& info = info_opt->get();
            info.failed_rec_deps.insert(loaded_asset_id);
            info.loading_rec_deps.erase(loaded_asset_id);
            info.rec_dep_state = error;
            // Resolve promises on parent assets waiting on this — a recursive dependency failed
            auto error_ptr = std::make_shared<AssetLoadError>(error);
            for (auto& task : info.waiting_tasks) {
                if (task)
                    task->set_value(
                        std::unexpected(WaitForAssetError{wait_for_asset_error::DependencyFailed{error_ptr}}));
            }
            info.waiting_tasks.clear();
            return std::move(info.deps_wait_on_rec_dep_load);
        }
        return std::nullopt;
    }();

    if (deps_wait_on_rec_load) {
        for (auto&& dep_id : *deps_wait_on_rec_load) {
            propagate_failed_state(waiting_id, dep_id, error);
        }
    }
}
void AssetInfos::process_asset_fail(const UntypedAssetId& failed_id, const AssetLoadError& error) {
    if (!infos.contains(failed_id)) return;

    auto [deps_wait_on_load, deps_wait_on_rec_dep_load] = [&]() {
        auto info_opt = get_info_mut(failed_id);
        if (!info_opt)
            return std::make_pair(std::unordered_set<UntypedAssetId>{}, std::unordered_set<UntypedAssetId>{});
        auto& info         = info_opt->get();
        info.state         = error;
        info.dep_state     = error;
        info.rec_dep_state = error;
        // Resolve all promises waiting on this asset with a failure result
        auto error_ptr = std::make_shared<AssetLoadError>(error);
        for (auto& task : info.waiting_tasks) {
            if (task) task->set_value(std::unexpected(WaitForAssetError{wait_for_asset_error::Failed{error_ptr}}));
        }
        info.waiting_tasks.clear();
        return std::make_pair(std::move(info.deps_wait_on_load), std::move(info.deps_wait_on_rec_dep_load));
    }();

    for (auto& waiting_id : deps_wait_on_load) {
        if (auto info_opt = get_info_mut(waiting_id)) {
            auto& info = info_opt->get();
            info.loading_deps.erase(failed_id);
            info.failed_deps.insert(failed_id);
            if (!info.dep_state.is_failed()) {
                info.dep_state = error;
            }
        }
    }

    for (auto& waiting_id : deps_wait_on_rec_dep_load) {
        propagate_failed_state(failed_id, waiting_id, error);
    }
}
void AssetInfos::process_asset_load(const UntypedAssetId& loaded_asset_id,
                                    ErasedLoadedAsset loaded_asset,
                                    epix::core::World& world,
                                    const epix::utils::Sender<InternalAssetEvent>& sender) {
    // Process all the labeled assets first so that they don't get skipped
    // due to the "parent" not having its handle alive.
    for (auto& [label, labeled] : loaded_asset.labeled_assets) {
        auto labeled_id = labeled.handle.id();
        process_asset_load(labeled_id, std::move(labeled.asset), world, sender);
    }

    if (!infos.contains(loaded_asset_id)) return;

    loaded_asset.value->insert(loaded_asset_id, world);

    std::unordered_set<UntypedAssetId> failed_deps;
    std::optional<AssetLoadError> dep_error;
    auto loading_rec_deps = loaded_asset.dependencies;
    std::unordered_set<UntypedAssetId> failed_rec_deps;
    std::optional<AssetLoadError> rec_dep_error;
    auto loading_deps = loaded_asset.dependencies | std::views::filter([&, this](const UntypedAssetId& dep_id) {
                            if (auto dep_info_opt = get_info_mut(dep_id)) {
                                auto& dep_info = dep_info_opt->get();
                                std::visit(epix::utils::visitor{
                                               [&](LoadStateOK state) {
                                                   switch (state) {
                                                       case LoadStateOK::NotLoaded:
                                                       case LoadStateOK::Loading: {
                                                           dep_info.deps_wait_on_rec_dep_load.insert(loaded_asset_id);
                                                           break;
                                                       }
                                                       case LoadStateOK::Loaded: {
                                                           loading_rec_deps.erase(dep_id);
                                                           break;
                                                       }
                                                   }
                                               },
                                               [&](AssetLoadError error) {
                                                   if (!rec_dep_error) rec_dep_error = std::move(error);
                                                   failed_rec_deps.insert(dep_id);
                                                   loading_rec_deps.erase(dep_id);
                                               },
                                           },
                                           dep_info.rec_dep_state);
                                return std::visit(epix::utils::visitor{
                                                      [&](LoadStateOK state) {
                                                          switch (state) {
                                                              case LoadStateOK::NotLoaded:
                                                              case LoadStateOK::Loading: {
                                                                  dep_info.deps_wait_on_load.insert(loaded_asset_id);
                                                                  return true;
                                                              }
                                                              case LoadStateOK::Loaded: {
                                                                  return false;
                                                              }
                                                              default:
                                                                  std::unreachable();
                                                          }
                                                      },
                                                      [&](AssetLoadError error) {
                                                          if (!dep_error) dep_error = std::move(error);
                                                          failed_deps.insert(dep_id);
                                                          return false;
                                                      },
                                                  },
                                                  dep_info.state);
                            } else {
                                spdlog::warn(
                                    "Dependency {} of asset {} is unknown. The dependency load state will not "
                                    "switch to 'Loaded' until it is loaded.",
                                    dep_id.to_string_short(), loaded_asset_id.to_string_short());
                                return true;
                            }
                        }) |
                        std::ranges::to<std::unordered_set>();

    auto dep_load_state = [&]() -> DependencyLoadState {
        if (failed_deps.empty()) {
            if (loading_deps.empty()) {
                return LoadStateOK::Loaded;
            } else {
                return LoadStateOK::Loading;
            }
        } else {
            return *dep_error;
        }
    }();

    auto rec_dep_load_state = [&]() -> RecursiveDependencyLoadState {
        if (failed_rec_deps.empty()) {
            if (loading_rec_deps.empty()) {
                sender.send(InternalAssetEvent{internal_asset_event::LoadedWithDeps{loaded_asset_id}});
                return LoadStateOK::Loaded;
            } else {
                return LoadStateOK::Loading;
            }
        } else {
            return *rec_dep_error;
        }
    }();

    auto [deps_wait_on_load, deps_wait_on_rec_dep_load] = [&]() {
        // If watching for changes, track reverse loader dependencies for hot reloading
        if (watching_for_changes) {
            if (auto info_opt = get_info(loaded_asset_id)) {
                auto& info = info_opt->get();
                if (info.path) {
                    for (auto& [loader_dep_path, _] : loaded_asset.loader_dependencies) {
                        loader_dependents[loader_dep_path].insert(*info.path);
                    }
                }
            }
        }

        auto& info            = get_info_mut(loaded_asset_id).value().get();
        info.loading_deps     = std::move(loading_deps);
        info.failed_deps      = std::move(failed_deps);
        info.loading_rec_deps = std::move(loading_rec_deps);
        info.failed_rec_deps  = std::move(failed_rec_deps);
        info.state            = LoadStateOK::Loaded;
        info.dep_state        = dep_load_state;
        info.rec_dep_state    = rec_dep_load_state;
        if (watching_for_changes) {
            info.loader_dependencies = std::move(loaded_asset.loader_dependencies);
        }

        auto deps_wait_on_rec_load = [&]() -> std::optional<std::unordered_set<UntypedAssetId>> {
            if (rec_dep_load_state.is_loaded() || rec_dep_load_state.is_failed()) {
                return std::move(info.deps_wait_on_rec_dep_load);
            } else {
                return std::nullopt;
            }
        }();

        return std::make_pair(std::move(info.deps_wait_on_load), std::move(deps_wait_on_rec_load));
    }();

    for (auto&& id : deps_wait_on_load) {
        if (auto dep_info_opt = get_info_mut(id)) {
            auto& dep_info = dep_info_opt->get();
            dep_info.loading_deps.erase(loaded_asset_id);
            if (dep_info.loading_deps.empty() && !std::holds_alternative<AssetLoadError>(dep_info.dep_state)) {
                dep_info.dep_state = LoadStateOK::Loaded;
            }
        }
    }

    if (deps_wait_on_rec_dep_load) {
        auto& deps_wait_on_rec_load = *deps_wait_on_rec_dep_load;
        std::visit(epix::utils::visitor{
                       [&](const LoadStateOK& state) {
                           if (state == LoadStateOK::Loaded) {
                               for (auto&& dep_id : deps_wait_on_rec_load) {
                                   propagate_loaded_state(loaded_asset_id, dep_id, sender);
                               }
                           } else {
                               std::unreachable();
                           }
                       },
                       [&](const AssetLoadError& error) {
                           for (auto&& dep_id : deps_wait_on_rec_load) {
                               propagate_failed_state(loaded_asset_id, dep_id, error);
                           }
                       },
                   },
                   rec_dep_load_state);
    }
}
auto AssetInfos::get_handles_by_path(const AssetPath& path) const {
    return get_path_ids(path) |
           std::views::transform([this](const UntypedAssetId& id) { return get_handle_by_id(id); }) |
           std::views::filter([](const std::optional<UntypedHandle>& handle) { return handle.has_value(); }) |
           std::views::transform([](const std::optional<UntypedHandle>& handle) { return *handle; });
}
bool AssetInfos::is_path_alive(const AssetPath& path) const {
    if (auto it = path_to_ids.find(path); it != path_to_ids.end()) {
        for (const auto& [type, id] : it->second) {
            if (auto info = get_info(id); info && !info->get().weak_handle.expired()) {
                return true;
            }
        }
    }
    return false;
}
bool AssetInfos::should_reload(const AssetPath& path) const {
    if (is_path_alive(path)) {
        return true;
    }
    if (auto it = living_labeled_assets.find(path); it != living_labeled_assets.end()) {
        return !it->second.empty();
    }
    return false;
}
void AssetInfos::remove_dependents_and_labels(const AssetInfo& info,
                                              decltype(loader_dependents)& loader_dependents,
                                              const AssetPath& path,
                                              decltype(living_labeled_assets)& living_labeled_assets) {
    for (auto& [loader_dep, _] : info.loader_dependencies) {
        if (auto it = loader_dependents.find(loader_dep); it != loader_dependents.end()) {
            it->second.erase(path);
        }
    }

    if (!path.label) return;

    auto without_label  = path;
    without_label.label = std::nullopt;

    auto it = living_labeled_assets.find(without_label);
    if (it == living_labeled_assets.end()) return;

    it->second.erase(*path.label);
    if (it->second.empty()) {
        living_labeled_assets.erase(it);
    }
}
bool AssetInfos::process_handle_destruction(const UntypedAssetId& id) {
    auto info_res = get_info_mut(id);
    if (!info_res) return false;  // asset already removed or is not managed by AssetServer

    if (auto& info = info_res->get(); info.handle_destruct_skip > 0) {
        info.handle_destruct_skip--;
        return false;
    }

    pending_tasks.erase(id);

    auto type_index = id.type;
    auto info       = std::move(info_res->get());
    infos.erase(id);
    infos_generation++;
    if (!info.path) return true;  // asset without path, just remove
    auto& path = *info.path;

    if (watching_for_changes) {
        remove_dependents_and_labels(info, loader_dependents, path, living_labeled_assets);
    }

    if (auto it = path_to_ids.find(path); it != path_to_ids.end()) {
        auto& type_map = it->second;
        type_map.erase(type_index);
        if (type_map.empty()) {
            path_to_ids.erase(it);
        }
    }
    return true;
}
std::optional<std::reference_wrapper<const AssetInfo>> AssetInfos::get_info(const UntypedAssetId& id) const {
    auto it = infos.find(id);
    if (it != infos.end()) {
        return std::cref(it->second);
    }
    return std::nullopt;
}
std::optional<std::reference_wrapper<AssetInfo>> AssetInfos::get_info_mut(const UntypedAssetId& id) {
    auto it = infos.find(id);
    if (it != infos.end()) {
        return std::ref(it->second);
    }
    return std::nullopt;
}
epix::utils::input_iterable<UntypedAssetId> AssetInfos::get_path_ids(const AssetPath& path) const {
    auto it = path_to_ids.find(path);
    if (it != path_to_ids.end()) {
        return it->second | std::views::values;
    }
    return epix::utils::input_iterable<UntypedAssetId>{};
}
std::optional<UntypedHandle> AssetInfos::get_handle_by_id(const UntypedAssetId& id) const {
    return get_info(id).and_then([](const AssetInfo& info) -> std::optional<UntypedHandle> {
        if (auto handle = info.weak_handle.lock()) return UntypedHandle(handle);
        return std::nullopt;
    });
}
std::pair<UntypedHandle, bool> AssetInfos::get_or_create_handle_untyped(const AssetPath& path,
                                                                        std::optional<epix::meta::type_index> type,
                                                                        HandleLoadingMode loading_mode,
                                                                        std::optional<MetaTransform> meta_transform) {
    auto res = get_or_create_handle_internal(path, type, loading_mode, std::move(meta_transform));
    if (!res) {
        // error handling
        throw std::runtime_error("Failed to get or create handle: " + path.string());
    }
    return res.value();
}
std::expected<UntypedHandle, GetOrCreateHandleInternalError> AssetInfos::create_handle_internal(
    decltype(infos)& infos,
    decltype(handle_providers)& handle_providers,
    decltype(living_labeled_assets)& living_labeled_assets,
    bool watching_for_changes,
    epix::meta::type_index type,
    std::optional<AssetPath> path,
    std::optional<MetaTransform> meta_transform,
    bool loading) {
    auto provider_it = handle_providers.find(type);
    if (provider_it == handle_providers.end()) {
        return std::unexpected(
            GetOrCreateHandleInternalError{get_or_create_handle_internal_errors::MissingHandleProviderError{type}});
    }
    auto provider = provider_it->second;

    if (watching_for_changes && path && path->label) {
        auto without_label  = *path;
        auto label          = std::move(*without_label.label);
        without_label.label = std::nullopt;
        living_labeled_assets[std::move(without_label)].insert(std::move(label));
    }

    auto handle = provider->reserve(true, path, std::move(meta_transform));
    AssetInfo info(handle, path);
    if (loading) {
        info.state         = LoadStateOK::Loading;
        info.dep_state     = LoadStateOK::Loading;
        info.rec_dep_state = LoadStateOK::Loading;
    }
    infos.emplace(handle->id, std::move(info));
    return handle;
}
auto AssetInfos::get_or_create_handle_internal(const AssetPath& path,
                                               std::optional<epix::meta::type_index> type,
                                               HandleLoadingMode loading_mode,
                                               std::optional<MetaTransform> meta_transform)
    -> std::expected<std::pair<UntypedHandle, bool>, GetOrCreateHandleInternalError> {
    auto& handles = path_to_ids[path];

    type = type.or_else([&]() -> std::optional<epix::meta::type_index> {
        if (handles.size() == 1) {
            return handles.begin()->first;
        }
        return std::nullopt;
    });
    if (!type)
        return std::unexpected(
            GetOrCreateHandleInternalError{get_or_create_handle_internal_errors::HandleMissingButTypeIdNotSpecified{}});
    auto type_index = *type;

    if (auto handle_it = handles.find(type_index); handle_it != handles.end()) {
        auto& id         = handle_it->second;
        auto& info       = infos.at(id);
        bool should_load = false;
        if (loading_mode == HandleLoadingMode::Force ||
            (loading_mode == HandleLoadingMode::Request &&
             std::visit(epix::utils::visitor{
                            [](const LoadStateOK& state) { return state == LoadStateOK::NotLoaded; },
                            [](const AssetLoadError& error) { return true; },
                        },
                        info.state))) {
            info.state         = LoadStateOK::Loading;
            info.dep_state     = LoadStateOK::Loading;
            info.rec_dep_state = LoadStateOK::Loading;
            should_load        = true;
        }

        if (auto strong_handle = info.weak_handle.lock()) {
            return std::make_pair(UntypedHandle(strong_handle), should_load);
        } else {
            // AssetInfo exists but handle released. This means Assets::handle_events haven't been run to remove the
            // asset.

            // We can just create a new strong handle for that.
            info.handle_destruct_skip++;
            auto provider_it = handle_providers.find(type_index);
            if (provider_it == handle_providers.end())
                return std::unexpected(GetOrCreateHandleInternalError{
                    get_or_create_handle_internal_errors::MissingHandleProviderError{type_index}});
            auto provider    = provider_it->second;
            auto handle      = provider->get_handle(id, true, path, std::move(meta_transform));
            info.weak_handle = handle;
            return std::make_pair(UntypedHandle(handle), should_load);
        }
    } else {
        bool should_load = loading_mode != HandleLoadingMode::NotLoading;
        auto handle_res  = create_handle_internal(infos, handle_providers, living_labeled_assets, watching_for_changes,
                                                  type_index, path, std::move(meta_transform), should_load);
        if (!handle_res) return std::unexpected(handle_res.error());
        auto handle = std::move(handle_res.value());
        handles.emplace(type_index, handle.id());
        infos_generation++;
        return std::make_pair(UntypedHandle(handle), should_load);
    }
}
UntypedHandle AssetInfos::create_loading_handle_untyped(epix::meta::type_index type_id) {
    // Use an empty (pathless) AssetPath with Force mode so a fresh handle is always created.
    // Matches bevy_asset's AssetInfos::create_loading_handle_untyped.
    auto result = get_or_create_handle_internal(AssetPath{}, type_id, HandleLoadingMode::Force, std::nullopt);
    if (!result) {
        throw std::runtime_error(std::string("create_loading_handle_untyped: no provider for type ") +
                                 std::string(type_id.name()));
    }
    return result->first;
}
}  // namespace epix::assets