module;

#include <spdlog/spdlog.h>

module epix.assets;

import std;

using namespace epix::assets;

// ---- ProcessorAssetInfo ----

ProcessorAssetInfo::ProcessorAssetInfo() {
    auto [sender, receiver] = utils::make_broadcast_channel<ProcessStatus>();
    status_sender           = std::move(sender);
    status_receiver         = std::move(receiver);
}

void ProcessorAssetInfo::update_status(ProcessStatus new_status) {
    if (status != new_status) {
        status = new_status;
        status_sender.send(new_status);
    }
}

// ---- ProcessorAssetInfos ----

ProcessorAssetInfo& ProcessorAssetInfos::get_or_insert(const AssetPath& asset_path) {
    auto [it, inserted] = infos.try_emplace(asset_path);
    if (inserted) {
        if (auto dep_it = non_existent_dependents.find(asset_path); dep_it != non_existent_dependents.end()) {
            it->second.dependents = std::move(dep_it->second);
            non_existent_dependents.erase(dep_it);
        }
    }
    return it->second;
}

ProcessorAssetInfo* ProcessorAssetInfos::get(const AssetPath& asset_path) {
    auto it = infos.find(asset_path);
    return it != infos.end() ? &it->second : nullptr;
}

const ProcessorAssetInfo* ProcessorAssetInfos::get(const AssetPath& asset_path) const {
    auto it = infos.find(asset_path);
    return it != infos.end() ? &it->second : nullptr;
}

void ProcessorAssetInfos::add_dependent(const AssetPath& asset_path, AssetPath dependent) {
    if (auto* info = get(asset_path)) {
        info->dependents.insert(std::move(dependent));
    } else {
        non_existent_dependents[asset_path].insert(std::move(dependent));
    }
}

std::shared_ptr<std::shared_mutex> ProcessorAssetInfos::remove(const AssetPath& asset_path) {
    auto it = infos.find(asset_path);
    if (it == infos.end()) return nullptr;
    auto info = std::move(it->second);
    infos.erase(it);
    if (info.processed_info) {
        clear_dependencies(asset_path, *info.processed_info);
    }
    info.status_sender.send(ProcessStatus::NonExistent);
    if (!info.dependents.empty()) {
        spdlog::error("Asset {} was removed but had dependents. Consider updating paths.", asset_path.string());
        non_existent_dependents[asset_path] = std::move(info.dependents);
    }
    return info.file_transaction_lock;
}

std::optional<std::pair<std::shared_ptr<std::shared_mutex>, std::shared_ptr<std::shared_mutex>>>
ProcessorAssetInfos::rename(const AssetPath& old_path,
                            const AssetPath& new_path,
                            utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& reprocess_sender) {
    auto it = infos.find(old_path);
    if (it == infos.end()) return std::nullopt;

    auto info = std::move(it->second);
    infos.erase(it);

    if (!info.dependents.empty()) {
        spdlog::error(
            "The asset at {} was removed, but it had assets that depend on it to be processed. Consider updating the "
            "path in the following assets.",
            old_path.string());
        non_existent_dependents.emplace(old_path, std::move(info.dependents));
    }

    if (info.processed_info) {
        for (const auto& dep : info.processed_info->process_dependencies) {
            AssetPath dep_path(dep.path);
            if (auto dep_it = infos.find(dep_path); dep_it != infos.end()) {
                dep_it->second.dependents.erase(old_path);
                dep_it->second.dependents.insert(new_path);
            } else if (auto dep_it = non_existent_dependents.find(dep_path); dep_it != non_existent_dependents.end()) {
                dep_it->second.erase(old_path);
                dep_it->second.insert(new_path);
            }
        }
    }

    info.status_sender.send(ProcessStatus::NonExistent);

    auto old_lock           = info.file_transaction_lock;
    auto& new_info          = get_or_insert(new_path);
    new_info.processed_info = std::move(info.processed_info);
    new_info.status         = info.status;
    if (new_info.status) {
        new_info.status_sender.send(*new_info.status);
    }

    reprocess_sender.send(std::pair{new_path.source, new_path.path});
    auto dependents = std::vector<AssetPath>(new_info.dependents.begin(), new_info.dependents.end());
    for (const auto& dependent : dependents) {
        reprocess_sender.send(std::pair{dependent.source, dependent.path});
    }

    return std::pair{std::move(old_lock), new_info.file_transaction_lock};
}

void ProcessorAssetInfos::finish_processing(
    const AssetPath& asset_path,
    std::expected<ProcessResult, ProcessError>& result,
    utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& reprocess_sender) {
    if (result) {
        switch (result->kind) {
            case ProcessResultKind::Processed: {
                spdlog::debug("Finished processing \"{}\"", asset_path.string());
                auto* existing = get(asset_path);
                if (existing && existing->processed_info) {
                    clear_dependencies(asset_path, *existing->processed_info);
                }
                for (auto& dep_info : result->processed_info->process_dependencies) {
                    add_dependent(AssetPath(dep_info.path), asset_path);
                }
                auto& info          = get_or_insert(asset_path);
                info.processed_info = std::move(result->processed_info);
                info.update_status(ProcessStatus::Processed);
                auto dependents = std::vector<AssetPath>(info.dependents.begin(), info.dependents.end());
                for (auto& path : dependents) {
                    reprocess_sender.send(std::pair{path.source, path.path});
                }
                break;
            }
            case ProcessResultKind::SkippedNotChanged: {
                spdlog::debug("Skipping processing (unchanged) \"{}\"", asset_path.string());
                auto* info = get(asset_path);
                if (info) info->update_status(ProcessStatus::Processed);
                break;
            }
            case ProcessResultKind::Ignored:
                spdlog::debug("Skipping processing (ignored) \"{}\"", asset_path.string());
                break;
        }
    } else {
        auto& err = result.error();
        bool handled =
            std::visit(utils::visitor{
                           [](const process_errors::ExtensionRequired&) { return true; },
                           [&](const process_errors::MissingAssetLoaderForExtension&) {
                               spdlog::trace("No loader found for {}", asset_path.string());
                               return true;
                           },
                           [&](const process_errors::AssetReaderError&) {
                               spdlog::trace("No need to process {} because it does not exist", asset_path.string());
                               return true;
                           },
                           [&](const auto&) { return false; },
                       },
                       err);
        if (!handled) {
            spdlog::error("Failed to process asset {}", asset_path.string());
            auto* info = get(asset_path);
            if (info) info->update_status(ProcessStatus::Failed);
        }
    }
}

void ProcessorAssetInfos::clear_dependencies(const AssetPath& asset_path, const ProcessedInfo& removed_info) {
    for (auto& old_dep : removed_info.process_dependencies) {
        AssetPath dep_path(old_dep.path);
        if (auto* info = get(dep_path)) {
            info->dependents.erase(asset_path);
        } else if (auto it = non_existent_dependents.find(dep_path); it != non_existent_dependents.end()) {
            it->second.erase(asset_path);
        }
    }
}

// ---- AssetProcessorData ----

AssetProcessorData::AssetProcessorData(std::shared_ptr<AssetSources> sources,
                                       std::shared_ptr<ProcessingState> processing_state)
    : AssetProcessorData(
          std::move(sources), std::move(processing_state), std::make_unique<FileTransactionLogFactory>()) {}

AssetProcessorData::AssetProcessorData(std::shared_ptr<AssetSources> sources,
                                       std::shared_ptr<ProcessingState> processing_state,
                                       std::unique_ptr<ProcessorTransactionLogFactory> transaction_log_factory)
    : processing_state(std::move(processing_state)),
      log_factory(std::optional<std::unique_ptr<ProcessorTransactionLogFactory>>(std::move(transaction_log_factory))),
      log(std::optional<std::unique_ptr<ProcessorTransactionLog>>{}),
      sources(std::move(sources)),
      task_sender(AssetProcessorData::TaskSenderState{}) {}

void AssetProcessorData::set_task_sender(utils::Sender<std::pair<AssetSourceId, std::filesystem::path>> sender) const {
    auto guarded    = task_sender.lock();
    guarded->sender = std::move(sender);
    if (guarded->shutdown_requested) {
        guarded->sender->close();
        guarded->sender.reset();
    }
}

void AssetProcessorData::shutdown() const {
    {
        auto guarded                = task_sender.lock();
        guarded->shutdown_requested = true;
        if (guarded->sender.has_value()) {
            guarded->sender->close();
            guarded->sender.reset();
        }
    }
    processing_state->shutdown();
}

std::expected<void, SetTransactionLogFactoryError> AssetProcessorData::set_log_factory(
    std::unique_ptr<ProcessorTransactionLogFactory> factory) const {
    auto guarded_factory = log_factory.lock();
    if (!guarded_factory->has_value()) {
        return std::unexpected(SetTransactionLogFactoryError{set_transaction_log_factory_errors::AlreadyInUse{}});
    }
    *guarded_factory = std::move(factory);
    return {};
}

ProcessStatus AssetProcessorData::wait_until_processed(const AssetPath& path) const {
    return processing_state->wait_until_processed(path);
}

void AssetProcessorData::wait_until_initialized() const { processing_state->wait_until_initialized(); }

void AssetProcessorData::wait_until_finished() const { processing_state->wait_until_finished(); }

ProcessorState AssetProcessorData::state() const { return processing_state->get_state(); }

// ---- ProcessingState ----

ProcessingState::ProcessingState() {
    auto [is, ir]          = utils::make_broadcast_channel<bool>();
    auto [fs, fr]          = utils::make_broadcast_channel<bool>();
    m_initialized_sender   = std::move(is);
    m_initialized_receiver = std::move(ir);
    m_finished_sender      = std::move(fs);
    m_finished_receiver    = std::move(fr);
}

void ProcessingState::set_state(ProcessorState state) {
    {
        auto guard = m_state.write();
        *guard     = state;
    }
    switch (state) {
        case ProcessorState::Processing:
            m_initialized_sender.send(true);
            break;
        case ProcessorState::Finished:
            m_finished_sender.send(true);
            break;
        default:
            break;
    }
}

ProcessorState ProcessingState::get_state() const {
    auto guard = m_state.read();
    return *guard;
}

ProcessStatus ProcessingState::wait_until_processed(const AssetPath& path) const {
    // Fast path: asset already tracked and has a final status.
    {
        auto guard = m_asset_infos.read();
        if (auto* info = guard->get(path)) {
            if (info->status) return *info->status;
        }
    }

    // Asset not yet in the map — initialize() hasn't run yet.  Wait only for initialization
    // (which registers every source path) rather than for ALL processing to finish.
    wait_until_initialized();

    // After initialization every source path is registered.  Check for a status or pick up
    // the per-asset receiver so we block only on this one asset, not the entire pipeline.
    std::optional<utils::BroadcastReceiver<ProcessStatus>> pending_receiver;
    {
        auto guard = m_asset_infos.read();
        if (auto* info = guard->get(path)) {
            if (info->status) return *info->status;
            pending_receiver = info->status_receiver;
        }
    }
    if (pending_receiver) {
        auto status = pending_receiver->receive();
        if (status) return *status;
    }

    // Asset not tracked even after initialization — unknown/nonexistent path.
    // Fall back to the global finished signal as a safety net.
    wait_until_finished();
    {
        auto guard = m_asset_infos.read();
        if (auto* info = guard->get(path)) {
            return info->status.value_or(ProcessStatus::NonExistent);
        }
    }
    return ProcessStatus::NonExistent;
}

void ProcessingState::wait_until_initialized() const {
    auto receiver = m_initialized_receiver;
    auto ready    = receiver.receive();
    (void)ready;
}

void ProcessingState::wait_until_finished() const {
    auto receiver = m_finished_receiver;
    auto ready    = receiver.receive();
    (void)ready;
}

void ProcessingState::shutdown() {
    m_initialized_sender.close();
    m_finished_sender.close();
    auto infos = m_asset_infos.write();
    for (auto& [_, info] : infos->infos) {
        info.status_sender.close();
    }
}

std::expected<std::shared_ptr<std::shared_mutex>, AssetReaderError> ProcessingState::get_transaction_lock(
    const AssetPath& path) const {
    auto guard = m_asset_infos.read();
    auto* info = guard->get(path);
    if (!info) {
        return std::unexpected(AssetReaderError(reader_errors::NotFound{path.path}));
    }
    return info->file_transaction_lock;
}
