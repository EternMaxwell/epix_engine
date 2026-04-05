module;

#include <spdlog/spdlog.h>

module epix.assets;

import std;

using namespace epix::assets;

// ---- AssetProcessor constructor ----

AssetProcessor::AssetProcessor(std::shared_ptr<AssetProcessorData> processor_data, bool watching_for_changes)
    : data(std::move(processor_data)) {
    auto sources_val = data->source_builders->build_sources(true, watching_for_changes);
    auto state       = data->processing_state;
    sources_val.gate_on_processor([state](AssetSourceId id, const AssetReader& reader) -> std::unique_ptr<AssetReader> {
        return std::make_unique<ProcessorGatedReader>(std::move(id), reader, state);
    });
    auto sources = std::make_shared<AssetSources>(std::move(sources_val));
    server = AssetServer(std::move(sources), AssetServerMode::Processed, AssetMetaCheck{asset_meta_check::Always{}},
                         false, UnapprovedPathMode::Forbid);
}

// ---- start ----

void AssetProcessor::start(core::Res<AssetProcessor> processor) {
    auto proc = *processor;
    utils::IOTaskPool::instance().detach_task([proc]() mutable {
        auto start_time = std::chrono::steady_clock::now();
        spdlog::debug("Processing Assets");

        proc.initialize();

        auto [new_task_sender, new_task_receiver] =
            utils::make_channel<std::pair<AssetSourceId, std::filesystem::path>>();

        proc.queue_initial_processing_tasks(new_task_sender);

        // Spawn task executor in background.
        // Pass a copy of new_task_sender so the executor can immediately downgrade it to a
        // WeakSender (matching Bevy's pattern) �?avoiding keeping the channel artificially open.
        {
            auto p = proc;
            auto s = new_task_sender;  // copy: executor will downgrade and drop this
            auto r = new_task_receiver;
            utils::IOTaskPool::instance().detach_task(
                [p, s, r]() mutable { p.execute_processing_tasks(std::move(s), r); });
        }

        // Start source-change listeners BEFORE waiting. Listeners use a WeakSender so they
        // do NOT keep new_task_sender alive. After spawn_source_change_event_listeners returns,
        // new_task_sender is dropped, signaling execute_processing_tasks that there are no more
        // producers, allowing it to eventually reach ProcessorState::Finished.
        spdlog::debug("Listening for changes to source assets");
        proc.spawn_source_change_event_listeners(new_task_sender);
        new_task_sender = {};  // drop last strong sender -> unblocks execute_processing_tasks

        proc.data->wait_until_finished();

        auto end_time = std::chrono::steady_clock::now();
        auto elapsed  = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        spdlog::debug("Processing finished in {}ms", elapsed.count());
    });
}

// ---- Logging helpers ----

void AssetProcessor::log_begin_processing(const AssetPath& path) const {
    if (data->log) {
        data->log->begin_processing(path);
    }
}

void AssetProcessor::log_end_processing(const AssetPath& path) const {
    if (data->log) {
        data->log->end_processing(path);
    }
}

void AssetProcessor::log_unrecoverable() const {
    if (data->log) {
        data->log->unrecoverable();
    }
}

// ---- validate_transaction_log_and_recover ----

void AssetProcessor::validate_transaction_log_and_recover() const {
    if (!data->log_factory) {
        spdlog::error("Asset processor log factory not set. Cannot validate transaction log.");
        return;
    }
    auto result = validate_transaction_log(*data->log_factory);
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
    auto new_log = data->log_factory->create_new_log();
    if (new_log) {
        data->log = std::move(*new_log);
    } else {
        spdlog::error("Failed to create new transaction log: {}", new_log.error());
    }
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

void AssetProcessor::initialize() const {
    validate_transaction_log_and_recover();
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
            processed_writer.remove_directory(empty_dir);  // best-effort
        }

        for (auto& path : unprocessed_paths) {
            infos_guard->get_or_insert(AssetPath(source.id(), path));
        }

        for (auto& path : processed_paths) {
            // Skip the transaction log file — it lives in the processed dir but is not an asset.
            if (data->log_factory && path == data->log_factory->log_path()) continue;

            std::vector<AssetPath> dependencies;
            auto asset_path = AssetPath(source.id(), path);
            auto* info      = infos_guard->get(asset_path);
            if (info) {
                auto meta_bytes = processed_reader.read_meta_bytes(asset_path.path);
                if (meta_bytes) {
                    // Try to deserialize ProcessedInfo from meta bytes
                    // For now, we store the raw processed_info if available
                    // TODO: actual deserialization of meta bytes into ProcessedInfo
                    spdlog::trace("Found processed meta for {}", asset_path.string());
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
        // No processor, try to just copy through (load action)
        auto meta_result = reader.read_meta_bytes(path);
        if (!meta_result) {
            // No meta file and no processor - check if there's a loader
            auto loader = server.get_path_asset_loader(asset_path);
            if (!loader) {
                return std::unexpected(ProcessError{process_errors::MissingAssetLoaderForExtension{std::string(*ext)}});
            }
        }

        // Copy-through: read from source, write to processed
        auto writer_opt = source.processed_writer();
        if (!writer_opt) {
            return std::unexpected(ProcessError{process_errors::MissingProcessedAssetWriter{}});
        }
        auto& processed_writer = writer_opt->get();

        auto source_stream = reader.read(path);
        if (!source_stream) return std::unexpected(reader_err(source_stream.error()));

        auto dest_stream = processed_writer.write(path);
        if (!dest_stream) return std::unexpected(writer_err(dest_stream.error()));

        // Copy bytes
        (**dest_stream) << (**source_stream).rdbuf();

        // Also copy/write meta
        auto meta_bytes = reader.read_meta_bytes(path);
        if (meta_bytes) {
            processed_writer.write_meta_bytes(path, *meta_bytes);
        }

        return ProcessResult{ProcessResultKind::Processed};
    }

    // We have a processor - use it
    auto writer_opt = source.processed_writer();
    if (!writer_opt) {
        return std::unexpected(ProcessError{process_errors::MissingProcessedAssetWriter{}});
    }
    auto& processed_writer = writer_opt->get();

    // Get transaction lock
    auto _transaction_lock = [&]() -> std::shared_ptr<std::shared_mutex> {
        auto infos_guard = data->processing_state->m_asset_infos.write();
        auto& info       = infos_guard->get_or_insert(asset_path);
        return info.file_transaction_lock;
    }();
    std::unique_lock write_lock(*_transaction_lock);

    log_begin_processing(asset_path);

    auto source_stream = reader.read(path);
    if (!source_stream) return std::unexpected(reader_err(source_stream.error()));

    auto dest_stream = processed_writer.write(path);
    if (!dest_stream) return std::unexpected(writer_err(dest_stream.error()));

    ProcessedInfo new_processed_info;
    auto default_settings = processor->default_settings();
    ProcessContext context(*this, asset_path, **source_stream, new_processed_info);

    auto process_result = processor->process(context, *default_settings, **dest_stream);
    if (!process_result) {
        return std::unexpected(std::move(process_result.error()));
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
        writer_opt->get().remove_directory(path);  // ignore errors; may already be cleaned
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

    // Remove old and add new in infos
    auto old_lock = [&]() -> std::shared_ptr<std::shared_mutex> {
        auto infos_guard = data->processing_state->m_asset_infos.write();
        return infos_guard->remove(old_asset);
    }();
    if (old_lock) {
        std::unique_lock lock(*old_lock);
        processed_writer.rename(old_path, new_path);
        processed_writer.rename_meta(old_path, new_path);
    }
    // Queue the new path for processing
    sender.send(std::pair{source.id(), new_path});
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
        sender.send(std::pair{source.id(), folder});
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
    // Downgrade to a WeakSender so listener tasks do NOT keep the new_task channel alive.
    // This allows execute_processing_tasks to detect input_closed when the owning (start)
    // task drops its strong Sender.
    auto weak_s = sender.downgrade();
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
        auto ws               = weak_s;  // copy of WeakSender
        auto r                = receiver_opt->get();
        utils::IOTaskPool::instance().detach_task([weak_data, weak_server_data, source_id, ws, r]() mutable {
            while (true) {
                auto event = r.receive();
                if (!event) return;
                auto locked_data        = weak_data.lock();
                auto locked_server_data = weak_server_data.lock();
                if (!locked_data || !locked_server_data) return;
                // Upgrade WeakSender to send a new processing task - if it fails the
                // executor has already shut down, so we exit.
                auto s = ws.upgrade();
                if (!s) return;
                // Reconstruct a temporary AssetProcessor from the locked weak_ptr data.
                // Note: we do NOT use AssetProcessor() default-construction (not available);
                // instead we use the (AssetServer, shared_ptr<AssetProcessorData>) constructor.
                AssetServer tmp_server;
                tmp_server.data = locked_server_data;
                AssetProcessor proc(std::move(tmp_server), locked_data);
                auto source_opt = proc.get_source(source_id);
                if (!source_opt) return;
                proc.handle_asset_source_event(source_opt->get(), *event, *s);
                // `s` (strong sender) goes out of scope and is dropped here
            }
        });
    }
}

void AssetProcessor::execute_processing_tasks(
    utils::Sender<std::pair<AssetSourceId, std::filesystem::path>> new_task_sender,
    utils::Receiver<std::pair<AssetSourceId, std::filesystem::path>>& receiver) const {
    // Convert the Sender into a WeakSender so that once all task producers terminate (and drop
    // their sender), this executor doesn't keep itself alive. We can still upgrade when spawning
    // tasks that need to queue dependency assets.  Matches Bevy's WeakSender downgrade pattern.
    auto weak_sender = new_task_sender.downgrade();
    new_task_sender  = {};  // drop the strong ref

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
                // No tasks are queued right now �?go to Finished immediately.
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

    // Unified event loop �?equivalent to Bevy's `select_biased!` over new_task_receiver and
    // task_finished_receiver.  We prefer StartTaskEvent (start tasks) over FinishedTaskEvent so
    // we don't prematurely mark as Finished before all queued tasks are dispatched.
    while (!input_closed || pending_tasks > 0) {
        auto event = event_receiver.receive();
        if (!event) break;  // event channel closed unexpectedly

        std::visit(utils::visitor{
                       [&](StartTaskEvent& e) {
                           // Upgrade the weak sender to verify that task producers are still alive.
                           // Matches Bevy: `let Some(new_task_sender) = new_task_sender.upgrade()`.
                           auto upgraded = weak_sender.upgrade();
                           if (!upgraded) {
                               // All producers (e.g. watcher tasks) are gone �?safe to ignore.
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

    // TODO: implement meta serialization and write default meta bytes
    // auto meta = processor->default_meta();
    // auto meta_bytes = meta->serialize();
    // writer.write_meta_bytes(asset_path.path, meta_bytes);
}
