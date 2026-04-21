module;

#ifndef EPIX_IMPORT_STD
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <chrono>
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
import epix.tasks;

using namespace epix::assets;

namespace {

std::optional<std::int64_t> to_persisted_mtime_ns(std::optional<std::filesystem::file_time_type> time) {
    if (!time.has_value()) return std::nullopt;
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(time->time_since_epoch()).count();
    return static_cast<std::int64_t>(ns);
}

}  // namespace

// ---- start ----

void AssetProcessor::start(core::Res<AssetProcessor> processor) {
    auto proc = *processor;
    tasks::IoTaskPool::get()
        .spawn([](AssetProcessor proc) mutable -> asio::awaitable<void> {
            auto start_time = std::chrono::steady_clock::now();
            spdlog::debug("Processing Assets");

            co_await proc.initialize();

            auto [new_task_sender, new_task_receiver] =
                async_channel::unbounded<std::pair<AssetSourceId, std::filesystem::path>>();
            proc.data->set_task_sender(new_task_sender);

            co_await proc.queue_initial_processing_tasks(new_task_sender);

            // Spawn task executor in background.
            {
                auto p = proc;
                auto s = new_task_sender;
                auto r = new_task_receiver;
                tasks::IoTaskPool::get()
                    .spawn([](AssetProcessor p,
                              async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>> s,
                              async_channel::Receiver<std::pair<AssetSourceId, std::filesystem::path>> r)
                               -> asio::awaitable<void> {
                        co_await p.execute_processing_tasks(std::move(s), std::move(r));
                    }(std::move(p), std::move(s), std::move(r)))
                    .detach();
            }

            co_await proc.data->wait_until_finished();

            auto end_time = std::chrono::steady_clock::now();
            auto elapsed  = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            spdlog::debug("Processing finished in {}ms", elapsed.count());

            spdlog::debug("Listening for changes to source assets");
            proc.spawn_source_change_event_listeners(new_task_sender);
        }(std::move(proc)))
        .detach();
}

// ---- Logging helpers ----

void AssetProcessor::log_begin_processing(const AssetPath& path) const {
    auto log = data->log.write();
    if (log->has_value() && log->value()) {
        if (auto result = log->value()->begin_processing(path); !result) {
            spdlog::error("Failed to write begin-processing log entry for {}: {}", path.string(), result.error());
        }
    }
}

void AssetProcessor::log_end_processing(const AssetPath& path) const {
    auto log = data->log.write();
    if (log->has_value() && log->value()) {
        if (auto result = log->value()->end_processing(path); !result) {
            spdlog::error("Failed to write end-processing log entry for {}: {}", path.string(), result.error());
        }
    }
}

void AssetProcessor::log_unrecoverable() const {
    auto log = data->log.write();
    if (log->has_value() && log->value()) {
        if (auto result = log->value()->unrecoverable(); !result) {
            spdlog::error("Failed to write unrecoverable log entry: {}", result.error());
        }
    }
}

// ---- validate_transaction_log_and_recover ----

asio::awaitable<std::filesystem::path> AssetProcessor::validate_transaction_log_and_recover() const {
    std::unique_ptr<ProcessorTransactionLogFactory> log_factory;
    {
        auto guarded_log_factory = data->log_factory.lock();
        if (!guarded_log_factory->has_value()) {
            spdlog::error("Asset processor log factory not set. Cannot validate transaction log.");
            co_return std::filesystem::path{};
        }
        log_factory = std::move(guarded_log_factory->value());
        guarded_log_factory->reset();
    }

    if (!log_factory) {
        spdlog::error("Asset processor log factory not set. Cannot validate transaction log.");
        co_return std::filesystem::path{};
    }
    auto log_path = log_factory->log_path();
    auto result   = validate_transaction_log(*log_factory);
    if (!result) {
        auto& err        = result.error();
        bool state_valid = true;
        // Collect paths that need cleanup — can't co_await inside std::visit lambdas
        std::vector<AssetPath> unfinished_paths;
        std::visit(utils::visitor{
                       [&](const validate_log_errors::ReadLogError& e) {
                           spdlog::error(
                               "Failed to read processor log file. Processed assets cannot be validated "
                               "so they must be re-generated: {}",
                               e.msg);
                           state_valid = false;
                       },
                       [&](const validate_log_errors::UnrecoverableError&) {
                           spdlog::error(
                               "Encountered an unrecoverable error in the last run. Processed assets "
                               "cannot be validated so they must be re-generated");
                           state_valid = false;
                       },
                       [&](const validate_log_errors::EntryErrors& e) {
                           for (auto& entry_err : e.errors) {
                               std::visit(utils::visitor{
                                              [&](const log_entry_errors::DuplicateTransaction& t) {
                                                  spdlog::error("Log entry error for {}", t.path.string());
                                                  state_valid = false;
                                              },
                                              [&](const log_entry_errors::EndedMissingTransaction& t) {
                                                  spdlog::error("Log entry error for {}", t.path.string());
                                                  state_valid = false;
                                              },
                                              [&](const log_entry_errors::UnfinishedTransaction& t) {
                                                  unfinished_paths.push_back(t.path);
                                              },
                                          },
                                          entry_err);
                               if (!state_valid) break;
                           }
                       },
                   },
                   err);
        // Process unfinished paths (async writer calls)
        for (auto& upath : unfinished_paths) {
            spdlog::debug("Asset {:?} did not finish processing. Clearing state.", upath.string());
            auto source_opt = get_source(upath.source);
            if (!source_opt) {
                spdlog::error("Failed to remove asset {}: AssetSource does not exist", upath.string());
                state_valid = false;
                continue;
            }
            auto& source    = source_opt->get();
            auto writer_opt = source.processed_writer();
            if (!writer_opt) {
                spdlog::error("Failed to remove asset {}: no processed writer", upath.string());
                state_valid = false;
                continue;
            }
            auto& writer = writer_opt->get();
            auto rm1     = co_await writer.remove(upath.path);
            if (!rm1) {
                if (!std::holds_alternative<writer_errors::IoError>(rm1.error())) {
                    spdlog::error("Failed to remove asset {}", upath.string());
                    state_valid = false;
                }
            }
            auto rm2 = co_await writer.remove_meta(upath.path);
            if (!rm2) {
                if (!std::holds_alternative<writer_errors::IoError>(rm2.error())) {
                    spdlog::error("Failed to remove meta for {}", upath.string());
                    state_valid = false;
                }
            }
        }
        if (!state_valid) {
            spdlog::error(
                "Processed asset transaction log state was invalid and unrecoverable. "
                "Removing processed assets and starting fresh.");
            for (auto& source : sources()->iter_processed()) {
                auto writer_opt = source.processed_writer();
                if (!writer_opt) continue;
                auto& writer = writer_opt->get();
                auto result  = co_await writer.clear_directory(std::filesystem::path(""));
                if (!result) {
                    spdlog::critical(
                        "Processed assets were in a bad state. Attempted to remove all processed "
                        "assets and start from scratch, but this failed. Try restarting or "
                        "deleting the processed asset folder manually.");
                }
            }
        }
    }
    // Create new log
    auto new_log = log_factory->create_new_log();
    if (new_log) {
        auto log = data->log.write();
        *log     = std::move(*new_log);
    } else {
        spdlog::error("Failed to create new transaction log: {}", new_log.error());
    }

    co_return log_path;
}

// ---- initialize ----

static asio::awaitable<void> get_asset_paths(const AssetReader& reader,
                                             const std::filesystem::path& path,
                                             std::vector<std::filesystem::path>& paths,
                                             std::vector<std::filesystem::path>* empty_dirs) {
    auto is_dir = co_await reader.is_directory(path);
    if (is_dir && *is_dir) {
        auto dir_result = co_await reader.read_directory(path);
        if (!dir_result) co_return;
        bool contains_files = false;
        for (auto child_path : *dir_result) {
            auto before = paths.size();
            co_await get_asset_paths(reader, child_path, paths, empty_dirs);
            contains_files |= (paths.size() > before);
        }
        if (!contains_files && path.has_parent_path() && empty_dirs) {
            empty_dirs->push_back(path);
        }
    } else {
        paths.push_back(path);
    }
}

// ---- AssetProcessor ----

AssetProcessor::AssetProcessor(AssetServer srv, std::shared_ptr<AssetProcessorData> proc_data)
    : server(std::move(srv)), data(std::move(proc_data)), m_owns_shutdown(false) {}

AssetProcessor::AssetProcessor(const AssetProcessor& other)
    : server(other.server), data(other.data), m_owns_shutdown(false) {}

AssetProcessor::AssetProcessor(AssetProcessor&& other) noexcept
    : server(std::move(other.server)),
      data(std::move(other.data)),
      m_owns_shutdown(std::exchange(other.m_owns_shutdown, false)) {}

AssetProcessor::AssetProcessor(std::reference_wrapper<AssetSourceBuilders> builders,
                               bool watching_for_changes,
                               std::unique_ptr<ProcessorTransactionLogFactory> log_factory) {
    auto state       = std::make_shared<ProcessingState>();
    auto sources_val = builders.get().build_sources(true, watching_for_changes);
    sources_val.gate_on_processor([state](AssetSourceId id, const AssetReader& reader) -> std::unique_ptr<AssetReader> {
        return std::make_unique<ProcessorGatedReader>(std::move(id), reader, state);
    });
    auto sources = std::make_shared<AssetSources>(std::move(sources_val));
    data =
        std::shared_ptr<AssetProcessorData>(new AssetProcessorData(sources, std::move(state), std::move(log_factory)));
    server = AssetServer(std::move(sources), AssetServerMode::Processed, AssetMetaCheck{asset_meta_check::Always{}},
                         false, UnapprovedPathMode::Forbid);
}

AssetProcessor& AssetProcessor::operator=(const AssetProcessor& other) {
    if (this == &other) return *this;
    if (m_owns_shutdown && data) {
        data->shutdown();
    }
    server          = other.server;
    data            = other.data;
    m_owns_shutdown = false;
    return *this;
}

AssetProcessor& AssetProcessor::operator=(AssetProcessor&& other) noexcept {
    if (this == &other) return *this;
    if (m_owns_shutdown && data) {
        data->shutdown();
    }
    server          = std::move(other.server);
    data            = std::move(other.data);
    m_owns_shutdown = std::exchange(other.m_owns_shutdown, false);
    return *this;
}

AssetProcessor::~AssetProcessor() {
    if (m_owns_shutdown && data) {
        data->shutdown();
    }
}

const AssetServer& AssetProcessor::get_server() const { return server; }

const std::shared_ptr<AssetProcessorData>& AssetProcessor::get_data() const { return data; }

// AssetProcessor::get_source is now inline in mod.cppm

const std::shared_ptr<AssetSources>& AssetProcessor::sources() const { return data->sources; }

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

asio::awaitable<void> AssetProcessor::initialize() const {
    auto log_path    = co_await validate_transaction_log_and_recover();
    auto infos_guard = data->processing_state->m_asset_infos.write();

    for (auto& source : sources()->iter_processed()) {
        auto ungated_opt = source.ungated_processed_reader();
        if (!ungated_opt) continue;
        auto const& processed_reader = ungated_opt->get();
        auto writer_opt              = source.processed_writer();
        if (!writer_opt) continue;
        auto& processed_writer = writer_opt->get();

        std::vector<std::filesystem::path> unprocessed_paths;
        co_await get_asset_paths(source.reader(), std::filesystem::path(""), unprocessed_paths, nullptr);

        std::vector<std::filesystem::path> processed_paths;
        std::vector<std::filesystem::path> empty_dirs;
        co_await get_asset_paths(processed_reader, std::filesystem::path(""), processed_paths, &empty_dirs);

        // Clean up empty dirs in processed output
        for (auto& empty_dir : empty_dirs) {
            (void)co_await processed_writer.remove_directory(empty_dir);
        }

        for (auto& path : unprocessed_paths) {
            infos_guard->get_or_insert(AssetPath(source.id(), path));
        }

        for (auto& path : processed_paths) {
            // Skip the transaction log file - it lives in the processed dir but is not an asset.
            if (!log_path.empty() && path == log_path) continue;
            // Skip .meta sidecar files - they are managed alongside their asset file.
            if (path.extension() == ".meta") continue;

            std::vector<AssetPath> dependencies;
            auto asset_path = AssetPath(source.id(), path);
            auto* info      = infos_guard->get(asset_path);
            if (info) {
                auto meta_bytes = co_await processed_reader.read_meta_bytes(asset_path.path);
                if (meta_bytes) {
                    auto pi = deserialize_processed_info(*meta_bytes);
                    if (pi && pi->has_value()) {
                        info->processed_info = std::move(**pi);
                        spdlog::trace("Restored ProcessedInfo for {} (hash {}...)", asset_path.string(),
                                      static_cast<int>(info->processed_info->hash[0]));
                    } else {
                        spdlog::trace("Found processed meta for {} (no ProcessedInfo stored)", asset_path.string());
                    }
                } else {
                    spdlog::trace("Removing processed data for {} because meta failed to load", asset_path.string());
                    co_await remove_processed_asset_and_meta(source, asset_path.path);
                }
            } else {
                spdlog::trace("Removing processed data for non-existent asset {}", asset_path.string());
                co_await remove_processed_asset_and_meta(source, asset_path.path);
            }

            for (auto& dep : dependencies) {
                infos_guard->add_dependent(dep, asset_path);
            }
        }
    }

    co_await data->processing_state->set_state(ProcessorState::Processing);
}

// ---- process_asset ----

asio::awaitable<void> AssetProcessor::process_asset(
    const AssetSourceId& source_id,
    const std::filesystem::path& path,
    async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>> reprocess_sender) const {
    auto source_opt = get_source(source_id);
    if (!source_opt) {
        spdlog::error("AssetSource {} not found for processing",
                      source_id.is_default() ? "default" : source_id.value());
        co_return;
    }
    auto& source    = source_opt->get();
    auto asset_path = AssetPath(source.id(), path);
    auto result     = co_await process_asset_internal(source, asset_path);
    // finish_processing may co_await async_broadcast sends, but utils::RwLock is sync.
    // We must NOT hold infos_guard across a co_await. finish_processing handles this by
    // doing all mutations while internally managing lock scope before awaiting.
    co_await data->processing_state->m_asset_infos.write()->finish_processing(asset_path, result, reprocess_sender);
}

// ---- process_asset_internal ----

asio::awaitable<std::expected<ProcessResult, ProcessError>> AssetProcessor::process_asset_internal(
    const AssetSource& source, const AssetPath& asset_path) const {
    spdlog::debug("Processing {}", asset_path.string());
    auto path    = asset_path.path;
    auto& reader = source.reader();

    auto reader_err = [&](AssetReaderError err) -> ProcessError {
        return ProcessError{process_errors::AssetReaderError{asset_path, std::move(err)}};
    };
    auto writer_err = [&](AssetWriterError err) -> ProcessError {
        return ProcessError{process_errors::AssetWriterError{asset_path, std::move(err)}};
    };

    // Get full extension to determine processor
    auto ext = asset_path.get_full_extension();
    if (!ext || ext->empty()) {
        co_return std::unexpected(ProcessError{process_errors::ExtensionRequired{}});
    }

    // Find the processor for this extension
    auto processor = get_default_processor(*ext);
    if (!processor) {
        auto meta_raw = co_await reader.read_meta_bytes(path);
        if (!meta_raw) {
            if (!server.get_path_asset_loader(asset_path)) {
                co_return std::unexpected(
                    ProcessError{process_errors::MissingAssetLoaderForExtension{std::string(*ext)}});
            }
        }

        auto current_mtime_ns = to_persisted_mtime_ns(reader.last_modified(path));
        {
            auto infos_guard = data->processing_state->m_asset_infos.read();
            if (const auto* existing = infos_guard->get(asset_path)) {
                if (existing->processed_info && current_mtime_ns.has_value() &&
                    existing->processed_info->source_mtime_ns == current_mtime_ns) {
                    bool dep_changed = false;
                    for (const auto& dep : existing->processed_info->process_dependencies) {
                        const auto* dep_info = infos_guard->get(AssetPath(dep.path));
                        bool live_ok         = dep_info && dep_info->processed_info &&
                                               dep_info->processed_info->full_hash == dep.full_hash;
                        if (!live_ok) {
                            dep_changed = true;
                            break;
                        }
                    }
                    if (!dep_changed) co_return ProcessResult::skipped_not_changed();
                }
            }
        }

        std::vector<std::byte> meta_bytes_for_hash = meta_raw ? std::move(*meta_raw) : std::vector<std::byte>{};
        AssetHash new_hash                         = {};
        {
            auto hash_reader = co_await reader.read(path);
            if (hash_reader) new_hash = co_await get_asset_hash(meta_bytes_for_hash, **hash_reader);
        }

        {
            auto infos_guard = data->processing_state->m_asset_infos.read();
            if (const auto* existing = infos_guard->get(asset_path)) {
                if (existing->processed_info && existing->processed_info->hash == new_hash) {
                    bool dep_changed = false;
                    for (const auto& dep : existing->processed_info->process_dependencies) {
                        const auto* dep_info = infos_guard->get(AssetPath(dep.path));
                        bool live_ok         = dep_info && dep_info->processed_info &&
                                               dep_info->processed_info->full_hash == dep.full_hash;
                        if (!live_ok) {
                            dep_changed = true;
                            break;
                        }
                    }
                    if (!dep_changed) co_return ProcessResult::skipped_not_changed();
                }
            }
        }

        ProcessedInfo new_processed_info;
        new_processed_info.hash            = new_hash;
        new_processed_info.full_hash       = new_hash;
        new_processed_info.source_mtime_ns = current_mtime_ns;
        co_return ProcessResult{ProcessResultKind::Processed, std::move(new_processed_info)};
    }

    // --- We have a processor - use it ---
    auto writer_opt = source.processed_writer();
    if (!writer_opt) {
        co_return std::unexpected(ProcessError{process_errors::MissingProcessedAssetWriter{}});
    }
    auto& processed_writer = writer_opt->get();

    // 1. Get source meta
    std::unique_ptr<AssetMetaDyn> source_meta;
    std::vector<std::byte> meta_bytes_for_hash;
    {
        auto mb_opt = co_await reader.read_meta_bytes(path);
        if (mb_opt) {
            auto dm = processor->deserialize_meta(*mb_opt);
            if (dm) {
                source_meta         = std::move(*dm);
                meta_bytes_for_hash = source_meta->serialize_bytes();
            }
        }
        if (!source_meta) {
            source_meta         = processor->default_meta();
            meta_bytes_for_hash = source_meta->serialize_bytes();
        }
    }

    const Settings* settings_to_use = source_meta->process_settings();
    std::unique_ptr<Settings> fallback_settings;
    if (!settings_to_use) {
        fallback_settings = processor->default_settings();
        settings_to_use   = fallback_settings.get();
    }

    // 2a. Fast-path: mtime check
    auto current_mtime_ns = to_persisted_mtime_ns(reader.last_modified(path));
    {
        auto infos_guard = data->processing_state->m_asset_infos.read();
        if (const auto* existing = infos_guard->get(asset_path)) {
            if (existing->processed_info && current_mtime_ns.has_value() &&
                existing->processed_info->source_mtime_ns == current_mtime_ns) {
                bool dep_changed = false;
                for (const auto& dep : existing->processed_info->process_dependencies) {
                    const auto* dep_info = infos_guard->get(AssetPath(dep.path));
                    bool live_ok =
                        dep_info && dep_info->processed_info && dep_info->processed_info->full_hash == dep.full_hash;
                    if (!live_ok) {
                        dep_changed = true;
                        break;
                    }
                }
                if (!dep_changed) co_return ProcessResult::skipped_not_changed();
            }
        }
    }

    // 2b. Compute new_hash
    AssetHash new_hash = {};
    {
        auto hash_reader = co_await reader.read(path);
        if (!hash_reader) co_return std::unexpected(reader_err(hash_reader.error()));
        new_hash = co_await get_asset_hash(meta_bytes_for_hash, **hash_reader);
    }

    // 3. Skip-unchanged check
    {
        auto infos_guard = data->processing_state->m_asset_infos.read();
        if (const auto* existing = infos_guard->get(asset_path)) {
            if (existing->processed_info && existing->processed_info->hash == new_hash) {
                bool dep_changed = false;
                for (const auto& dep : existing->processed_info->process_dependencies) {
                    const auto* dep_info = infos_guard->get(AssetPath(dep.path));
                    bool live_ok =
                        dep_info && dep_info->processed_info && dep_info->processed_info->full_hash == dep.full_hash;
                    if (!live_ok) {
                        dep_changed = true;
                        break;
                    }
                }
                if (!dep_changed) co_return ProcessResult::skipped_not_changed();
            }
        }
    }

    // 4. Acquire transaction lock and log
    auto _transaction_lock = [&]() -> std::shared_ptr<std::shared_mutex> {
        auto infos_guard = data->processing_state->m_asset_infos.write();
        auto& info       = infos_guard->get_or_insert(asset_path);
        return info.file_transaction_lock;
    }();
    std::unique_lock write_lock(*_transaction_lock);

    log_begin_processing(asset_path);

    // 5. Open reader and writer for the actual process
    auto source_reader = co_await reader.read(path);
    if (!source_reader) co_return std::unexpected(reader_err(source_reader.error()));

    auto dest_writer = co_await processed_writer.write(path);
    if (!dest_writer) co_return std::unexpected(writer_err(dest_writer.error()));

    // 6. Process
    ProcessedInfo new_processed_info;
    new_processed_info.hash            = new_hash;
    new_processed_info.full_hash       = new_hash;
    new_processed_info.source_mtime_ns = current_mtime_ns;
    ProcessContext context(*this, asset_path, **source_reader, new_processed_info);

    auto process_result = co_await processor->process(context, *settings_to_use, **dest_writer);
    if (!process_result) {
        co_return std::unexpected(std::move(process_result.error()));
    }
    auto& processed_meta = *process_result;

    // 7. Compute full_hash
    {
        std::vector<AssetHash> dep_hashes;
        dep_hashes.reserve(new_processed_info.process_dependencies.size());
        for (const auto& dep : new_processed_info.process_dependencies) {
            dep_hashes.push_back(dep.full_hash);
        }
        new_processed_info.full_hash = get_full_asset_hash(new_hash, dep_hashes);
    }

    // 8. Write processed meta
    processed_meta->processed_info_mut() = new_processed_info;
    auto processed_meta_bytes            = processed_meta->serialize_bytes();
    if (!processed_meta_bytes.empty()) {
        auto meta_write = co_await processed_writer.write_meta_bytes(path, processed_meta_bytes);
        if (!meta_write) co_return std::unexpected(writer_err(meta_write.error()));
    }

    log_end_processing(asset_path);

    co_return ProcessResult{ProcessResultKind::Processed, std::move(new_processed_info)};
}

// ---- Event handling ----

asio::awaitable<void> AssetProcessor::handle_asset_source_event(
    const AssetSource& source,
    const AssetSourceEvent& event,
    async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    if (auto* e = std::get_if<source_events::AddedAsset>(&event)) {
        (void)co_await sender.send(std::pair{source.id(), e->path});
    } else if (auto* e = std::get_if<source_events::ModifiedAsset>(&event)) {
        (void)co_await sender.send(std::pair{source.id(), e->path});
    } else if (auto* e = std::get_if<source_events::AddedMeta>(&event)) {
        (void)co_await sender.send(std::pair{source.id(), e->path});
    } else if (auto* e = std::get_if<source_events::ModifiedMeta>(&event)) {
        (void)co_await sender.send(std::pair{source.id(), e->path});
    } else if (auto* e = std::get_if<source_events::RemovedAsset>(&event)) {
        co_await handle_removed_asset(source, e->path);
    } else if (auto* e = std::get_if<source_events::RemovedMeta>(&event)) {
        co_await handle_removed_meta(source, e->path, sender);
    } else if (auto* e = std::get_if<source_events::AddedDirectory>(&event)) {
        co_await handle_added_folder(source, e->path, sender);
    } else if (auto* e = std::get_if<source_events::RemovedDirectory>(&event)) {
        co_await handle_removed_folder(source, e->path);
    } else if (auto* e = std::get_if<source_events::RenamedAsset>(&event)) {
        if (e->old_path == e->new_path) {
            (void)co_await sender.send(std::pair{source.id(), e->new_path});
        } else {
            co_await handle_renamed_asset(source, e->old_path, e->new_path, sender);
        }
    } else if (auto* e = std::get_if<source_events::RenamedMeta>(&event)) {
        if (e->old_path == e->new_path) {
            (void)co_await sender.send(std::pair{source.id(), e->new_path});
        } else {
            spdlog::debug("Meta renamed from {} to {}", e->old_path.string(), e->new_path.string());
            (void)co_await sender.send(std::pair{source.id(), e->old_path});
            (void)co_await sender.send(std::pair{source.id(), e->new_path});
        }
    } else if (auto* e = std::get_if<source_events::RenamedDirectory>(&event)) {
        if (e->old_path == e->new_path) {
            co_await handle_added_folder(source, e->new_path, sender);
        } else {
            co_await handle_removed_folder(source, e->old_path);
            co_await handle_added_folder(source, e->new_path, sender);
        }
    } else if (auto* e = std::get_if<source_events::RemovedUnknown>(&event)) {
        auto ungated_opt = source.ungated_processed_reader();
        if (ungated_opt) {
            auto& processed_reader = ungated_opt->get();
            auto is_dir            = co_await processed_reader.is_directory(e->path);
            if (is_dir && *is_dir) {
                co_await handle_removed_folder(source, e->path);
            } else if (e->is_meta) {
                co_await handle_removed_meta(source, e->path, sender);
            } else {
                co_await handle_removed_asset(source, e->path);
            }
        }
    }
}

asio::awaitable<void> AssetProcessor::handle_added_folder(
    const AssetSource& source,
    const std::filesystem::path& path,
    async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    spdlog::debug("Folder {} was added. Attempting to re-process", AssetPath(source.id(), path).string());
    co_await queue_processing_tasks_for_folder(source, path, sender);
}

asio::awaitable<void> AssetProcessor::handle_removed_meta(
    const AssetSource& source,
    const std::filesystem::path& path,
    async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    spdlog::debug("Meta for asset {} was removed. Attempting to re-process", AssetPath(source.id(), path).string());
    (void)co_await sender.send(std::pair{source.id(), path});
}

asio::awaitable<void> AssetProcessor::handle_removed_asset(const AssetSource& source,
                                                           const std::filesystem::path& path) const {
    auto asset_path = AssetPath(source.id(), path);
    spdlog::debug("Removing processed {} because source was removed", asset_path.string());
    // remove() is async; same-as above note: unbounded send + overflow broadcast never suspend
    auto lock = co_await data->processing_state->m_asset_infos.write()->remove(asset_path);
    if (!lock) co_return;
    std::unique_lock write_lock(*lock);
    co_await remove_processed_asset_and_meta(source, path);
}

asio::awaitable<void> AssetProcessor::handle_removed_folder(const AssetSource& source,
                                                            const std::filesystem::path& path) const {
    spdlog::debug("Removing folder {} because source was removed", path.string());
    auto ungated_opt = source.ungated_processed_reader();
    if (!ungated_opt) co_return;
    auto& processed_reader = ungated_opt->get();
    auto dir_result        = co_await processed_reader.read_directory(path);
    if (dir_result) {
        for (auto child_path : *dir_result) {
            co_await handle_removed_asset(source, child_path);
        }
    } else {
        auto& err = dir_result.error();
        if (!std::holds_alternative<reader_errors::NotFound>(err)) {
            log_unrecoverable();
            spdlog::error(
                "Unrecoverable Error: Failed to read processed assets at {} to remove "
                "assets that no longer exist. Restart the asset processor to fully reprocess.",
                path.string());
        }
    }
    auto writer_opt = source.processed_writer();
    if (writer_opt) {
        (void)co_await writer_opt->get().remove_directory(path);
    }
}

asio::awaitable<void> AssetProcessor::handle_renamed_asset(
    const AssetSource& source,
    const std::filesystem::path& old_path,
    const std::filesystem::path& new_path,
    async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    auto old_asset  = AssetPath(source.id(), old_path);
    auto new_asset  = AssetPath(source.id(), new_path);
    auto writer_opt = source.processed_writer();
    if (!writer_opt) co_return;
    auto& processed_writer = writer_opt->get();

    // rename() is async; unbounded send + overflow broadcast never suspend
    auto lock_pair = co_await data->processing_state->m_asset_infos.write()->rename(old_asset, new_asset, sender);
    if (!lock_pair) co_return;

    auto& [old_lock, new_lock] = *lock_pair;
    std::unique_lock old_write_lock(*old_lock);
    std::unique_lock new_write_lock(*new_lock);
    (void)co_await processed_writer.rename(old_path, new_path);
    (void)co_await processed_writer.rename_meta(old_path, new_path);
}

// ---- Task dispatching ----

asio::awaitable<void> AssetProcessor::queue_processing_tasks_for_folder(
    const AssetSource& source,
    const std::filesystem::path& folder,
    async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    auto is_dir = co_await source.reader().is_directory(folder);
    if (is_dir && *is_dir) {
        auto dir_result = co_await source.reader().read_directory(folder);
        if (!dir_result) co_return;
        for (auto child_path : *dir_result) {
            co_await queue_processing_tasks_for_folder(source, child_path, sender);
        }
    } else {
        if (folder.extension() != ".meta") {
            (void)co_await sender.send(std::pair{source.id(), folder});
        }
    }
}

asio::awaitable<void> AssetProcessor::queue_initial_processing_tasks(
    async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    for (auto& source : sources()->iter_processed()) {
        co_await queue_processing_tasks_for_folder(source, std::filesystem::path(""), sender);
    }
}

void AssetProcessor::spawn_source_change_event_listeners(
    async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    for (auto& source : sources()->iter_processed()) {
        auto receiver_opt = source.event_receiver();
        if (!receiver_opt) continue;
        auto source_id   = source.id();
        auto proc        = *this;
        auto task_sender = sender;
        auto r           = receiver_opt->get();
        tasks::IoTaskPool::get()
            .spawn([](AssetProcessor proc, AssetSourceId source_id,
                      async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>> task_sender,
                      async_channel::Receiver<AssetSourceEvent> r) mutable -> asio::awaitable<void> {
                while (true) {
                    auto event = co_await r.recv();
                    if (!event) {
                        spdlog::trace("[assets.processor] Source change listener exiting after receiver closed.");
                        co_return;
                    }
                    auto source_opt = proc.get_source(source_id);
                    if (!source_opt) co_return;
                    co_await proc.handle_asset_source_event(source_opt->get(), *event, task_sender);
                }
            }(std::move(proc), std::move(source_id), task_sender, std::move(r)))
            .detach();
    }
}

asio::awaitable<void> AssetProcessor::execute_processing_tasks(
    async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>> new_task_sender,
    async_channel::Receiver<std::pair<AssetSourceId, std::filesystem::path>> receiver) const {
    // Convert the Sender into a WeakSender so that once all task producers terminate (and drop
    // their sender), this task doesn't keep itself alive. Matches Bevy's WeakSender downgrade pattern.
    auto weak_sender = new_task_sender.downgrade();
    new_task_sender  = {};

    // If there are no initial tasks in the channel, go straight to Finished.
    if (receiver.is_empty()) {
        co_await data->processing_state->set_state(ProcessorState::Finished);
    }

    using Task = std::pair<AssetSourceId, std::filesystem::path>;
    enum class ProcessorTaskEventKind { Start, Finished };
    struct ProcessorTaskEvent {
        ProcessorTaskEventKind kind;
        std::optional<Task> task;
    };

    auto [task_finished_sender, task_finished_receiver] = async_channel::unbounded<std::monostate>();

    int pending_tasks = 0;

    // Unified event queue: forward both new tasks and finished events here.
    // This mirrors Bevy's select_biased! over new_task_receiver and task_finished_receiver.
    auto [event_sender, event_receiver] = async_channel::unbounded<ProcessorTaskEvent>();

    // Spawn a forwarder coroutine that reads from receiver and feeds events.
    {
        auto fwd_recv   = receiver;
        auto fwd_sender = event_sender;
        tasks::IoTaskPool::get()
            .spawn([](async_channel::Receiver<Task> fwd_recv,
                      async_channel::Sender<ProcessorTaskEvent> fwd_sender) -> asio::awaitable<void> {
                while (true) {
                    auto task = co_await fwd_recv.recv();
                    if (!task) {
                        fwd_sender.close();
                        co_return;
                    }
                    if (!(co_await fwd_sender.send(
                            ProcessorTaskEvent{ProcessorTaskEventKind::Start, std::move(*task)}))) {
                        co_return;
                    }
                }
            }(std::move(fwd_recv), std::move(fwd_sender)))
            .detach();
    }
    // Spawn a forwarder for task_finished_receiver.
    {
        auto fin_recv   = task_finished_receiver;
        auto fwd_sender = event_sender;
        tasks::IoTaskPool::get()
            .spawn([](async_channel::Receiver<std::monostate> fin_recv,
                      async_channel::Sender<ProcessorTaskEvent> fwd_sender) -> asio::awaitable<void> {
                while (true) {
                    auto fin = co_await fin_recv.recv();
                    if (!fin) co_return;
                    if (!(co_await fwd_sender.send(ProcessorTaskEvent{ProcessorTaskEventKind::Finished, {}}))) {
                        co_return;
                    }
                }
            }(std::move(fin_recv), std::move(fwd_sender)))
            .detach();
    }

    while (true) {
        auto event = co_await event_receiver.recv();
        if (!event) break;

        if (event->kind == ProcessorTaskEventKind::Start) {
            auto upgraded = weak_sender.upgrade();
            if (!upgraded) continue;
            auto p  = *this;
            auto s  = std::move(*upgraded);
            auto es = task_finished_sender;
            auto t  = std::move(*event->task);
            pending_tasks++;
            tasks::IoTaskPool::get()
                .spawn([](AssetProcessor p, async_channel::Sender<std::pair<AssetSourceId, std::filesystem::path>> s,
                          async_channel::Sender<std::monostate> es, Task t) -> asio::awaitable<void> {
                    co_await p.process_asset(t.first, t.second, std::move(s));
                    (void)co_await es.send(std::monostate{});
                }(std::move(p), std::move(s), es, std::move(t)))
                .detach();
            co_await data->processing_state->set_state(ProcessorState::Processing);
        } else {
            // Finished
            pending_tasks--;
            if (pending_tasks == 0) {
                co_await data->processing_state->set_state(ProcessorState::Finished);
            }
        }
    }
}

// ---- File system helpers ----

asio::awaitable<void> AssetProcessor::remove_processed_asset_and_meta(const AssetSource& source,
                                                                      const std::filesystem::path& path) const {
    auto writer_opt = source.processed_writer();
    if (!writer_opt) co_return;
    auto& writer = writer_opt->get();

    auto rm1 = co_await writer.remove(path);
    if (!rm1) {
        spdlog::warn("Failed to remove non-existent asset {}", path.string());
    }
    auto rm2 = co_await writer.remove_meta(path);
    if (!rm2) {
        spdlog::warn("Failed to remove non-existent meta {}", path.string());
    }
    co_await clean_empty_processed_ancestor_folders(source, path);
}

asio::awaitable<void> AssetProcessor::clean_empty_processed_ancestor_folders(const AssetSource& source,
                                                                             const std::filesystem::path& path) const {
    if (path.is_absolute()) {
        spdlog::error("Attempted to clean up ancestor folders of an absolute path. Skipping.");
        co_return;
    }
    auto writer_opt = source.processed_writer();
    if (!writer_opt) co_return;
    auto& writer = writer_opt->get();

    auto current = path.parent_path();
    while (!current.empty()) {
        auto result = co_await writer.remove_directory(current);
        if (!result) break;
        current = current.parent_path();
    }
}

asio::awaitable<void> AssetProcessor::write_default_meta_file_for_path(const AssetSource& source,
                                                                       const AssetPath& asset_path) const {
    auto ext = asset_path.get_full_extension();
    if (!ext) co_return;

    auto processor = get_default_processor(*ext);
    if (!processor) co_return;

    auto& reader       = source.reader();
    auto existing_meta = co_await reader.read_meta_bytes(asset_path.path);
    if (existing_meta) co_return;

    auto meta       = processor->default_meta();
    auto meta_bytes = meta->serialize_bytes();
    if (meta_bytes.empty()) co_return;

    auto writer_opt = source.writer();
    if (!writer_opt) co_return;
    (void)co_await writer_opt->get().write_meta_bytes(asset_path.path, meta_bytes);
}
