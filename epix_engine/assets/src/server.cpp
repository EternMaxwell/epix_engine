module;

#include <spdlog/spdlog.h>

module epix.assets;

import std;

using namespace assets;

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

namespace assets {
bool asset_server_process_handle_destruction(const AssetServer& server, const UntypedAssetId& id) {
    return server.process_handle_destruction(id);
}

void log_asset_error(const AssetError& error, const std::string_view& header, const std::string_view& operation) {
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
}  // namespace assets

void AssetServer::spawn_load_task(const UntypedAssetId& id, const AssetPath& path) const {
    auto server     = *this;  // Copy AssetServer (shared_ptr copy, cheap)
    auto asset_id   = id;
    auto asset_path = path;

    utils::IOTaskPool::instance().detach_task([server, asset_id, asset_path]() mutable {
        std::optional<MaybeAssetLoader> maybe_loader;
        {
            auto loaders_guard = server.data->loaders->read();
            maybe_loader       = loaders_guard->get_by_path(asset_path.path);
        }
        if (!maybe_loader) {
            spdlog::error("No loader found for asset path: {}", asset_path.string());
            server.data->asset_event_sender.send(internal_asset_event::Failed{
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
            server.data->asset_event_sender.send(internal_asset_event::Failed{
                asset_id, asset_path,
                load_error::AssetLoaderException{std::make_exception_ptr(std::runtime_error("Failed to read asset")),
                                                 asset_path, loader->loader_type().short_name()}});
            return;
        }

        auto context     = AssetServer::make_load_context(server, asset_path);
        auto settings    = loader->default_settings();
        auto load_result = loader->load(**read_result, *settings, context);
        if (load_result) {
            server.data->asset_event_sender.send(
                internal_asset_event::Loaded{asset_id, std::move(load_result.value())});
        } else {
            spdlog::error("Failed to load asset: {}", asset_path.string());
            server.data->asset_event_sender.send(internal_asset_event::Failed{
                asset_id, asset_path,
                load_error::AssetLoaderException{load_result.error(), asset_path, loader->loader_type().short_name()}});
        }
    });
}

void AssetServer::handle_internal_events(core::ParamSet<core::World&, core::Res<AssetServer>> params) {
    auto&& [world, server] = params.get();
    auto receiver          = server->data->asset_event_receiver;
    while (auto event = receiver.try_receive()) {
        std::visit(utils::visitor{
                       [&](internal_asset_event::Loaded& loaded) {
                           auto guard = server->data->infos.write();
                           guard->process_asset_load(loaded.id, std::move(loaded.asset), world,
                                                     server->data->asset_event_sender);
                       },
                       [&](internal_asset_event::LoadedWithDeps&) {
                           // LoadedWithDependencies propagation is handled inside process_asset_load
                       },
                       [&](internal_asset_event::Failed& failed) {
                           auto guard = server->data->infos.write();
                           auto info  = guard->get_info_mut(failed.id);
                           if (info) {
                               info->get().state = failed.error;
                           }
                           spdlog::error("Asset load failed for {}", failed.path.string());
                       },
                   },
                   *event);
    }
}