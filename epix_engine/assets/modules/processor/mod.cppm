module;

#include <spdlog/spdlog.h>

export module epix.assets:processor;

import std;
import epix.meta;
import epix.utils;
import epix.core;

import :path;
import :meta;
import :io.reader;
import :io.source;
import :server.loader;
import :server;
import :processor.process;
import :processor.log;

namespace epix::assets {

// ---- ProcessorState ----

/** @brief The current state of the AssetProcessor.
 *  Matches bevy_asset's ProcessorState. */
export enum class ProcessorState {
    Initializing,
    Processing,
    Finished,
};

// ---- Processors registry ----

/** @brief Entry in the short_type_path_to_processor map.
 *  Matches bevy_asset's ShortTypeProcessorEntry. */
struct ShortTypeProcessorEntry {
    struct Unique {
        std::string_view type_path;
        std::shared_ptr<ErasedProcessor> processor;
    };
    struct Ambiguous {
        std::vector<std::string_view> type_paths;
    };
    std::variant<Unique, Ambiguous> entry;
};

/** @brief Maps processor type names to their type-erased instances, and extensions to default processors.
 *  Matches bevy_asset's Processors. */
struct Processors {
    std::unordered_map<std::string_view, std::shared_ptr<ErasedProcessor>> type_path_to_processor;
    std::unordered_map<std::string_view, ShortTypeProcessorEntry> short_type_path_to_processor;
    std::unordered_map<std::string, std::string_view> file_extension_to_default_processor;
};

// ---- ProcessorAssetInfo ----

/** @brief Per-asset processing state tracked by the processor.
 *  Matches bevy_asset's ProcessorAssetInfo. */
struct ProcessorAssetInfo {
    std::optional<ProcessedInfo> processed_info;
    std::unordered_set<AssetPath> dependents;
    std::optional<ProcessStatus> status;
    /** @brief A lock that controls read/write access to processed asset files.
     *  Shared for both asset bytes and meta bytes. */
    std::shared_ptr<std::shared_mutex> file_transaction_lock = std::make_shared<std::shared_mutex>();
    utils::BroadcastSender<ProcessStatus> status_sender;
    utils::BroadcastReceiver<ProcessStatus> status_receiver;

    ProcessorAssetInfo() {
        auto [sender, receiver] = utils::make_broadcast_channel<ProcessStatus>();
        status_sender           = std::move(sender);
        status_receiver         = std::move(receiver);
    }

    void update_status(ProcessStatus new_status) {
        if (status != new_status) {
            status = new_status;
            status_sender.send(new_status);
        }
    }
};

// ---- ProcessorAssetInfos ----

/** @brief The "current" in-memory view of the asset space.
 *  Matches bevy_asset's ProcessorAssetInfos. */
struct ProcessorAssetInfos {
    std::unordered_map<AssetPath, ProcessorAssetInfo> infos;
    std::unordered_map<AssetPath, std::unordered_set<AssetPath>> non_existent_dependents;

    ProcessorAssetInfo& get_or_insert(const AssetPath& asset_path) {
        auto [it, inserted] = infos.try_emplace(asset_path);
        if (inserted) {
            if (auto dep_it = non_existent_dependents.find(asset_path); dep_it != non_existent_dependents.end()) {
                it->second.dependents = std::move(dep_it->second);
                non_existent_dependents.erase(dep_it);
            }
        }
        return it->second;
    }

    ProcessorAssetInfo* get(const AssetPath& asset_path) {
        auto it = infos.find(asset_path);
        return it != infos.end() ? &it->second : nullptr;
    }

    const ProcessorAssetInfo* get(const AssetPath& asset_path) const {
        auto it = infos.find(asset_path);
        return it != infos.end() ? &it->second : nullptr;
    }

    void add_dependent(const AssetPath& asset_path, AssetPath dependent) {
        if (auto* info = get(asset_path)) {
            info->dependents.insert(std::move(dependent));
        } else {
            non_existent_dependents[asset_path].insert(std::move(dependent));
        }
    }

    /** @brief Remove an asset from tracking. Returns the transaction lock if it existed. */
    std::shared_ptr<std::shared_mutex> remove(const AssetPath& asset_path) {
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

    /** @brief Finalize processing for an asset, incorporating the result. */
    void finish_processing(const AssetPath& asset_path,
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
            auto& err    = result.error();
            bool handled = std::visit(utils::visitor{
                                          [](const process_errors::ExtensionRequired&) { return true; },
                                          [&](const process_errors::MissingAssetLoaderForExtension&) {
                                              spdlog::trace("No loader found for {}", asset_path.string());
                                              return true;
                                          },
                                          [&](const process_errors::AssetReaderError&) {
                                              spdlog::trace("No need to process {} because it does not exist",
                                                            asset_path.string());
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

    void clear_dependencies(const AssetPath& asset_path, const ProcessedInfo& removed_info) {
        for (auto& old_dep : removed_info.process_dependencies) {
            AssetPath dep_path(old_dep.path);
            if (auto* info = get(dep_path)) {
                info->dependents.erase(asset_path);
            } else if (auto it = non_existent_dependents.find(dep_path); it != non_existent_dependents.end()) {
                it->second.erase(asset_path);
            }
        }
    }
};

// ---- ProcessingState ----

/** @brief The current state of processing, including the overall state and the state of all assets.
 *  Matches bevy_asset's ProcessingState. Uses blocking primitives instead of async. */
export struct ProcessingState {
   private:
    mutable utils::RwLock<ProcessorState> m_state{ProcessorState::Initializing};

    utils::BroadcastSender<bool> m_initialized_sender;
    utils::BroadcastReceiver<bool> m_initialized_receiver;
    utils::BroadcastSender<bool> m_finished_sender;
    utils::BroadcastReceiver<bool> m_finished_receiver;

    mutable utils::RwLock<ProcessorAssetInfos> m_asset_infos;

    friend struct AssetProcessor;
    friend struct AssetProcessorData;

   public:
    ProcessingState();

    /** @brief Set the overall state of processing and broadcast appropriate events. */
    void set_state(ProcessorState state);
    /** @brief Retrieve the current ProcessorState. */
    ProcessorState get_state() const;

    /** @brief Block until the path has been processed. Returns the ProcessStatus. */
    ProcessStatus wait_until_processed(const AssetPath& path) const;
    /** @brief Get a transaction lock for the given asset path (shared read lock).
     *  Used by ProcessorGatedReader to hold the lock while reading. */
    std::expected<std::shared_ptr<std::shared_mutex>, AssetReaderError> get_transaction_lock(
        const AssetPath& path) const;
    /** @brief Block until the processor has been initialized. */
    void wait_until_initialized() const;
    /** @brief Block until processing has finished. */
    void wait_until_finished() const;
};

// ---- AssetProcessorData ----

/** @brief Shared data for the AssetProcessor, accessible across threads.
 *  Matches bevy_asset's AssetProcessorData. */
export struct AssetProcessorData {
    std::shared_ptr<ProcessingState> processing_state;
    std::shared_ptr<ProcessorTransactionLogFactory> log_factory;
    std::shared_ptr<ProcessorTransactionLog> log;
    utils::RwLock<Processors> processors;
    std::shared_ptr<AssetSourceBuilders> source_builders;

    AssetProcessorData()
        : processing_state(std::make_shared<ProcessingState>()),
          source_builders(std::make_shared<AssetSourceBuilders>()) {}

    void set_log_factory(std::shared_ptr<ProcessorTransactionLogFactory> factory) { log_factory = std::move(factory); }

    void wait_until_initialized() const { processing_state->wait_until_initialized(); }
    void wait_until_finished() const { processing_state->wait_until_finished(); }
    ProcessorState state() const { return processing_state->get_state(); }
};

// ---- AssetProcessor ----

/** @brief The main asset processor. Processes assets from source readers into processed writers.
 *  Matches bevy_asset's AssetProcessor. */
export struct AssetProcessor {
    AssetServer server;
    std::shared_ptr<AssetProcessorData> data;

    AssetProcessor(const AssetProcessor&)            = default;
    AssetProcessor(AssetProcessor&&)                 = default;
    AssetProcessor& operator=(const AssetProcessor&) = default;
    AssetProcessor& operator=(AssetProcessor&&)      = default;

    /** @brief Construct a new processor. Sets up the internal AssetServer in Processed mode,
     *  sharing its loaders. Matches bevy_asset's AssetProcessor::new(). */
    AssetProcessor(std::shared_ptr<AssetProcessorData> data, bool watching_for_changes);

    /** @brief Get a reference to the internal AssetServer. */
    const AssetServer& get_server() const { return server; }

    /** @brief Get a reference to the processor data. */
    const std::shared_ptr<AssetProcessorData>& get_data() const { return data; }

    /** @brief Get a source by id from the internal server. */
    std::optional<std::reference_wrapper<const AssetSource>> get_source(const AssetSourceId& source_id) const {
        return server.get_source(source_id);
    }

    /** @brief Get all sources from the internal server. */
    const std::shared_ptr<AssetSources>& sources() const { return server.data->sources; }

    // ---- Processor Registration (templates, inline) ----

    /** @brief Register a Process implementation.
     *  Matches bevy_asset's AssetProcessor::register_processor. */
    template <Process P>
    void register_processor(P processor) const {
        auto erased = std::make_shared<ErasedProcessorImpl<P>>(std::move(processor));
        auto guard  = data->processors.write();
        auto tp     = erased->type_path();
        auto stp    = erased->short_type_path();
        guard->type_path_to_processor.emplace(tp, erased);
        auto it = guard->short_type_path_to_processor.find(stp);
        if (it == guard->short_type_path_to_processor.end()) {
            guard->short_type_path_to_processor.emplace(
                stp, ShortTypeProcessorEntry{ShortTypeProcessorEntry::Unique{tp, erased}});
        } else {
            std::visit(utils::visitor{
                           [&](ShortTypeProcessorEntry::Unique& u) {
                               auto old_tp = u.type_path;
                               it->second  = ShortTypeProcessorEntry{ShortTypeProcessorEntry::Ambiguous{{old_tp, tp}}};
                           },
                           [&](ShortTypeProcessorEntry::Ambiguous& a) { a.type_paths.push_back(tp); },
                       },
                       it->second.entry);
        }
    }

    /** @brief Set the default processor for a given extension.
     *  Matches bevy_asset's AssetProcessor::set_default_processor. */
    template <Process P>
    void set_default_processor(std::string_view extension) const {
        auto guard = data->processors.write();
        // Find the processor name for P
        for (auto& [name, proc] : guard->type_path_to_processor) {
            // If P is registered, map extension to its name
            if (auto* impl = dynamic_cast<ErasedProcessorImpl<P>*>(proc.get())) {
                guard->file_extension_to_default_processor[std::string(extension)] = name;
                return;
            }
        }
        spdlog::warn("Cannot set default processor: type not registered");
    }

    /** @brief Get the default processor for a given extension. */
    std::shared_ptr<ErasedProcessor> get_default_processor(std::string_view extension) const {
        auto guard  = data->processors.read();
        auto ext_it = guard->file_extension_to_default_processor.find(std::string(extension));
        if (ext_it == guard->file_extension_to_default_processor.end()) return nullptr;
        auto proc_it = guard->type_path_to_processor.find(ext_it->second);
        if (proc_it == guard->type_path_to_processor.end()) return nullptr;
        return proc_it->second;
    }

    /** @brief Get a processor by type name (supports both short and full type path).
     *  Matches bevy_asset's AssetProcessor::get_processor. */
    std::expected<std::shared_ptr<ErasedProcessor>, GetProcessorError> get_processor(std::string_view type_name) const {
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

    // ---- Processing lifecycle (declared, defined in .cpp) ----

    /** @brief Start the processor. This is the main entry point, typically called as a system.
     *  Spawns background tasks to initialize and process assets.
     *  Matches bevy_asset's AssetProcessor::start. */
    static void start(core::Res<AssetProcessor> processor);

    /** @brief Initialize the processor: validate logs, recover, and queue initial tasks. */
    void initialize() const;

    // ---- Processing tasks ----

    /** @brief Process a single asset at the given source + path. */
    void process_asset(const AssetSourceId& source, const std::filesystem::path& path) const;

    /** @brief Internal implementation: process a single asset, returning result or error. */
    std::expected<ProcessResult, ProcessError> process_asset_internal(const AssetSource& source,
                                                                      const AssetPath& asset_path) const;

    // ---- Event handling ----

    /** @brief Handle an AssetSourceEvent from a source watcher. */
    void handle_asset_source_event(const AssetSource& source,
                                   const AssetSourceEvent& event,
                                   utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;

    /** @brief Handle a new folder that appeared in a source. Queues processing for all files in the folder. */
    void handle_added_folder(const AssetSource& source,
                             const std::filesystem::path& path,
                             utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;

    /** @brief Handle a removed meta file. */
    void handle_removed_meta(const AssetSource& source,
                             const std::filesystem::path& path,
                             utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;

    /** @brief Handle a removed asset file. */
    void handle_removed_asset(const AssetSource& source, const std::filesystem::path& path) const;

    /** @brief Handle a removed folder. */
    void handle_removed_folder(const AssetSource& source, const std::filesystem::path& path) const;

    /** @brief Handle a renamed asset. */
    void handle_renamed_asset(const AssetSource& source,
                              const std::filesystem::path& old_path,
                              const std::filesystem::path& new_path,
                              utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;

    // ---- Task dispatching ----

    /** @brief Queue processing tasks for all files in a folder. */
    void queue_processing_tasks_for_folder(
        const AssetSource& source,
        const std::filesystem::path& folder,
        utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;

    /** @brief Queue initial processing tasks for all processed sources. */
    void queue_initial_processing_tasks(utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;

    /** @brief Spawn background listeners for source change events. */
    void spawn_source_change_event_listeners(
        utils::Sender<std::pair<AssetSourceId, std::filesystem::path>>& sender) const;

    /** @brief Execute pending processing tasks from the receiver. */
    void execute_processing_tasks(utils::Receiver<std::pair<AssetSourceId, std::filesystem::path>>& receiver) const;

    // ---- Logging helpers ----

    void log_begin_processing(const AssetPath& path) const;
    void log_end_processing(const AssetPath& path) const;
    void log_unrecoverable() const;

    /** @brief Validate the transaction log and recover from incomplete transactions. */
    void validate_transaction_log_and_recover() const;

    // ---- File system helpers ----

    /** @brief Remove the processed asset and its meta file from the processed writer. */
    void remove_processed_asset_and_meta(const AssetSource& source, const std::filesystem::path& path) const;

    /** @brief Clean empty ancestor directories in the processed output. */
    void clean_empty_processed_ancestor_folders(const AssetSource& source, const std::filesystem::path& path) const;

    /** @brief Write a default meta file for the given path (if the asset has no meta). */
    void write_default_meta_file_for_path(const AssetSource& source, const AssetPath& asset_path) const;
};

// ---- ProcessingState inline implementations ----

inline ProcessingState::ProcessingState() {
    auto [is, ir]          = utils::make_broadcast_channel<bool>();
    auto [fs, fr]          = utils::make_broadcast_channel<bool>();
    m_initialized_sender   = std::move(is);
    m_initialized_receiver = std::move(ir);
    m_finished_sender      = std::move(fs);
    m_finished_receiver    = std::move(fr);
}

inline void ProcessingState::set_state(ProcessorState state) {
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

inline ProcessorState ProcessingState::get_state() const {
    auto guard = m_state.read();
    return *guard;
}

inline ProcessStatus ProcessingState::wait_until_processed(const AssetPath& path) const {
    // First check if already processed
    {
        auto guard = m_asset_infos.read();
        if (auto* info = guard->get(path)) {
            if (info->status) {
                return *info->status;
            }
            // Not yet processed; clone the receiver to wait on
            auto receiver = info->status_receiver;
            // Release lock, then wait
            return receiver.receive();
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

inline void ProcessingState::wait_until_initialized() const {
    auto receiver = m_initialized_receiver;
    receiver.receive();
}

inline void ProcessingState::wait_until_finished() const {
    auto receiver = m_finished_receiver;
    receiver.receive();
}

inline std::expected<std::shared_ptr<std::shared_mutex>, AssetReaderError> ProcessingState::get_transaction_lock(
    const AssetPath& path) const {
    auto guard = m_asset_infos.read();
    auto* info = guard->get(path);
    if (!info) {
        return std::unexpected(AssetReaderError(reader_errors::NotFound{path.path}));
    }
    return info->file_transaction_lock;
}

}  // namespace assets