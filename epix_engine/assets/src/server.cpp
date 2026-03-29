module;

#include <spdlog/spdlog.h>

module epix.assets;

import std;

import :store;

using namespace epix::assets;

AssetServer::AssetServer(std::shared_ptr<AssetSources> sources, AssetServerMode mode, bool watching_for_changes)
    : data(std::make_shared<AssetServerData>()) {
    data->sources                                                  = std::move(sources);
    data->mode                                                     = mode;
    data->watching_for_changes_flag                                = watching_for_changes;
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
    data->watching_for_changes_flag                                = watching_for_changes;
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
    data->watching_for_changes_flag                                = watching_for_changes;
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

void AssetServer::spawn_load_task(const UntypedHandle& handle, const AssetPath& path, AssetInfos& infos) const {
    infos.status.started_load_tasks++;

    auto server       = *this;  // Copy AssetServer (shared_ptr copy, cheap)
    auto owned_handle = handle;
    auto asset_path   = path;

    utils::IOTaskPool::instance().detach_task([server, owned_handle, asset_path]() mutable {
        auto asset_id = owned_handle.id();

        std::optional<MaybeAssetLoader> maybe_loader;
        {
            auto loaders_guard = server.data->loaders->read();
            maybe_loader       = loaders_guard->get_by_path(asset_path.path);
        }
        if (!maybe_loader) {
            spdlog::error("No loader found for asset path: {}", asset_path.string());
            server.send_asset_event(internal_asset_event::Failed{
                asset_id, asset_path,
                load_error::MissingAssetLoader{
                    std::nullopt, std::nullopt, asset_path,
                    std::vector<std::string>{std::string(asset_path.get_extension().value_or(""))}}});
            return;
        }
        auto loader = maybe_loader->get();
        if (!loader) {
            spdlog::error("Loader not available for asset path: {}", asset_path.string());
            return;
        }

        // Get the asset source
        auto source = server.data->sources->get(asset_path.source);
        if (!source) {
            spdlog::error("Asset source not found for: {}", asset_path.string());
            return;
        }

        // Get the appropriate reader
        const AssetReader* reader_ptr = nullptr;
        std::optional<std::reference_wrapper<const AssetReader>> processed_reader_opt;
        if (server.data->mode == AssetServerMode::Processed) {
            processed_reader_opt = source->get().processed_reader();
            if (!processed_reader_opt) {
                spdlog::error("No processed reader available for source: {}", asset_path.string());
                return;
            }
            reader_ptr = &processed_reader_opt->get();
        } else {
            reader_ptr = &source->get().reader();
        }

        auto read_result = reader_ptr->read(asset_path.path);
        if (!read_result) {
            spdlog::error("Failed to read asset: {}", asset_path.string());
            server.send_asset_event(internal_asset_event::Failed{
                asset_id, asset_path,
                load_error::AssetLoaderException{std::make_exception_ptr(std::runtime_error("Failed to read asset")),
                                                 asset_path, loader->loader_type().short_name()}});
            return;
        }

        auto context  = AssetServer::make_load_context(server, asset_path);
        auto settings = loader->default_settings();

        // Apply meta transform to override settings (e.g. from load_with_settings)
        if (auto* mt = owned_handle.meta_transform()) {
            // Create a temporary AssetMeta-like wrapper to apply the transform.
            // The meta_transform expects an AssetMetaDyn&, so we create a minimal one
            // that wraps the settings.
            struct SettingsMetaWrapper : AssetMetaDyn {
                Settings* m_settings;
                explicit SettingsMetaWrapper(Settings* s) : m_settings(s) {}
                std::optional<std::string_view> loader_name() const override { return std::nullopt; }
                std::optional<std::string_view> processor_name() const override { return std::nullopt; }
                AssetActionType action_type() const override { return AssetActionType::Load; }
                const ProcessedInfo* processed_info() const override { return nullptr; }
                Settings* loader_settings() override { return m_settings; }
                const Settings* loader_settings() const override { return m_settings; }
            };
            SettingsMetaWrapper wrapper(settings.get());
            (*mt)(wrapper);
        }

        auto load_result = loader->load(**read_result, *settings, context);
        if (load_result) {
            server.send_asset_event(internal_asset_event::Loaded{asset_id, std::move(load_result.value())});
        } else {
            spdlog::error("Failed to load asset: {}", asset_path.string());
            server.send_asset_event(internal_asset_event::Failed{
                asset_id, asset_path,
                load_error::AssetLoaderException{load_result.error(), asset_path, loader->loader_type().short_name()}});
        }
    });
}

void AssetServer::spawn_load_task(const UntypedHandle& handle, const AssetPath& path) const {
    auto guard = data->infos.write();
    spawn_load_task(handle, path, *guard);
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
    data->infos.write()->status.started_load_tasks++;

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
            std::visit(
                utils::visitor{
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
                        // Wake any tasks waiting on this asset
                        if (auto info = guard->get_info_mut(loaded_with_deps.id)) {
                            for (auto& waiter : info->get().waiting) {
                                waiter.wait();
                            }
                            info->get().waiting.clear();
                        }
                    },
                    [&](internal_asset_event::Failed& failed) {
                        guard->process_asset_fail(failed.id, failed.error);

                        // Collect untyped failure event
                        untyped_failures.push_back(UntypedAssetLoadFailedEvent{
                            failed.id, failed.path,
                            std::visit(utils::visitor{
                                           [](const load_error::RequestHandleMismatch& e)
                                               -> std::variant<std::string, std::exception_ptr> {
                                               return std::string("Request handle type mismatch for ") +
                                                      e.path.string();
                                           },
                                           [](const load_error::MissingAssetLoader& e)
                                               -> std::variant<std::string, std::exception_ptr> {
                                               return std::string("Missing asset loader for ") + e.path.string();
                                           },
                                           [](const load_error::AssetLoaderException& e)
                                               -> std::variant<std::string, std::exception_ptr> { return e.exception; },
                                       },
                                       failed.error)});

                        // Dispatch typed failure event
                        auto type_id = failed.id.type;
                        if (auto it = guard->dependency_failed_event_sender.find(type_id);
                            it != guard->dependency_failed_event_sender.end()) {
                            auto index = std::get<AssetIndex>(failed.id.id);
                            it->second(world, index, failed.path, failed.error);
                        }

                        spdlog::error("Asset load failed for {}", failed.path.string());
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
