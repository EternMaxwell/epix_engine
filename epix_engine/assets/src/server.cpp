module;

#include <spdlog/spdlog.h>

module epix.assets;

import std;

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

static void load_folder_recursive(const AssetSourceId& source,
                                  const std::filesystem::path& dir_path,
                                  const AssetReader& reader,
                                  const AssetServer& server,
                                  std::vector<UntypedHandle>& handles) {
    auto is_dir = reader.is_directory(dir_path);
    if (!is_dir || !*is_dir) return;

    auto dir_result = reader.read_directory(dir_path);
    if (!dir_result) return;

    for (auto child_path : *dir_result) {
        auto child_is_dir = reader.is_directory(child_path);
        if (child_is_dir && *child_is_dir) {
            load_folder_recursive(source, child_path, reader, server, handles);
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

        std::vector<UntypedHandle> handles;
        load_folder_recursive(asset_path.source, asset_path.path, *reader_ptr, server, handles);

        server.data->asset_event_sender.send(
            internal_asset_event::Loaded{asset_id, ErasedLoadedAsset::from_asset(LoadedFolder{std::move(handles)})});
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
std::expected<ErasedLoadedAsset, AssetLoadError> AssetServer::load_with_settings_loader_and_reader(
    const AssetPath& asset_path,
    const Settings& settings,
    const ErasedAssetLoader& loader,
    std::istream& reader) const {
    try {
        auto context     = AssetServer::make_load_context(*this, asset_path);
        auto load_result = loader.load(reader, settings, context);
        if (!load_result) {
            return std::unexpected(AssetLoadError{
                load_error::AssetLoaderException{load_result.error(), asset_path, loader.loader_type().short_name()}});
        }
        return std::move(*load_result);
    } catch (...) {
        return std::unexpected(AssetLoadError{
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
// NOTE: full .meta deserialization (settings override from file) still requires
// the caller to deserialize the full AssetMeta<LS,PS> with desialize_asset_meta().
// ---------------------------------------------------------------------------
std::optional<AssetServer::MetaLoaderReader> AssetServer::get_meta_loader_and_reader(
    const AssetPath& asset_path,
    std::optional<meta::type_index> asset_type_id,
    std::optional<AssetLoadError>& out_error) const {
    // 1. Resolve the source
    auto source_opt = get_source(asset_path.source);
    if (!source_opt) {
        out_error = AssetLoadError{load_error::MissingAssetSourceError{asset_path.source}};
        return std::nullopt;
    }
    const AssetSource& source = source_opt->get();

    // 2. Pick the reader (processed or raw)
    const AssetReader* reader_ptr = nullptr;
    std::optional<std::reference_wrapper<const AssetReader>> processed_reader_opt;
    if (data->mode == AssetServerMode::Processed) {
        processed_reader_opt = source.processed_reader();
        if (!processed_reader_opt) {
            out_error = AssetLoadError{load_error::MissingProcessedAssetReaderError{asset_path.source}};
            return std::nullopt;
        }
        reader_ptr = &processed_reader_opt->get();
    } else {
        reader_ptr = &source.reader();
    }

    // 3. Determine whether to check for a .meta file
    // Matches bevy_asset's AssetMetaCheck logic.
    bool read_meta =
        std::visit(utils::visitor{
                       [](const asset_meta_check::Always&) { return true; },
                       [](const asset_meta_check::Never&) { return false; },
                       [&asset_path](const asset_meta_check::Paths& p) { return p.paths.contains(asset_path); },
                   },
                   data->meta_check);

    // 4. Try to find a loader.  When read_meta is enabled we peek at the .meta
    //    file to get the loader_name.  We keep the bytes so we can deserialize
    //    the full settings after selecting the loader (step 5 below).
    std::shared_ptr<ErasedAssetLoader> loader;
    std::optional<std::vector<std::byte>> stored_meta_bytes;
    if (read_meta) {
        auto meta_bytes = reader_ptr->read_meta_bytes(asset_path.path);
        if (meta_bytes) {
            stored_meta_bytes = std::move(meta_bytes.value());
            // .meta file found — deserialize the minimal meta to look up the loader by name.
            auto minimal = deserialize_meta_minimal(*stored_meta_bytes);
            if (minimal && !minimal->asset.loader.empty()) {
                loader = [&]() -> std::shared_ptr<ErasedAssetLoader> {
                    auto guard = data->loaders->read();
                    auto maybe =
                        guard->find(std::string_view{minimal->asset.loader}, asset_type_id, std::nullopt, std::nullopt);
                    return maybe ? maybe->get() : nullptr;
                }();
            }
        }
        // fall through to extension-based lookup if no loader found from .meta
    }

    if (!loader) {
        // Use find() which correctly intersects type+extension (matches Bevy precedence).
        auto maybe = [&]() -> std::optional<MaybeAssetLoader> {
            auto guard = data->loaders->read();
            return guard->find(std::nullopt, asset_type_id, std::nullopt,
                               std::optional<std::reference_wrapper<const AssetPath>>{asset_path});
        }();

        if (!maybe) {
            std::vector<std::string> exts;
            if (auto e = asset_path.get_extension()) exts.push_back(std::string(*e));
            out_error = AssetLoadError{
                load_error::MissingAssetLoader{std::nullopt, asset_type_id, asset_path, std::move(exts)}};
            return std::nullopt;
        }
        loader = maybe->get();
        if (!loader) {
            std::vector<std::string> exts;
            if (auto e = asset_path.get_extension()) exts.push_back(std::string(*e));
            out_error = AssetLoadError{
                load_error::MissingAssetLoader{std::nullopt, asset_type_id, asset_path, std::move(exts)}};
            return std::nullopt;
        }
    }

    // 5. Build meta from the loader.  Prefer the stored .meta file bytes so that
    //    user-customised loader settings are respected (matches Bevy's
    //    ErasedAssetLoader::deserialize_meta path).  Return a DeserializeMeta error
    //    if the full meta bytes fail to deserialize (matches Bevy's load_internal).
    auto meta = [&]() -> std::expected<std::unique_ptr<AssetMetaDyn>, AssetLoadError> {
        if (stored_meta_bytes) {
            auto dm = loader->deserialize_meta(*stored_meta_bytes);
            if (dm) return std::move(*dm);
            return std::unexpected(AssetLoadError{load_error::DeserializeMeta{asset_path, std::string(dm.error())}});
        }
        return loader->default_meta();
    }();
    if (!meta) {
        out_error = meta.error();
        return std::nullopt;
    }

    // 5b. Check meta action type: Ignore and Process are not loadable directly.
    //     Matches bevy_asset AssetLoadError::CannotLoadIgnoredAsset / CannotLoadProcessedAsset.
    switch ((*meta)->action_type()) {
        case AssetActionType::Ignore:
            out_error = AssetLoadError{load_error::CannotLoadIgnoredAsset{asset_path}};
            return std::nullopt;
        case AssetActionType::Process:
            if (data->mode != AssetServerMode::Processed) {
                out_error = AssetLoadError{load_error::CannotLoadProcessedAsset{asset_path}};
                return std::nullopt;
            }
            break;
        case AssetActionType::Load:
            break;
    }

    // 6. Open the asset file for reading
    auto read_result = reader_ptr->read(asset_path.path);
    if (!read_result) {
        out_error = AssetLoadError{load_error::AssetReaderError{read_result.error()}};
        return std::nullopt;
    }

    return MetaLoaderReader{std::move(*meta), std::move(loader), std::move(*read_result)};
}

// ---------------------------------------------------------------------------
// load_internal
// Matches bevy_asset's AssetServer::load_internal.
// Called from inside a task spawned by spawn_load_task.
// ---------------------------------------------------------------------------
void AssetServer::load_internal(std::optional<UntypedHandle> input_handle,
                                AssetPath path,
                                bool force,
                                std::optional<MetaTransform> meta_transform) const {
    // Determine asset_type_id hint from input handle (if typed)
    std::optional<meta::type_index> input_type_id;
    if (input_handle) input_type_id = input_handle->type();

    // --- Get meta, loader and reader ---------------------------------
    std::optional<AssetLoadError> get_error;
    auto mlr = get_meta_loader_and_reader(path, input_type_id, get_error);
    if (!mlr) {
        // If we had an input handle, propagate failure so the handle's state is updated
        if (input_handle) {
            send_asset_event(InternalAssetEvent{internal_asset_event::Failed{
                input_handle->id(), path,
                get_error.value_or(
                    AssetLoadError{load_error::MissingAssetLoader{std::nullopt, input_type_id, path, {}}})}});
        }
        return;
    }
    auto& [meta, loader, reader] = *mlr;

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
    // Use a lambda to guarantee asset_id is always initialized before use.
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
    if (!asset_id) return;  // no provider

    // --- Early-out if already loaded ---------------------------------
    if (!should_load && !force) {
        return;
    }

    // --- Verify type matches loader ----------------------------------
    if (asset_id->type != loader->asset_type()) {
        auto err = AssetLoadError{load_error::RequestHandleMismatch{path, asset_id->type, loader->asset_type(),
                                                                    loader->loader_type().short_name()}};
        send_asset_event(InternalAssetEvent{internal_asset_event::Failed{*asset_id, path, err}});
        return;
    }

    // --- Load --------------------------------------------------------
    auto settings_ptr = meta->loader_settings();
    if (!settings_ptr) {
        auto err = AssetLoadError{load_error::AssetLoaderException{
            std::make_exception_ptr(std::runtime_error("Asset meta has no loader settings")), path,
            loader->loader_type().short_name()}};
        send_asset_event(InternalAssetEvent{internal_asset_event::Failed{*asset_id, path, err}});
        return;
    }

    auto load_result = load_with_settings_loader_and_reader(path, *settings_ptr, *loader, *reader);
    if (load_result) {
        send_asset_event(InternalAssetEvent{internal_asset_event::Loaded{*asset_id, std::move(*load_result)}});
    } else {
        // Detailed error is logged later in handle_internal_events when the Failed event is processed.
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
        server.load_internal(std::move(owned_handle), std::move(asset_path), false, std::nullopt);
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
std::expected<ErasedLoadedAsset, AssetLoadError> AssetServer::load_direct_untyped(const AssetPath& path) const {
    std::optional<AssetLoadError> err;
    auto mlr = get_meta_loader_and_reader(path, std::nullopt, err);
    if (!mlr) {
        return std::unexpected(
            err.value_or(AssetLoadError{load_error::MissingAssetLoader{std::nullopt, std::nullopt, path, {}}}));
    }
    auto settings = mlr->meta->loader_settings();
    if (!settings) {
        auto def = mlr->loader->default_settings();
        return load_with_settings_loader_and_reader(path, *def, *mlr->loader, *mlr->reader);
    }
    return load_with_settings_loader_and_reader(path, *settings, *mlr->loader, *mlr->reader);
}

std::expected<ErasedLoadedAsset, AssetLoadError> AssetServer::load_direct_with_reader_untyped(
    const AssetPath& path, std::istream& reader) const {
    // Use type-id-only lookup to get the right loader
    std::optional<AssetLoadError> err;
    auto mlr = get_meta_loader_and_reader(path, std::nullopt, err);
    if (!mlr) {
        // Fall back: just try extension-based lookup, use the provided reader directly
        auto maybe = [&]() -> std::optional<MaybeAssetLoader> {
            auto guard = data->loaders->read();
            return guard->get_by_path(path);
        }();
        if (!maybe) {
            return std::unexpected(
                err.value_or(AssetLoadError{load_error::MissingAssetLoader{std::nullopt, std::nullopt, path, {}}}));
        }
        auto loader = maybe->get();
        if (!loader) {
            return std::unexpected(
                AssetLoadError{load_error::MissingAssetLoader{std::nullopt, std::nullopt, path, {}}});
        }
        auto def = loader->default_settings();
        return load_with_settings_loader_and_reader(path, *def, *loader, reader);
    }
    // We have meta+loader but use the caller-provided reader instead of mlr's reader
    auto settings = mlr->meta->loader_settings();
    if (!settings) {
        auto def = mlr->loader->default_settings();
        return load_with_settings_loader_and_reader(path, *def, *mlr->loader, reader);
    }
    return load_with_settings_loader_and_reader(path, *settings, *mlr->loader, reader);
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

        for (auto& h : handles) {
            server.load_internal(h, asset_path, true, std::nullopt);
            reloaded = true;
        }

        // If no handles were alive but the path still has living subassets, do an untyped reload
        if (!reloaded) {
            bool should = server.data->infos.read()->should_reload(asset_path);
            if (should) {
                server.data->infos.write()->stats.started_load_tasks++;
                server.load_internal(std::nullopt, asset_path, true, std::nullopt);
                reloaded = true;
            }
        }

        if (log && reloaded) {
            spdlog::info("Reloaded {}", asset_path.string());
        }
    });
}
