module;

#include <spdlog/spdlog.h>

module epix.assets;

import std;

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
    utils::IOTaskPool::instance().detach_task([proc]() mutable {
        auto start_time = std::chrono::steady_clock::now();
        spdlog::debug("Processing Assets");

        proc.initialize();

        auto [new_task_sender, new_task_receiver] =
            utils::make_channel<std::pair<AssetSourceId, std::filesystem::path>>();
        proc.data->set_task_sender(new_task_sender);

        proc.queue_initial_processing_tasks(new_task_sender);

        // Spawn task executor in background.
        // Pass a copy of new_task_sender so the executor can immediately downgrade it to a
        // WeakSender (matching Bevy's pattern), avoiding keeping the channel artificially open.
        {
            auto p = proc;
            auto s = new_task_sender;  // copy: executor will downgrade and drop this
            auto r = new_task_receiver;
            utils::IOTaskPool::instance().detach_task(
                [p, s, r]() mutable { p.execute_processing_tasks(std::move(s), r); });
        }

        proc.data->wait_until_finished();

        auto end_time = std::chrono::steady_clock::now();
        auto elapsed  = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        spdlog::debug("Processing finished in {}ms", elapsed.count());

        spdlog::debug("Listening for changes to source assets");
        proc.spawn_source_change_event_listeners(new_task_sender);
    });
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

std::filesystem::path AssetProcessor::validate_transaction_log_and_recover() const {
    std::unique_ptr<ProcessorTransactionLogFactory> log_factory;
    {
        auto guarded_log_factory = data->log_factory.lock();
        if (!guarded_log_factory->has_value()) {
            spdlog::error("Asset processor log factory not set. Cannot validate transaction log.");
            return {};
        }
        log_factory = std::move(guarded_log_factory->value());
        guarded_log_factory->reset();
    }

    if (!log_factory) {
        spdlog::error("Asset processor log factory not set. Cannot validate transaction log.");
        return {};
    }
    auto log_path = log_factory->log_path();
    auto result   = validate_transaction_log(*log_factory);
    if (!result) {
        auto& err              = result.error();
        bool state_valid       = true;
        auto handle_unfinished = [&](const AssetPath& path) {
            spdlog::debug("Asset {:?} did not finish processing. Clearing state.", path.string());
            auto source_opt = get_source(path.source);
            if (!source_opt) {
                spdlog::error("Failed to remove asset {}: AssetSource does not exist", path.string());
                state_valid = false;
                return;
            }
            auto& source    = source_opt->get();
            auto writer_opt = source.processed_writer();
            if (!writer_opt) {
                spdlog::error("Failed to remove asset {}: no processed writer", path.string());
                state_valid = false;
                return;
            }
            auto& writer = writer_opt->get();
            auto rm1     = writer.remove(path.path);
            if (!rm1) {
                // NotFound is ok, anything else is bad
                if (!std::holds_alternative<writer_errors::IoError>(rm1.error())) {
                    spdlog::error("Failed to remove asset {}", path.string());
                    state_valid = false;
                }
            }
            auto rm2 = writer.remove_meta(path.path);
            if (!rm2) {
                if (!std::holds_alternative<writer_errors::IoError>(rm2.error())) {
                    spdlog::error("Failed to remove meta for {}", path.string());
                    state_valid = false;
                }
            }
        };
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
                                                  handle_unfinished(t.path);
                                              },
                                          },
                                          entry_err);
                               if (!state_valid) break;
                           }
                       },
                   },
                   err);
        if (!state_valid) {
            spdlog::error(
                "Processed asset transaction log state was invalid and unrecoverable. "
                "Removing processed assets and starting fresh.");
            for (auto& source : sources()->iter_processed()) {
                auto writer_opt = source.processed_writer();
                if (!writer_opt) continue;
                auto& writer = writer_opt->get();
                auto result  = writer.clear_directory(std::filesystem::path(""));
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

    return log_path;
}

// ---- initialize ----

static void get_asset_paths(const AssetReader& reader,
                            const std::filesystem::path& path,
                            std::vector<std::filesystem::path>& paths,
                            std::vector<std::filesystem::path>* empty_dirs) {
    auto is_dir = reader.is_directory(path);
    if (is_dir && *is_dir) {
        auto dir_result = reader.read_directory(path);
        if (!dir_result) return;
        bool contains_files = false;
        for (auto child_path : *dir_result) {
            auto before = paths.size();
            get_asset_paths(reader, child_path, paths, empty_dirs);
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

void AssetProcessor::initialize() const {
    auto log_path    = validate_transaction_log_and_recover();
    auto infos_guard = data->processing_state->m_asset_infos.write();

    for (auto& source : sources()->iter_processed()) {
        auto ungated_opt = source.ungated_processed_reader();
        if (!ungated_opt) continue;
        auto const& processed_reader = ungated_opt->get();
        auto writer_opt              = source.processed_writer();
        if (!writer_opt) continue;
        auto& processed_writer = writer_opt->get();

        std::vector<std::filesystem::path> unprocessed_paths;
        get_asset_paths(source.reader(), std::filesystem::path(""), unprocessed_paths, nullptr);

        std::vector<std::filesystem::path> processed_paths;
        std::vector<std::filesystem::path> empty_dirs;
        get_asset_paths(processed_reader, std::filesystem::path(""), processed_paths, &empty_dirs);

        // Clean up empty dirs in processed output
        for (auto& empty_dir : empty_dirs) {
            (void)processed_writer.remove_directory(empty_dir);
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
                auto meta_bytes = processed_reader.read_meta_bytes(asset_path.path);
                if (meta_bytes) {
                    // Deserialize ProcessedInfo from the stored processed meta to enable
                    // the skip-unchanged check on next run.  We only read the first two
                    // fields (version + processed) so we don't need to know the settings types.
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
                    remove_processed_asset_and_meta(source, asset_path.path);
                }
            } else {
                spdlog::trace("Removing processed data for non-existent asset {}", asset_path.string());
                remove_processed_asset_and_meta(source, asset_path.path);
            }

            for (auto& dep : dependencies) {
                infos_guard->add_dependent(dep, asset_path);
            }
        }
    }

    data->processing_state->set_state(ProcessorState::Processing);
}

// ---- process_asset ----

void AssetProcessor::process_asset(
    const AssetSourceId& source_id,
    const std::filesystem::path& path,
    utils::Sender<std::pair<AssetSourceId, std::filesystem::path>> reprocess_sender) const {
    auto source_opt = get_source(source_id);
    if (!source_opt) {
        spdlog::error("AssetSource {} not found for processing",
                      source_id.is_default() ? "default" : source_id.value());
        return;
    }
    auto& source     = source_opt->get();
    auto asset_path  = AssetPath(source.id(), path);
    auto result      = process_asset_internal(source, asset_path);
    auto infos_guard = data->processing_state->m_asset_infos.write();
    infos_guard->finish_processing(asset_path, result, reprocess_sender);
}

// ---- process_asset_internal ----

std::expected<ProcessResult, ProcessError> AssetProcessor::process_asset_internal(const AssetSource& source,
                                                                                  const AssetPath& asset_path) const {
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
        return std::unexpected(ProcessError{process_errors::ExtensionRequired{}});
    }

    // Find the processor for this extension
    auto processor = get_default_processor(*ext);
    if (!processor) {
        // No processor for this extension: the server reads from the source reader directly.
        // Check there is a loader (or an explicit .meta file) so we can detect invalid assets early.
        auto meta_raw = reader.read_meta_bytes(path);
        if (!meta_raw) {
            if (!server.get_path_asset_loader(asset_path)) {
                return std::unexpected(ProcessError{process_errors::MissingAssetLoaderForExtension{std::string(*ext)}});
            }
        }

        // Fast-path: if the source file's mtime matches what we saw last time and there are no
        // dependency hash mismatches, skip without reading or hashing the file bytes.
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
                    if (!dep_changed) return ProcessResult::skipped_not_changed();
                }
            }
        }

        // Hash = (.meta bytes if present) + asset bytes — used only for in-memory skip-check.
        std::vector<std::byte> meta_bytes_for_hash = meta_raw ? std::move(*meta_raw) : std::vector<std::byte>{};
        AssetHash new_hash                         = {};
        {
            auto hash_reader = reader.read(path);
            if (hash_reader) new_hash = get_asset_hash(meta_bytes_for_hash, **hash_reader);
        }

        // Skip if unchanged (in-memory; nothing is written to the processed output).
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
                    if (!dep_changed) return ProcessResult::skipped_not_changed();
                }
            }
        }

        // No disk writes — return processed info for in-memory deduplication within this run.
        ProcessedInfo new_processed_info;
        new_processed_info.hash            = new_hash;
        new_processed_info.full_hash       = new_hash;
        new_processed_info.source_mtime_ns = current_mtime_ns;
        return ProcessResult{ProcessResultKind::Processed, std::move(new_processed_info)};
    }

    // --- We have a processor - use it ---
    auto writer_opt = source.processed_writer();
    if (!writer_opt) {
        return std::unexpected(ProcessError{process_errors::MissingProcessedAssetWriter{}});
    }
    auto& processed_writer = writer_opt->get();

    // 1. Get source meta (deserialize from file if available, else use default)
    std::unique_ptr<AssetMetaDyn> source_meta;
    std::vector<std::byte> meta_bytes_for_hash;
    {
        auto mb_opt = reader.read_meta_bytes(path);
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

    // Settings to use for processing (from meta, falling back to default)
    const Settings* settings_to_use = source_meta->process_settings();
    std::unique_ptr<Settings> fallback_settings;
    if (!settings_to_use) {
        fallback_settings = processor->default_settings();
        settings_to_use   = fallback_settings.get();
    }

    // 2a. Fast-path: if source mtime matches, skip without hashing.
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
                if (!dep_changed) return ProcessResult::skipped_not_changed();
            }
        }
    }

    // 2b. Compute new_hash = hash(meta_bytes + asset_bytes)
    AssetHash new_hash = {};
    {
        auto hash_reader = reader.read(path);
        if (!hash_reader) return std::unexpected(reader_err(hash_reader.error()));
        new_hash = get_asset_hash(meta_bytes_for_hash, **hash_reader);
    }

    // 3. Skip-unchanged check (hash-based fallback when mtime unavailable)
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
                if (!dep_changed) return ProcessResult::skipped_not_changed();
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

    // 5. Open streams for the actual process
    auto source_stream = reader.read(path);
    if (!source_stream) return std::unexpected(reader_err(source_stream.error()));

    auto dest_stream = processed_writer.write(path);
    if (!dest_stream) return std::unexpected(writer_err(dest_stream.error()));

    // 6. Process
    ProcessedInfo new_processed_info;
    new_processed_info.hash            = new_hash;
    new_processed_info.full_hash       = new_hash;
    new_processed_info.source_mtime_ns = current_mtime_ns;
    ProcessContext context(*this, asset_path, **source_stream, new_processed_info);

    auto process_result = processor->process(context, *settings_to_use, **dest_stream);
    if (!process_result) {
        return std::unexpected(std::move(process_result.error()));
    }
    auto& processed_meta = *process_result;

    // 7. Compute full_hash = hash(new_hash, dep full_hashes)
    {
        std::vector<AssetHash> dep_hashes;
        dep_hashes.reserve(new_processed_info.process_dependencies.size());
        for (const auto& dep : new_processed_info.process_dependencies) {
            dep_hashes.push_back(dep.full_hash);
        }
        new_processed_info.full_hash = get_full_asset_hash(new_hash, dep_hashes);
    }

    // 8. Embed ProcessedInfo in the processed meta and write meta file
    processed_meta->processed_info_mut() = new_processed_info;
    auto processed_meta_bytes            = processed_meta->serialize_bytes();
    if (!processed_meta_bytes.empty()) {
        auto meta_write = processed_writer.write_meta_bytes(path, processed_meta_bytes);
        if (!meta_write) return std::unexpected(writer_err(meta_write.error()));
    }

    log_end_processing(asset_path);

    return ProcessResult{ProcessResultKind::Processed, std::move(new_processed_info)};
}

// ---- Event handling ----

void AssetProcessor::handle_asset_source_event(
    const AssetSource& source,
    const AssetSourceEvent& event,
    utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    std::visit(utils::visitor{
                   [&](const source_events::AddedAsset& e) { sender.send(std::pair{source.id(), e.path}); },
                   [&](const source_events::ModifiedAsset& e) { sender.send(std::pair{source.id(), e.path}); },
                   [&](const source_events::AddedMeta& e) { sender.send(std::pair{source.id(), e.path}); },
                   [&](const source_events::ModifiedMeta& e) { sender.send(std::pair{source.id(), e.path}); },
                   [&](const source_events::RemovedAsset& e) { handle_removed_asset(source, e.path); },
                   [&](const source_events::RemovedMeta& e) { handle_removed_meta(source, e.path, sender); },
                   [&](const source_events::AddedDirectory& e) { handle_added_folder(source, e.path, sender); },
                   [&](const source_events::RemovedDirectory& e) { handle_removed_folder(source, e.path); },
                   [&](const source_events::RenamedAsset& e) {
                       if (e.old_path == e.new_path) {
                           sender.send(std::pair{source.id(), e.new_path});
                       } else {
                           handle_renamed_asset(source, e.old_path, e.new_path, sender);
                       }
                   },
                   [&](const source_events::RenamedMeta& e) {
                       if (e.old_path == e.new_path) {
                           sender.send(std::pair{source.id(), e.new_path});
                       } else {
                           spdlog::debug("Meta renamed from {} to {}", e.old_path.string(), e.new_path.string());
                           sender.send(std::pair{source.id(), e.old_path});
                           sender.send(std::pair{source.id(), e.new_path});
                       }
                   },
                   [&](const source_events::RenamedDirectory& e) {
                       if (e.old_path == e.new_path) {
                           handle_added_folder(source, e.new_path, sender);
                       } else {
                           handle_removed_folder(source, e.old_path);
                           handle_added_folder(source, e.new_path, sender);
                       }
                   },
                   [&](const source_events::RemovedUnknown& e) {
                       auto ungated_opt = source.ungated_processed_reader();
                       if (!ungated_opt) return;
                       auto& processed_reader = ungated_opt->get();
                       auto is_dir            = processed_reader.is_directory(e.path);
                       if (is_dir && *is_dir) {
                           handle_removed_folder(source, e.path);
                       } else if (e.is_meta) {
                           handle_removed_meta(source, e.path, sender);
                       } else {
                           handle_removed_asset(source, e.path);
                       }
                   },
               },
               event);
}

void AssetProcessor::handle_added_folder(const AssetSource& source,
                                         const std::filesystem::path& path,
                                         utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    spdlog::debug("Folder {} was added. Attempting to re-process", AssetPath(source.id(), path).string());
    queue_processing_tasks_for_folder(source, path, sender);
}

void AssetProcessor::handle_removed_meta(const AssetSource& source,
                                         const std::filesystem::path& path,
                                         utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    spdlog::debug("Meta for asset {} was removed. Attempting to re-process", AssetPath(source.id(), path).string());
    sender.send(std::pair{source.id(), path});
}

void AssetProcessor::handle_removed_asset(const AssetSource& source, const std::filesystem::path& path) const {
    auto asset_path = AssetPath(source.id(), path);
    spdlog::debug("Removing processed {} because source was removed", asset_path.string());
    auto lock = [&]() -> std::shared_ptr<std::shared_mutex> {
        auto infos_guard = data->processing_state->m_asset_infos.write();
        return infos_guard->remove(asset_path);
    }();
    if (!lock) return;
    // Wait for uncontested write access
    std::unique_lock write_lock(*lock);
    remove_processed_asset_and_meta(source, path);
}

void AssetProcessor::handle_removed_folder(const AssetSource& source, const std::filesystem::path& path) const {
    spdlog::debug("Removing folder {} because source was removed", path.string());
    auto ungated_opt = source.ungated_processed_reader();
    if (!ungated_opt) return;
    auto& processed_reader = ungated_opt->get();
    auto dir_result        = processed_reader.read_directory(path);
    if (dir_result) {
        for (auto child_path : *dir_result) {
            handle_removed_asset(source, child_path);
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
        (void)writer_opt->get().remove_directory(path);
    }
}

void AssetProcessor::handle_renamed_asset(
    const AssetSource& source,
    const std::filesystem::path& old_path,
    const std::filesystem::path& new_path,
    utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    auto old_asset  = AssetPath(source.id(), old_path);
    auto new_asset  = AssetPath(source.id(), new_path);
    auto writer_opt = source.processed_writer();
    if (!writer_opt) return;
    auto& processed_writer = writer_opt->get();

    auto lock_pair =
        [&]() -> std::optional<std::pair<std::shared_ptr<std::shared_mutex>, std::shared_ptr<std::shared_mutex>>> {
        auto infos_guard = data->processing_state->m_asset_infos.write();
        return infos_guard->rename(old_asset, new_asset, sender);
    }();
    if (!lock_pair) return;

    auto& [old_lock, new_lock] = *lock_pair;
    std::unique_lock old_write_lock(*old_lock);
    std::unique_lock new_write_lock(*new_lock);
    (void)processed_writer.rename(old_path, new_path);
    (void)processed_writer.rename_meta(old_path, new_path);
}

// ---- Task dispatching ----

void AssetProcessor::queue_processing_tasks_for_folder(
    const AssetSource& source,
    const std::filesystem::path& folder,
    utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    auto is_dir = source.reader().is_directory(folder);
    if (is_dir && *is_dir) {
        auto dir_result = source.reader().read_directory(folder);
        if (!dir_result) return;
        for (auto child_path : *dir_result) {
            queue_processing_tasks_for_folder(source, child_path, sender);
        }
    } else {
        // Skip .meta sidecar files - they are managed alongside their asset file.
        if (folder.extension() != ".meta") {
            sender.send(std::pair{source.id(), folder});
        }
    }
}

void AssetProcessor::queue_initial_processing_tasks(
    utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    for (auto& source : sources()->iter_processed()) {
        queue_processing_tasks_for_folder(source, std::filesystem::path(""), sender);
    }
}

void AssetProcessor::spawn_source_change_event_listeners(
    utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const {
    for (auto& source : sources()->iter_processed()) {
        auto receiver_opt = source.event_receiver();
        if (!receiver_opt) continue;
        auto source_id = source.id();
        // Use weak_ptr to data and server.data to avoid a reference cycle:
        // listener task -> proc -> server.data -> sources -> watcher (sender to r).
        // If the processor is destroyed (e.g. App exits), the sources are freed,
        // the watcher's sender is dropped, r.receive() returns Closed, and the task exits.
        auto weak_data        = std::weak_ptr<AssetProcessorData>(data);
        auto weak_server_data = std::weak_ptr<AssetServerData>(server.data);
        auto task_sender      = sender;
        auto r                = receiver_opt->get();
        utils::IOTaskPool::instance().detach_task([weak_data, weak_server_data, source_id, task_sender, r]() mutable {
            while (true) {
                auto event = r.receive();
                if (!event) {
                    spdlog::trace("[assets.processor] Source change listener exiting after receiver closed.");
                    return;
                }
                auto locked_data        = weak_data.lock();
                auto locked_server_data = weak_server_data.lock();
                if (!locked_data || !locked_server_data) {
                    spdlog::trace("[assets.processor] Source change listener exiting after processor data expired.");
                    return;
                }
                // Reconstruct a temporary AssetProcessor from the locked weak_ptr data.
                AssetServer tmp_server;
                tmp_server.data = locked_server_data;
                AssetProcessor proc(std::move(tmp_server), locked_data);
                auto source_opt = proc.get_source(source_id);
                if (!source_opt) return;
                proc.handle_asset_source_event(source_opt->get(), *event, task_sender);
            }
        });
    }
}

void AssetProcessor::execute_processing_tasks(
    utils::Sender<std::pair<AssetSourceId, std::filesystem::path>> new_task_sender,
    utils::Receiver<std::pair<AssetSourceId, std::filesystem::path>>& receiver) const {
    // Convert the Sender into a WeakSender so that once all task producers terminate (and drop
    // their sender), this executor doesn't keep itself alive. We can still upgrade when spawning
    // tasks that need to queue dependency assets. Matches Bevy's WeakSender downgrade pattern.
    auto weak_sender = new_task_sender.downgrade();
    new_task_sender  = {};

    // If there are no initial tasks in the channel, go straight to Finished (matches Bevy check:
    // `if new_task_receiver.is_empty() { set_state(Finished) }`).
    // We use try_receive: if it returns Empty the channel has no items yet (but may receive more
    // from file-watcher senders later); if it returns a value we'll re-inject it below.
    using Task = std::pair<AssetSourceId, std::filesystem::path>;

    struct StartTaskEvent {
        Task task;
    };
    struct FinishedTaskEvent {};
    struct InputClosedEvent {};
    using ProcessingTaskEvent = std::variant<StartTaskEvent, FinishedTaskEvent, InputClosedEvent>;

    auto [event_sender, event_receiver] = utils::make_channel<ProcessingTaskEvent>();

    // Handle the initial-empty check using try_receive so we don't block.
    // If a value is present we forward it into the event queue first.
    {
        auto first = receiver.try_receive();
        if (!first) {
            if (first.error() == utils::ReceiveError::Empty) {
                // No tasks are queued right now; go to Finished immediately.
                data->processing_state->set_state(ProcessorState::Finished);
            }
            // If Closed, the InputClosedEvent from the forwarding thread handles it.
        } else {
            event_sender.send(StartTaskEvent{std::move(*first)});
        }
    }

    // Background forwarding thread: pulls from the outer task receiver and forwards as
    // StartTaskEvent/InputClosedEvent into the unified event queue.  This mirrors Bevy's
    // `new_task_receiver.recv()` arm inside `select_biased!`.
    auto fwd_receiver = receiver;
    auto fwd_sender   = event_sender;
    utils::IOTaskPool::instance().detach_task([fwd_receiver, fwd_sender]() mutable {
        while (true) {
            auto task = fwd_receiver.receive();
            if (!task) {
                fwd_sender.send(InputClosedEvent{});
                break;
            }
            fwd_sender.send(StartTaskEvent{std::move(*task)});
        }
    });

    int pending_tasks = 0;
    bool input_closed = false;

    // Unified event loop, equivalent to Bevy's `select_biased!` over new_task_receiver and
    // task_finished_receiver.  We prefer StartTaskEvent (start tasks) over FinishedTaskEvent so
    // we don't prematurely mark as Finished before all queued tasks are dispatched.
    while (!input_closed || pending_tasks > 0) {
        auto event = event_receiver.receive();
        if (!event) {
            break;
        }

        std::visit(utils::visitor{
                       [&](StartTaskEvent& e) {
                           auto upgraded = weak_sender.upgrade();
                           if (!upgraded) {
                               return;
                           }
                           pending_tasks++;
                           auto p  = *this;
                           auto s  = std::move(*upgraded);
                           auto es = event_sender;
                           auto t  = std::move(e.task);
                           utils::IOTaskPool::instance().detach_task([p, s, es, t]() mutable {
                               auto& [source_id, path] = t;
                               p.process_asset(source_id, path, std::move(s));
                               es.send(FinishedTaskEvent{});
                           });
                           data->processing_state->set_state(ProcessorState::Processing);
                       },
                       [&](FinishedTaskEvent&) {
                           pending_tasks--;
                           if (pending_tasks == 0) {
                               data->processing_state->set_state(ProcessorState::Finished);
                           }
                       },
                       [&](InputClosedEvent&) {
                           input_closed = true;
                           // If there are no in-flight tasks left, processing is done now.
                           if (pending_tasks == 0) {
                               data->processing_state->set_state(ProcessorState::Finished);
                           }
                       },
                   },
                   *event);
    }
}

// ---- File system helpers ----

void AssetProcessor::remove_processed_asset_and_meta(const AssetSource& source,
                                                     const std::filesystem::path& path) const {
    auto writer_opt = source.processed_writer();
    if (!writer_opt) return;
    auto& writer = writer_opt->get();

    auto rm1 = writer.remove(path);
    if (!rm1) {
        spdlog::warn("Failed to remove non-existent asset {}", path.string());
    }
    auto rm2 = writer.remove_meta(path);
    if (!rm2) {
        spdlog::warn("Failed to remove non-existent meta {}", path.string());
    }
    clean_empty_processed_ancestor_folders(source, path);
}

void AssetProcessor::clean_empty_processed_ancestor_folders(const AssetSource& source,
                                                            const std::filesystem::path& path) const {
    if (path.is_absolute()) {
        spdlog::error("Attempted to clean up ancestor folders of an absolute path. Skipping.");
        return;
    }
    auto writer_opt = source.processed_writer();
    if (!writer_opt) return;
    auto& writer = writer_opt->get();

    auto current = path.parent_path();
    while (!current.empty()) {
        auto result = writer.remove_directory(current);
        if (!result) break;  // stop walking up if removal fails
        current = current.parent_path();
    }
}

void AssetProcessor::write_default_meta_file_for_path(const AssetSource& source, const AssetPath& asset_path) const {
    auto ext = asset_path.get_full_extension();
    if (!ext) return;

    auto processor = get_default_processor(*ext);
    if (!processor) return;

    // Check if meta already exists
    auto& reader       = source.reader();
    auto existing_meta = reader.read_meta_bytes(asset_path.path);
    if (existing_meta) return;  // meta already exists

    auto meta       = processor->default_meta();
    auto meta_bytes = meta->serialize_bytes();
    if (meta_bytes.empty()) return;

    auto writer_opt = source.writer();
    if (!writer_opt) return;
    (void)writer_opt->get().write_meta_bytes(asset_path.path, meta_bytes);
}
