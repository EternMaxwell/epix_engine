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
auto AssetInfos::get_handles_by_path(const AssetPath& path) const {
    return get_path_ids(path) |
           std::views::transform([this](const UntypedAssetId& id) { return get_handle_by_id(id); }) |
           std::views::filter([](const std::optional<UntypedHandle>& handle) { return handle.has_value(); }) |
           std::views::transform([](const std::optional<UntypedHandle>& handle) { return *handle; });
}
}  // namespace epix::assets
