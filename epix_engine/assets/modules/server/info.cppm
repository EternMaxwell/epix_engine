module;

#include <spdlog/spdlog.h>

export module epix.assets:server.info;

import std;
import epix.meta;
import epix.utils;
import epix.core;

import :store;
import :server.loader;

namespace assets {
export namespace load_error {
struct RequestHandleMismatch {
    AssetPath path;
    meta::type_index requested_type;
    meta::type_index actual_type;
    std::string_view loader_name;

    bool operator==(const RequestHandleMismatch& other) const = default;
};
struct MissingAssetLoader {
    std::optional<std::string> loader_name;
    std::optional<meta::type_index> asset_type;
    AssetPath path;
    std::vector<std::string> extension;

    bool operator==(const MissingAssetLoader& other) const = default;
};
struct AssetLoaderException {
    std::exception_ptr exception;
    AssetPath path;
    std::string_view loader_name;

    bool operator==(const AssetLoaderException& other) const = default;
};
}  // namespace load_error
export using AssetLoadError =
    std::variant<load_error::RequestHandleMismatch, load_error::MissingAssetLoader, load_error::AssetLoaderException>;
export enum LoadStateOK {
    NotLoaded, /**< Asset is not loaded and not queued for loading. */
    Loading,   /**< A loader is actively loading this asset. */
    Loaded,    /**< Asset has been loaded and is ready to use. */
};
/** @brief Current state of an asset's loading lifecycle. */
export using LoadState = std::variant<LoadStateOK, AssetLoadError>;
namespace internal_asset_event {
struct Loaded {
    UntypedAssetId id;
    ErasedLoadedAsset asset;
};
struct LoadedWithDeps {
    UntypedAssetId id;
};
struct Failed {
    UntypedAssetId id;
    AssetPath path;
    AssetLoadError error;
};
}  // namespace internal_asset_event
using InternalAssetEvent =
    std::variant<internal_asset_event::Loaded, internal_asset_event::LoadedWithDeps, internal_asset_event::Failed>;
struct AssetInfo {
    std::weak_ptr<StrongHandle> weak_handle;
    std::optional<AssetPath> path;
    LoadState state;
    LoadState dep_state;
    LoadState rec_dep_state;
    std::unordered_set<UntypedAssetId> loading_deps;
    std::unordered_set<UntypedAssetId> failed_deps;
    std::unordered_set<UntypedAssetId> loading_rec_deps;
    std::unordered_set<UntypedAssetId> failed_rec_deps;
    std::unordered_set<UntypedAssetId> deps_wait_on_load;
    std::unordered_set<UntypedAssetId> deps_wait_on_rec_dep_load;
    /// Loader dependencies: asset paths the loader read during loading, with their hashes.
    /// Only populated when watching_for_changes is true, to save memory.
    std::unordered_map<AssetPath, std::size_t> loader_dependencies;
    std::vector<std::shared_future<void>> waiting;
    std::size_t handle_destruct_skip = 0;

    AssetInfo(std::weak_ptr<StrongHandle> weak_handle, std::optional<AssetPath> path)
        : weak_handle(std::move(weak_handle)), path(std::move(path)), state(LoadStateOK::NotLoaded) {}
};

struct AssetServerStatus {
    std::size_t started_load_tasks;
};

struct GetOrCreateHandleError {
    enum class Type {
        NoProvider,             /**< No provider found for the asset's type. */
        NoHandleAndNoTypeIndex, /**< No handle exists and no type index provided to create one. */
    } type;
};

enum class HandleLoadingMode {
    NotLoading, /**< The handle is for an asset that isn't loading/loaded yet. */
    Request,    /**< The handle is for an asset that is begin requested to load if not loading */
    Force,      /**< The handle is for an asset that should be forced to load, even if already loading/loaded. */
};

struct AssetInfos {
    std::unordered_map<AssetPath, std::unordered_map<meta::type_index, UntypedAssetId>> path_to_ids;
    std::unordered_map<UntypedAssetId, AssetInfo> infos;
    std::unordered_map<meta::type_index, std::shared_ptr<HandleProvider>> handle_providers;

    std::unordered_map<meta::type_index, void (*)(core::World&, AssetIndex)> dependency_loaded_event_sender;
    std::unordered_map<meta::type_index, void (*)(core::World&, AssetIndex, AssetPath, AssetLoadError)>
        dependency_failed_event_sender;

    std::unordered_map<UntypedAssetId, std::variant<std::packaged_task<void()>, std::shared_future<void>>>
        pending_tasks;
    AssetServerStatus status;
    bool watching_for_changes = false;
    /// Tracks assets that depend on the "key" asset path inside their asset loaders ("loader dependencies").
    /// Only set when watching for changes to avoid unnecessary work.
    std::unordered_map<AssetPath, std::unordered_set<AssetPath>> loader_dependents;
    /// Tracks living labeled assets for a given source asset.
    /// Only set when watching for changes to avoid unnecessary work.
    std::unordered_map<AssetPath, std::unordered_set<std::string>> living_labeled_assets;

   private:
    static std::expected<UntypedHandle, GetOrCreateHandleError> create_handle_internal(
        decltype(infos)& infos,
        decltype(handle_providers)& handle_providers,
        decltype(living_labeled_assets)& living_labeled_assets,
        bool watching_for_changes,
        meta::type_index type,
        std::optional<AssetPath> path,
        bool loading);
    static void remove_dependents_and_labels(const AssetInfo& info,
                                             decltype(loader_dependents)& loader_dependents,
                                             const AssetPath& path,
                                             decltype(living_labeled_assets)& living_labeled_assets);

   public:
    template <typename T>
    std::pair<Handle<T>, bool> get_or_create_handle(const AssetPath& path, HandleLoadingMode loading_mode);
    std::pair<UntypedHandle, bool> get_or_create_handle_untyped(const AssetPath& path,
                                                                std::optional<meta::type_index> type,
                                                                HandleLoadingMode loading_mode);
    auto get_or_create_handle_internal(const AssetPath& path,
                                       std::optional<meta::type_index> type,
                                       HandleLoadingMode loading_mode)
        -> std::expected<std::pair<UntypedHandle, bool>, GetOrCreateHandleError>;
    bool contains_key(const UntypedAssetId& id) const { return infos.contains(id); }
    std::optional<std::reference_wrapper<const AssetInfo>> get_info(const UntypedAssetId& id) const;
    std::optional<std::reference_wrapper<AssetInfo>> get_info_mut(const UntypedAssetId& id);
    utils::input_iterable<UntypedAssetId> get_path_ids(const AssetPath& path) const;
    std::optional<UntypedHandle> get_handle_by_id(const UntypedAssetId& id) const;
    auto get_handles_by_path(const AssetPath& path) const;
    auto get_handle_by_path_type(const AssetPath& path, meta::type_index type) const -> std::optional<UntypedHandle>;
    bool is_path_alive(const AssetPath& path) const;
    bool should_reload(const AssetPath& path) const;
    /** @brief Returns `true` if the asset should be removed from collection. */
    bool process_handle_destruction(const UntypedAssetId& id);
    void process_asset_load(const UntypedAssetId& loaded_asset_id,
                            ErasedLoadedAsset loaded_asset,
                            core::World& world,
                            const utils::Sender<InternalAssetEvent>& event_sender);
    void propagate_loaded_state(UntypedAssetId loaded_asset_id,
                                UntypedAssetId waiting_id,
                                const utils::Sender<InternalAssetEvent>& sender);
    void propagate_failed_state(UntypedAssetId loaded_asset_id, UntypedAssetId waiting_id, const AssetLoadError& error);
    // void remove_deps(const AssetInfo& info,
    //                  std::unordered_map<AssetPath, std::unordered_set<AssetPath>>&
    //                  loader_deps, const AssetPath& path) {}
};
}  // namespace assets

namespace assets {
template <typename T>
std::pair<Handle<T>, bool> AssetInfos::get_or_create_handle(const AssetPath& path, HandleLoadingMode loading_mode) {
    auto res = get_or_create_handle_internal(path, meta::type_id<T>{}, loading_mode);
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
auto AssetInfos::get_handle_by_path_type(const AssetPath& path,
                                         meta::type_index type) const -> std::optional<UntypedHandle> {
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
}  // namespace assets

namespace assets {
void AssetInfos::propagate_loaded_state(UntypedAssetId loaded_asset_id,
                                        UntypedAssetId waiting_id,
                                        const utils::Sender<InternalAssetEvent>& sender) {
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
void AssetInfos::process_asset_load(const UntypedAssetId& loaded_asset_id,
                                    ErasedLoadedAsset loaded_asset,
                                    core::World& world,
                                    const utils::Sender<InternalAssetEvent>& sender) {
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
                                std::visit(utils::visitor{
                                               [&](LoadStateOK state) {
                                                   switch (state) {
                                                       case LoadStateOK::NotLoaded:
                                                       case LoadStateOK::Loading: {
                                                           dep_info.deps_wait_on_rec_dep_load.insert(dep_id);
                                                       }
                                                       case LoadStateOK::Loaded: {
                                                           loading_rec_deps.erase(dep_id);
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
                                return std::visit(utils::visitor{
                                                      [&](LoadStateOK state) {
                                                          switch (state) {
                                                              case LoadStateOK::NotLoaded:
                                                              case LoadStateOK::Loading: {
                                                                  dep_info.deps_wait_on_load.insert(dep_id);
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
                                                  dep_info.dep_state);
                            } else {
                                spdlog::warn(
                                    "Dependency {} of asset {} is unknown. The dependency load state will not "
                                    "switch to 'Loaded' until it is loaded.",
                                    dep_id.to_string_short(), loaded_asset_id.to_string_short());
                                return true;
                            }
                        }) |
                        std::ranges::to<std::unordered_set>();

    auto dep_load_state = [&]() -> LoadState {
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

    auto rec_dep_load_state = [&]() -> LoadState {
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
            if (rec_dep_load_state == LoadState{LoadStateOK::Loaded} ||
                std::holds_alternative<AssetLoadError>(rec_dep_load_state)) {
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
        std::visit(utils::visitor{
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
utils::input_iterable<UntypedAssetId> AssetInfos::get_path_ids(const AssetPath& path) const {
    auto it = path_to_ids.find(path);
    if (it != path_to_ids.end()) {
        return it->second | std::views::values;
    }
    return utils::input_iterable<UntypedAssetId>{};
}
std::optional<UntypedHandle> AssetInfos::get_handle_by_id(const UntypedAssetId& id) const {
    return get_info(id).and_then([](const AssetInfo& info) -> std::optional<UntypedHandle> {
        if (auto handle = info.weak_handle.lock()) return UntypedHandle(handle);
        return std::nullopt;
    });
}
std::pair<UntypedHandle, bool> AssetInfos::get_or_create_handle_untyped(const AssetPath& path,
                                                                        std::optional<meta::type_index> type,
                                                                        HandleLoadingMode loading_mode) {
    auto res = get_or_create_handle_internal(path, type, loading_mode);
    if (!res) {
        // error handling
        throw std::runtime_error("Failed to get or create handle: " + path.string());
    }
    return res.value();
}
std::expected<UntypedHandle, GetOrCreateHandleError> AssetInfos::create_handle_internal(
    decltype(infos)& infos,
    decltype(handle_providers)& handle_providers,
    decltype(living_labeled_assets)& living_labeled_assets,
    bool watching_for_changes,
    meta::type_index type,
    std::optional<AssetPath> path,
    bool loading) {
    auto provider_it = handle_providers.find(type);
    if (provider_it == handle_providers.end()) {
        return std::unexpected(GetOrCreateHandleError{GetOrCreateHandleError::Type::NoProvider});
    }
    auto provider = provider_it->second;

    if (watching_for_changes && path && path->label) {
        auto without_label  = *path;
        auto label          = std::move(*without_label.label);
        without_label.label = std::nullopt;
        living_labeled_assets[std::move(without_label)].insert(std::move(label));
    }

    auto handle = provider->reserve(true, path);
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
                                               std::optional<meta::type_index> type,
                                               HandleLoadingMode loading_mode)
    -> std::expected<std::pair<UntypedHandle, bool>, GetOrCreateHandleError> {
    auto& handles = path_to_ids[path];

    type = type.or_else([&]() -> std::optional<meta::type_index> {
        if (handles.size() == 1) {
            return handles.begin()->first;
        }
        return std::nullopt;
    });
    if (!type) return std::unexpected(GetOrCreateHandleError{GetOrCreateHandleError::Type::NoHandleAndNoTypeIndex});
    auto type_index = *type;

    if (auto handle_it = handles.find(type_index); handle_it != handles.end()) {
        auto& id         = handle_it->second;
        auto& info       = infos.at(id);
        bool should_load = false;
        if (loading_mode == HandleLoadingMode::Force ||
            (loading_mode == HandleLoadingMode::Request &&
             std::visit(utils::visitor{
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
                return std::unexpected(GetOrCreateHandleError{GetOrCreateHandleError::Type::NoProvider});
            auto provider    = provider_it->second;
            auto handle      = provider->get_handle(id, true, path);
            info.weak_handle = handle;
            return std::make_pair(UntypedHandle(handle), should_load);
        }
    } else {
        bool should_load = loading_mode != HandleLoadingMode::NotLoading;
        auto handle_res  = create_handle_internal(infos, handle_providers, living_labeled_assets, watching_for_changes,
                                                  type_index, path, should_load);
        if (!handle_res) return std::unexpected(handle_res.error());
        auto handle = std::move(handle_res.value());
        handles.emplace(type_index, handle.id());
        return std::make_pair(UntypedHandle(handle), should_load);
    }
}
}  // namespace assets