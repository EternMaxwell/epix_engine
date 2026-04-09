module;

#include <spdlog/spdlog.h>

module epix.assets;

import std;
import epix.meta;
import epix.utils;
import epix.core;
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
                                        const std::shared_ptr<AssetLoadError>& error_ptr) {
    auto deps_wait_on_rec_load = [&]() -> std::optional<std::unordered_set<UntypedAssetId>> {
        if (auto info_opt = get_info_mut(waiting_id)) {
            auto& info = info_opt->get();
            info.failed_rec_deps.insert(loaded_asset_id);
            info.loading_rec_deps.erase(loaded_asset_id);
            info.rec_dep_state = error_ptr;
            // Resolve promises on parent assets waiting on this — a recursive dependency failed
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
            propagate_failed_state(waiting_id, dep_id, error_ptr);
        }
    }
}
void AssetInfos::process_asset_fail(const UntypedAssetId& failed_id, const AssetLoadError& error) {
    if (!infos.contains(failed_id)) return;

    auto error_ptr = std::make_shared<AssetLoadError>(error);

    auto [deps_wait_on_load, deps_wait_on_rec_dep_load] = [&]() {
        auto info_opt = get_info_mut(failed_id);
        if (!info_opt)
            return std::make_pair(std::unordered_set<UntypedAssetId>{}, std::unordered_set<UntypedAssetId>{});
        auto& info         = info_opt->get();
        info.state         = error_ptr;
        info.dep_state     = error_ptr;
        info.rec_dep_state = error_ptr;
        // Resolve all promises waiting on this asset with a failure result
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
                info.dep_state = error_ptr;
            }
        }
    }

    for (auto& waiting_id : deps_wait_on_rec_dep_load) {
        propagate_failed_state(failed_id, waiting_id, error_ptr);
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
    std::shared_ptr<AssetLoadError> dep_error;
    auto loading_rec_deps = loaded_asset.dependencies;
    std::unordered_set<UntypedAssetId> failed_rec_deps;
    std::shared_ptr<AssetLoadError> rec_dep_error;
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
                                               [&](std::shared_ptr<AssetLoadError> error_ptr) {
                                                   if (!rec_dep_error) rec_dep_error = error_ptr;
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
                                                      [&](std::shared_ptr<AssetLoadError> error_ptr) {
                                                          if (!dep_error) dep_error = error_ptr;
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
            return dep_error;  // shared_ptr<AssetLoadError>
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
            return rec_dep_error;  // shared_ptr<AssetLoadError>
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
            if (dep_info.loading_deps.empty() && !dep_info.dep_state.is_failed()) {
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
                       [&](const std::shared_ptr<AssetLoadError>& error_ptr) {
                           for (auto&& dep_id : deps_wait_on_rec_load) {
                               propagate_failed_state(loaded_asset_id, dep_id, error_ptr);
                           }
                       },
                   },
                   rec_dep_load_state);
    }
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
                            [](const std::shared_ptr<AssetLoadError>&) { return true; },
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