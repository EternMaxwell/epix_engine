module;

#ifndef EPIX_IMPORT_STD
#include <expected>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
#endif
module epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
using namespace epix::assets;

UntypedHandle AssetServer::load_untyped(const AssetPath& path) const {
    std::optional<meta::type_index> loader_type;
    {
        auto loaders_guard = data->loaders->read();
        auto maybe         = loaders_guard->get_by_path(path);
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

UntypedHandle AssetServer::load_erased(meta::type_index type_id, const AssetPath& path) const {
    auto guard                 = data->infos.write();
    auto [handle, should_load] = guard->get_or_create_handle_untyped(path, type_id, HandleLoadingMode::Request);
    if (should_load) {
        spawn_load_task(handle, path, *guard);
    }
    return handle;
}

Handle<LoadedFolder> AssetServer::load_folder(const AssetPath& path) const {
    auto guard                 = data->infos.write();
    auto [handle, should_load] = guard->template get_or_create_handle<LoadedFolder>(path, HandleLoadingMode::Request);
    if (!should_load) return handle;
    load_folder_internal(handle.id(), path);
    return handle;
}

std::expected<void, MissingAssetSourceError> AssetServer::reload(const AssetPath& path) const {
    if (!get_source(path.source)) {
        return std::unexpected(MissingAssetSourceError{path.source});
    }
    reload_internal(path, false);
    return {};
}

std::expected<void, WaitForAssetError> AssetServer::wait_for_asset_untyped(const UntypedHandle& handle) const {
    return wait_for_asset_id(handle.id());
}

std::expected<void, WaitForAssetError> AssetServer::wait_for_asset_id(const UntypedAssetId& id) const {
    // ---- Fast path: check state without registering a promise ----
    {
        auto guard    = data->infos.read();
        auto info_opt = guard->get_info(id);
        if (!info_opt) return std::unexpected(WaitForAssetError{wait_for_asset_error::NotLoaded{}});
        const auto& info = info_opt->get();
        if (auto* ok = std::get_if<LoadStateOK>(&info.state)) {
            if (*ok == LoadStateOK::NotLoaded)
                return std::unexpected(WaitForAssetError{wait_for_asset_error::NotLoaded{}});
            if (*ok == LoadStateOK::Loaded && info.rec_dep_state.is_loaded()) return {};
        }
        if (auto* ep = std::get_if<std::shared_ptr<AssetLoadError>>(&info.state))
            return std::unexpected(WaitForAssetError{wait_for_asset_error::Failed{*ep}});
        if (auto ep = info.rec_dep_state.error())
            return std::unexpected(WaitForAssetError{wait_for_asset_error::DependencyFailed{ep}});
    }
    // ---- Slow path: register a promise and block until it is resolved ----
    auto promise = std::make_shared<std::promise<std::expected<void, WaitForAssetError>>>();
    std::shared_future<std::expected<void, WaitForAssetError>> future = promise->get_future();
    {
        auto guard    = data->infos.write();
        auto info_opt = guard->get_info_mut(id);
        if (!info_opt) return std::unexpected(WaitForAssetError{wait_for_asset_error::NotLoaded{}});
        auto& info = info_opt->get();
        // Re-check under write lock to close the TOCTOU window
        if (auto* ok = std::get_if<LoadStateOK>(&info.state)) {
            if (*ok == LoadStateOK::NotLoaded)
                return std::unexpected(WaitForAssetError{wait_for_asset_error::NotLoaded{}});
            if (*ok == LoadStateOK::Loaded && info.rec_dep_state.is_loaded()) return {};
        }
        if (auto* ep = std::get_if<std::shared_ptr<AssetLoadError>>(&info.state))
            return std::unexpected(WaitForAssetError{wait_for_asset_error::Failed{*ep}});
        if (auto ep = info.rec_dep_state.error())
            return std::unexpected(WaitForAssetError{wait_for_asset_error::DependencyFailed{ep}});
        // Asset is still loading — register the promise; event handler will resolve it
        info.waiting_tasks.push_back(std::move(promise));
    }
    return future.get();
}

UntypedHandle AssetServer::get_or_create_path_handle_erased(const AssetPath& path,
                                                            meta::type_index type_id,
                                                            std::optional<MetaTransform> meta_transform) const {
    return data->infos.write()
        ->get_or_create_handle_untyped(path, type_id, HandleLoadingMode::NotLoading, std::move(meta_transform))
        .first;
}

std::optional<std::tuple<LoadState, DependencyLoadState, RecursiveDependencyLoadState>> AssetServer::get_load_states(
    const UntypedAssetId& id) const {
    auto guard = data->infos.read();
    auto info  = guard->get_info(id);
    if (!info) return std::nullopt;
    return std::make_tuple(info->get().state, info->get().dep_state, info->get().rec_dep_state);
}

std::optional<LoadState> AssetServer::get_load_state(const UntypedAssetId& id) const {
    auto guard = data->infos.read();
    auto info  = guard->get_info(id);
    if (!info) return std::nullopt;
    return info->get().state;
}

LoadState AssetServer::load_state(const UntypedAssetId& id) const {
    return get_load_state(id).value_or(LoadState{LoadStateOK::NotLoaded});
}

std::optional<DependencyLoadState> AssetServer::get_dependency_load_state(const UntypedAssetId& id) const {
    auto guard = data->infos.read();
    auto info  = guard->get_info(id);
    if (!info) return std::nullopt;
    return info->get().dep_state;
}

DependencyLoadState AssetServer::dependency_load_state(const UntypedAssetId& id) const {
    return get_dependency_load_state(id).value_or(DependencyLoadState{LoadStateOK::NotLoaded});
}

std::optional<RecursiveDependencyLoadState> AssetServer::get_recursive_dependency_load_state(
    const UntypedAssetId& id) const {
    auto guard = data->infos.read();
    auto info  = guard->get_info(id);
    if (!info) return std::nullopt;
    return info->get().rec_dep_state;
}

RecursiveDependencyLoadState AssetServer::recursive_dependency_load_state(const UntypedAssetId& id) const {
    return get_recursive_dependency_load_state(id).value_or(RecursiveDependencyLoadState{LoadStateOK::NotLoaded});
}

bool AssetServer::is_loaded(const UntypedAssetId& id) const {
    auto state = get_load_state(id);
    return state && std::holds_alternative<LoadStateOK>(*state) && std::get<LoadStateOK>(*state) == LoadStateOK::Loaded;
}

bool AssetServer::is_loaded_with_direct_dependencies(const UntypedAssetId& id) const {
    return is_loaded(id) && [&] {
        auto dep = get_dependency_load_state(id);
        return dep && dep->is_loaded();
    }();
}

bool AssetServer::is_loaded_with_dependencies(const UntypedAssetId& id) const {
    return is_loaded(id) && [&] {
        auto rec = get_recursive_dependency_load_state(id);
        return rec && rec->is_loaded();
    }();
}

std::optional<UntypedHandle> AssetServer::get_handle_untyped(const AssetPath& path) const {
    auto guard = data->infos.read();
    auto ids   = guard->get_path_ids(path);
    for (auto id : ids) {
        auto handle = guard->get_handle_by_id(id);
        if (handle) return handle;
    }
    return std::nullopt;
}

std::vector<UntypedHandle> AssetServer::get_handles_untyped(const AssetPath& path) const {
    std::vector<UntypedHandle> result;
    auto guard = data->infos.read();
    auto ids   = guard->get_path_ids(path);
    for (auto id : ids) {
        auto handle = guard->get_handle_by_id(id);
        if (handle) result.push_back(*handle);
    }
    return result;
}

std::optional<UntypedHandle> AssetServer::get_id_handle_untyped(const UntypedAssetId& id) const {
    auto guard = data->infos.read();
    return guard->get_handle_by_id(id);
}

std::optional<UntypedHandle> AssetServer::get_path_and_type_id_handle(const AssetPath& path,
                                                                      meta::type_index type_id) const {
    auto guard = data->infos.read();
    return guard->get_handle_by_path_type(path, type_id);
}

std::optional<AssetPath> AssetServer::get_path(const UntypedAssetId& id) const {
    auto guard = data->infos.read();
    auto info  = guard->get_info(id);
    if (!info) return std::nullopt;
    return info->get().path;
}

std::optional<UntypedAssetId> AssetServer::get_path_id(const AssetPath& path) const {
    auto guard = data->infos.read();
    auto ids   = guard->get_path_ids(path);
    for (auto id : ids) return id;
    return std::nullopt;
}

std::vector<UntypedAssetId> AssetServer::get_path_ids(const AssetPath& path) const {
    auto guard = data->infos.read();
    std::vector<UntypedAssetId> result;
    for (auto id : guard->get_path_ids(path)) {
        result.push_back(id);
    }
    return result;
}

bool AssetServer::is_managed(const UntypedAssetId& id) const {
    auto guard = data->infos.read();
    return guard->contains_key(id);
}

AssetServerMode AssetServer::mode() const { return data->mode; }

bool AssetServer::watching_for_changes() const { return data->watching_for_changes; }

std::optional<std::reference_wrapper<const AssetSource>> AssetServer::get_source(const AssetSourceId& source_id) const {
    if (!data->sources) return std::nullopt;
    return data->sources->get(source_id);
}

std::shared_ptr<ErasedAssetLoader> AssetServer::get_asset_loader_with_extension(std::string_view extension) const {
    auto guard = data->loaders->read();
    auto maybe = guard->get_by_extension(extension);
    if (!maybe) return nullptr;
    return maybe->get();
}

std::shared_ptr<ErasedAssetLoader> AssetServer::get_asset_loader_with_type_name(std::string_view type_name) const {
    auto guard = data->loaders->read();
    auto maybe = guard->get_by_name(type_name);
    if (!maybe) return nullptr;
    return maybe->get();
}

std::shared_ptr<ErasedAssetLoader> AssetServer::get_path_asset_loader(const AssetPath& path) const {
    auto guard = data->loaders->read();
    auto maybe = guard->get_by_path(path);
    if (!maybe) return nullptr;
    return maybe->get();
}

std::shared_ptr<ErasedAssetLoader> AssetServer::get_asset_loader_with_asset_type_id(meta::type_index type_id) const {
    auto guard = data->loaders->read();
    auto maybe = guard->get_by_type(type_id);
    if (!maybe) return nullptr;
    return maybe->get();
}

bool AssetServer::process_handle_destruction(const UntypedAssetId& id) const {
    auto guard = data->infos.write();
    return guard->process_handle_destruction(id);
}

UntypedHandle NestedLoader::load_untyped(const AssetPath& path) {
    UntypedHandle handle = m_context.asset_server().load_untyped(path);
    m_context.track_dependency(handle.id());
    return handle;
}
