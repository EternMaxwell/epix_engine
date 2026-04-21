module;

#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <spdlog/spdlog.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>

module epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
using namespace epix::assets;

AssetServer::AssetServer(std::shared_ptr<AssetSources> sources, AssetServerMode mode, bool watching_for_changes)
    : data(std::make_shared<AssetServerData>()) {
    data->sources                                                  = std::move(sources);
    data->mode                                                     = mode;
    data->watching_for_changes                                     = watching_for_changes;
    data->loaders                                                  = std::make_shared<utils::RwLock<AssetLoaders>>();
    std::tie(data->asset_event_sender, data->asset_event_receiver) = utils::make_channel<InternalAssetEvent>();
    auto guard                                                     = data->infos.write();
    guard->watching_for_changes                                    = watching_for_changes;
}

AssetServer::AssetServer(std::shared_ptr<AssetSources> sources,
                         AssetServerMode mode,
                         AssetMetaCheck meta_check,
                         bool watching_for_changes,
                         UnapprovedPathMode unapproved_path_mode)
    : data(std::make_shared<AssetServerData>()) {
    data->sources                                                  = std::move(sources);
    data->mode                                                     = mode;
    data->watching_for_changes                                     = watching_for_changes;
    data->meta_check                                               = meta_check;
    data->unapproved_path_mode                                     = unapproved_path_mode;
    data->loaders                                                  = std::make_shared<utils::RwLock<AssetLoaders>>();
    std::tie(data->asset_event_sender, data->asset_event_receiver) = utils::make_channel<InternalAssetEvent>();
    auto guard                                                     = data->infos.write();
    guard->watching_for_changes                                    = watching_for_changes;
}

AssetServer::AssetServer(std::shared_ptr<AssetSources> sources,
                         std::shared_ptr<utils::RwLock<AssetLoaders>> loaders,
                         AssetServerMode mode,
                         AssetMetaCheck meta_check,
                         bool watching_for_changes,
                         UnapprovedPathMode unapproved_path_mode)
    : data(std::make_shared<AssetServerData>()) {
    data->sources                                                  = std::move(sources);
    data->mode                                                     = mode;
    data->watching_for_changes                                     = watching_for_changes;
    data->meta_check                                               = meta_check;
    data->unapproved_path_mode                                     = unapproved_path_mode;
    data->loaders                                                  = std::move(loaders);
    std::tie(data->asset_event_sender, data->asset_event_receiver) = utils::make_channel<InternalAssetEvent>();
    auto guard                                                     = data->infos.write();
    guard->watching_for_changes                                    = watching_for_changes;
}

bool ::epix::assets::asset_server_process_handle_destruction(const AssetServer& server, const UntypedAssetId& id) {
    return server.process_handle_destruction(id);
}

void AssetServer::set_processor_check(std::function<bool(std::string_view)> check) const {
    data->has_processor_for_ext = std::move(check);
}

void ::epix::assets::log_asset_error(const AssetError& error,
                                     const std::string_view& header,
                                     const std::string_view& operation) {
    std::visit(utils::visitor{
                   [&header, &operation](const AssetNotPresent& e) {
                       spdlog::error("[{}:{}] Asset not present at {}", header, operation,
                                     std::visit(utils::visitor{[](const AssetIndex& idx) {
                                                                   return std::format("index: {}, generation: {}",
                                                                                      idx.index(), idx.generation());
                                                               },
                                                               [](const uuids::uuid& id) {
                                                                   return std::format("uuid: {}", uuids::to_string(id));
                                                               }},
                                                e));
                   },
                   [&header, &operation](const IndexOutOfBound& e) {
                       spdlog::error("[{}:{}] Index out of bound: {}", header, operation, e.index);
                   },
                   [&header, &operation](const SlotEmpty& e) {
                       spdlog::error("[{}:{}] Slot is empty at index {}", header, operation, e.index);
                   },
                   [&header, &operation](const GenMismatch& e) {
                       spdlog::error("[{}:{}] Generation mismatch at index {} (current: {}, expected: {})", header,
                                     operation, e.index, e.current_gen, e.expected_gen);
                   }},
               error);
}

static asio::awaitable<void> load_folder_recursive(const AssetSourceId& source,
                                                   const std::filesystem::path& dir_path,
                                                   const AssetReader& reader,
                                                   const AssetServer& server,
                                                   std::vector<UntypedHandle>& handles) {
    auto is_dir = co_await reader.is_directory(dir_path);
    if (!is_dir || !*is_dir) co_return;

    auto dir_result = co_await reader.read_directory(dir_path);
    if (!dir_result) co_return;

    for (auto child_path : *dir_result) {
        auto child_is_dir = co_await reader.is_directory(child_path);
        if (child_is_dir && *child_is_dir) {
            co_await load_folder_recursive(source, child_path, reader, server, handles);
        } else {
            auto asset_path = AssetPath(source, child_path);
            // Only load files that have a registered loader
            auto loader = server.get_path_asset_loader(asset_path);
            if (loader) {
                handles.push_back(server.load_untyped(asset_path));
            }
        }
    }
}

void AssetServer::load_folder_internal(const UntypedAssetId& id, const AssetPath& path) const {
    data->infos.write()->stats.started_load_tasks++;

    auto server     = *this;
    auto asset_id   = id;
    auto asset_path = path;

    utils::IOTaskPool::instance().detach_task([server, asset_id, asset_path]() {
        auto source = server.data->sources->get(asset_path.source);
        if (!source) {
            spdlog::error("Failed to load folder {}. AssetSource does not exist", asset_path.string());
            return;
        }

        const AssetReader* reader_ptr = nullptr;
        if (server.data->mode == AssetServerMode::Processed) {
            auto processed_reader_opt = source->get().processed_reader();
            if (!processed_reader_opt) {
                spdlog::error("Failed to load folder {}. No processed reader available", asset_path.string());
                return;
            }
            reader_ptr = &processed_reader_opt->get();
        } else {
            reader_ptr = &source->get().reader();
        }

        asio::io_context ctx;
        asio::co_spawn(
            ctx,
            [&]() -> asio::awaitable<void> {
                std::vector<UntypedHandle> handles;
                co_await load_folder_recursive(asset_path.source, asset_path.path, *reader_ptr, server, handles);

                server.data->asset_event_sender.send(internal_asset_event::Loaded{
                    asset_id, ErasedLoadedAsset::from_asset(LoadedFolder{std::move(handles)})});
            }(),
            asio::detached);
        ctx.run();
    });
}

void AssetServer::handle_internal_events(core::ParamSet<core::World&, core::Res<AssetServer>> params) {
    auto&& [world, server] = params.get();
    auto receiver          = server->data->asset_event_receiver;

    std::vector<UntypedAssetLoadFailedEvent> untyped_failures;
    std::unordered_set<AssetPath> paths_to_reload;
    bool watching_for_changes = false;

    {
        auto guard = server->data->infos.write();

        while (auto event = receiver.try_receive()) {
            std::visit(utils::visitor{
                           [&](internal_asset_event::Loaded& loaded) {
                               guard->process_asset_load(loaded.id, std::move(loaded.asset), world,
                                                         server->data->asset_event_sender);
                           },
                           [&](internal_asset_event::LoadedWithDeps& loaded_with_deps) {
                               // Dispatch typed LoadedWithDependencies event
                               auto type_id = loaded_with_deps.id.type;
                               if (auto it = guard->dependency_loaded_event_sender.find(type_id);
                                   it != guard->dependency_loaded_event_sender.end()) {
                                   auto index = std::get<AssetIndex>(loaded_with_deps.id.id);
                                   it->second(world, index);
                               }
                               // Resolve all promises waiting on this asset — full load complete
                               if (auto info = guard->get_info_mut(loaded_with_deps.id)) {
                                   for (auto& task : info->get().waiting_tasks) {
                                       if (task) task->set_value({});
                                   }
                                   info->get().waiting_tasks.clear();
                               }
                           },
                           [&](internal_asset_event::Failed& failed) {
                               guard->process_asset_fail(failed.id, failed.error);

                               // Collect untyped failure event
                               untyped_failures.push_back(UntypedAssetLoadFailedEvent{
                                   failed.id, failed.path, format_asset_load_error(failed.error)});

                               // Dispatch typed failure event
                               auto type_id = failed.id.type;
                               if (auto it = guard->dependency_failed_event_sender.find(type_id);
                                   it != guard->dependency_failed_event_sender.end()) {
                                   auto index = std::get<AssetIndex>(failed.id.id);
                                   it->second(world, index, failed.path, failed.error);
                               }

                               log_asset_load_error(failed.error, failed.path);
                           },
                       },
                       *event);
        }

        if (!untyped_failures.empty()) {
            auto events_opt = world.get_resource_mut<core::Events<UntypedAssetLoadFailedEvent>>();
            if (events_opt) {
                for (auto& failure : untyped_failures) {
                    events_opt->get().push(std::move(failure));
                }
            }
        }

        // Hot-reload: process watcher events
        watching_for_changes = guard->watching_for_changes;
        if (watching_for_changes) {
            auto reload_parent_folders = [&](const std::filesystem::path& path, const AssetSourceId& source) {
                auto current = path.parent_path();
                while (!current.empty() && current != path) {
                    auto parent_asset_path = AssetPath(source, current);
                    for (auto handle : guard->get_handles_by_path(parent_asset_path)) {
                        spdlog::info("Reloading folder {} because content has changed", parent_asset_path.string());
                        server->load_folder_internal(handle.id(), parent_asset_path);
                    }
                    auto next = current.parent_path();
                    if (next == current) break;
                    current = next;
                }
            };

            std::function<void(const AssetPath&)> queue_ancestors = [&](const AssetPath& asset_path) {
                if (auto it = guard->loader_dependents.find(asset_path); it != guard->loader_dependents.end()) {
                    for (auto& dependent : it->second) {
                        paths_to_reload.insert(dependent);
                        queue_ancestors(dependent);
                    }
                }
            };

            auto reload_path = [&](const std::filesystem::path& path, const AssetSourceId& source) {
                auto asset_path = AssetPath(source, path);
                queue_ancestors(asset_path);
                paths_to_reload.insert(asset_path);
            };

            auto handle_source_event = [&](const AssetSourceId& source, const AssetSourceEvent& event) {
                std::visit(utils::visitor{
                               [&](const source_events::AddedAsset& e) {
                                   reload_parent_folders(e.path, source);
                                   reload_path(e.path, source);
                               },
                               [&](const source_events::ModifiedAsset& e) { reload_path(e.path, source); },
                               [&](const source_events::ModifiedMeta& e) { reload_path(e.path, source); },
                               [&](const source_events::RenamedDirectory& e) {
                                   reload_parent_folders(e.old_path, source);
                                   reload_parent_folders(e.new_path, source);
                               },
                               [&](const source_events::RemovedAsset& e) { reload_parent_folders(e.path, source); },
                               [&](const source_events::RemovedDirectory& e) { reload_parent_folders(e.path, source); },
                               [&](const source_events::AddedDirectory& e) { reload_parent_folders(e.path, source); },
                               [&](const auto&) {},
                           },
                           event);
            };

            for (auto& source : server->data->sources->iter()) {
                switch (server->data->mode) {
                    case AssetServerMode::Unprocessed: {
                        if (auto receiver_opt = source.event_receiver()) {
                            while (auto ev = receiver_opt->get().try_receive()) {
                                handle_source_event(source.id(), *ev);
                            }
                        }
                        break;
                    }
                    case AssetServerMode::Processed: {
                        if (auto receiver_opt = source.processed_event_receiver()) {
                            while (auto ev = receiver_opt->get().try_receive()) {
                                handle_source_event(source.id(), *ev);
                            }
                        }
                        break;
                    }
                }
            }
        }
    }  // guard dropped here

    // Reload paths outside the lock to avoid deadlock
    for (auto& path : paths_to_reload) {
        server->reload(path);
    }
}

// ---------------------------------------------------------------------------
// load_with_settings_loader_and_reader
// Matches bevy_asset's AssetServer::load_with_settings_loader_and_reader.
// Calls the loader, wrapping any thrown exception into an AssetLoadError.
// ---------------------------------------------------------------------------
asio::awaitable<std::expected<ErasedLoadedAsset, AssetLoadError>> AssetServer::load_with_settings_loader_and_reader(
    const AssetPath& asset_path, const Settings& settings, const ErasedAssetLoader& loader, Reader& reader) const {
    try {
        auto context     = AssetServer::make_load_context(*this, asset_path);
        auto load_result = co_await loader.load(reader, settings, context);
        if (!load_result) {
            co_return std::unexpected(AssetLoadError{
                load_error::AssetLoaderException{load_result.error(), asset_path, loader.loader_type().short_name()}});
        }
        co_return std::move(*load_result);
    } catch (...) {
        co_return std::unexpected(AssetLoadError{
            load_error::AssetLoaderException{std::current_exception(), asset_path, loader.loader_type().short_name()}});
    }
}

// ---------------------------------------------------------------------------
// get_meta_loader_and_reader
// Matches bevy_asset's AssetServer::get_meta_loader_and_reader.
// Selects the reader mode (processed vs unprocessed), checks AssetMetaCheck,
// and picks a loader. If a .meta file is found its loader_name field is used
// (via binary zpp::bits deserialization of AssetMetaMinimal); otherwise the
// extension-based loader is used and default_meta() is returned.
// ---------------------------------------------------------------------------
asio::awaitable<std::expected<AssetServer::MetaLoaderReader, AssetLoadError>> AssetServer::get_meta_loader_and_reader(
    const AssetPath& asset_path, std::optional<meta::type_index> asset_type_id) const {
    // 1. Resolve the source
    auto source_opt = get_source(asset_path.source);
    if (!source_opt) {
        co_return std::unexpected(AssetLoadError{load_error::MissingAssetSourceError{asset_path.source}});
    }
    const AssetSource& source = source_opt->get();

    // 2. Pick the reader (processed or raw)
    const AssetReader* reader_ptr = nullptr;
    std::optional<std::reference_wrapper<const AssetReader>> processed_reader_opt;
    if (data->mode == AssetServerMode::Processed) {
        bool use_source = data->has_processor_for_ext &&
                          !data->has_processor_for_ext(asset_path.get_full_extension().value_or(std::string{}));
        if (use_source) {
            reader_ptr = &source.reader();
        } else {
            processed_reader_opt = source.processed_reader();
            if (!processed_reader_opt) {
                co_return std::unexpected(
                    AssetLoadError{load_error::MissingProcessedAssetReaderError{asset_path.source}});
            }
            reader_ptr = &processed_reader_opt->get();
        }
    } else {
        reader_ptr = &source.reader();
    }

    // 3. Determine whether to check for a .meta file
    bool read_meta =
        std::visit(utils::visitor{
                       [](const asset_meta_check::Always&) { return true; },
                       [](const asset_meta_check::Never&) { return false; },
                       [&asset_path](const asset_meta_check::Paths& p) { return p.paths.contains(asset_path); },
                   },
                   data->meta_check);

    // 4. Try to find a loader.
    std::shared_ptr<ErasedAssetLoader> loader;
    std::optional<std::vector<std::byte>> stored_meta_bytes;
    if (read_meta) {
        auto meta_bytes = co_await reader_ptr->read_meta_bytes(asset_path.path);
        if (meta_bytes) {
            stored_meta_bytes = std::move(meta_bytes.value());
            auto minimal      = deserialize_meta_minimal(*stored_meta_bytes);
            if (minimal && !minimal->asset.loader.empty()) {
                loader = [&]() -> std::shared_ptr<ErasedAssetLoader> {
                    auto guard = data->loaders->read();
                    auto maybe =
                        guard->find(std::string_view{minimal->asset.loader}, asset_type_id, std::nullopt, std::nullopt);
                    return maybe ? maybe->get() : nullptr;
                }();
            }
        }
    }

    if (!loader) {
        auto maybe = [&]() -> std::optional<MaybeAssetLoader> {
            auto guard = data->loaders->read();
            return guard->find(std::nullopt, asset_type_id, std::nullopt,
                               std::optional<std::reference_wrapper<const AssetPath>>{asset_path});
        }();

        if (!maybe) {
            std::vector<std::string> exts;
            if (auto e = asset_path.get_extension()) exts.push_back(std::string(*e));
            co_return std::unexpected(AssetLoadError{
                load_error::MissingAssetLoader{std::nullopt, asset_type_id, asset_path, std::move(exts)}});
        }
        loader = maybe->get();
        if (!loader) {
            std::vector<std::string> exts;
            if (auto e = asset_path.get_extension()) exts.push_back(std::string(*e));
            co_return std::unexpected(AssetLoadError{
                load_error::MissingAssetLoader{std::nullopt, asset_type_id, asset_path, std::move(exts)}});
        }
    }

    // 5. Build meta from the loader.
    auto meta = [&]() -> std::expected<std::unique_ptr<AssetMetaDyn>, AssetLoadError> {
        if (stored_meta_bytes) {
            auto dm = loader->deserialize_meta(*stored_meta_bytes);
            if (dm) return std::move(*dm);
            return std::unexpected(AssetLoadError{load_error::DeserializeMeta{asset_path, std::string(dm.error())}});
        }
        return loader->default_meta();
    }();
    if (!meta) {
        co_return std::unexpected(meta.error());
    }

    // 5b. Check meta action type
    switch ((*meta)->action_type()) {
        case AssetActionType::Ignore:
            co_return std::unexpected(AssetLoadError{load_error::CannotLoadIgnoredAsset{asset_path}});
        case AssetActionType::Process:
            if (data->mode != AssetServerMode::Processed) {
                co_return std::unexpected(AssetLoadError{load_error::CannotLoadProcessedAsset{asset_path}});
            }
            break;
        case AssetActionType::Load:
            break;
    }

    // 6. Open the asset file for reading
    auto read_result = co_await reader_ptr->read(asset_path.path);
    if (!read_result) {
        co_return std::unexpected(AssetLoadError{load_error::AssetReaderError{read_result.error()}});
    }

    co_return MetaLoaderReader{std::move(*meta), std::move(loader), std::move(*read_result)};
}

// ---------------------------------------------------------------------------
// load_internal
// Matches bevy_asset's AssetServer::load_internal.
// Called from inside a task spawned by spawn_load_task.
// ---------------------------------------------------------------------------
asio::awaitable<void> AssetServer::load_internal(std::optional<UntypedHandle> input_handle,
                                                 AssetPath path,
                                                 bool force,
                                                 std::optional<MetaTransform> meta_transform) const {
    // Determine asset_type_id hint from input handle (if typed)
    std::optional<meta::type_index> input_type_id;
    if (input_handle) input_type_id = input_handle->type();

    // --- Get meta, loader and reader ---------------------------------
    auto mlr_result = co_await get_meta_loader_and_reader(path, input_type_id);
    if (!mlr_result) {
        // If we had an input handle, propagate failure so the handle's state is updated
        if (input_handle) {
            send_asset_event(
                InternalAssetEvent{internal_asset_event::Failed{input_handle->id(), path, mlr_result.error()}});
        }
        co_return;
    }
    auto& [meta, loader, reader] = *mlr_result;

    // Apply the meta_transform carried by the handle (settings override)
    // Priority: meta_transform argument > handle's built-in meta_transform
    if (meta_transform) {
        (*meta_transform)(*meta);
    } else if (input_handle) {
        if (auto* mt = input_handle->meta_transform()) {
            (*mt)(*meta);
        }
    }

    // --- Resolve asset ID and should_load flag -----------------------
    std::optional<UntypedHandle> fetched_handle;
    bool should_load               = true;
    auto [asset_id, _fetched_pair] = [&]() -> std::pair<std::optional<UntypedAssetId>, std::optional<UntypedHandle>> {
        if (input_handle) return {input_handle->id(), std::nullopt};
        auto guard = data->infos.write();
        auto result =
            guard->get_or_create_handle_internal(path, loader->asset_type(), HandleLoadingMode::Request, std::nullopt);
        if (!result) return {std::nullopt, std::nullopt};
        auto& [handle, result_should_load] = *result;
        should_load                        = result_should_load;
        return {handle.id(), handle};
    }();
    fetched_handle = _fetched_pair;
    if (!asset_id) co_return;  // no provider

    // --- Early-out if already loaded ---------------------------------
    if (!should_load && !force) {
        co_return;
    }

    // --- Verify type matches loader ----------------------------------
    if (asset_id->type != loader->asset_type()) {
        auto err = AssetLoadError{load_error::RequestHandleMismatch{path, asset_id->type, loader->asset_type(),
                                                                    loader->loader_type().short_name()}};
        send_asset_event(InternalAssetEvent{internal_asset_event::Failed{*asset_id, path, err}});
        co_return;
    }

    // --- Load --------------------------------------------------------
    auto settings_ptr = meta->loader_settings();
    if (!settings_ptr) {
        auto err = AssetLoadError{load_error::AssetLoaderException{
            std::make_exception_ptr(std::runtime_error("Asset meta has no loader settings")), path,
            loader->loader_type().short_name()}};
        send_asset_event(InternalAssetEvent{internal_asset_event::Failed{*asset_id, path, err}});
        co_return;
    }

    auto load_result = co_await load_with_settings_loader_and_reader(path, *settings_ptr, *loader, *reader);
    if (load_result) {
        send_asset_event(InternalAssetEvent{internal_asset_event::Loaded{*asset_id, std::move(*load_result)}});
    } else {
        send_asset_event(InternalAssetEvent{internal_asset_event::Failed{*asset_id, path, load_result.error()}});
    }
}

// ---------------------------------------------------------------------------
// spawn_load_task — now a thin wrapper that calls load_internal.
// Matches bevy_asset's AssetServer::spawn_load_task.
// ---------------------------------------------------------------------------
void AssetServer::spawn_load_task(const UntypedHandle& handle, const AssetPath& path, AssetInfos& infos) const {
    infos.stats.started_load_tasks++;

    auto server       = *this;
    auto owned_handle = handle;
    auto asset_path   = path;

    utils::IOTaskPool::instance().detach_task([server, owned_handle, asset_path]() mutable {
        asio::io_context ctx;
        asio::co_spawn(ctx, server.load_internal(std::move(owned_handle), std::move(asset_path), false, std::nullopt),
                       asio::detached);
        ctx.run();
    });
}

void AssetServer::spawn_load_task(const UntypedHandle& handle, const AssetPath& path) const {
    auto guard = data->infos.write();
    spawn_load_task(handle, path, *guard);
}

// ---------------------------------------------------------------------------
// load_direct_untyped / load_direct_with_reader_untyped
// Matches bevy_asset's AssetServer::load_direct / load_direct_with_reader.
// Runs the full loader pipeline synchronously without caching the result.
// ---------------------------------------------------------------------------
asio::awaitable<std::expected<ErasedLoadedAsset, AssetLoadError>> AssetServer::load_direct_untyped(
    const AssetPath& path) const {
    auto mlr = co_await get_meta_loader_and_reader(path, std::nullopt);
    if (!mlr) {
        co_return std::unexpected(mlr.error());
    }
    auto settings = mlr->meta->loader_settings();
    if (!settings) {
        auto def = mlr->loader->default_settings();
        co_return co_await load_with_settings_loader_and_reader(path, *def, *mlr->loader, *mlr->reader);
    }
    co_return co_await load_with_settings_loader_and_reader(path, *settings, *mlr->loader, *mlr->reader);
}

asio::awaitable<std::expected<ErasedLoadedAsset, AssetLoadError>> AssetServer::load_direct_with_reader_untyped(
    const AssetPath& path, Reader& reader) const {
    // Use type-id-only lookup to get the right loader
    auto mlr = co_await get_meta_loader_and_reader(path, std::nullopt);
    if (!mlr) {
        // Fall back: just try extension-based lookup, use the provided reader directly
        auto maybe = [&]() -> std::optional<MaybeAssetLoader> {
            auto guard = data->loaders->read();
            return guard->get_by_path(path);
        }();
        if (!maybe) {
            co_return std::unexpected(mlr.error());
        }
        auto loader = maybe->get();
        if (!loader) {
            co_return std::unexpected(
                AssetLoadError{load_error::MissingAssetLoader{std::nullopt, std::nullopt, path, {}}});
        }
        auto def = loader->default_settings();
        co_return co_await load_with_settings_loader_and_reader(path, *def, *loader, reader);
    }
    // We have meta+loader but use the caller-provided reader instead of mlr's reader
    auto settings = mlr->meta->loader_settings();
    if (!settings) {
        auto def = mlr->loader->default_settings();
        co_return co_await load_with_settings_loader_and_reader(path, *def, *mlr->loader, reader);
    }
    co_return co_await load_with_settings_loader_and_reader(path, *settings, *mlr->loader, reader);
}

// ---------------------------------------------------------------------------
// load_asset_untyped
// Matches bevy_asset's AssetServer::load_asset_untyped.
// Inserts a pre-built asset and immediately sends a Loaded event.
// ---------------------------------------------------------------------------
UntypedHandle AssetServer::load_asset_untyped(std::optional<AssetPath> path, ErasedLoadedAsset asset) const {
    UntypedHandle handle = [&]() {
        auto guard = data->infos.write();
        if (path) {
            return guard->get_or_create_handle_untyped(*path, asset.asset_type_id(), HandleLoadingMode::NotLoading)
                .first;
        } else {
            return guard->create_loading_handle_untyped(asset.asset_type_id());
        }
    }();
    send_asset_event(InternalAssetEvent{internal_asset_event::Loaded{handle.id(), std::move(asset)}});
    return handle;
}

// ---------------------------------------------------------------------------
// reload_internal
// Matches bevy_asset's AssetServer::reload_internal.
// Spawns a task that calls load_internal for each handle to the path.
// ---------------------------------------------------------------------------
void AssetServer::reload_internal(const AssetPath& path, bool log) const {
    auto server     = *this;
    auto asset_path = path;

    // Synchronously mark existing handles as Loading so callers observe the transition immediately.
    {
        auto guard = data->infos.write();
        if (auto pit = guard->path_to_ids.find(asset_path); pit != guard->path_to_ids.end()) {
            for (auto& [type, id] : pit->second) {
                if (auto iit = guard->infos.find(id); iit != guard->infos.end()) {
                    if (!iit->second.weak_handle.expired()) {
                        iit->second.state = LoadStateOK::Loading;
                    }
                }
            }
        }
    }

    utils::IOTaskPool::instance().detach_task([server, asset_path, log]() {
        bool reloaded = false;

        // Collect handles while holding the lock, then release before calling load_internal
        std::vector<UntypedHandle> handles;
        {
            auto guard = server.data->infos.write();
            for (auto h : guard->get_handles_by_path(asset_path)) {
                guard->stats.started_load_tasks++;
                handles.push_back(h);
            }
        }

        asio::io_context ctx;
        for (auto& h : handles) {
            asio::co_spawn(ctx, server.load_internal(h, asset_path, true, std::nullopt), asio::detached);
            reloaded = true;
        }

        // If no handles were alive but the path still has living subassets, do an untyped reload
        if (!reloaded) {
            bool should = server.data->infos.read()->should_reload(asset_path);
            if (should) {
                server.data->infos.write()->stats.started_load_tasks++;
                asio::co_spawn(ctx, server.load_internal(std::nullopt, asset_path, true, std::nullopt), asio::detached);
                reloaded = true;
            }
        }

        ctx.run();

        if (log && reloaded) {
            spdlog::info("Reloaded {}", asset_path.string());
        }
    });
}
