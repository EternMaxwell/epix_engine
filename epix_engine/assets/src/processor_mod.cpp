module;

#ifndef EPIX_IMPORT_STD
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <variant>
#include <vector>
#endif
#include <spdlog/spdlog.h>

#include <asio/awaitable.hpp>

module epix.assets;
#ifdef EPIX_IMPORT_STD
import std;
#endif
using namespace epix::assets;

// ---- ProcessorAssetInfo ----

ProcessorAssetInfo::ProcessorAssetInfo() {
    auto [sender, receiver] = async_broadcast::broadcast<ProcessStatus>(1);
    sender.set_overflow(true);
    status_sender   = std::move(sender);
    status_receiver = std::move(receiver);
}

asio::awaitable<void> ProcessorAssetInfo::update_status(ProcessStatus new_status) {
    if (status != new_status) {
        status = new_status;
        (void)co_await status_sender.broadcast(new_status);
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

asio::awaitable<std::shared_ptr<std::shared_mutex>> ProcessorAssetInfos::remove(const AssetPath& asset_path) {
    auto it = infos.find(asset_path);
    if (it == infos.end()) co_return nullptr;
    auto info = std::move(it->second);
    infos.erase(it);
    if (info.processed_info) {
        clear_dependencies(asset_path, *info.processed_info);
    }
    (void)co_await info.status_sender.broadcast(ProcessStatus::NonExistent);
    if (!info.dependents.empty()) {
        spdlog::error("Asset {} was removed but had dependents. Consider updating paths.", asset_path.string());
        non_existent_dependents[asset_path] = std::move(info.dependents);
    }
    co_return info.file_transaction_lock;
}

asio::awaitable<std::optional<std::pair<std::shared_ptr<std::shared_mutex>, std::shared_ptr<std::shared_mutex>>>>
ProcessorAssetInfos::rename(const AssetPath& old_path,
                            const AssetPath& new_path,
                            async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>>& reprocess_sender) {
    auto it = infos.find(old_path);
    if (it == infos.end()) co_return std::nullopt;

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

    (void)co_await info.status_sender.broadcast(ProcessStatus::NonExistent);

    auto old_lock           = info.file_transaction_lock;
    auto& new_info          = get_or_insert(new_path);
    new_info.processed_info = std::move(info.processed_info);
    new_info.status         = info.status;
    if (new_info.status) {
        (void)co_await new_info.status_sender.broadcast(*new_info.status);
    }

    (void)co_await reprocess_sender.send(std::pair{new_path.source, new_path.path});
    auto dependents = std::vector<AssetPath>(new_info.dependents.begin(), new_info.dependents.end());
    for (const auto& dependent : dependents) {
        (void)co_await reprocess_sender.send(std::pair{dependent.source, dependent.path});
    }

    co_return std::pair{std::move(old_lock), new_info.file_transaction_lock};
}

asio::awaitable<void> ProcessorAssetInfos::finish_processing(
    const AssetPath& asset_path,
    std::expected<ProcessResult, ProcessError>& result,
    async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>>& reprocess_sender) {
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
                co_await info.update_status(ProcessStatus::Processed);
                auto dependents = std::vector<AssetPath>(info.dependents.begin(), info.dependents.end());
                for (auto& path : dependents) {
                    (void)co_await reprocess_sender.send(std::pair{path.source, path.path});
                }
                break;
            }
            case ProcessResultKind::SkippedNotChanged: {
                spdlog::debug("Skipping processing (unchanged) \"{}\"", asset_path.string());
                auto* info = get(asset_path);
                if (info) co_await info->update_status(ProcessStatus::Processed);
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
            if (info) co_await info->update_status(ProcessStatus::Failed);
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

void AssetProcessorData::set_task_sender(
    async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>> sender) const {
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

asio::awaitable<ProcessStatus> AssetProcessorData::wait_until_processed(const AssetPath& path) const {
    co_return co_await processing_state->wait_until_processed(path);
}

asio::awaitable<void> AssetProcessorData::wait_until_initialized() const {
    co_await processing_state->wait_until_initialized();
}

asio::awaitable<void> AssetProcessorData::wait_until_finished() const {
    co_await processing_state->wait_until_finished();
}

asio::awaitable<ProcessorState> AssetProcessorData::state() const { co_return co_await processing_state->get_state(); }

// ---- ProcessingState ----

ProcessingState::ProcessingState() {
    auto [is, ir] = async_broadcast::broadcast<bool>(1);
    auto [fs, fr] = async_broadcast::broadcast<bool>(1);
    is.set_overflow(true);
    fs.set_overflow(true);
    m_initialized_sender   = std::move(is);
    m_initialized_receiver = std::move(ir);
    m_finished_sender      = std::move(fs);
    m_finished_receiver    = std::move(fr);
}

asio::awaitable<void> ProcessingState::set_state(ProcessorState state) {
    ProcessorState last_state;
    {
        auto guard = m_state.write();
        last_state = *guard;
        *guard     = state;
    }
    if (last_state != ProcessorState::Finished && state == ProcessorState::Finished) {
        (void)co_await m_finished_sender.broadcast(true);
    } else if (last_state != ProcessorState::Processing && state == ProcessorState::Processing) {
        (void)co_await m_initialized_sender.broadcast(true);
    }
}

asio::awaitable<ProcessorState> ProcessingState::get_state() const {
    auto guard = m_state.read();
    co_return *guard;
}

asio::awaitable<ProcessStatus> ProcessingState::wait_until_processed(const AssetPath& path) const {
    // Fast path: asset already tracked and has a final status.
    {
        auto guard = m_asset_infos.read();
        if (auto* info = guard->get(path)) {
            if (info->status) co_return *info->status;
        }
    }

    // Wait until initialization (which registers every source path).
    co_await wait_until_initialized();

    // After initialization, check for a status or subscribe to the per-asset receiver.
    std::optional<async_broadcast::Receiver<ProcessStatus>> pending_receiver;
    {
        auto guard = m_asset_infos.read();
        if (auto* info = guard->get(path)) {
            if (info->status) co_return *info->status;
            pending_receiver = info->status_receiver;
        }
    }
    if (pending_receiver) {
        auto result = co_await pending_receiver->recv();
        if (result) co_return *result;
    }

    // Fall back to waiting for the global finished signal.
    co_await wait_until_finished();
    {
        auto guard = m_asset_infos.read();
        if (auto* info = guard->get(path)) {
            co_return info->status.value_or(ProcessStatus::NonExistent);
        }
    }
    co_return ProcessStatus::NonExistent;
}

asio::awaitable<void> ProcessingState::wait_until_initialized() const {
    std::optional<async_broadcast::Receiver<bool>> receiver;
    {
        auto guard = m_state.read();
        if (*guard == ProcessorState::Initializing) {
            receiver = m_initialized_receiver;
        }
    }
    if (receiver) {
        (void)co_await receiver->recv();
    }
}

asio::awaitable<void> ProcessingState::wait_until_finished() const {
    std::optional<async_broadcast::Receiver<bool>> receiver;
    {
        auto guard = m_state.read();
        if (*guard != ProcessorState::Finished) {
            receiver = m_finished_receiver;
        }
    }
    if (receiver) {
        (void)co_await receiver->recv();
    }
}

void ProcessingState::shutdown() {
    m_initialized_sender.close();
    m_finished_sender.close();
    auto infos = m_asset_infos.write();
    for (auto& [_, info] : infos->infos) {
        info.status_sender.close();
    }
}

asio::awaitable<std::expected<std::shared_ptr<std::shared_mutex>, AssetReaderError>>
ProcessingState::get_transaction_lock(const AssetPath& path) const {
    auto guard = m_asset_infos.read();
    auto* info = guard->get(path);
    if (!info) {
        co_return std::unexpected(AssetReaderError(reader_errors::NotFound{path.path}));
    }
    co_return info->file_transaction_lock;
}
