module;

#include <spdlog/spdlog.h>

module epix.assets;

import std;

using namespace epix::assets;

// ---- ProcessorAssetInfo ----

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

AssetProcessorData::AssetProcessorData()
    : processing_state(std::make_shared<ProcessingState>()), source_builders(std::make_shared<AssetSourceBuilders>()) {}

void AssetProcessorData::set_log_factory(std::shared_ptr<ProcessorTransactionLogFactory> factory) {
    log_factory = std::move(factory);
}

void AssetProcessorData::wait_until_initialized() const { processing_state->wait_until_initialized(); }

void AssetProcessorData::wait_until_finished() const { processing_state->wait_until_finished(); }

ProcessorState AssetProcessorData::state() const { return processing_state->get_state(); }

// ---- AssetProcessor ----

AssetProcessor::AssetProcessor(AssetServer srv, std::shared_ptr<AssetProcessorData> proc_data)
    : server(std::move(srv)), data(std::move(proc_data)) {}

const AssetServer& AssetProcessor::get_server() const { return server; }

const std::shared_ptr<AssetProcessorData>& AssetProcessor::get_data() const { return data; }

std::optional<std::reference_wrapper<const AssetSource>> AssetProcessor::get_source(
    const AssetSourceId& source_id) const {
    return server.get_source(source_id);
}

const std::shared_ptr<AssetSources>& AssetProcessor::sources() const { return server.data->sources; }

std::shared_ptr<ErasedProcessor> AssetProcessor::get_default_processor(std::string_view extension) const {
    auto guard  = data->processors.read();
    auto ext_it = guard->file_extension_to_default_processor.find(std::string(extension));
    if (ext_it == guard->file_extension_to_default_processor.end()) return nullptr;
    auto proc_it = guard->type_path_to_processor.find(ext_it->second);
    if (proc_it == guard->type_path_to_processor.end()) return nullptr;
    return proc_it->second;
}

std::expected<std::shared_ptr<ErasedProcessor>, GetProcessorError> AssetProcessor::get_processor(
    std::string_view type_name) const {
    auto guard = data->processors.read();
    // First try short type path lookup
    auto short_it = guard->short_type_path_to_processor.find(type_name);
    if (short_it != guard->short_type_path_to_processor.end()) {
        return std::visit(
            utils::visitor{
                [](const ShortTypeProcessorEntry::Unique& u)
                    -> std::expected<std::shared_ptr<ErasedProcessor>, GetProcessorError> { return u.processor; },
                [&](const ShortTypeProcessorEntry::Ambiguous& a)
                    -> std::expected<std::shared_ptr<ErasedProcessor>, GetProcessorError> {
                    return std::unexpected(
                        GetProcessorError{get_processor_errors::Ambiguous{std::string(type_name), a.type_paths}});
                },
            },
            short_it->second.entry);
    }
    // Fall back to full type path
    auto it = guard->type_path_to_processor.find(type_name);
    if (it != guard->type_path_to_processor.end()) return it->second;
    return std::unexpected(GetProcessorError{get_processor_errors::Missing{std::string(type_name)}});
}

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
    // First check if already processed
    std::optional<utils::BroadcastReceiver<ProcessStatus>> pending_receiver;
    {
        auto guard = m_asset_infos.read();
        if (auto* info = guard->get(path)) {
            if (info->status) {
                return *info->status;
            }
            // Not yet processed; clone the receiver to wait on
            pending_receiver = info->status_receiver;
        }
    }
    if (pending_receiver) {
        auto status = pending_receiver->receive();
        if (status) {
            return *status;
        }
    }
    // Asset not tracked yet, wait for finished state instead
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

std::expected<std::shared_ptr<std::shared_mutex>, AssetReaderError> ProcessingState::get_transaction_lock(
    const AssetPath& path) const {
    auto guard = m_asset_infos.read();
    auto* info = guard->get(path);
    if (!info) {
        return std::unexpected(AssetReaderError(reader_errors::NotFound{path.path}));
    }
    return info->file_transaction_lock;
}
